/*
 * mod-gauntlet - the druid's one: Bound Skin
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"
#include "SelfControl.h"
#include "TimedLockout.h"

#include "Chat.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Creature.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <string>
#include <iterator>

// Design section 3, family C, druid. The identity verb: powershifting is gone
// and leaving Cat to heal becomes a commitment rather than a flicker. The card
// names the real change -- the class decides *before* the pull whether this is
// a Bear fight or a Cat fight, and learns to heal at 50% instead of 15%.
//
// The commitment is priced on the way *in*, not on the way out. Charging the
// drop to caster as well made cancelling a form cost a second full lockout,
// which played as a bug rather than as a cost; see OnShapeshift.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_BOUND_SKIN = 64;

        // The card's ladder, in seconds of lockout after any form change.
        constexpr uint32 SHIFT_LOCK_MS = 6000;

        // Every form-granting spell, so the cooldown lands on the buttons and
        // not merely on the state. Base ranks; GetFirstSpellInChain is not
        // needed because none of these has one.
        constexpr std::array<uint32, 7> FORM_SPELLS = { {
            5487,   // Bear Form
            9634,   // Dire Bear Form
            768,    // Cat Form
            783,    // Travel Form
            1066,   // Aquatic Form
            24858,  // Moonkin Form
            33891   // Tree of Life
        } };


        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        class BoundSkin final : public IMechanic
        {
        public:
            // Every form this affix locked, back immediately: a druid who
            // swapped it away should not be stuck in caster form for ten
            // seconds. Only the ones it locked, though -- see TimedLockout.
            void OnDetach(Ctx& ctx) override { _lock.ReleaseAll(ctx.player); }

            // Entering a form, and only entering one.
            //
            // This used to fire on every form change including the drop back to
            // caster, on the reasoning that the card prices *changing*. Played,
            // that is a double charge and it reads as a bug: entering Bear
            // starts the lock, and cancelling Bear starts a second one, so a
            // druid who shifts out is held in caster form for a fresh full
            // lockout they did nothing to earn.
            //
            // The cost belongs on the commitment. Dropping out is free; getting
            // back in is what waits, because entering locked every form
            // including the one just entered. `form` is the form being set
            // (Unit.cpp:17289-17292), so FORM_NONE is the drop -- the same test
            // Nature's Toll makes a few lines below.
            void OnShapeshift(Ctx& ctx, uint8 form) override
            {
                if (form == FORM_NONE)
                    return;

                Player* player = ctx.player;
                if (!player)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const ms = SHIFT_LOCK_MS;

                for (uint32 id : FORM_SPELLS)
                    _lock.Lock(player, id, ms);

                ++_shifts;

                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_BOUND_SKIN, "c37_bound_skin"),
                                         ms / 1000u, "Bound Skin");
            }

            // Boon::BonusMaxHealth, and the card gates it: "+10% max health
            // while shapeshifted". AggregateFactor is Player-free and cannot
            // see the form, so it is applied here where the pool is built.
            void OnMaxHealth(Ctx& ctx, float& value) override
            {
                Player* player = ctx.player;
                if (!player || !ctx.self || ctx.self->boonMag == 0)
                    return;
                if (player->GetShapeshiftForm() == FORM_NONE)
                    return;

                value *= 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = SHIFT_LOCK_MS / 1000u;
                uint32 const pct  = self.boonMag;

                std::string out = "Entering a form locks every form for " + std::to_string(secs)
                                + " seconds. Dropping to caster is free; getting back in is what"
                                  " waits. Decide before the pull whether this is a Bear fight"
                                  " or a Cat fight.";

                if (pct != 0)
                    out += " In exchange you have " + std::to_string(pct)
                         + "% more health while shapeshifted.";

                return out;
            }

            std::string Diagnose(Ctx& /*ctx*/) const override
            {
                return "bound skin: " + std::to_string(SHIFT_LOCK_MS / 1000u)
                     + "s lock, " + std::to_string(_shifts) + " shift(s)";
            }

        private:
            uint32 _shifts = 0;
            TimedLockout _lock;
        };

        // ==================================================================
        // C38 - Nature's Toll (65)
        //
        // "Every kill made as a beast leaves you bleeding until you calm."
        //
        // Shift out after the fight, heal, shift back -- a rhythm the class
        // used to skip. Chain-pulling in Cat is possible and costs exactly
        // what it should. The registry makes it exclusive with Bound Skin,
        // because together they would be a tax rather than a rhythm.
        // ==================================================================
        constexpr uint32 TOLL_PCT = 3;

        class NaturesToll final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                _bleeding = false;
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(65, "c38_natures_toll"), 0);
            }

            void OnKill(Ctx& ctx, Creature* killed) override
            {
                Player* player = ctx.player;
                if (!player || !killed)
                    return;
                if (player->GetShapeshiftForm() == FORM_NONE)
                    return;

                _bleeding = true;
                AddonFor(ctx)->QueueStat(ctx.player, KeyOf(65, "c38_natures_toll"), 1);

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r The kill opens you. Shift out to close it.");
            }

            // Returning to caster form is what stops it, which is the card's
            // whole counterplay.
            void OnShapeshift(Ctx& ctx, uint8 form) override
            {
                if (form != FORM_NONE || !_bleeding)
                    return;

                _bleeding = false;
                AddonFor(ctx)->QueueStat(ctx.player, KeyOf(65, "c38_natures_toll"), 0);

                if (ctx.player && ctx.player->GetSession())
                    ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                        "|cff20ff20[Gauntlet]|r The bleeding stops.");
            }

            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                Player* player = ctx.player;
                if (!_bleeding || !player || !player->IsAlive())
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                _sinceMs += diffMs;
                if (_sinceMs < 1000)
                    return;
                _sinceMs -= 1000;

                uint32 const want   = uint32(uint64(player->GetMaxHealth())
                                           * TOLL_PCT / 100u);
                uint32 const health = uint32(player->GetHealth());
                uint32 const cost   = health > 1 ? std::min(want, health - 1) : 0;
                if (cost == 0)
                    return;

                bool* flag = ctx.run ? &ctx.run->selfDamage : nullptr;
                if (flag)
                    *flag = true;

                Unit::DealDamage(player, player, cost, nullptr, SELF_DAMAGE,
                                 SPELL_SCHOOL_MASK_NORMAL, nullptr, /*durabilityLoss*/ false);

                if (flag)
                    *flag = false;
            }

            // Boon: feral damage, which is what the bleeding is paying for.
            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                Player* player = ctx.player;
                if (!player || !ctx.self || ctx.self->boonMag == 0)
                    return 1.0f;

                uint8 const form = player->GetShapeshiftForm();
                if (form != FORM_CAT && form != FORM_BEAR && form != FORM_DIREBEAR)
                    return 1.0f;

                return 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const pct  = TOLL_PCT;
                uint32 const boon = self.boonMag;

                std::string out = "A kill made in Cat or Bear form leaves you bleeding "
                                + std::to_string(pct) + "% of your maximum health every second."
                                  " Returning to caster form stops it. It cannot kill you.";

                if (boon != 0)
                    out += " In exchange your feral damage is " + std::to_string(boon) + "% higher.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return std::string("nature's toll: ") + (_bleeding ? "BLEEDING" : "calm");
            }

        private:
            bool   _bleeding = false;
            uint32 _sinceMs  = 0;
        };

        // ==================================================================
        // C39 - Commitment of Roots (66)
        //
        // "Entangling Roots holds you as long as it holds them."
        //
        // Root-and-kite becomes root-and-fight, or root-and-heal: the druid
        // decides what the seconds are for. Nature's Grasp is untouched, so the
        // escape root still exists.
        // ==================================================================
        constexpr uint32 SPELL_ENTANGLING_ROOTS = 339;

        // The root's own duration is long; this is what the affix holds the
        // druid for, which the card ties to the enemy's. Twelve seconds is
        // Entangling Roots' base at most ranks.
        constexpr uint32 ROOT_MS = 10000;   // TODO(design)

        class CommitmentOfRoots final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override { _control.Release(ctx.player); }

            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                if (_control.Tick(ctx.player, diffMs) && ctx.player && ctx.player->GetSession())
                    ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                        "|cff20ff20[Gauntlet]|r The roots let you go.");
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                Player* player = ctx.player;
                if (!player || !spell)
                    return;

                SpellInfo const* info = spell->GetSpellInfo();
                if (!info || sSpellMgr->GetFirstSpellInChain(info->Id) != SPELL_ENTANGLING_ROOTS)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const ms = ROOT_MS;
                _control.Apply(player, SelfControl::Kind::Root, ms);

                // The boon: Roots cost half, refunded for the usual reason.
                if (ctx.self && ctx.self->boonMag != 0 && info->ManaCost != 0)
                {
                    uint32 const back = uint32(uint64(info->ManaCost) * ctx.self->boonMag / 100u);
                    player->SetPower(POWER_MANA, player->GetPower(POWER_MANA) + int32(back));
                }

                AddonFor(ctx)->SendEvent(player, KeyOf(66, "c39_commitment_of_roots"),
                                         ms / 1000u, "Rooted");
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = ROOT_MS / 1000u;

                std::string out = "Casting Entangling Roots roots you for " + std::to_string(secs)
                                + " seconds too. Nature's Grasp is untouched.";

                if (self.boonMag != 0)
                    out += " In exchange Roots costs " + std::to_string(self.boonMag) + "% less.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return std::string("commitment of roots: ") + (_control.Held() ? "ROOTED" : "free");
            }

        private:
            SelfControl _control;
        };

        // ==================================================================
        // C40 - Two Faces (67)
        //
        // "By day your spells are weaker; by night your claws are."
        //
        // Play the clock: caster levelling by night, feral by day, or accept
        // the penalty and take the other half's bonus. The one affix in the
        // module whose state the player cannot change at all -- only work
        // around -- which is why both halves are always paid.
        // ==================================================================
        constexpr uint32 FACE_PENALTY_PCT = 30;
        constexpr uint32 FACE_BONUS_PCT = 10;

        class TwoFaces final : public IMechanic
        {
        public:
            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const* info) override
            {
                Player* player = ctx.player;
                if (!player)
                    return 1.0f;

                uint8 const form  = player->GetShapeshiftForm();
                bool const  feral = form == FORM_CAT || form == FORM_BEAR || form == FORM_DIREBEAR;
                bool const  spell = info != nullptr;

                // Only the two halves the card names. A feral swing at night
                // and a spell by day are penalised; the mirrors are rewarded.
                if (!feral && !spell)
                    return 1.0f;

                bool const day = IsDaytime();
                bool const penalised = (day && spell) || (!day && feral);

                if (penalised)
                    return 1.0f - float(FACE_PENALTY_PCT) / 100.0f;

                return 1.0f + float(FACE_BONUS_PCT) / 100.0f;
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                uint32 const pct = FACE_PENALTY_PCT;

                return "By day your spells deal " + std::to_string(pct) + "% less and your claws "
                     + std::to_string(FACE_BONUS_PCT) + "% more. By night it is the other way"
                       " round. Play the clock.";
            }

            std::string Diagnose(Ctx&) const override
            {
                return std::string("two faces: ") + (IsDaytime() ? "day" : "night");
            }

        private:
            // Server time, matching the Condition::AtDay the aggregate already
            // uses so a druid is never told it is day by one affix and night by
            // another.
            static bool IsDaytime()
            {
                time_t const now = time(nullptr);
                tm lt{};
                localtime_r(&now, &lt);
                return lt.tm_hour >= 6 && lt.tm_hour < 18;
            }
        };
    }

    GAUNTLET_MECHANIC(65, NaturesToll);
    GAUNTLET_MECHANIC(66, CommitmentOfRoots);
    GAUNTLET_MECHANIC(67, TwoFaces);
    GAUNTLET_MECHANIC(64, BoundSkin);
}
