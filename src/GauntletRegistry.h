/*
 * mod-gauntlet - procedurally generated hardcore affix challenge for AzerothCore
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_REGISTRY_H
#define MOD_GAUNTLET_REGISTRY_H

#include "Gauntlet.h"
#include <string_view>
#include <vector>

namespace Gauntlet
{
    // One row of the mechanic table. This is the single description of an
    // affix that the offer builder, the addon exporter, the debug commands
    // and the tests all read, so an affix cannot exist that the addon cannot
    // name. No behaviour lives here: a mechanic's implementation is created
    // by id through the factory in GauntletMechanic.h, and the table stays
    // pure data.
    struct MechanicDef
    {
        uint16      id;
        char const* key;            // "shade", "champions", "c01_red_mist"
        char const* name;           // "The Shade"
        Family      family;
        uint32      classMask;      // 0 = every class
        uint8       minTier, maxTier;
        uint8       maxRank;
        uint32      flags;          // MF_*
        char const* exclusiveKeys;  // '|'-separated; no two active mechanics share one
        Boon        boon;
        uint32      requiresSpell;  // 0 = no spell gate
        char const* blurb;          // one player-facing sentence, present tense
    };

    // Both lookups are indexed, not scanned: they sit on the damage path.
    // They return nullptr for an id or key the table does not carry, which
    // is the normal answer for a run migrated from a future generator.
    MechanicDef const* FindMechanic(uint16 id);
    MechanicDef const* FindMechanic(std::string_view key);

    // The whole table, in id order.
    std::vector<MechanicDef> const& AllMechanics();

    // The generator's offer filter. In Phase 0 this is true for Exposed and
    // Feeble only; see the comment at the head of the table in the .cpp for
    // why Withering and Forgetful are excluded despite having mechanics.
    bool IsImplemented(MechanicDef const& def);
}

#endif // MOD_GAUNTLET_REGISTRY_H
