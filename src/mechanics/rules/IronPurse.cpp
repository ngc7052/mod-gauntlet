/*
 * mod-gauntlet - R3 Iron Purse: repairs cost double
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Chat.h"
#include "Player.h"

#include <string>

// Registry id 25. Design section 3, card R3: "Repairs cost double."
//
// The design is candid about why the Rules family exists at all -- "these do
// not create moments ... they exist because hardcore players already impose
// them on themselves and enjoy the identity" -- and this is the plainest of
// the three. It is one hook and one multiplication.
//
// It is also, said plainly and on purpose, the weakest row in the table. Its
// window is tiers 1-3, which with Gauntlet.TierInterval at 5 is levels 5 to
// 15, and a repair bill at level 10 is a few silver; doubling it is a rounding
// error against the first quest reward. On a hardcore realm the player also
// only ever dies once, so the durability loss that makes repairs matter at all
// hardly arrives.
//
// It earns its place anyway, for a reason that has nothing to do with how it
// feels: tier 1 carried four offerable rows across exactly three families
// against three distinct-family slots, with no slack at all, and this is one
// of the three cheapest rows that widen it. That is a real argument and it is
// not the same as the affix being good. If a later phase wants the slot back,
// this is the row to spend.

namespace Gauntlet
{
    namespace
    {
        // The card's one number, and it does not ladder: R3 is maxRank 1 in
        // the registry, like every Rules row. A rule is a rule or it is a
        // scalar with extra steps.
        constexpr float REPAIR_MULT = 2.0f;

        class IronPurse final : public IMechanic
        {
        public:
            // The name says discount and the arithmetic says multiplier. It
            // arrives as the reputation price discount
            // ($CORE/src/server/game/Handlers/NPCHandler.cpp:780-782) and the
            // core spends it as `costs = uint32(costs * discountMod * rate)`
            // (Player.cpp:4955), so the bill is doubled by *multiplying* this
            // number, not by halving it. Reading the name instead of the call
            // site turns the affix into a 50% repair discount -- a curse that
            // is quietly a reward, and invisible until someone reads the gold.
            void OnRepair(Ctx& ctx, float& discountMod) override
            {
                if (discountMod <= 0.0f)
                    return;

                discountMod *= REPAIR_MULT;
                Announce(ctx.player);
            }

            std::string Describe(AffixInstance const& self) const override
            {
                std::string out = "Repairing your gear costs twice as much.";
                out += BoonClause(self.boon, self.boonMag);
                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "iron purse: repairs x2, told " + std::to_string(_told) + " time(s)";
            }

        private:
            // Once per session, at the first repair. The bill is on screen
            // already and the affix is in the panel; a line every visit to
            // every blacksmith would be noise, and design section 5's rule is
            // that the player must be able to see the affix act -- once is
            // enough to connect the price to the cause.
            void Announce(Player* player)
            {
                if (_told != 0 || !player || !player->GetSession())
                    return;

                ++_told;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Iron Purse: that repair cost you double.");
            }

            uint32 _told = 0;
        };
    }

    GAUNTLET_MECHANIC(25, IronPurse);
}
