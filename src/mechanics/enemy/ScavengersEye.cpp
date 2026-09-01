/*
 * mod-gauntlet - 88 Scavenger's Eye: seen sooner, and paid for a clean fight
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <algorithm>
#include <string>
#include <vector>

// Registry id 88, from docs/greed-redesign.md section 7.2, and the first
// Uncommon in the table that is a mechanic rather than a table row.
//
// It is half of Keen-nosed (13): five yards of extra notice where that card
// gives eight, sharing the same sweep out of Nearby.cpp. What makes it its own
// card is the other half -- a fight in which nothing lays a hand on you rolls
// its corpse twice.
//
// That pairing is the point. The curse pulls fights you did not choose, and
// the reward is only paid for a fight fought perfectly, so the card asks the
// player to be *better* at the thing it makes harder rather than to endure it.
// It is why this row carries MF_RewardShaped honestly: the loot is not a boon
// bolted on, it is the card's own mechanic paying out on engagement, which is
// the standard Champions and Killing Floor set. docs/commons.md has the
// measurement that made three such cards the most valuable thing the table
// could gain.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_SCAVENGERS_EYE = 88;

        // Keen-nosed's numbers for the search itself: it has to reach past the
        // widest aggro range a creature can have plus the bonus, and asking
        // twice a second is asking more often than a walking player can change
        // the answer.
        constexpr float  SEARCH_YARDS = 60.0f;
        constexpr uint32 SWEEP_MS     = 1000;

        // How many corpses can be waiting to be looted at once. A player
        // clears a camp and loots it corpse by corpse, so this has to hold a
        // pull; it is capped because nothing else prunes it -- a corpse the
        // player never opens would otherwise sit in the list until logout.
        constexpr size_t MAX_PENDING = 32;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_SCAVENGERS_EYE);
            return def ? def->key : "scavengers_eye";
        }

        class ScavengersEye final : public IMechanic
        {
        public:
            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;

                _sweepMs += diffMs;
                if (_sweepMs < SWEEP_MS)
                    return;
                _sweepMs = 0;

                if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                    return;

                // Keen-nosed's exclusions, and every one of them is an escape
                // design section 2.8 says must stay: stealth, the mount, the
                // inn, and being somewhere the affix has no business.
                if (!player->IsInWorld() || !player->IsAlive())
                    return;
                if (player->IsMounted() || player->IsInFlight())
                    return;
                if (player->HasStealthAura())
                    return;
                if (player->IsGameMaster())
                    return;

                // This is about being noticed *before* the pull, and keeping
                // the grid search off the hot path of a real fight.
                if (player->IsInCombat())
                    return;

                if (player->HasRestFlag(REST_FLAG_IN_TAVERN) ||
                    player->HasRestFlag(REST_FLAG_IN_CITY))
                    return;

                if (Map* map = player->GetMap())
                    if (map->IsBattlegroundOrArena())
                        return;

                uint32 const alerted = AlertUnaware(player, Rules::SCAVENGER_YARDS, SEARCH_YARDS);
                if (alerted == 0)
                    return;

                _alerted += alerted;

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Scavenger's Eye");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Something saw you coming.");
            }

            // A fight starts clean. `wasOutOfCombat` is what separates a new
            // pull from another enemy joining one already in progress -- a
            // fight that is going badly must not be laundered into a clean one
            // by a second attacker arriving.
            void OnEnterCombat(Ctx& /*ctx*/, Unit* /*enemy*/, bool wasOutOfCombat) override
            {
                if (wasOutOfCombat)
                    _hit = false;
            }

            // Any damage at all, from anything. Not "damage from the thing you
            // are fighting": a fall, a patrol's arrow and a proc all end the
            // claim, because the card promises a fight nothing touched you in.
            void OnDamageTaken(Ctx& /*ctx*/, Unit* /*attacker*/, uint32 amount) override
            {
                if (amount > 0)
                    _hit = true;
            }

            void OnKill(Ctx& ctx, Creature* killed) override { Mark(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Mark(ctx, killed); }

            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "Enemies notice you from " + std::to_string(int32(Rules::SCAVENGER_YARDS))
                     + " yards further away. A fight in which nothing lays a hand on you rolls its "
                       "loot twice.";
            }

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "scavenger's eye: alerted " + std::to_string(_alerted)
                                + " creature(s), " + std::to_string(_marked) + " clean kill(s), "
                                + std::to_string(_rolled) + " corpse(s) rolled again, "
                                + std::to_string(_pending.size()) + " waiting";
                out += _hit ? "; this fight is already dirty" : "; this fight is still clean";
                return out;
            }

        private:
            void Mark(Ctx& /*ctx*/, Creature* killed)
            {
                if (!killed || _hit)
                    return;

                // A creature with no loot table of its own can still be
                // marked: whether it has one is LootMgr's answer at loot time,
                // not this card's, and asking here would mean holding a
                // template pointer for a creature that is about to despawn.
                if (_pending.size() >= MAX_PENDING)
                    _pending.erase(_pending.begin());

                _pending.push_back(killed->GetGUID());
                ++_marked;
            }

            std::vector<ObjectGuid> _pending;
            uint32 _sweepMs = 0;
            uint32 _alerted = 0;
            uint32 _marked  = 0;
            uint32 _rolled  = 0;
            bool   _hit     = false;
        };

        // Split out because it is the half of the card that touches the core's
        // loot machinery, and that machinery deserves its argument written down.
        void ScavengersEye::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot)
        {
            Player* player = ctx.player;
            if (!loot || !player)
                return;

            auto const it = std::find(_pending.begin(), _pending.end(), lootGuid);
            if (it == _pending.end())
                return;

            _pending.erase(it);

            Creature* creature = ObjectAccessor::GetCreature(*player, lootGuid);
            if (!creature)
                return;

            uint32 const lootId = creature->GetCreatureTemplate()->lootid;
            if (lootId == 0)
                return;   // nothing to roll twice; a beast with an empty table

            // Loot::FillLoot *appends*: it calls LootTemplate::Process, which
            // hands each winning entry to Loot::AddItem (LootMgr.cpp:561, :481).
            // So a second call on a Loot that is already filled is a second
            // roll of the same table rather than a replacement, which is
            // exactly what "rolls its loot twice" has to mean -- a percentage
            // on the first roll would have been a boon, and this card's upside
            // must be something the player earned.
            //
            // It is called here, from OnPlayerBeforeSendLoot, because that
            // hook fires before the window packet goes out
            // (GauntletScripts.cpp) -- a second roll after the client had the
            // list would be items the player could never see. AddItem stops at
            // MAX_NR_LOOT_ITEMS on its own, so a rich table cannot overflow
            // the window.
            for (uint32 roll = 1; roll < Rules::SCAVENGER_ROLLS; ++roll)
                loot->FillLoot(lootId, LootTemplates_Creature, player, true, true);

            ++_rolled;

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, "Scavenger's Eye");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Untouched: this one is worth searching twice.");
        }
    }

    GAUNTLET_MECHANIC(88, ScavengersEye);
}
