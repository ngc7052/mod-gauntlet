/*
 * mod-gauntlet - the aggregate multiplier and its caps
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAggregate.h"
#include "GauntletMechanic.h"
#include <algorithm>

namespace Gauntlet
{
    namespace
    {
        // Does a curse of this kind multiply the base up or down? Only two
        // kinds are bad when they are large.
        bool Raises(AggregateKind k)
        {
            return k == AggregateKind::DamageTaken || k == AggregateKind::EnemySpeed;
        }

        // Which aggregate, if any, a boon offsets. Mgr::Multiplier paired each
        // Boon with the Effect it cancelled and this keeps those pairings, so a
        // migrated Wrathful Feeble still buys its damage back.
        //
        // Four boons are missing on purpose. BonusHealing offset HealingDone,
        // which is not HealingReceived and never was: a Mending Withering did
        // not soften its own curse before the redesign and does not now.
        // BonusMoveSpeed, BonusMoney and BonusRegen offset effects the redesign
        // has no AggregateKind for at all. All four are inert today because the
        // effects they name were never rolled, and they stay inert here rather
        // than quietly becoming stronger on a live character.
        bool BoonOffsets(Boon b, AggregateKind k)
        {
            switch (b)
            {
                case Boon::BonusDamage:     return k == AggregateKind::DamageDone;
                case Boon::BonusExperience: return k == AggregateKind::Experience;
                case Boon::BonusMaxHealth:  return k == AggregateKind::MaxHealth;
                default:                    return false;
            }
        }

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

            product *= a.impl->AggregateFactor(a, in.kind);

            // The boon rides the same condition as the curse it pays for, as it
            // did in Mgr::Multiplier. It is a factor of its own rather than a
            // subtraction from the curse's percentage, which is what turning a
            // sum into a product means.
            if (a.boon != Boon::None && a.boonMag != 0 && BoonOffsets(a.boon, in.kind))
            {
                float const pct = static_cast<float>(a.boonMag) / 100.0f;
                product *= Raises(in.kind) ? std::max(0.0f, 1.0f - pct) : (1.0f + pct);
            }
        }

        return ClampProduct(product, in.kind, caps);
    }
}
