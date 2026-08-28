/*
 * mod-gauntlet - A3 Exposed: you take more damage
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Scalars.h"

// Registry id 21. The oldest affix in the module and, per design section 5,
// the one scalar that keeps its place unchanged -- provided it carries a state
// condition, which the generator now guarantees. The number is the whole
// mechanic; the decision it creates is the condition's.
namespace Gauntlet
{
    namespace
    {
        class Exposed final : public ScalarMechanic
        {
        public:
            Exposed() : ScalarMechanic(AggregateKind::DamageTaken, true,
                                       "you take ", "% more damage") { }
        };
    }

    IMechanic* MakeExposed() { return new Exposed(); }
}
