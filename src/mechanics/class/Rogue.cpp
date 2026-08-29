/*
 * mod-gauntlet - the rogue's two: Cold Trail, Exposed Back
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletRegistry.h"
#include "PermanentCooldown.h"

#include "Chat.h"
#include "Player.h"
#include "Random.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <cmath>
#include <string>

// Design section 3, family C, rogue. Engine: Stealth, energy, combo points,
// Sinister Strike and Eviscerate. The classic death the design names is "a
// string of parries on a low-health rogue with Vanish on cooldown" -- so one of
// these takes the reset away and the other takes away the assumption that
// nothing is behind you.

namespace Gauntlet
{
    namespace
    {
        constexpr uint32 SPELL_VANISH = 1856;   // the registry's own requiresSpell for C13
        constexpr uint32 SPELL_SPRINT = 2983;

        uint8 RankIndexOf(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        // ==================================================================
        // C13 - Cold Trail (40)
        //
        // "Vanish has a ten-minute cooldown."
        //
        // The shortcut verb. Stealth openers are untouched, so the class still
        // chooses its fights -- it just cannot un-choose them, which is the
        // card's own phrasing and the whole of the affix.
        //
        // Rank III is a standing denial rather than a long cooldown, and the
        // card names the reason: Preparation cannot reset what is re-applied
        // every tick.
        // ==================================================================
        // 10 min, 30 min, then gone.
        constexpr uint32 VANISH_COOLDOWN_MS[MAX_RANK] = { 600000, 1800000, 0 };

        class ColdTrail final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Sync(ctx); }
            void OnDetach(Ctx& ctx) override { PermanentCooldown::Allow(ctx.player, SPELL_VANISH); }
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                uint32 const ms = VANISH_COOLDOWN_MS[RankIndexOf(ctx.self)];
                return std::string("cold trail: ")
                     + (ms == 0 ? "Vanish denied outright"
                                : "Vanish on " + std::to_string(ms / 60000) + " min");
            }

        private:
            void Sync(Ctx& ctx)
            {
                // Rank III only, and Hold() is what makes Preparation and Cold
                // Snap unable to give the button back -- the card asks for
                // exactly that.
                if (VANISH_COOLDOWN_MS[RankIndexOf(ctx.self)] == 0)
                    PermanentCooldown::Hold(ctx.player, SPELL_VANISH);
            }
        };

        void ColdTrail::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return;

            uint32 const base = sSpellMgr->GetFirstSpellInChain(info->Id);

            // The boon: Sprint comes back sooner, cut from the cooldown the
            // core has just set.
            if (base == SPELL_SPRINT && ctx.self && ctx.self->boonMag != 0)
            {
                uint32 const now = player->GetSpellCooldownDelay(info->Id);
                if (now != 0)
                    player->ModifySpellCooldown(info->Id,
                                                -int32(uint64(now) * ctx.self->boonMag / 100u));
            }

            if (base != SPELL_VANISH)
                return;

            uint32 const ms = VANISH_COOLDOWN_MS[RankIndexOf(ctx.self)];
            if (ms == 0)
                return;

            player->AddSpellCooldown(info->Id, 0, ms, /*needSendToClient*/ true);

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r The trail goes cold. No Vanish for {} minutes.",
                    ms / 60000);
        }

        std::string ColdTrail::Describe(AffixInstance const& self) const
        {
            uint32 const ms  = VANISH_COOLDOWN_MS[RankIndexOf(&self)];
            uint32 const pct = self.boonMag;

            std::string out = ms == 0
                ? std::string("Vanish does not answer at all, and Preparation will not give it"
                              " back. Stealth still opens fights; nothing un-opens them.")
                : "Vanish has a " + std::to_string(ms / 60000)
                  + " minute cooldown. Sprint, Gouge, Blind and route choice are your escape.";

            if (pct != 0)
                out += " In exchange, Sprint comes back " + std::to_string(pct) + "% sooner.";

            return out;
        }

        // ==================================================================
        // C15 - Exposed Back (42)
        //
        // "Attacks from behind you deal 50% more damage."
        //
        // The anchor verb, and the card's own joke is the design of it: the
        // class that teaches positioning to enemies now has to learn it
        // itself. Back to a wall, Blind the second mob, Gouge and reposition.
        // ==================================================================
        // The card's ladder for damage taken from outside the front arc.
        constexpr float BEHIND_MULT[MAX_RANK] = { 1.30f, 1.50f, 1.75f };

        class ExposedBack final : public IMechanic
        {
        public:
            float DamageTakenMult(Ctx& ctx, Unit* attacker, SpellInfo const* info) override
            {
                Player* player = ctx.player;
                if (!player || !attacker)
                    return 1.0f;

                // The boon first, because avoidance is checked before severity
                // for the same reason a real dodge is: an attack that does not
                // land cannot land harder.
                //
                // This is not a dodge in the combat log's sense. There is no
                // server-side way to add flat dodge without applying an aura,
                // and an aura needs a spell id whose DBC tooltip would then
                // describe something else -- the cost Falling Sky's speed buff
                // already pays. So the boon is delivered as what a dodge
                // actually does: the blow deals nothing. The difference a
                // player can see is the log line, which reads as a zero rather
                // than as "Dodge", and Describe() says "avoid" rather than
                // "dodge" for that reason.
                if (ctx.self && ctx.self->boonMag != 0 && !info
                    && roll_chance_i(int32(ctx.self->boonMag)))
                    return 0.0f;

                // HasInArc(pi, pos) is true for the forward half -- the arc is
                // centred on the facing and measured whole, so pi is "in front"
                // (Position.h:232). Anything else is behind.
                if (player->HasInArc(float(M_PI), attacker))
                    return 1.0f;

                return BEHIND_MULT[RankIndexOf(ctx.self)];
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx&) const override
            {
                return "exposed back: nothing held; the arc is read at the damage site";
            }
        };

        std::string ExposedBack::Describe(AffixInstance const& self) const
        {
            uint32 const extra = uint32((BEHIND_MULT[RankIndexOf(&self)] - 1.0f) * 100.0f + 0.5f);
            uint32 const pct   = self.boonMag;

            std::string out = "Anything striking you from behind deals " + std::to_string(extra)
                            + "% more damage. Keep your back to a wall.";

            if (pct != 0)
                out += " In exchange you avoid " + std::to_string(pct)
                     + "% of melee attacks outright.";

            return out;
        }
    }

    GAUNTLET_MECHANIC(40, ColdTrail);
    GAUNTLET_MECHANIC(42, ExposedBack);
}
