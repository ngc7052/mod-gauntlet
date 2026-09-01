/*
 * mod-gauntlet - 115 Tribute: every twenty-fifth kill leaves a chest, and the chest is watched
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletState.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "Player.h"

#include <algorithm>
#include <string>
#include <vector>

// Registry id 115, docs/greed-redesign.md section 7.3.
//
// A chest every twenty-fifth kill, and two scavengers when it is opened. The
// chest is not the card; the *decision about when to open it* is. It stands
// for five minutes, so a player can leave it and come back with the ground
// cleared, or open it now and fight for it -- which is the greed loop this
// whole document is about, at its smallest scale.
//
// Both seams are ones this module has already proven. The chest is
// WorldObject::SummonGameObject with the level-banded world chests Trophy
// Hunter uses, so nothing about the game object itself is new. And opening it
// arrives as an ordinary loot window: OnPlayerBeforeSendLoot fires for a
// chest's Loot exactly as it does for a corpse's, with the game object's guid,
// which is what tells this card its own chest was opened rather than somebody
// else's.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_TRIBUTE = 115;

        // Persistent, for Carrion's reason: a counter that resets at the login
        // screen cannot be planned around, and twenty-five kills is long
        // enough to cross a session.
        constexpr char const* KEY_KILLS = "tribute.kills";

        constexpr uint32 SCAVENGER_LIFE_MS = 120000;
        constexpr float  SCAVENGER_YARDS   = 5.0f;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_TRIBUTE);
            return def ? def->key : "tribute";
        }

        class Tribute final : public IMechanic
        {
        public:
            void OnKill(Ctx& ctx, Creature* killed) override { Count(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Count(ctx, killed); }

            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* /*loot*/) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;

                auto const it = std::find(_chests.begin(), _chests.end(), lootGuid);
                if (it == _chests.end())
                    return;   // somebody else's chest, or a corpse

                _chests.erase(it);
                ++_opened;

                // The watchers. Carrion's pack, chosen rather than inflicted:
                // the player opened this.
                uint32 spawned = 0;
                for (uint32 n = 0; n < Rules::TRIBUTE_SCAVENGERS; ++n)
                {
                    Position at = player->GetPosition();
                    player->GetClosePoint(at.m_positionX, at.m_positionY, at.m_positionZ,
                                          player->GetObjectSize(), SCAVENGER_YARDS);

                    Creature* scavenger = sGauntletSummons->Summon(player, ENTRY_SCAVENGER, at,
                                                                   SCAVENGER_LIFE_MS,
                                                                   /*countsAsStalker*/ false,
                                                                   MECHANIC_TRIBUTE);
                    if (!scavenger)
                        break;   // the caps refused; whatever arrived is the pack

                    ++spawned;
                }

                _scavengers += spawned;

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Tribute");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Something was watching the chest.");
            }

            void OnDetach(Ctx& ctx) override
            {
                _chests.clear();
                if (ctx.player)
                    sGauntletSummons->DespawnFor(ctx.player, MECHANIC_TRIBUTE);
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "Every " + std::to_string(Rules::TRIBUTE_EVERY)
                     + "th kill leaves a chest. Opening it draws "
                     + std::to_string(Rules::TRIBUTE_SCAVENGERS) + " scavengers.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                int32 const kills = ctx.state ? ctx.state->Get(KEY_KILLS, 0) : 0;
                return "tribute: " + std::to_string(kills) + " kill(s), "
                     + std::to_string(_left) + " chest(s) left, " + std::to_string(_opened)
                     + " opened, " + std::to_string(_scavengers) + " scavenger(s) drawn; "
                     + std::to_string(Rules::TRIBUTE_EVERY - (kills % Rules::TRIBUTE_EVERY))
                     + " kill(s) to the next";
            }

        private:
            void Count(Ctx& ctx, Creature* killed)
            {
                Player* player = ctx.player;
                if (!player || !killed || !player->IsInWorld() || !player->IsAlive())
                    return;
                if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                    return;

                // The module's own summons are not tribute. A card that paid
                // for killing another card's Shade would be paying the player
                // for a fight this module started.
                if (sGauntletSummons->IsGauntletSummon(killed) || !IsOrdinaryFoe(killed))
                    return;

                int32 const next = (ctx.state ? ctx.state->Get(KEY_KILLS, 0) : 0) + 1;
                if (ctx.state)
                    ctx.state->Set(KEY_KILLS, next);

                if (next % Rules::TRIBUTE_EVERY != 0)
                    return;

                uint32 const entry = Rules::TrophyChestFor(player->GetLevel());
                if (!player->SummonGameObject(entry,
                                              killed->GetPositionX(), killed->GetPositionY(),
                                              killed->GetPositionZ(), killed->GetOrientation(),
                                              0.0f, 0.0f, 0.0f, 0.0f,
                                              Rules::TROPHY_CHEST_SECONDS))
                    return;

                // SummonGameObject answers the object, but the module keeps no
                // registry of game objects the way it does of creatures, so the
                // guid is taken from the world: the chest is the one thing of
                // this entry standing where the corpse is.
                if (GameObject* chest = player->FindNearestGameObject(entry, 10.0f))
                {
                    constexpr std::size_t MAX_CHESTS = 8;
                    if (_chests.size() >= MAX_CHESTS)
                        _chests.erase(_chests.begin());
                    _chests.push_back(chest->GetGUID());
                }

                ++_left;

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Tribute");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r It left something behind. Opening it will not go unnoticed.");
            }

            std::vector<ObjectGuid> _chests;
            uint32 _left       = 0;
            uint32 _opened     = 0;
            uint32 _scavengers = 0;
        };
    }

    GAUNTLET_MECHANIC(115, Tribute);
}
