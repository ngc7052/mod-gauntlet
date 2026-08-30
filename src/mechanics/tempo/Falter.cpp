/*
 * mod-gauntlet - T4 Falter: your hands fail you, on a schedule
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "../Boons.h"

#include "Chat.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "Unit.h"

#include <string>
#include <iterator>

// Registry id 17. Design section 3, card T4: "Every 45 seconds in combat your
// hands fail you for three seconds: disarmed if you fight with weapons, silenced
// if you cast. You are warned two seconds ahead."
//
// Deterministic on purpose, and the card says why: a random-proc silence would
// be a coin flip against a permadeath run. Scheduled, it is a planning problem
// -- do not be mid-burst or low when it lands, pre-cast the heal, pop a
// defensive, and for a melee class it is the moment to use the shield bash or
// the kick they never press.
//
// It shares the single "role tax" slot with Cunning through the registry's
// "roletax" key: two affixes that both take your buttons away is not two
// affixes, it is one affix twice.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_FALTER = 17;

        // The card's ladders: cadence 60 -> 45 -> 30 s, duration 2 -> 3 -> 4 s.
        // Rank IV is 22 s and 5 s past the card -- a fifth of every fight
        // spent unable to act, which is where a tempo tax should top out: it
        // is survivable by planning around it and unsurvivable by ignoring it.
        constexpr uint32 CADENCE_MS[]  = { 60000, 45000, 30000, 22000 };
        static_assert(std::size(CADENCE_MS) >= MAX_RANK, "CADENCE_MS is short a rank");
        constexpr uint32 DURATION_MS[] = { 2000, 3000, 4000, 5000 };
        static_assert(std::size(DURATION_MS) >= MAX_RANK, "DURATION_MS is short a rank");

        // "You are warned two seconds ahead."
        constexpr uint32 WARN_MS = 2000;

        // Spell 676 "Disarm", named by plan appendix A and verified there by
        // name against Spell.dbc. It is the warrior ability, applied here to
        // the player by the player, so the aura's SPELL_AURA_MOD_DISARM lands
        // on the character that carries the affix and on nobody else.
        //
        // Spell 15487 "Silence", the same appendix, the priest's Shadow talent.
        // Its aura is SPELL_AURA_MOD_SILENCE.
        //
        // Both carry their own DBC duration -- 10 s for the disarm, 5 s for the
        // silence -- which is far longer than any rank of this card, so both are
        // shortened on the aura after they are applied. That is the same
        // deviation Falling Sky's dodge buff makes and it has the same visible
        // cost: the client builds a tooltip from its own copy of Spell.dbc, so
        // hovering the icon reads the DBC's duration and not the two, three or
        // four seconds the server is enforcing. The countdown on the icon is
        // right, because that number goes over the wire in SMSG_AURA_UPDATE;
        // the tooltip cannot be without a client patch, which is out of bounds.
        constexpr uint32 SPELL_DISARM  = 676;
        constexpr uint32 SPELL_SILENCE = 15487;

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_FALTER);
            return def ? def->name : "Falter";
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_FALTER);
            return def ? def->key : "falter";
        }

        // "Class decides which; druids in forms get the disarm."
        //
        // TODO(design): the card gives the rule and one exception, not a table.
        // The reading here is: a character is silenced when the thing it would
        // miss is a cast, and disarmed when it is a swing. Warrior, rogue and
        // death knight have no cast-time spells at all in 3.3.5 and are always
        // disarmed; mage, warlock and priest are always silenced; paladin,
        // hunter and shaman all level with a weapon in hand and are disarmed,
        // which is also the harsher answer for the two of them that heal, and
        // therefore the one that keeps the affix a cost; and the druid follows
        // its form, exactly as the card says.
        bool Silenced(Player* player)
        {
            switch (player->getClass())
            {
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                case CLASS_PRIEST:
                    return true;

                case CLASS_DRUID:
                {
                    // Bear, dire bear and cat are the forms that fight with
                    // claws; everything else a druid stands in casts.
                    ShapeshiftForm const form = player->GetShapeshiftForm();   // Unit.h:2050
                    return form != FORM_BEAR && form != FORM_DIREBEAR && form != FORM_CAT;
                }

                default:
                    return false;
            }
        }

        class Falter final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Sync(ctx); }

            // Disarm() here is the scheduler's, not the spell's -- this file
            // uses Arm/Disarm for its own queue entry, and SPELL_DISARM is a
            // different thing that happens to share the word. Cancelling the
            // event was all detach used to do, which left whichever debuff had
            // already landed sitting on the player after the affix was gone.
            //
            // `.gauntlet debug soak` found it: it releases a mechanic's own
            // events and then detaches, and reported "aura 676 still applied"
            // with six weapon passives dropped behind it -- the collateral of
            // a disarm nobody was carrying any more.
            //
            // It expires on its own in two to four seconds, so this is small.
            // It is still the rule the rest of the module states plainly: a
            // curse the player no longer carries must not go on biting.
            void OnDetach(Ctx& ctx) override
            {
                Disarm(ctx);

                if (Player* player = ctx.player)
                {
                    player->RemoveAura(SPELL_DISARM);
                    player->RemoveAura(SPELL_SILENCE);
                }
            }
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnEnterCombat(Ctx& ctx, Unit* /*enemy*/, bool /*wasOutOfCombat*/) override { Sync(ctx); }
            void OnLeaveCombat(Ctx& ctx) override { Disarm(ctx); }

            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            // BonusMaxHealth. Two to four seconds with no hands is survived or
            // it is not, and a bigger pool is what survives it -- the one
            // resource that answers a window in which the player cannot act.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Sync(Ctx& ctx);
            void Arm(Ctx& ctx);
            void Disarm(Ctx& ctx);
            void Land(Ctx& ctx, Player* player);

            uint32 _eventId = 0;
            bool   _armed   = false;
        };

        void Falter::Sync(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;

            // "In combat" is this file's one rule; mounted, in flight, in a
            // sanctuary, dead, inside the grace window and with an offer on the
            // table are the scheduler's, and it has already applied every one
            // of them before either callback below is reached.
            bool const fighting = player->IsInWorld() && player->IsAlive() && player->IsInCombat();
            if (fighting == _armed)
                return;

            if (fighting)
                Arm(ctx);
            else
                Disarm(ctx);
        }

        void Falter::Arm(Ctx& ctx)
        {
            ++_eventId;
            _armed = true;
            ctx.clock->Arm(MECHANIC_FALTER, _eventId, CADENCE_MS[RankIndex(ctx.self)], WARN_MS);
        }

        void Falter::Disarm(Ctx& ctx)
        {
            _armed = false;

            // Bumping the tag is what makes a Fire that has already been handed
            // out unrecognisable: one Tick can release a Warn and its Fire
            // together, and a cancel from inside the Warn cannot take the Fire
            // out of a batch that has already been returned.
            ++_eventId;

            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_FALTER);
        }

        void Falter::OnWarn(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (!player || eventId != _eventId)
                return;

            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
            {
                Disarm(ctx);
                return;
            }

            // The warning is the affix. Without it this is a random silence
            // with a schedule the player cannot see, which is the thing the
            // card exists to avoid.
            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), WARN_MS / 1000u, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    Silenced(player) ? "|cffff2020[Gauntlet]|r Your voice is about to fail you."
                                     : "|cffff2020[Gauntlet]|r Your grip is about to fail you.");
        }

        void Falter::OnEvent(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock || eventId != _eventId)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
            {
                Disarm(ctx);
                return;
            }

            Land(ctx, player);

            // Re-armed from the landing rather than from the last arming, so
            // the cadence reads as "this long after the last one" however long
            // the scheduler held this one behind another affix's event.
            if (player->IsInCombat())
                Arm(ctx);
            else
                Disarm(ctx);
        }

        void Falter::Land(Ctx& ctx, Player* player)
        {
            bool const   silence = Silenced(player);
            uint32 const spellId = silence ? SPELL_SILENCE : SPELL_DISARM;
            uint32 const ms      = DURATION_MS[RankIndex(ctx.self)];

            // Unit::AddAura(uint32, Unit*) applies the aura with no cast, no
            // global cooldown and no line of sight, and answers null rather
            // than throwing for a spell the world does not know, a dead target
            // or a target immune to it (Unit.h:1351, Unit.cpp:15150-15187). A
            // player under an immunity that refuses it has simply answered the
            // affix with a cooldown, which is the counterplay the card names.
            Aura* aura = player->AddAura(spellId, player);
            if (!aura)
                return;

            // The DBC duration is 10 s for the disarm and 5 s for the silence,
            // and neither is this card's. SetMaxDuration first and SetDuration
            // second: the maximum is what the client draws the bar against and
            // what a refresh would restore to, so writing only the current
            // duration would give a three-second aura a ten-second bar
            // (SpellAuras.h:134).
            aura->SetMaxDuration(static_cast<int32>(ms));
            aura->SetDuration(static_cast<int32>(ms));

            // It can end a run by taking the heal that would have saved it, so
            // it claims the death the moment it acts. Mgr::Tick has already
            // noted the mechanic for this Fire; this covers the case where the
            // aura outlives the note.
            if (ctx.run)
                ctx.run->NoteActor(MECHANIC_FALTER);

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    silence ? "|cffff2020[Gauntlet]|r You are silenced for {} seconds."
                            : "|cffff2020[Gauntlet]|r You are disarmed for {} seconds.",
                    ms / 1000u);
        }

        std::string Falter::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "Every " + std::to_string(CADENCE_MS[i] / 1000u)
                            + " seconds in combat your hands fail you for "
                            + std::to_string(DURATION_MS[i] / 1000u)
                            + " seconds: disarmed if you fight with weapons, silenced if you cast."
                              " You are warned two seconds ahead, every time.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(17, Falter);
}
