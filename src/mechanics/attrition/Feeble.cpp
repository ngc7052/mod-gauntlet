/*
 * mod-gauntlet - A4 Feeble: you deal less damage
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Scalars.h"

// Registry id 22. Design section 5 demotes this one to a price rather than an
// affix in its own right, but that is the generator's business: what it does
// when carried has not changed, and a run that already holds one must keep
// feeling exactly what it felt before.
namespace Gauntlet
{
    namespace
    {
        class Feeble final : public ScalarMechanic
        {
        public:
            Feeble() : ScalarMechanic(AggregateKind::DamageDone, false,
                                      "you deal ", "% less damage") { }
        };
    }

    IMechanic* MakeFeeble() { return new Feeble(); }

    GAUNTLET_MECHANIC_FN(22, MakeFeeble);
}
