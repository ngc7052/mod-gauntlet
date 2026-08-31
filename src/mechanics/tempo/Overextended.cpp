/*
 * mod-gauntlet - T3 Overextended: every attacker past the first hurts more
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"
#include "GauntletRules.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <iterator>

// Registry id 16. Design section 3, card T3: "Each enemy attacking you beyond
// the first increases the damage you take by 20%."
//
// The card's own note is the reason this one is in the set at all: "this is what
// a scalar looks like when it maps to a verb". Design section 5's second rule
// for a scalar that earns its place is "count-based, not flat" -- the player
// controls the number every pull through pull discipline, kill order, CC and
// letting a pet hold something -- so it is a coefficient the player operates
// rather than weather.
//
// Attackers on a pet deliberately do not count, which is what makes "let the pet
// hold one" a real answer and not a technicality.

namespace Gauntlet
{
    namespace
    {
        // The ladders and the arithmetic live in GauntletRules.h so that
        // tests/RulesTest.cpp can reach them: this file includes Player.h and
        // is therefore invisible to the Player-free test build.
        using namespace Gauntlet::Rules;

        constexpr uint16 MECHANIC_OVEREXTENDED = 16;

        // The card's ladder: 15 -> 20 -> 30% per extra attacker, and 40 at
        // rank IV -- where three extra attackers is the whole of
        // Gauntlet.Caps.DamageTaken on its own, which is the point: the top
        // rank makes a bad pull immediately fatal rather than gradually so.
        // What an enemy behind you adds to the damage it deals.
        //
        // This card used to price the *number* of things attacking you, and
        // that is very often not the player's choice: a patrol, an add, a
        // respawn on top of a pull. It punished bad luck and good pulls
        // identically, and there was nothing to do about it once it started --
        // a passive multiplier with no verb, which is why it was reported from
        // play as a tax.
        //
        // It prices your back now. An enemy you are not facing hits harder, so
        // the verb is facing: turn, back into a corner, keep the pack in front
        // of you. It is the same lesson every melee fight already teaches and
        // this card simply charges for it.
        //
        // It chains with Hubris, which prices *who* you open on: one card asks
        // which way you are pointed and the other asks at what. Carrying both
        // turns a pull into a plan. See docs/tempo-redesign.md.

        // The readout's ceiling, for the addon's counter. Not a cap on the
        // effect -- plan section 2.5's damage-taken ceiling is what bounds that,
        // and it is applied to the product in one place -- only on how many
        // pips are worth drawing.
        constexpr uint32 SHOWN_MAX = 5;


        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_OVEREXTENDED);
            return def ? def->key : "overextended";
        }

        class Overextended final : public IMechanic
        {
        public:
            void OnTick(Ctx& ctx, uint32 diffMs) override;
            void OnLeaveCombat(Ctx& ctx) override { Publish(ctx, 0); }

            float DamageTakenMult(Ctx& ctx, Unit* attacker, SpellInfo const*) override
            {
                Player* player = ctx.player;
                if (!player || !attacker)
                    return 1.0f;

                // The core's own arc test, the same one every behind-me
                // requirement in the game uses, so "behind" means to the player
                // what it means to a rogue's Backstab.
                if (!attacker->isInBack(player))
                    return 1.0f;

                return OverextendedTakenMult(/*behind*/ true);
            }

            // BonusHealing. The curse is paid in damage taken while surrounded;
            // the boon is paid in every heal that lands, which is the resource
            // a player fighting three things is actually spending. Delivered
            // here rather than through the aggregate because there is no
            // AggregateKind a boon may raise on the healing side -- the cap on
            // that kind is a floor, not a ceiling, so nothing clamps this.
            float HealTakenMult(Ctx& ctx, Unit* /*healer*/, SpellInfo const*) override
            {
                return ctx.self ? BoonHealMult(*ctx.self) : 1.0f;
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            // How many attackers are currently behind the player. This is
            // what the card charges for, so it is what the readout shows: a
            // scalar you cannot see acting is one you cannot learn from
            // (design section 5), and publishing the old attacker count would
            // now be showing a number the card does not use.
            static uint32 Behind(Player* player)
            {
                if (!player || !player->IsInWorld() || !player->IsInCombat())
                    return 0;

                uint32 n = 0;
                for (Unit* attacker : player->getAttackers())
                    if (attacker && attacker->IsAlive() && attacker->isInBack(player))
                        ++n;

                return n;
            }

            void Publish(Ctx& ctx, uint32 extra);

            uint32 _shown = 0;
        };

        void Overextended::OnTick(Ctx& ctx, uint32 /*diffMs*/)
        {
            // The multiplier itself is computed at the damage site, where the
            // number is current; this exists only so the player can see it
            // coming. Design section 5's fourth rule for a scalar that earns
            // its place: "visible when active. A scalar you cannot see acting
            // is a scalar you cannot learn from."
            Publish(ctx, Behind(ctx.player));
        }

        void Overextended::Publish(Ctx& ctx, uint32 extra)
        {
            if (extra == _shown)
                return;
            _shown = extra;

            if (ctx.addon && ctx.player)
                ctx.addon->QueueCounter(ctx.player, MechanicKey(), extra, SHOWN_MAX);
        }

        std::string Overextended::Describe(AffixInstance const& self) const
        {

            std::string out = "Anything hitting you from behind deals "
                            + std::to_string(BEHIND_PCT)
                            + "% more damage. Keep them in front of you.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(16, Overextended);
}
