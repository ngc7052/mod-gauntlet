/*
 * mod-gauntlet - 113 Scavenge: looting is how you recover
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "../Boons.h"

#include "Chat.h"
#include "LootMgr.h"
#include "Player.h"

#include <string>

// Registry id 113, docs/commons.md section 4b.
//
// A common by the ladder's own definition -- one small trade, a single axis,
// no state -- with the upside paid on an act rather than through a boon, which
// is what MF_RewardShaped means and why this card exists at all. Section 4b
// has the measurement: slot B's reward-shaped guarantee draws from the cards
// of a family the other two slots did not use, and at tier 1 the Attrition
// family's only reward-shaped card was Blood Price, a rare. So a tier-1 set
// whose other slots took Rules and Enemy had no non-rare answer.
//
// It is deliberately Blood Price's opposite. That card makes opening a corpse
// cost health; this one makes it restore health. A run offered both is being
// asked what kind of looter it is, and carrying both is a wash on the same
// axis -- which the generator is free to allow, because the two are different
// mechanics rather than one card twice.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_SCAVENGE = 113;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_SCAVENGE);
            return def ? def->key : "scavenge";
        }

        class Scavenge final : public IMechanic
        {
        public:
            // The curse is a coefficient and needs no code beyond this: the
            // aggregate applies it, caps it and gates it on the instance's
            // condition, all of which is free.
            float AggregateFactor(AffixInstance const& /*self*/, AggregateKind kind) const override
            {
                if (kind != AggregateKind::DamageTaken)
                    return 1.0f;
                return 1.0f + float(Rules::SCAVENGE_TAKEN_PCT) / 100.0f;
            }

            void OnLoot(Ctx& ctx, ObjectGuid const& /*lootGuid*/, Loot* loot) override
            {
                Player* player = ctx.player;
                if (!loot || !player || !player->IsInWorld() || !player->IsAlive())
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const heal = Rules::ScavengeHeal(player->GetMaxHealth());
                uint32 const before = player->GetHealth();
                player->ModifyHealth(static_cast<int32>(heal));

                ++_looted;
                _healed += player->GetHealth() - before;

                // Once, at the first corpse. The health bar moves and the
                // affix is in the panel; a line per corpse would be noise, but
                // the player has to be told once why looting is mending them.
                if (_looted == 1 && player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Scavenge: the corpse gives something back.");

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Scavenge");
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "You take " + std::to_string(Rules::SCAVENGE_TAKEN_PCT)
                     + "% more damage. Every corpse you loot restores "
                     + std::to_string(Rules::SCAVENGE_HEAL_PCT) + "% of your health.";
            }

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "scavenge: " + std::to_string(_looted) + " corpse(s) looted, "
                                + std::to_string(_healed) + " health restored";
                if (_looted == 0)
                    out += "; nothing has been looted since this was attached";
                return out;
            }

        private:
            uint32 _looted = 0;
            uint32 _healed = 0;
        };
    }

    GAUNTLET_MECHANIC(113, Scavenge);
}
