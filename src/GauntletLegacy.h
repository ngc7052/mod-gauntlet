/*
 * mod-gauntlet - generator version 1, preserved for the one-shot migration
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_LEGACY_H
#define MOD_GAUNTLET_LEGACY_H

#include "Gauntlet.h"

namespace Gauntlet
{
    // Generator version 1's roll, moved here unchanged from GauntletAffix.cpp.
    //
    // Nothing in a live run calls this. Its only caller is the migration that
    // converts a pre-redesign gauntlet_affix row -- which stored no more than
    // (tier, roll index) -- back into the affix that character has been
    // playing with. A change of a single arithmetic step here silently
    // rewrites those runs, so the function is frozen: tests/fixtures/
    // legacy_rolls.json is the record of what it produced before the rewrite
    // and is the thing to trust if the two ever disagree.
    Affix LegacyRoll(uint32 seed, uint32 tier, uint32 rollIndex);
}

#endif // MOD_GAUNTLET_LEGACY_H
