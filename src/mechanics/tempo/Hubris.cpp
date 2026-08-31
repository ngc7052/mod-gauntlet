/*
 * mod-gauntlet - T5 Hubris: the one you open on is the one you can fight
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"
#include "GauntletRules.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Creature.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <iterator>

// Registry id 18.
//
// Hubris was an experience rule: enemies below your level paid nothing, enemies
// above paid more. It was written to replace Forgetful, whose fault design
// section 5 names exactly -- "a flat experience tax: no moment, no verb" -- and
// it made the same mistake in a different currency. It never acted. It sat
// there and made ordinary play worth less, and its whole instruction to the
// player was "go and grind somewhere else", which is a routing tax rather than
// a decision. Reported from play as one of six cards that felt like taxes; see
// docs/tempo-redesign.md.
//
// What it does now happens in the first second of every fight and asks a
// question the player answers with their target key.
//
// The first enemy you strike is your duel. It hits you for less; everything
// else hits you for more. Open on the dangerous one and the rest of the pull is
// sharper than usual, or open on a straggler and be soft to the thing you were
// actually worried about. The duel is redeclared the moment you leave combat,
// so it is a decision per pull rather than a state to be managed.
//
// It chains with Overextended, which prices *facing*: one card asks who you are
// pointed at and the other asks which way. Carrying both turns a pull into a
// plan.

namespace Gauntlet
{
    namespace
    {
        // The ladders and the arithmetic live in GauntletRules.h so that
        // tests/RulesTest.cpp can reach them: this file includes Player.h and
        // is therefore invisible to the Player-free test build.
        using namespace Gauntlet::Rules;

        constexpr uint16 MECHANIC_HUBRIS = 18;

        // What the duel does to damage taken, as percentages.
        //
        // The duel is a shelter that gets deeper as the rank rises, and the
        // rest of the pull gets sharper faster -- so a higher rank is a better
        // deal for a player who picks correctly and a worse one for a player
        // who does not. That asymmetry is the point: the ladder escalates the
        // *stakes* of the choice rather than the size of a tax.

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_HUBRIS);
            return def ? def->key : "hubris";
        }

        class Hubris final : public IMechanic
        {
        public:
            // The duel is declared on the first blow of the fight, in either
            // direction: the enemy that opened on you counts just as much as
            // the one you opened on, because a player who is ambushed still
            // made no choice and should not be punished for the order.
            void OnEnterCombat(Ctx& ctx, Unit* enemy, bool /*wasOutOfCombat*/) override
            {
                if (_duel.IsEmpty() && enemy)
                    Declare(ctx, enemy);
            }

            void OnCreatureDamaged(Ctx& ctx, Creature* victim, uint32 /*damage*/) override
            {
                if (_duel.IsEmpty() && victim)
                    Declare(ctx, victim);
            }

            // A new pull is a new question.
            void OnLeaveCombat(Ctx& ctx) override
            {
                _duel.Clear();
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, MechanicKey(), 0);
            }

            // The whole card. Everything that is not the duel hits harder, and
            // the duel itself hits softer.
            float DamageTakenMult(Ctx& /*ctx*/, Unit* attacker, SpellInfo const*) override
            {
                if (!attacker || _duel.IsEmpty())
                    return 1.f;

                return HubrisTakenMult(attacker->GetGUID() == _duel);
            }

            std::string Describe(AffixInstance const& self) const override
            {

                std::string out = "The first enemy in a fight becomes your duel: it deals "
                                + std::to_string(100 - DUEL_TAKEN_PCT)
                                + "% less damage to you and everything else deals "
                                + std::to_string(OTHER_TAKEN_PCT - 100)
                                + "% more. Open on the one that frightens you.";

                out += BoonClause(self.boon, self.boonMag);
                return out;
            }

            std::string Diagnose(Ctx& /*ctx*/) const override
            {
                return "hubris: duel " + std::string(_duel.IsEmpty() ? "none" : _duel.ToString())
                     + ", duel x" + std::to_string(DUEL_TAKEN_PCT)
                     + "%, others x" + std::to_string(OTHER_TAKEN_PCT) + "%";
            }

        private:
            void Declare(Ctx& ctx, Unit* who)
            {
                _duel = who->GetGUID();

                if (ctx.addon && ctx.player)
                    ctx.addon->SendEvent(ctx.player, MechanicKey(), 0, "Duel declared");
            }

            // Cleared on leaving combat, so it never outlives the pull that
            // set it.
            ObjectGuid _duel;
        };
    }

    GAUNTLET_MECHANIC(18, Hubris);
}
