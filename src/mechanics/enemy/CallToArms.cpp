/*
 * mod-gauntlet - E3 Call to Arms: killing an enemy alerts its nearest kin
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <vector>
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
        constexpr float RADIUS_YARDS = 30.0f;
        constexpr uint32 KIN_COUNT = 1;


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

            // docs/greed-redesign.md section 3's sharpening: fighting next to
            // the camp becomes the lean-in instead of the mistake. This file is
            // what sends the kin, so it is the only thing that knows which
            // creatures arrived because of the card -- and the XP hook carries
            // the victim, so no guessing.
            void OnXP(Ctx& /*ctx*/, uint32& amount, Unit* victim) override
            {
                if (!victim)
                    return;

                auto const it = std::find(_answered.begin(), _answered.end(), victim->GetGUID());
                if (it == _answered.end())
                    return;

                _answered.erase(it);
                amount = Rules::CallToArmsXP(amount);
                ++_paid;
            }

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
            std::string Diagnose(Ctx&) const override
            {
                std::string out = "call to arms: " + std::to_string(_answered.size())
                                + " kin still owed the bonus, " + std::to_string(_paid) + " paid";
                if (_answered.empty() && _paid == 0)
                    out += "; nothing has answered a call since this was attached";
                return out;
            }

        private:
            void Alert(Ctx& ctx, Creature* killed);

            // The kin this card pulled, waiting to be worth more than they
            // would have been. Capped: a camp is a handful and nothing else
            // prunes this.
            void Answered(ObjectGuid const& guid)
            {
                constexpr std::size_t MAX_ANSWERED = 16;
                if (_answered.size() >= MAX_ANSWERED)
                    _answered.erase(_answered.begin());
                _answered.push_back(guid);
            }

            std::vector<ObjectGuid> _answered;
            uint32                  _paid = 0;
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

            float const  range  = RADIUS_YARDS;
            uint32 const wanted = KIN_COUNT;

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
                if (kin)
                    Answered(kin->GetGUID());
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

            std::string out = "Killing an enemy alerts ";
            out += KIN_COUNT == 1 ? "the nearest idle enemy of its own kind"
                                     : "the two nearest idle enemies of its own kind";
            out += " within " + std::to_string(static_cast<uint32>(RADIUS_YARDS))
                 + " yards of the corpse. Kill the outermost first, and retreat between kills.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(8, CallToArms);
}
