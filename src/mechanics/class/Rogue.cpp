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
#include "SharedDefines.h"
#include "Unit.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <iterator>

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
        // LADDER-SENTINEL: 0 is not a shorter cooldown, it is no Vanish.
        // Rank III is a standing denial and there is nothing past never, so Cold
        // Trail keeps maxRank = 3; the fourth entry is unreachable.
        constexpr uint32 VANISH_COOLDOWN_MS = 600000;

        class ColdTrail final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Sync(ctx); }
            void OnDetach(Ctx& ctx) override { PermanentCooldown::Allow(ctx.player, SPELL_VANISH); }
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& /*ctx*/) const override
            {
                uint32 const ms = VANISH_COOLDOWN_MS;
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
                if (VANISH_COOLDOWN_MS == 0)
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

            uint32 const ms = VANISH_COOLDOWN_MS;
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
            uint32 const ms  = VANISH_COOLDOWN_MS;
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
        constexpr float BEHIND_MULT = 1.50f;

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

                return BEHIND_MULT;
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx&) const override
            {
                return "exposed back: nothing held; the arc is read at the damage site";
            }
        };

        std::string ExposedBack::Describe(AffixInstance const& self) const
        {
            uint32 const extra = uint32((BEHIND_MULT - 1.0f) * 100.0f + 0.5f);
            uint32 const pct   = self.boonMag;

            std::string out = "Anything striking you from behind deals " + std::to_string(extra)
                            + "% more damage. Keep your back to a wall.";

            if (pct != 0)
                out += " In exchange you avoid " + std::to_string(pct)
                     + "% of melee attacks outright.";

            return out;
        }

        // ==================================================================
        // C14 - Poisoned Blades (41)
        //
        // "A quarter of the poison damage you deal ticks on you as well."
        //
        // Poison choice becomes a decision: Crippling and Mind-numbing cost
        // nothing because they deal no damage, Instant and Deadly cost blood.
        // Multi-DoTting a camp is the greedy play and bleeds accordingly, and
        // unpoisoned blades are always an option.
        // ==================================================================
        constexpr uint32 POISON_SHARE_PCT = 25;

        class PoisonedBlades final : public IMechanic
        {
        public:
            void OnPeriodicTick(Ctx& ctx, Unit* victim, uint32& damage,
                                SpellInfo const* info) override
            {
                Player* player = ctx.player;
                if (!player || !info || damage == 0)
                    return;
                if (!victim || victim == player)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                // The rogue's own poisons, and nothing else. SpellFamilyName
                // is what separates a poison tick from a bleed, a trap or a
                // mage's DoT applied by something else in the fight.
                if (info->SpellFamilyName != SPELLFAMILY_ROGUE)
                    return;

                uint32 const share = uint32(uint64(damage)
                                          * POISON_SHARE_PCT / 100u);
                if (share == 0)
                    return;

                uint32 const health = uint32(player->GetHealth());
                uint32 const cost   = health > 1 ? std::min(share, health - 1) : 0;
                if (cost == 0)
                    return;

                // Unmitigated and unable to kill, as the card says. The flag is
                // the module's own, so this makes no Deep Wound and spends no
                // Last Rites charge.
                bool* flag = ctx.run ? &ctx.run->selfDamage : nullptr;
                if (flag)
                    *flag = true;

                Unit::DealDamage(player, player, cost, nullptr, SELF_DAMAGE,
                                 SPELL_SCHOOL_MASK_NORMAL, nullptr, /*durabilityLoss*/ false);

                if (flag)
                    *flag = false;

                _bled += cost;
            }

            // Boon::BonusDamage, on the poisons themselves: the card's "+30%
            // poison damage", which is what makes the trade worth taking.
            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const* info) override
            {
                if (!info || !ctx.self || ctx.self->boonMag == 0)
                    return 1.0f;
                if (info->SpellFamilyName != SPELLFAMILY_ROGUE)
                    return 1.0f;

                return 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const pct  = POISON_SHARE_PCT;
                uint32 const boon = self.boonMag;

                std::string out = std::to_string(pct) + "% of the damage your poisons deal is"
                                  " dealt to you as well. It cannot kill you. Crippling and"
                                  " Mind-numbing cost nothing.";

                if (boon != 0)
                    out += " In exchange your poisons deal " + std::to_string(boon) + "% more.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "poisoned blades: " + std::to_string(_bled) + " health bled";
            }

        private:
            uint32 _bled = 0;
        };

        // ==================================================================
        // C16 - Slow Hands (43)
        //
        // "Energy does not regenerate while you move in combat."
        //
        // The kite-and-poke middle ground is gone: stand and fight, or leave.
        // Kidney Shot and Gouge buy stationary seconds, and Sprint goes back to
        // being for escaping rather than for repositioning every three seconds.
        // ==================================================================
        // The card's ladder: half, then none, then none plus no combo points.
        // Half, then none, then none plus no combo points -- and there the ladder
        // ends. Regeneration is already zero and the combo-point half is spent,
        // so Slow Hands keeps maxRank = 3 rather than offering a rank-up that
        // changes nothing. The fourth entry is unreachable.
        constexpr float MOVING_REGEN = 0.0f;

        // The boon's flat addition to the energy bar.
        constexpr uint32 EXTRA_ENERGY = 20;

        class SlowHands final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override
            {
                if (Player* player = ctx.player)
                {
                    _baseMax  = player->GetMaxPower(POWER_ENERGY);
                    _lastEnergy = player->GetPower(POWER_ENERGY);
                }
            }

            void OnDetach(Ctx& ctx) override
            {
                if (ctx.player && _baseMax != 0)
                    ctx.player->SetMaxPower(POWER_ENERGY, _baseMax);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override;

            std::string Describe(AffixInstance const& /*self*/) const override
            {

                std::string out = MOVING_REGEN > 0.0f
                    ? std::string("Your energy regenerates at half rate while you are moving in"
                                  " combat.")
                    : std::string("Your energy does not regenerate at all while you are moving in"
                                  " combat.");

                out += " In exchange your energy bar holds " + std::to_string(EXTRA_ENERGY)
                     + " more. Stand and fight, or leave.";
                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                bool const moving = ctx.player && ctx.player->isMoving() && ctx.player->IsInCombat();
                return std::string("slow hands: ") + (moving ? "MOVING, regen taxed" : "still");
            }

        private:
            uint32 _baseMax     = 0;
            int32  _lastEnergy  = 0;
            uint32 _taxedTicks  = 0;
        };

        void SlowHands::OnTick(Ctx& ctx, uint32 /*diffMs*/)
        {
            Player* player = ctx.player;
            if (!player || player->getPowerType() != POWER_ENERGY)
                return;

            // The boon, held rather than set once: the stat chain recomputes
            // maximum power on level-up and on some aura changes, so an affix
            // that set it at attach would quietly lose it.
            if (ctx.self && ctx.self->boonMag != 0)
            {
                uint32 const want = (_baseMax != 0 ? _baseMax : 100u) + EXTRA_ENERGY;
                if (player->GetMaxPower(POWER_ENERGY) < want)
                    player->SetMaxPower(POWER_ENERGY, want);
            }

            int32 const now = player->GetPower(POWER_ENERGY);

            // The curse. There is no hook on energy regeneration, so what is
            // taxed is the *gain* since the last tick: the core has already
            // added it, and this gives back only the share the rank allows.
            // Spending energy shows up as a fall and is never touched.
            if (now > _lastEnergy && player->isMoving() && player->IsInCombat())
            {
                float const keep   = MOVING_REGEN;
                int32 const gained = now - _lastEnergy;
                int32 const allowed = int32(float(gained) * keep);

                player->SetPower(POWER_ENERGY, _lastEnergy + allowed);
                ++_taxedTicks;
            }

            _lastEnergy = player->GetPower(POWER_ENERGY);
        }
    }

    GAUNTLET_MECHANIC(41, PoisonedBlades);
    GAUNTLET_MECHANIC(43, SlowHands);
    GAUNTLET_MECHANIC(40, ColdTrail);
    GAUNTLET_MECHANIC(42, ExposedBack);
}
