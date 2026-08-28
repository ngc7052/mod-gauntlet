/*
 * mod-gauntlet - the aggregate multiplier and its caps
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAggregate.h"
#include "GauntletRegistry.h"
#include "GauntletMechanic.h"
#include <algorithm>

namespace Gauntlet
{
    namespace
    {
        // Plan section 2.5. Experience is the one kind with no ceiling and no
        // floor; the design never gives it one and inventing one here would be
        // a balance decision rather than an implementation.
        //
        // The floor is applied after the ceiling so that a configuration whose
        // min exceeds its max resolves to the min: taking less than base damage
        // is the outcome the cap exists to prevent, so the floor must win.
        float ClampProduct(float v, AggregateKind k, AggregateCaps const& caps)
        {
            switch (k)
            {
                case AggregateKind::DamageTaken:
                    return std::max(caps.damageTakenMin, std::min(caps.damageTakenMax, v));
                case AggregateKind::DamageDone:  return std::max(caps.damageDoneMin, v);
                case AggregateKind::HealTaken:   return std::max(caps.healTakenMin, v);
                case AggregateKind::MaxHealth:   return std::max(caps.maxHealthMin, v);
                case AggregateKind::EnemySpeed:  return std::min(caps.enemySpeedMax, v);
                case AggregateKind::Experience:  return v;
                default:                         return v;
            }
        }
    }

    float Aggregate(std::vector<AffixInstance> const& affixes,
                    AggregateInput const& in, AggregateCaps const& caps)
    {
        float product = 1.0f;

        for (AffixInstance const& a : affixes)
        {
            // No implementation: a run migrated from a newer registry, or a
            // family this build does not carry. Not an error, not a crash.
            if (!a.impl)
                continue;

            // A stored row is only as trustworthy as the database it came from.
            if (a.condition >= Condition::MAX)
                continue;

            if (!in.conditionActive[static_cast<size_t>(a.condition)])
                continue;

            // Curse and boon in one call. The aggregate pays no boon of its
            // own any more: until Phase 2 it paid the generically-rolled boon a
            // Scalar carried, and with the Scalars deleted every boon is named
            // by MechanicDef::boon and delivered by the mechanic that names it
            // -- several of them by returning BoonFactor() from exactly this
            // callback (src/mechanics/Boons.h).
            //
            // The consequence is that a mechanic's whole contribution to one
            // AggregateKind, upside and downside together, is one number, and
            // the clamp below is applied once to the product of all of them.
            product *= a.impl->AggregateFactor(a, in.kind);
        }

        return ClampProduct(product, in.kind, caps);
    }
}
