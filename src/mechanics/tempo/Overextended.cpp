/*
 * mod-gauntlet - T3 Overextended: every attacker past the first hurts more
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <string>

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
        constexpr uint16 MECHANIC_OVEREXTENDED = 16;

        // The card's ladder: 15 -> 20 -> 30% per extra attacker.
        constexpr uint32 PER_ATTACKER_PCT[MAX_RANK] = { 15, 20, 30 };

        // The readout's ceiling, for the addon's counter. Not a cap on the
        // effect -- plan section 2.5's damage-taken ceiling is what bounds that,
        // and it is applied to the product in one place -- only on how many
        // pips are worth drawing.
        constexpr uint32 SHOWN_MAX = 5;

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_OVEREXTENDED);
            return def ? def->key : "overextended";
        }

        // How many things are hitting *you*. Unit::getAttackers is the set of
        // units currently attacking this one (Unit.h:901), which is exactly the
        // card's meaning and is exactly why a pet's attackers are not in it.
        uint32 AttackerCount(Player* player)
        {
            if (!player || !player->IsInWorld() || !player->IsInCombat())
                return 0;

            uint32 count = 0;
            for (Unit* attacker : player->getAttackers())
                if (attacker && attacker->IsAlive())
                    ++count;

            return count;
        }

        class Overextended final : public IMechanic
        {
        public:
            void OnTick(Ctx& ctx, uint32 diffMs) override;
            void OnLeaveCombat(Ctx& ctx) override { Publish(ctx, 0); }

            float DamageTakenMult(Ctx& ctx, Unit* /*attacker*/, SpellInfo const*) override
            {
                uint32 const extra = Extra(ctx.player);
                if (extra == 0)
                    return 1.0f;

                return 1.0f + float(PER_ATTACKER_PCT[RankIndex(ctx.self)]) / 100.0f * float(extra);
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
            static uint32 Extra(Player* player)
            {
                uint32 const n = AttackerCount(player);
                return n > 1 ? n - 1 : 0;
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
            Publish(ctx, Extra(ctx.player));
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
            uint8 const i = RankIndex(&self);

            std::string out = "Every enemy attacking you beyond the first increases the damage you"
                              " take by " + std::to_string(PER_ATTACKER_PCT[i])
                            + "%. Anything holding your pet does not count.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(16, Overextended);
}
