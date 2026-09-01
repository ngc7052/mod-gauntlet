/*
 * mod-gauntlet - E8 Keen-nosed: enemies notice you from further away
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "LootMgr.h"
#include "Map.h"
#include "Player.h"
#include "Unit.h"

#include <limits>
#include <string>
#include <iterator>

// Registry id 13. Design section 3, card E8: "Enemies notice you from further
// away."
//
// The card is precise about what it is for: "the map you already know is
// suddenly wrong by eight yards, and re-learning it is the content". So this is
// a routing rule, not a damage rule, and every one of its escapes is kept --
// stealth works, a mount works, hugging the edge of a path works, and a ranged
// opener still pulls a single.
//
// It is the module's one genuinely per-tick grid search. Design section 6
// measured it: "grid searches every 500 ms for a handful of real players is
// cheap; do not run them for bots (already excluded by IsEligible)". The tick
// below is throttled to that 500 ms and does nothing at all while the player is
// in combat, mounted, stealthed or standing in a rest area, which is most of a
// session.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_KEEN_NOSED = 13;

        // The card's number lives in Rules.h, where a test can reach it:
        // Scavenger's Eye (88) is the uncommon version of this curse and the
        // two are only meaningful against each other. Past about twelve yards
        // the affix stops being about pulling carefully and starts being
        // about whether a zone can be crossed at all, which is a different
        // affix.
        constexpr float BONUS_YARDS = Rules::KEEN_NOSED_YARDS;

        // How wide a net the grid search casts. It has to reach past the
        // widest aggro range a creature can have plus the widest bonus above;
        // a level 80 creature against a level 1 player tops out near 45 yards
        // (Creature::GetAggroRange, Creature.cpp:2185-2222), so sixty is
        // comfortable and still one grid cell.
        constexpr float SEARCH_YARDS = 60.0f;   // TODO(design)

        // The tick's own cadence, on top of the module's 500 ms. Aggro is a
        // proximity question and a player crosses at most a few yards in a
        // second at foot speed, so asking twice a second is asking more often
        // than the answer can change.
        constexpr uint32 SWEEP_MS = 1000;   // TODO(design)


        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_KEEN_NOSED);
            return def ? def->key : "keen_nosed";
        }

        class KeenNosed final : public IMechanic
        {
        public:
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // BonusMoney. The curse is a routing tax paid in fights the player
            // did not choose; the boon is what those fights leave on the
            // ground. Paid at the purse rather than through the aggregate,
            // because there is no AggregateKind for coin -- see Boons.h.
            void OnLootMoney(Ctx& ctx, Loot* loot) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Sweep(Ctx& ctx, Player* player);

            uint32 _sweepMs = 0;
        };

        void KeenNosed::OnTick(Ctx& ctx, uint32 diffMs)
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

            // The card's own exclusions, and each of them is an escape design
            // section 2.8's eighth principle says must stay: stealth, the
            // mount, and simply being somewhere the affix does not reach.
            if (!player->IsInWorld() || !player->IsAlive())
                return;
            if (player->IsMounted() || player->IsInFlight())            // Unit.h:1887, :1709
                return;
            if (player->HasStealthAura())                               // Unit.h:1841
                return;
            if (player->IsGameMaster())
                return;

            // An affix that alerts a camp while the player is already fighting
            // it is Call to Arms, not this one: this is about being noticed
            // *before* the pull. It also keeps the search off the hot path of
            // an actual fight.
            if (player->IsInCombat())
                return;

            // Nothing happens in an inn, a city or a battleground -- the first
            // two because a player at rest is not routing, the third because a
            // creature only its owner can be alerted by is furniture in
            // somebody else's match.
            if (player->HasRestFlag(REST_FLAG_IN_TAVERN) ||             // Player.h:1221
                player->HasRestFlag(REST_FLAG_IN_CITY))
                return;

            if (Map* map = player->GetMap())
                if (map->IsBattlegroundOrArena())                       // Map.h:307
                    return;

            // The grace window is the scheduler's rule for *events*, and this
            // is not one -- but a character that has just logged in or zoned is
            // exactly the character the courtesy was written for.
            if (ctx.run && ctx.run->graceMs != 0)
                return;

            Sweep(ctx, player);
        }

        void KeenNosed::Sweep(Ctx& ctx, Player* player)
        {
            // The search itself is Nearby.cpp's AlertUnaware, shared with
            // Scavenger's Eye. What is left here is the card's own half: how
            // far, and what the player is told.
            uint32 const alerted = AlertUnaware(player, BONUS_YARDS, SEARCH_YARDS);
            if (alerted == 0)
                return;

            // One line per sweep at most, and only when something actually
            // woke: this affix can fire every second while a player walks a
            // road, and a chat line per creature would be unreadable.
            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, "Keen-nosed");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r You are scented from further off than you should be.");
        }

        void KeenNosed::OnLootMoney(Ctx& ctx, Loot* loot)
        {
            if (!loot || !loot->gold || !ctx.self)
                return;

            float const mult = BoonMoneyMult(*ctx.self);
            if (mult <= 1.0f)
                return;

            uint64 const raised = static_cast<uint64>(static_cast<double>(loot->gold) * mult);
            loot->gold = static_cast<uint32>(std::min<uint64>(raised, std::numeric_limits<uint32>::max()));
        }

        std::string KeenNosed::Describe(AffixInstance const& self) const
        {

            std::string out = "Enemies notice you from "
                            + std::to_string(static_cast<uint32>(BONUS_YARDS))
                            + " yards further away than they should. Stealth still hides you, a"
                              " mount still outruns them, and nothing wakes while you are resting"
                              " or already fighting.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(13, KeenNosed);
}
