/*
 * mod-gauntlet - 112 Trophy Hunter: the silver dragon is a threat and a payday
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletState.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "SharedDefines.h"

#include <string>

// Registry id 112, docs/greed-redesign.md section 7.3, and an uncommon by the
// rarity ladder's own definition: a trade with a condition. The condition is
// "a silver dragon is nearby", which is a thing the world decides rather than
// the clock or the player's health, and that is what makes it worth having --
// the other ten conditions in the table are all states the player is in.
//
// While one is alive within a hundred yards everything hits 15% harder. Kill
// it and it leaves a chest and banks a reroll charge, which is the link
// between this document and the offer economy of the rarity plan's step 3:
// loot that pays in the currency the offer screen spends.
//
// "Rare" here is the silver dragon, CreatureTemplate::rank of
// CREATURE_ELITE_RARE (4) or CREATURE_ELITE_RAREELITE (2) -- SharedDefines.h:
// 2962-2969. Creature::isElite() deliberately excludes rank 4, which is why
// this reads the rank itself rather than asking isElite().

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_TROPHY_HUNTER = 112;

        // The scan's cadence, on top of the module's 500 ms tick. A hundred
        // yards is a wide net, and Keen-nosed's note applies unchanged: a grid
        // search once a second for the handful of real players carrying an
        // affix is cheap, and bots never reach it because IsEligible refuses
        // them. A silver dragon does not arrive faster than that.
        constexpr uint32 SWEEP_MS = 1000;

        bool IsSilverDragon(Creature const* c)
        {
            if (!c)
                return false;
            uint32 const rank = c->GetCreatureTemplate()->rank;
            return rank == CREATURE_ELITE_RARE || rank == CREATURE_ELITE_RAREELITE;
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_TROPHY_HUNTER);
            return def ? def->key : "trophy_hunter";
        }

        class TrophyHunter final : public IMechanic
        {
        public:
            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                Player* player = ctx.player;
                if (!player || !player->IsInWorld() || !player->IsAlive())
                    return;

                _sweepMs += diffMs;
                if (_sweepMs < SWEEP_MS)
                    return;
                _sweepMs = 0;

                bool near = false;
                for (Creature* c : CreaturesNear(player, Rules::TROPHY_YARDS))
                    if (c->IsAlive() && IsSilverDragon(c))
                    {
                        near = true;
                        break;
                    }

                if (near == _near)
                    return;

                _near = near;

                // Only on the edge. The card is a standing danger while one is
                // alive and a line every second would be unreadable, but the
                // player has to be told *why* everything started hitting
                // harder -- a 15% multiplier with no cause on screen is the
                // silent card this module keeps being told not to ship.
                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Trophy Hunter");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        _near ? "|cffff2020[Gauntlet]|r Something rare is close. Everything hits harder "
                                "while it lives."
                              : "|cffff2020[Gauntlet]|r The pressure lifts.");
            }

            float DamageTakenMult(Ctx& /*ctx*/, Unit* /*attacker*/, SpellInfo const*) override
            {
                return _near ? Rules::TrophyTakenMult() : 1.0f;
            }

            void OnKill(Ctx& ctx, Creature* killed) override { Claim(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Claim(ctx, killed); }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "While a rare creature is alive within a hundred yards you take "
                     + std::to_string(Rules::TROPHY_TAKEN_PCT)
                     + "% more damage. Killing one leaves a chest and banks a reroll charge.";
            }

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "trophy hunter: " + std::to_string(_claimed)
                                + " rare(s) killed, " + std::to_string(_chests) + " chest(s) left, "
                                + std::to_string(_banked) + " charge(s) banked";
                out += _near ? "; one is close right now" : "; nothing rare is near";
                return out;
            }

        private:
            void Claim(Ctx& ctx, Creature* killed)
            {
                Player* player = ctx.player;
                if (!player || !player->IsInWorld() || !IsSilverDragon(killed))
                    return;

                ++_claimed;

                // The chest is a real world game object of the player's own
                // level band, so its contents are the zone's rather than
                // anything this module invented -- section 7.1's honest limit.
                uint32 const entry = Rules::TrophyChestFor(player->GetLevel());
                if (player->SummonGameObject(entry,
                                             killed->GetPositionX(), killed->GetPositionY(),
                                             killed->GetPositionZ(), killed->GetOrientation(),
                                             0.0f, 0.0f, 0.0f, 0.0f,
                                             Rules::TROPHY_CHEST_SECONDS))
                    ++_chests;

                // One charge, the same as a skip banks. Two ways of earning the
                // offer screen's currency should be worth the same, or the
                // cheaper one is the only one anybody uses.
                if (ctx.state)
                {
                    uint8 const held = static_cast<uint8>(std::max<int32>(0,
                        ctx.state->Get(RunKeys::RerollCharges, Rules::REROLL_STARTING_CHARGES)));
                    ctx.state->Set(RunKeys::RerollCharges,
                                   static_cast<int32>(Rules::ChargesAfterSkip(held)));
                    ++_banked;
                }

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Trophy Hunter");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r A trophy. It left something, and the next offer is "
                        "yours to reroll.");
            }

            uint32 _sweepMs = 0;
            uint32 _claimed = 0;
            uint32 _chests  = 0;
            uint32 _banked  = 0;
            bool   _near    = false;
        };
    }

    GAUNTLET_MECHANIC(112, TrophyHunter);
}
