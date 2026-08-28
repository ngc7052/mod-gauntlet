/*
 * mod-gauntlet - R1 Self-found: what you find is what you get
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Chat.h"
#include "LootMgr.h"
#include "Player.h"

#include <algorithm>
#include <limits>
#include <string>

// Registry id 23. Design section 3, card R1: "You cannot trade, mail, or use
// the auction house. Coin drops are 30% richer."
//
// Tiers 1-4, which is levels 5 to 20 -- "before it costs anything", as the
// card puts it, and that timing is the whole of the design. A self-found rule
// taken at level 5 shapes a run; the same rule taken at 60 just deletes a bank
// alt's worth of gear the player already owns.
//
// It is the one Rules row that is a rule in the strict sense: three refusals
// and no arithmetic. Which makes the refusals themselves the entire player
// experience of it, and the reason each one says which affix stopped it.

namespace Gauntlet
{
    namespace
    {
        class SelfFound final : public IMechanic
        {
        public:
            bool Allows(Ctx& ctx, Restricted what) override
            {
                char const* line = nullptr;
                switch (what)
                {
                    case Restricted::Trade:
                        line = "Self-found: you cannot trade. What you find is what you get.";
                        break;
                    case Restricted::Mail:
                        line = "Self-found: you cannot send mail. What you find is what you get.";
                        break;
                    case Restricted::AuctionBid:
                        line = "Self-found: the auction house is closed to you.";
                        break;
                }

                // The refusal is the affix. The core's own answer to a vetoed
                // trade is a generic client error and to a vetoed mail is
                // nothing at all, so without this line the player sees a
                // button that does not work and has no way to learn why --
                // which is design section 5's fourth rule, in the one family
                // that has no visible effect of any other kind.
                if (line && ctx.player && ctx.player->GetSession())
                    ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r {}", line);

                ++_refused;
                return false;
            }

            // The card's boon, paid where the coin is counted. Note what is
            // NOT restricted: looting, vendors, quest rewards, the mailbox as
            // a *receiver*. The card names three verbs and this implements
            // exactly those three.
            //
            // Mail already in the box when the affix is taken is deliberately
            // left alone, and an auction already bid on is left to resolve:
            // both are the player's existing property, and confiscating what a
            // character owned before the rule existed is a different and much
            // ruder affix than the one on the card. The rule binds what the
            // player does from now on.
            void OnLootMoney(Ctx& ctx, Loot* loot) override
            {
                if (!loot || !loot->gold || !ctx.self)
                    return;

                float const mult = BoonMoneyMult(*ctx.self);
                if (mult <= 1.0f)
                    return;

                uint64 const raised = static_cast<uint64>(static_cast<double>(loot->gold) * mult);
                loot->gold = static_cast<uint32>(std::min<uint64>(raised, std::numeric_limits<uint32>::max()));
            }

            std::string Describe(AffixInstance const& self) const override
            {
                std::string out = "You cannot trade, send mail or bid at the auction house."
                                  " Anything you use, you found.";
                out += BoonClause(self.boon, self.boonMag);
                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "self-found: refused " + std::to_string(_refused) + " action(s) this session";
            }

        private:
            uint32 _refused = 0;
        };
    }

    GAUNTLET_MECHANIC(23, SelfFound);
}
