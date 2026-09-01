/*
 * mod-gauntlet - 110 Fresh Kill: loot it now, or it holds nothing but the quest
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "GameTime.h"
#include "Group.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <algorithm>
#include <string>
#include <vector>

// Registry id 110, docs/greed-redesign.md section 7.3.
//
// The card rewrites when a corpse pays. Open it within eight seconds of the
// kill and its table is rolled twice; leave it and it holds nothing but the
// quest item. Everything the player does between those two states -- finishing
// the pull, chaining a Frenzy, backing off to heal -- is now a choice with a
// price on it, which is the whole design: the curse is the clock, and the
// reward is for taking the risk of stopping mid-fight to loot.
//
// Two rules from section 7.1 are load-bearing here:
//
//   * A card may take your greens, never your quest. Loot::items and
//     Loot::quest_items are separate vectors (LootMgr.h:320-321), so emptying
//     one leaves the other standing.
//   * One player's curse may never be the group's. Emptying a corpse in a
//     group would take loot from people who are not carrying this affix, so
//     the emptying half is skipped entirely while grouped. The doubling half
//     is not: a second roll is generous, and generosity is allowed to spill.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_FRESH_KILL = 110;

        // How many corpses can be waiting at once. A pull leaves a handful and
        // the window is eight seconds, so this only has to outlast a fight;
        // it is capped because nothing else prunes it.
        constexpr size_t MAX_PENDING = 32;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_FRESH_KILL);
            return def ? def->key : "fresh_kill";
        }

        class FreshKill final : public IMechanic
        {
        public:
            void OnKill(Ctx& ctx, Creature* killed) override { Remember(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Remember(ctx, killed); }

            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;

            std::string Describe(AffixInstance const& self) const override
            {
                std::string out = "A corpse opened within "
                                + std::to_string(Rules::FRESH_KILL_WINDOW_MS / 1000u)
                                + " seconds of the kill is looted twice. After that it holds "
                                  "nothing but the quest.";
                out += BoonClause(self.boon, self.boonMag);
                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "fresh kill: " + std::to_string(_marked) + " kill(s) timed, "
                                + std::to_string(_doubled) + " looted in time, "
                                + std::to_string(_emptied) + " emptied, "
                                + std::to_string(_pending.size()) + " still warm";
                if (ctx.player && ctx.player->GetGroup())
                    out += "; grouped, so nothing is emptied";
                return out;
            }

        private:
            struct Corpse
            {
                ObjectGuid guid;
                uint32     killedMs = 0;
            };

            void Remember(Ctx& /*ctx*/, Creature* killed)
            {
                if (!killed)
                    return;

                if (_pending.size() >= MAX_PENDING)
                    _pending.erase(_pending.begin());

                _pending.push_back({ killed->GetGUID(),
                                     static_cast<uint32>(GameTime::GetGameTimeMS().count()) });
                ++_marked;
            }

            std::vector<Corpse> _pending;
            uint32 _marked  = 0;
            uint32 _doubled = 0;
            uint32 _emptied = 0;
        };

        void FreshKill::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot)
        {
            Player* player = ctx.player;
            if (!loot || !player)
                return;

            auto const it = std::find_if(_pending.begin(), _pending.end(),
                                         [&](Corpse const& c) { return c.guid == lootGuid; });
            if (it == _pending.end())
                return;   // not something this card watched die

            uint32 const now     = static_cast<uint32>(GameTime::GetGameTimeMS().count());
            bool   const inTime  = now - it->killedMs <= Rules::FRESH_KILL_WINDOW_MS;
            _pending.erase(it);

            if (inTime)
            {
                Creature* creature = ObjectAccessor::GetCreature(*player, lootGuid);
                uint32 const lootId = creature ? creature->GetCreatureTemplate()->lootid : 0;
                if (lootId == 0)
                    return;

                // FillLoot appends through LootTemplate::Process -> Loot::AddItem
                // (LootMgr.cpp:561, :481), so a second call is a second roll of
                // the same table rather than a replacement. Called from
                // OnPlayerBeforeSendLoot, which runs before the window packet.
                for (uint32 roll = 1; roll < Rules::FRESH_KILL_ROLLS; ++roll)
                    loot->FillLoot(lootId, LootTemplates_Creature, player, true, true);

                ++_doubled;
                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Fresh Kill");
                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Still warm: this one pays twice.");
                return;
            }

            // Cold. Section 7.1's group rule: never take loot from people who
            // are not carrying this card.
            if (player->GetGroup())
                return;

            if (loot->items.empty())
                return;

            loot->items.clear();
            ++_emptied;

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Too slow. It has nothing left worth taking.");
        }
    }

    GAUNTLET_MECHANIC(110, FreshKill);
}
