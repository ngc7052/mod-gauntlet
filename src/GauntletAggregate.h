/*
 * mod-gauntlet - the aggregate multiplier and its caps
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_AGGREGATE_H
#define MOD_GAUNTLET_AGGREGATE_H

#include "Gauntlet.h"
#include <cstddef>
#include <vector>

namespace Gauntlet
{
    // Everything the aggregate needs to know about the live world, so the
    // maths stay Player-free and testable. Mgr evaluates the sixteen
    // conditions against the player once per query and fills the array;
    // nothing below this line has ever heard of a Player.
    struct AggregateInput
    {
        AggregateKind kind = AggregateKind::DamageTaken;
        bool conditionActive[static_cast<size_t>(Condition::MAX)] = {};
    };

    // Multiplies the factor of every carried affix whose condition is active,
    // then clamps the product with `caps`. The clamp is on the product and
    // never on a contribution, so three damage-taken affixes reach the ceiling
    // together instead of each being trimmed on the way in.
    //
    // An instance with a null `impl` -- a mechanic this build does not
    // implement -- is skipped whole, boon included: a boon whose curse is not
    // running would be a free upside.
    //
    // Returns 1.0 for an empty set, subject to the same clamp.
    float Aggregate(std::vector<AffixInstance> const& affixes,
                    AggregateInput const& in, AggregateCaps const& caps);
}

#endif // MOD_GAUNTLET_AGGREGATE_H
