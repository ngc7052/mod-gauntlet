/*
 * mod-gauntlet - 111 Blood Price: opening a corpse costs blood, and hurt pays double
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <string>

// Registry id 111, docs/greed-redesign.md section 7.3.
//
// It makes looting the dangerous act. Every corpse costs 3% of your health to
// open, and a corpse opened below half health is rolled twice -- so the card
// pays exactly when paying it is worst, and the greedy line is to loot while
// still bleeding rather than to rest first.
//
// The window is the unit of decision, not the item: the alternative seam is
// OnPlayerStoreNewItem, which charges per item taken, and it is rejected
// because a window is one choice and a fistful of greys is not four.
//
// It can never be the thing that kills the run. Rules::BloodPriceCost floors
// the cost at one health below the player's current total, the way Blood
// Magic's cost is floored, and it is the arithmetic that does it rather than a
// clamp someone has to remember at the call site.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_BLOOD_PRICE = 111;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_BLOOD_PRICE);
            return def ? def->key : "blood_price";
        }

        class BloodPrice final : public IMechanic
        {
        public:
            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override
            {
                Player* player = ctx.player;
                if (!loot || !player || !player->IsInWorld() || !player->IsAlive())
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                // Read before the cost is paid: the card's promise is about the
                // health the player chose to open the corpse at, and taking the
                // toll first would let a corpse pay double because of its own
                // toll.
                bool const hurt = player->GetHealthPct() < 50.0f;

                uint32 const cost = Rules::BloodPriceCost(player->GetMaxHealth(), player->GetHealth());
                if (cost != 0)
                {
                    player->ModifyHealth(-static_cast<int32>(cost));
                    _paid += cost;
                }
                ++_opened;

                if (!hurt)
                    return;

                Creature* creature = ObjectAccessor::GetCreature(*player, lootGuid);
                uint32 const lootId = creature ? creature->GetCreatureTemplate()->lootid : 0;
                if (lootId == 0)
                    return;

                for (uint32 roll = Rules::BLOOD_PRICE_ROLLS_WHOLE;
                     roll < Rules::BLOOD_PRICE_ROLLS_HURT; ++roll)
                    loot->FillLoot(lootId, LootTemplates_Creature, player, true, true);

                ++_doubled;

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Blood Price");
                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Bleeding hands find more. This one pays twice.");
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "Opening a corpse costs " + std::to_string(Rules::BLOOD_PRICE_PCT)
                     + "% of your health. A corpse opened below half health is looted twice.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "blood price: " + std::to_string(_opened) + " corpse(s) opened, "
                                + std::to_string(_paid) + " health paid, "
                                + std::to_string(_doubled) + " looted twice";
                if (ctx.player)
                    out += ctx.player->GetHealthPct() < 50.0f
                         ? "; hurt now, so the next corpse pays double"
                         : "; whole now, so the next corpse pays once";
                return out;
            }

        private:
            uint32 _opened  = 0;
            uint32 _paid    = 0;
            uint32 _doubled = 0;
        };
    }

    GAUNTLET_MECHANIC(111, BloodPrice);
}
