/*
 * mod-gauntlet - B1 Last Rites: one death bought back, and ten minutes to regret it
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Charges.h"

#include "Chat.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <iterator>

// Registry id 26. Design section 3, card B1: "Once per level, a killing blow
// leaves you at 1 health instead. For ten minutes afterwards you take 50% more
// damage and cannot be healed above half."
//
// The card's note is unusually direct about the intent -- "deliberately
// tempting ... it converts one certain death into a chase; it never removes
// the stakes" -- and both halves of that sentence are load-bearing. The save
// is real and it is the boon the registry names (Boon::SecondLife). The Mark
// is the price, and the card calls its window "the most memorable ten minutes
// of any run (run, hide, or die anyway)".
//
// Note which way the rank ladder runs. Every other mechanic gets worse as it
// ranks up by doing more; this one gets worse by doing *less*: a charge every
// level, then every two, then every three. The curse and the boon are the same
// event, so severity can only be expressed as scarcity.
//
// The implementation plan names this row as the one Phase 4's Ankh Pact and
// Stone of the Damned reuse, which is why the per-level accounting lives in
// mechanics/Charges.h rather than in three integers here.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_LAST_RITES = 26;

        // The card's ladder, as scarcity: a charge every N levels, and every
        // four at rank IV. This is the one ladder in the table where a higher
        // rank is a smaller gift and nothing else, because the gift is the
        // whole mechanic: Last Rites has no curse half to escalate.
        constexpr uint8 LEVELS_PER_CHARGE[] = { 1, 2, 3, 4 };
        static_assert(std::size(LEVELS_PER_CHARGE) >= MAX_RANK, "LEVELS_PER_CHARGE is short a rank");

        // The card's two fixed numbers.
        constexpr uint32 MARK_MS          = 600000;   // ten minutes
        constexpr float  MARK_DAMAGE_MULT = 1.5f;
        // Half the *current* maximum, so a Deep Wound and the Mark compound.

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_LAST_RITES);
            return def ? def->key : "last_rites";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // "last_rites.mark", the milliseconds left on the Mark. Under
        // State::MaxKeyLen with room to spare.
        std::string MarkKey() { return std::string(MechanicKey()) + ".mark"; }

        class LastRites final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override;
            void OnDetach(Ctx& ctx) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            uint32 OnLethal(Ctx& ctx, uint32 damage) override;

            float DamageTakenMult(Ctx&, Unit* /*attacker*/, SpellInfo const*) override
            {
                return _markMs != 0 ? MARK_DAMAGE_MULT : 1.0f;
            }

            // The heal ceiling, and it is OnHeal rather than HealTakenMult on
            // purpose. "Cannot be healed above half" names a place, not a
            // fraction: a multiplier that never sees the heal can only answer
            // all or nothing, and "all" at 49% health takes the player to full,
            // straight past the line. This one is handed the amount after the
            // aggregate and its floor, and lowers it to exactly the gap.
            void OnHeal(Ctx& ctx, uint32& heal) override;

            std::string Describe(AffixInstance const& self) const override;
            std::string Diagnose(Ctx& ctx) const override;

        private:
            void Save(Ctx& ctx) const;
            void Publish(Ctx& ctx);
            void PublishCharge(Ctx& ctx);

            uint8 Ladder(Ctx const& ctx) const { return LEVELS_PER_CHARGE[RankIndex(ctx.self)]; }

            uint32 _markMs    = 0;
            uint32 _saves     = 0;
            bool   _published = false;
        };

        void LastRites::OnAttach(Ctx& ctx)
        {
            // The Mark survives a logout, and it survives it *paused*: the
            // remaining time is stored rather than an expiry, because State
            // holds integers and this module has no wall clock in it. That
            // makes logging out the strictest possible answer rather than the
            // cheese it would be the other way round -- a player who logs out
            // at nine minutes comes back owing nine minutes.
            if (ctx.state)
                _markMs = uint32(std::max(0, ctx.state->Get(MarkKey())));

            Publish(ctx);
            PublishCharge(ctx);
        }

        void LastRites::OnDetach(Ctx& ctx)
        {
            Save(ctx);

            // A swap is not a pardon: the spent charge stays spent for the run
            // and the Mark keeps running. What is cleared is the display, so a
            // mechanic the player no longer carries stops drawing rows.
            if (ctx.addon && ctx.player)
            {
                ctx.addon->QueueStat(ctx.player, MechanicKey(), 0);
                AddonFor(ctx)->SendEvent(ctx.player, MechanicKey(), 0, "Last Rites");
            }
        }

        void LastRites::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (!_published)
                PublishCharge(ctx);

            if (_markMs == 0)
                return;

            if (_markMs > diffMs)
            {
                _markMs -= diffMs;

                // Written back on the cadence rather than only at logout: a
                // worldserver that stops badly must not hand the character back
                // a Mark it had already served.
                Save(ctx);
                return;
            }

            _markMs = 0;
            Save(ctx);
            Publish(ctx);

            if (ctx.player && ctx.player->GetSession())
                ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r The Mark of Last Rites fades. You are whole again.");
        }

        uint32 LastRites::OnLethal(Ctx& ctx, uint32 damage)
        {
            Player* player = ctx.player;
            if (!player || !ctx.state)
                return damage;

            // Mgr::OnLethal has already established that this blow is fatal and
            // that it is not the module's own self-damage.
            if (ctx.run && ctx.run->dead)
                return damage;

            uint8 const level = player->GetLevel();
            if (!Charges::Available(ctx.state, MechanicKey(), level, Ladder(ctx)))
                return damage;

            Charges::Spend(ctx.state, MechanicKey(), level);

            _markMs = MARK_MS;
            ++_saves;
            Save(ctx);

            uint32 const health = uint32(player->GetHealth());
            uint32 const left   = health > 1 ? health - 1 : 0;

            Publish(ctx);
            PublishCharge(ctx);

            // The countdown is the affix. Ten minutes at 1.5x damage with no
            // heal above half is a state the player has to plan around, and a
            // player who cannot see how long it has left is not being asked to
            // plan -- they are being asked to guess.
            AddonFor(ctx)->SendEvent(player, MechanicKey(), MARK_MS / 1000u, "Marked");

            if (player->GetSession())
            {
                ChatHandler handler(player->GetSession());
                handler.PSendSysMessage(
                    "|cffff2020[Gauntlet]|r LAST RITES. That blow would have ended the run.");
                handler.PSendSysMessage(
                    "|cffff2020[Gauntlet]|r You are Marked for ten minutes: you take half again as"
                    " much damage and cannot be healed above half. Run.");
            }

            return left;
        }

        void LastRites::OnHeal(Ctx& ctx, uint32& heal)
        {
            Player* player = ctx.player;
            if (_markMs == 0 || !player || heal == 0)
                return;

            // The ceiling is half the pool the player has *now*, which is the
            // right reading while Deep Wounds is on the table: a wound shrinks
            // the maximum, and half of a smaller pool is a smaller number. The
            // two affixes compound, and they are meant to.
            uint32 const ceiling = uint32(uint64(player->GetMaxHealth()) * 1 / 2);
            uint32 const health  = uint32(player->GetHealth());

            if (health >= ceiling)
            {
                heal = 0;
                return;
            }

            // Below the line, exactly as much as reaches it. Not a refusal:
            // healing from 10% to 50% is the whole of what the player can do
            // about the Mark, and the card takes away the half above, not the
            // half below.
            heal = std::min(heal, ceiling - health);
        }

        void LastRites::Save(Ctx& ctx) const
        {
            if (ctx.state)
                ctx.state->Set(MarkKey(), int32(_markMs));
        }

        void LastRites::Publish(Ctx& ctx)
        {
            if (!ctx.player)
                return;

            // A flag rather than a number: the countdown is already an EVT, and
            // a second row counting the same seconds down would be noise. What
            // this row says is "the Mark is on you", which is the fact that
            // changes how every pull is fought.
            AddonFor(ctx)->QueueStat(ctx.player, MechanicKey(), _markMs != 0 ? 1 : 0);
        }

        void LastRites::PublishCharge(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.state)
                return;

            _published = true;

            bool const up = Charges::Available(ctx.state, MechanicKey(), player->GetLevel(), Ladder(ctx));
            AddonFor(ctx)->QueueCounter(player, "last_rites_charge", up ? 1u : 0u, 1u);
        }

        std::string LastRites::Describe(AffixInstance const& self) const
        {
            uint8 const every = LEVELS_PER_CHARGE[RankIndex(&self)];

            std::string how_often = every == 1
                ? std::string("once per level")
                : "once every " + std::to_string(every) + " levels";

            // Rewritten in Phase 3 after a player read it and had to ask
            // whether it saves you from a killing blow -- which is the only
            // thing it does.
            //
            // Three faults, and they are the ones to avoid in every card here.
            // It opened on "Once per level," so the effect arrived in a
            // subordinate clause after the reader had already spent their
            // attention on a frequency. It said "half again as much damage",
            // which is a phrase and not a number. And it ended on "It is a
            // chase, not a pardon", which is the writer talking rather than
            // the affix.
            //
            // The rule the rest of these should follow: first sentence says
            // what happens to you, in plain words, with the number in digits.
            //
            // No BoonClause: the save *is* the boon (Boon::SecondLife has no
            // magnitude), and a sentence promising an upside on top would read
            // as a third thing the affix does.
            return "When a hit would kill you, it leaves you at 1 health instead. This saves you "
                 + how_often + ". For 10 minutes after a save you take 50% more damage and cannot"
                   " be healed above half your health.";
        }

        std::string LastRites::Diagnose(Ctx& ctx) const
        {
            std::string out = "last rites: ";

            if (ctx.state && ctx.player)
            {
                uint8 const level = ctx.player->GetLevel();
                uint8 const every = Ladder(ctx);

                if (Charges::Available(ctx.state, MechanicKey(), level, every))
                    out += "charge up";
                else
                    out += "charge spent, returns at level "
                         + std::to_string(Charges::ReturnsAtLevel(ctx.state, MechanicKey(), level, every));
            }
            else
            {
                out += "no state";
            }

            out += ", mark " + std::to_string(_markMs / 1000u) + "s, "
                 + std::to_string(_saves) + " save(s) this session";
            return out;
        }
    }

    GAUNTLET_MECHANIC(26, LastRites);
}
