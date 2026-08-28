/*
 * mod-gauntlet - Forgetful: you gain less experience
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Scalars.h"

// Registry id 73, flagged MF_NotImplemented: design section 5 cuts it from the
// pool outright. Kept working for migrated runs, as Withering is.
//
// AggregateKind::Experience is the one kind plan section 2.5 gives no cap, so
// this is also the only mechanic whose product is returned unclamped.
namespace Gauntlet
{
    namespace
    {
        class Forgetful final : public ScalarMechanic
        {
        public:
            Forgetful() : ScalarMechanic(AggregateKind::Experience, false,
                                         "you gain ", "% less experience") { }
        };
    }

    IMechanic* MakeForgetful() { return new Forgetful(); }
}
