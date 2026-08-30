/*
 * mod-gauntlet - E3 Call to Arms: killing an enemy alerts its nearest kin
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "Unit.h"

#include <string>
#include <iterator>

// Registry id 8. Design section 3, card E3: "Killing an enemy alerts its
// nearest kin."
//
// The card calls this the affix that turns a camp into a puzzle, and the puzzle
// is geometry: kill the outermost mob first so its nearest kin is far, peel with
// CC, retreat between kills so the alerted mob leashes. Nothing about it is
// random -- the same camp killed in the same order alerts the same creatures --
// which is design section 2.8's third principle, and it is what makes the
// puzzle learnable rather than a die roll.
//
// It is on-kill and positional, so it shares the "onkill" family cap with
// Carrion, Death Rattle and Grudge (design section 4.1).

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_CALL_TO_ARMS = 8;

        // The card's ladder: R = 20 -> 30 -> 40 yd, 1 -> 1 -> 2 kin. Rank IV
        // is past the card at 50 yd and 3 kin: the radius is already wider
        // than a pull, so what the last rank really adds is that a kill in a
        // camp brings most of the camp.
        constexpr float RADIUS_YARDS[] = { 20.0f, 30.0f, 40.0f, 50.0f };
        static_assert(std::size(RADIUS_YARDS) >= MAX_RANK, "RADIUS_YARDS is short a rank");
        constexpr uint32 KIN_COUNT[]    = { 1, 1, 2, 3 };
        static_assert(std::size(KIN_COUNT) >= MAX_RANK, "KIN_COUNT is short a rank");

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CALL_TO_ARMS);
            return def ? def->name : "Call to Arms";
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CALL_TO_ARMS);
            return def ? def->key : "call_to_arms";
        }

        class CallToArms final : public IMechanic
        {
        public:
            void OnKill(Ctx& ctx, Creature* killed) override { Alert(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Alert(ctx, killed); }

            // BonusExperience. The curse manufactures extra fights out of a
            // camp the player was going to clear anyway, and the boon is what
            // those fights are worth -- which is the one thing design section 5
            // says experience is good for ("use XP as boon currency").
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Alert(Ctx& ctx, Creature* killed);
        };

        void CallToArms::Alert(Ctx& ctx, Creature* killed)
        {
            Player* player = ctx.player;
            if (!player || !killed || !player->IsInWorld() || !player->IsAlive())
                return;

            if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                return;

            // Nothing this module put into the world may pull the zone in
            // behind it: a Shade dying next to a camp is not a kill the camp
            // has any business hearing.
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;

            // "its nearest kin" -- so the corpse has to be a creature the camp
            // would recognise as one of its own. An elite, a boss or a quest
            // giver is nobody's kin for this purpose, which is the same set
            // every other Phase 2 mechanic works from.
            if (!IsOrdinaryFoe(killed))
                return;

            uint8 const  i      = RankIndex(ctx.self);
            float const  range  = RADIUS_YARDS[i];
            uint32 const wanted = KIN_COUNT[i];

            // The search starts at the corpse and not at the player, which is
            // the whole of the counterplay: killing the outermost mob puts the
            // circle where the camp is thinnest, and retreating between kills
            // moves the player out of the circle the alerted creature has to
            // cross. `killed` is still in the world here -- OnPlayerCreatureKill
            // fires from Unit::Kill at Unit.cpp:14306, long before the corpse
            // decays -- so its position is the position it died at.
            Creature* previous = nullptr;
            uint32    alerted  = 0;

            for (uint32 n = 0; n < wanted; ++n)
            {
                Creature* kin = NearestIdleKin(player, killed, killed, range, previous);
                if (!kin)
                    break;

                // AI()->AttackStart is what the card names, and it is the same
                // entry point a creature's own aggro takes: it sets the victim,
                // enters combat and starts the chase. AddThreat afterwards
                // makes the owner the top of the list, so a groupmate standing
                // closer does not immediately steal what this player's affix
                // produced.
                if (CreatureAI* ai = kin->AI())
                    ai->AttackStart(player);

                kin->AddThreat(player, 1.0f);                     // Unit.h:1099

                previous = kin;
                ++alerted;
            }

            if (alerted == 0)
                return;

            // The affix says that it acted, which design section 4.8 asks for,
            // and it says it once per kill rather than once per creature.
            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    alerted == 1 ? "|cffff2020[Gauntlet]|r The kill is heard. One of its kin answers."
                                 : "|cffff2020[Gauntlet]|r The kill is heard. Its kin answer.");
        }

        std::string CallToArms::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "Killing an enemy alerts ";
            out += KIN_COUNT[i] == 1 ? "the nearest idle enemy of its own kind"
                                     : "the two nearest idle enemies of its own kind";
            out += " within " + std::to_string(static_cast<uint32>(RADIUS_YARDS[i]))
                 + " yards of the corpse. Kill the outermost first, and retreat between kills.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(8, CallToArms);
}
