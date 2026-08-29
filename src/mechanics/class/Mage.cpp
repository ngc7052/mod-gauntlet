/*
 * mod-gauntlet - the mage's two: Cold Feet, Mana Burn
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "AuraDurationEdit.h"
#include "PermanentCooldown.h"

#include "Chat.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "ObjectGuid.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

#include <algorithm>
#include <algorithm>
#include <string>
#include <vector>
#include <iterator>

// Design section 3, family C, mage. Both of these attack the same assumption
// from different sides: that a mage never gets touched. One prices the escape,
// the other prices being hit at all.

namespace Gauntlet
{
    namespace
    {
        constexpr uint32 SPELL_BLINK      = 1953;   // the registry's requiresSpell for C29
        constexpr uint32 SPELL_FROST_NOVA = 122;

        uint8 RankIndexOf(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // ==================================================================
        // C29 - Cold Feet (56)
        //
        // "Blink costs 15% of your maximum health."
        //
        // The shortcut verb. Blink becomes a decision instead of a reflex, and
        // Frost Nova, Polymorph, Ice Block and positioning carry the weight
        // they carried before everyone learned to Blink through everything.
        // Rank III takes it away, and the card is honest about what that is:
        // the class plays like a warlock without a pet, which is a real style
        // and still winnable.
        // ==================================================================
        constexpr uint16 MECHANIC_COLD_FEET = 56;

        // 15%, 25%, then gone -- price, higher price, removal, in one row.
        // 15%, 25%, then gone -- price, higher price, removal, in one row. There
        // is nothing past removal, so Cold Feet keeps maxRank = 3 and the fourth
        // entry is unreachable.
        constexpr uint32 BLINK_COST_PCT[] = { 15, 25, 0, 0 };
        static_assert(std::size(BLINK_COST_PCT) >= MAX_RANK, "BLINK_COST_PCT is short a rank");

        class ColdFeet final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Sync(ctx); }
            void OnDetach(Ctx& ctx) override { PermanentCooldown::Allow(ctx.player, SPELL_BLINK); }
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                uint32 const pct = BLINK_COST_PCT[RankIndexOf(ctx.self)];
                return std::string("cold feet: ")
                     + (pct == 0 ? "Blink denied outright"
                                 : "Blink costs " + std::to_string(pct) + "% max health")
                     + ", " + std::to_string(_paid) + " paid";
            }

        private:
            void Sync(Ctx& ctx)
            {
                if (BLINK_COST_PCT[RankIndexOf(ctx.self)] == 0)
                    PermanentCooldown::Hold(ctx.player, SPELL_BLINK);
            }

            uint32 _paid = 0;
        };

        void ColdFeet::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return;

            uint32 const base = sSpellMgr->GetFirstSpellInChain(info->Id);

            // The boon: Frost Nova comes back sooner, which is the button the
            // card names as carrying Blink's weight.
            if (base == SPELL_FROST_NOVA && ctx.self && ctx.self->boonMag != 0)
            {
                uint32 const now = player->GetSpellCooldownDelay(info->Id);
                if (now != 0)
                    player->ModifySpellCooldown(info->Id,
                                                -int32(uint64(now) * ctx.self->boonMag / 100u));
            }

            if (base != SPELL_BLINK)
                return;

            uint32 const pct = BLINK_COST_PCT[RankIndexOf(ctx.self)];
            if (pct == 0)
                return;   // rank III: Sync holds it denied

            uint32 const want   = uint32(uint64(player->GetMaxHealth()) * pct / 100u);
            uint32 const health = uint32(player->GetHealth());
            uint32 const cost   = health > 1 ? std::min(want, health - 1) : 0;
            if (cost == 0)
                return;

            ++_paid;

            // RunState::selfDamage across the call, exactly as Blood Magic
            // does: this is the module's own damage and must make no Deep
            // Wound and spend no Last Rites charge. It cannot kill -- the
            // card says so and the clamp above is what says it.
            bool* flag = ctx.run ? &ctx.run->selfDamage : nullptr;
            if (flag)
                *flag = true;

            Unit::DealDamage(player, player, cost, nullptr, SELF_DAMAGE, SPELL_SCHOOL_MASK_NORMAL,
                             nullptr, /*durabilityLoss*/ false);

            if (flag)
                *flag = false;

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_COLD_FEET, "c29_cold_feet"), 0, "Blink");
        }

        std::string ColdFeet::Describe(AffixInstance const& self) const
        {
            uint32 const pct  = BLINK_COST_PCT[RankIndexOf(&self)];
            uint32 const boon = self.boonMag;

            std::string out = pct == 0
                ? std::string("Blink does not answer at all. Frost Nova, Polymorph, Ice Block and"
                              " your feet are what you have.")
                : "Blink costs " + std::to_string(pct)
                  + "% of your maximum health. It cannot kill you.";

            if (boon != 0)
                out += " In exchange, Frost Nova comes back " + std::to_string(boon) + "% sooner.";

            return out;
        }

        // ==================================================================
        // C31 - Mana Burn (58)
        //
        // "Half the damage you take also burns your mana."
        //
        // The threshold verb, and the card's summary of it is the best one:
        // the glass cannon has to be glass that does not get touched. Mana
        // Shield becomes central and Ice Barrier is a mana-saver rather than a
        // mana cost.
        // ==================================================================
        constexpr uint16 MECHANIC_MANA_BURN = 58;

        // Rank III already burns a point of mana for every point of damage, and
        // there is nothing past all of it, so Mana Burn keeps maxRank = 3. The
        // fourth entry is unreachable.
        constexpr uint32 BURN_PCT[] = { 30, 50, 100, 100 };

        static_assert(std::size(BURN_PCT) >= MAX_RANK, "BURN_PCT is short a rank");

        class ManaBurn final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_MANA_BURN, "c31_mana_burn"), 0);
            }

            void OnAttach(Ctx& ctx) override
            {
                if (ctx.player)
                    AddonFor(ctx)->QueueStat(ctx.player, KeyOf(MECHANIC_MANA_BURN, "c31_mana_burn"),
                                             int32(BURN_PCT[RankIndexOf(ctx.self)]));
            }

            // An observer, on the damage that actually landed. It runs after
            // the aggregate and after any cheat death, so a blow Last Rites
            // refused does not also empty the bar.
            void OnDamageTaken(Ctx& ctx, Unit* /*attacker*/, uint32 amount) override
            {
                Player* player = ctx.player;
                if (!player || amount == 0)
                    return;

                ++_blows;

                // The pool, not the active bar.
                //
                // This was getPowerType() != POWER_MANA, which asks "is mana
                // the bar you are spending right now" -- false for a druid in
                // any form, and false for anyone the core has temporarily put
                // on another power type. The card's question is whether there
                // is a mana pool to burn, and GetMaxPower answers that one.
                // For the mage the curse is written for the two are the same
                // test, so this changes nothing there and stops the curse
                // going silently inert anywhere else.
                if (player->GetMaxPower(POWER_MANA) == 0)
                {
                    ++_noPool;
                    return;
                }

                uint32 const burn = uint32(uint64(amount) * BURN_PCT[RankIndexOf(ctx.self)] / 100u);
                if (burn == 0)
                    return;

                int32 const left = player->GetPower(POWER_MANA);
                int32 const took = std::min<int32>(left, int32(burn));
                if (took <= 0)
                {
                    ++_dry;
                    return;
                }

                player->SetPower(POWER_MANA, left - took);
                _burned += uint64(took);

                // Once per session, for Blood Magic's reason: a bar that
                // silently drops is the invisible scalar this redesign exists
                // to delete, and mana falling during a fight looks like mana
                // being spent.
                if (!_warned && player->GetSession())
                {
                    _warned = true;
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Mana Burn: the blows you take come out of your mana too.");
                }
            }

            // Boon::BonusDamage, on spells only, which is what the card says.
            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const* info) override
            {
                if (!info || !ctx.self || ctx.self->boonMag == 0)
                    return 1.0f;

                return 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const pct  = BURN_PCT[RankIndexOf(&self)];
                uint32 const boon = self.boonMag;

                std::string out = std::to_string(pct) + "% of the damage you take is also taken"
                                  " from your mana. Do not get touched.";

                if (boon != 0)
                    out += " In exchange your spells deal " + std::to_string(boon) + "% more damage.";

                return out;
            }

            // Counters, not just the configured number. "It is not working"
            // and "it is working and you are not seeing it" are the two
            // answers this has to tell apart, and the percentage alone told
            // neither: _blows counts every blow the observer saw, so a zero
            // there means the hook never reached this mechanic at all.
            std::string Diagnose(Ctx& ctx) const override
            {
                return "mana burn: " + std::to_string(BURN_PCT[RankIndexOf(ctx.self)])
                     + "% of damage taken, " + std::to_string(_blows) + " blow(s) seen, "
                     + std::to_string(_burned) + " mana burned"
                     + (_noPool ? ", " + std::to_string(_noPool) + " skipped (no mana pool)" : "")
                     + (_dry ? ", " + std::to_string(_dry) + " skipped (bar already empty)" : "");
            }

        private:
            uint64 _burned = 0;
            uint32 _blows  = 0;
            uint32 _noPool = 0;
            uint32 _dry    = 0;
            bool   _warned = false;
        };

        // ==================================================================
        // C30 - Fickle Sheep (57)
        //
        // "Polymorph breaks after five seconds, and the sheep comes back
        // angry."
        //
        // CC buys time, not neutralisation: sheep to reposition, to finish the
        // first target, to bandage for three seconds -- then deal with an
        // angrier second mob. The boon is that the sheep is instant, which is
        // what makes it usable as a three-second tool at all.
        // ==================================================================
        constexpr uint32 SPELL_POLYMORPH = 118;
        constexpr uint32 SPELL_ENRAGE    = 8599;

        // The card's ladder, and it shortens as the affix worsens.
        constexpr int32 SHEEP_MS[] = { 5000, 4000, 3000, 2000 };
        static_assert(std::size(SHEEP_MS) >= MAX_RANK, "SHEEP_MS is short a rank");

        constexpr int32 ENRAGE_MS = 10000;

        class FickleSheep final : public IMechanic
        {
        public:
            void OnAuraApplied(Ctx& ctx, Unit* target, Aura* aura) override
            {
                Player* player = ctx.player;
                if (!player || !aura || !target || target == player)
                    return;

                SpellInfo const* info = aura->GetSpellInfo();
                if (!info || sSpellMgr->GetFirstSpellInChain(info->Id) != SPELL_POLYMORPH)
                    return;

                // Ours only. OnAuraApply fires for every unit on the map, and a
                // second mage's sheep is not this affix's business.
                if (aura->GetCasterGUID() != player->GetGUID())
                    return;

                AuraDurationEdit::Edit(aura, SHEEP_MS[RankIndexOf(ctx.self)]);
                _sheeped.push_back(target->GetGUID());
            }

            void OnAuraRemoved(Ctx& ctx, Unit* target, AuraApplication* app) override;

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = uint32(SHEEP_MS[RankIndexOf(&self)] / 1000);

                std::string out = "Your Polymorph lasts " + std::to_string(secs)
                                + " seconds, whatever its tooltip says, and what comes out of it"
                                  " is enraged for 10 seconds.";

                if (self.boonMag != 0)
                    out += " In exchange it is instant.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "fickle sheep: " + std::to_string(_sheeped.size()) + " tracked, "
                     + std::to_string(_enraged) + " enraged";
            }

        private:
            std::vector<ObjectGuid> _sheeped;
            uint32                  _enraged = 0;
        };

        void FickleSheep::OnAuraRemoved(Ctx& ctx, Unit* target, AuraApplication* app)
        {
            Player* player = ctx.player;
            if (!player || !target || !app || target == player)
                return;

            Aura* aura = app->GetBase();
            SpellInfo const* info = aura ? aura->GetSpellInfo() : nullptr;
            if (!info || sSpellMgr->GetFirstSpellInChain(info->Id) != SPELL_POLYMORPH)
                return;

            auto const it = std::find(_sheeped.begin(), _sheeped.end(), target->GetGUID());
            if (it == _sheeped.end())
                return;   // not one of ours

            _sheeped.erase(it);
            ++_enraged;

            // 8599 Enrage is the core's own generic +damage aura, applied the
            // way Falling Sky applies its speed buff: AddAura takes no cast, no
            // global cooldown and no line of sight, and answers null rather
            // than throwing for anything it cannot land on.
            if (Aura* rage = target->AddAura(SPELL_ENRAGE, target))
                AuraDurationEdit::Edit(rage, ENRAGE_MS);

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r It wakes up angry.");
        }

        // ==================================================================
        // C32 - Arcane Frailty (59)
        //
        // "Thirty percent less health, thirty percent more spell damage."
        //
        // The purest trade in the family: a smaller pool for a bigger hit, with
        // no condition on either half. Both numbers are on the same card and
        // both are real.
        // ==================================================================
        // Half at rank IV, and it can go there because this mechanic relaxes the
        // MaxHealth floor to its own number -- see RelaxCaps below. Without
        // that, Gauntlet.Caps.MaxHealth at 0.6 would clamp the last rank back
        // to the third and the offer would promise a drop it never delivers.
        constexpr float FRAILTY_HEALTH[] = { 0.80f, 0.70f, 0.60f, 0.50f };
        static_assert(std::size(FRAILTY_HEALTH) >= MAX_RANK, "FRAILTY_HEALTH is short a rank");
        constexpr uint32 FRAILTY_DAMAGE_PCT[] = { 20, 30, 40, 50 };
        static_assert(std::size(FRAILTY_DAMAGE_PCT) >= MAX_RANK, "FRAILTY_DAMAGE_PCT is short a rank");

        class ArcaneFrailty final : public IMechanic
        {
        public:
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                if (kind != AggregateKind::MaxHealth)
                    return 1.0f;

                uint8 const rank = self.rank < 1 ? 1 : (self.rank > MAX_RANK ? MAX_RANK : self.rank);
                return FRAILTY_HEALTH[rank - 1];
            }

            // Without this the floor eats the affix: Gauntlet.Caps.MaxHealth is
            // 0.6 and rank III is exactly 0.6, so a wound or a grouped Lone
            // Wolf alongside it would be clamped away. It relaxes to its own
            // number and no further -- the same rule Lone Wolf and Cursed Hoard
            // established in Phase 3.
            void RelaxCaps(AffixInstance const& self, AggregateKind kind,
                           AggregateCaps& caps) const override
            {
                if (kind != AggregateKind::MaxHealth)
                    return;

                uint8 const rank = self.rank < 1 ? 1 : (self.rank > MAX_RANK ? MAX_RANK : self.rank);
                float const want = FRAILTY_HEALTH[rank - 1];
                if (caps.maxHealthMin > want)
                    caps.maxHealthMin = want;
            }

            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const* info) override
            {
                if (!info)
                    return 1.0f;   // spells only, which is what the card says

                return 1.0f + float(FRAILTY_DAMAGE_PCT[RankIndexOf(ctx.self)]) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint8 const  i    = RankIndexOf(&self);
                uint32 const less = uint32((1.0f - FRAILTY_HEALTH[i]) * 100.0f + 0.5f);

                // No BoonClause: the damage is the other half of the same
                // sentence, not a separate promise.
                return std::to_string(less) + "% less health, and "
                     + std::to_string(FRAILTY_DAMAGE_PCT[i]) + "% more spell damage.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return "arcane frailty: health x"
                     + std::to_string(FRAILTY_HEALTH[RankIndexOf(ctx.self)]);
            }
        };
    }

    GAUNTLET_MECHANIC(57, FickleSheep);
    GAUNTLET_MECHANIC(59, ArcaneFrailty);
    GAUNTLET_MECHANIC(56, ColdFeet);
    GAUNTLET_MECHANIC(58, ManaBurn);
}
