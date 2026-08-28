/*
 * mod-gauntlet - Withering: healing on you is weaker
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Scalars.h"

// Registry id 72, flagged MF_NotImplemented so the generator never offers it
// again -- design section 5 replaces it with Deep Wounds. It is implemented
// anyway because the flag governs what may be rolled, not what may be carried,
// and a character who took one before the redesign still has it.
namespace Gauntlet
{
    namespace
    {
        class Withering final : public ScalarMechanic
        {
        public:
            Withering() : ScalarMechanic(AggregateKind::HealTaken, false,
                                         "healing on you is ", "% weaker") { }
        };
    }

    IMechanic* MakeWithering() { return new Withering(); }

    GAUNTLET_MECHANIC_FN(MECHANIC_WITHERING, MakeWithering);
}
