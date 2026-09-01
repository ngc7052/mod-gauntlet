/*
 * mod-gauntlet - what an affix took, and whether it gave it back
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_AUDIT_H
#define MOD_GAUNTLET_AUDIT_H

#include "Gauntlet.h"

#include <cstddef>
#include <string>
#include <vector>

class Player;

namespace Gauntlet
{
    struct RunState;

    // One reading of everything an affix is able to change about a character
    // and could then fail to change back.
    //
    // The list is deliberately wider than any single mechanic needs. An audit
    // that only looks where a bug is expected finds only the bugs that were
    // expected, and the whole reason this exists is that sixty-nine OnAttach /
    // OnDetach pairs were written by hand and read once each.
    //
    // Every field is something a mechanic in this module actually reaches for:
    // auras (most Class curses), cooldowns (the four PermanentCooldown users),
    // max health and the aggregate (anything with an AggregateFactor), speed
    // (Tempo), talent points (Bargains), shapeshift (druid curses), summons
    // (the Spawn family), and the scheduler queue (every Timed mechanic).
    struct Footprint
    {
        // Applied aura spell ids, sorted, duplicates kept: two stacks of one
        // aura is not the same state as one stack, and an affix that leaves a
        // stack behind leaves it behind.
        std::vector<uint32> auras;

        // Spell ids with a cooldown running. Not the remaining time -- that
        // ticks down between two readings taken microseconds apart and would
        // make every cooldown a false positive.
        std::vector<uint32> cooldowns;

        // What is worn, one entry per equipment slot (Player.h's
        // EQUIPMENT_SLOT_START..END, nineteen of them), the item's low guid or
        // 0 for an empty slot. The denials put an item in the bags on attach
        // and back on detach, and "did it come back" is *the* question for
        // them -- one the reading used to answer only by accident, through
        // the item's auras, which the core adds and removes on its own terms
        // (GauntletAuditLive.cpp). This asks it directly.
        std::vector<uint32> equipment;

        uint32 maxHealth   = 0;
        uint32 maxPower    = 0;
        uint32 freeTalents = 0;
        uint32 summons     = 0;   // creatures the module owns for this player
        uint32 armed       = 0;   // entries in this player's scheduler queue
        uint32 carried     = 0;   // affixes on the run
        uint8  shapeshift  = 0;

        float  speedRun    = 1.f;
        float  speedSwim   = 1.f;

        float  aggregate[static_cast<std::size_t>(AggregateKind::MAX)] = {};
    };

    // Everything the second reading has that the first did not, one line each,
    // in the order a reader wants them: the things that are still standing in
    // the world first, the numbers after. Empty means nothing moved.
    //
    // Pure, and that is the point of the split -- Capture below needs Player.h
    // and cannot be unit-tested, this half is the part with the logic in it,
    // and tests/AuditTest.cpp exercises it with no core at all.
    std::vector<std::string> Diff(Footprint const& before, Footprint const& after);

    // The live half, defined in GauntletAuditLive.cpp: the only one of the two
    // that needs the core's headers.
    Footprint Capture(Player* player, RunState const* run);
}

#endif
