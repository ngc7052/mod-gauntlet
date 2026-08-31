/*
 * mod-gauntlet - A2 Blood Magic: every spell is paid for twice
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Chat.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <iterator>

// Registry id 20. Design section 3, card A2: "Spells cost 3% of your maximum
// health in addition to mana."
//
// The card's own note explains why it is the one character-side tax the design
// keeps: "it is the one here that touches a system the player actively
// manages". A flat damage-taken coefficient is something that happens to you;
// this is a price you pay, per cast, at a moment you chose, and the
// counterplay the card names -- "fewer, bigger casts; wands and melee; more
// deliberate downtime" -- is a change in how the character is played rather
// than a change in how carefully it is played.
//
// Heals are included on purpose. The card says so, and the loop of paying
// health to restore health is the most interesting thing in the affix: at full
// health a heal is pure loss, and the decision of when to top up stops being
// automatic.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_BLOOD_MAGIC = 20;

        // The card's ladder: 2 -> 3 -> 5% of the maximum pool per cast, and 7
        // at rank IV. It still cannot kill -- the cost is refused below the
        // health it would take -- so the ladder prices casting, not living.
        constexpr uint32 PCT_OF_MAX = 3;


        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_BLOOD_MAGIC);
            return def ? def->key : "blood_magic";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // "Any spell with a power cost", narrowed to a *mana* cost, because the
        // card's sentence is "in addition to mana" and the row is restricted to
        // mana users. Without the narrowing a feral druid would pay health for
        // every rage ability in bear form and a retribution paladin for nothing
        // at all, which is neither the card nor a rule anyone could learn.
        //
        // GetPowerCost() is the calculated cost after talents and reductions
        // (Spell.h:594), which is the right one: a spell made free by a proc
        // costs no mana, so it costs no blood either.
        bool CostsMana(Spell const* spell)
        {
            if (!spell || spell->GetPowerCost() <= 0)
                return false;

            SpellInfo const* info = spell->GetSpellInfo();
            return info && info->PowerType == POWER_MANA;
        }

        class BloodMagic final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Publish(ctx); }

            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, MechanicKey(), 0);
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            // Boon::BonusDamage, and the card asks for it by name: "pair with a
            // spell-power boon in every offer". A caster paying health per cast
            // needs each cast to be worth more, or the affix is only ever an
            // instruction to cast less.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& /*ctx*/) const override
            {
                return "blood magic: " + std::to_string(PCT_OF_MAX)
                     + "% of max per cast, " + std::to_string(_paid) + " cast(s) paid this session, "
                     + std::to_string(_spared) + " spared at low health";
            }

        private:
            void Publish(Ctx& ctx);

            uint32 _paid   = 0;
            uint32 _spared = 0;
            bool   _warned = false;
        };

        void BloodMagic::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !CostsMana(spell))
                return;
            if (ctx.run && ctx.run->dead)
                return;
            if (!player->IsAlive())
                return;

            uint32 const pct  = PCT_OF_MAX;
            uint32 const want = uint32(uint64(player->GetMaxHealth()) * pct / 100u);
            if (want == 0)
                return;

            // "cannot reduce health below 1", and the card means it as a floor
            // rather than as a saving throw: casting at 5 health is allowed and
            // costs almost nothing, which is exactly the desperate, deliberate
            // moment the affix is trying to create. It must never be the thing
            // that kills the character -- a hardcore run ended by its own
            // healing spell would be a bug wearing a mechanic's clothes.
            uint32 const health = uint32(player->GetHealth());
            uint32 const cost   = health > 1 ? std::min(want, health - 1) : 0;

            if (cost == 0)
            {
                ++_spared;
                return;
            }

            if (cost < want)
                ++_spared;

            ++_paid;

            // Through Unit::DealDamage and not ModifyHealth, deliberately: this
            // way the player sees a red number and a combat log line naming the
            // damage, which is the whole of the affix's visible existence.
            // A health bar that silently drops is the invisible scalar this
            // redesign exists to delete.
            //
            // RunState::selfDamage is raised across the call because nothing
            // downstream can tell this apart from a blow the world landed --
            // the attacker is the player either way. While it is up, Deep
            // Wounds makes no wound of it and Last Rites spends no charge on
            // it. It is cleared unconditionally, including on the paths that
            // return early inside the core.
            bool* flag = ctx.run ? &ctx.run->selfDamage : nullptr;
            if (flag)
                *flag = true;

            Unit::DealDamage(player, player, cost, nullptr, SELF_DAMAGE, SPELL_SCHOOL_MASK_NORMAL,
                             nullptr, /*durabilityLoss*/ false);

            if (flag)
                *flag = false;

            Publish(ctx);

            // Once, the first time it happens in a session. The red number is
            // the ongoing telegraph; this line is what connects it to a cause
            // the first time, for a player with no addon.
            if (!_warned && player->GetSession())
            {
                _warned = true;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Blood Magic: your spells are paid for in blood as well as mana.");
            }
        }

        void BloodMagic::Publish(Ctx& ctx)
        {
            if (!ctx.player)
                return;

            // A standing percentage, because the cost is a standing rule and
            // has no event: what the player wants on screen is how much a cast
            // is about to cost, not how many they have paid for.
            AddonFor(ctx)->QueueStat(ctx.player, MechanicKey(),
                                     int32(PCT_OF_MAX));
        }

        std::string BloodMagic::Describe(AffixInstance const& self) const
        {

            std::string out = "Every spell that costs mana also costs "
                            + std::to_string(PCT_OF_MAX)
                            + "% of your maximum health, healing spells included."
                              " It can never take you below one health.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(20, BloodMagic);
}
