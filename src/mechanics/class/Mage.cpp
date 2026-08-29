/*
 * mod-gauntlet - the mage's two: Cold Feet, Mana Burn
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "PermanentCooldown.h"

#include "Chat.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

#include <algorithm>
#include <string>

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
        constexpr uint32 BLINK_COST_PCT[MAX_RANK] = { 15, 25, 0 };

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

        constexpr uint32 BURN_PCT[MAX_RANK] = { 30, 50, 100 };

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
                if (player->getPowerType() != POWER_MANA)
                    return;

                uint32 const burn = uint32(uint64(amount) * BURN_PCT[RankIndexOf(ctx.self)] / 100u);
                if (burn == 0)
                    return;

                int32 const left = player->GetPower(POWER_MANA);
                player->SetPower(POWER_MANA, std::max<int32>(0, left - int32(burn)));
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

            std::string Diagnose(Ctx& ctx) const override
            {
                return "mana burn: " + std::to_string(BURN_PCT[RankIndexOf(ctx.self)]) + "% of damage taken";
            }
        };
    }

    GAUNTLET_MECHANIC(56, ColdFeet);
    GAUNTLET_MECHANIC(58, ManaBurn);
}
