/*
 * mod-gauntlet - the addon wire's string handling, away from the Player
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_WIRE_H
#define MOD_GAUNTLET_WIRE_H

#include "Gauntlet.h"

#include <cstddef>
#include <string>
#include <vector>

// Deliberately free of Player.h and of every core game header, like Gauntlet.h
// itself, so tests/run-tests.sh can compile it.
//
// That is the whole point of this file existing. The description splitter lived
// in GauntletAddon.cpp, which cannot be built without the core, so the one piece
// of wire logic in the module that has already produced a player-visible bug had
// no test and could not have one. The bug was "dungeons.In exchange": the
// splitter left the space on the end of a chunk, and a trailing space does not
// survive the trip to CHAT_MSG_ADDON.
//
// The rule both sides keep, and neither may change alone:
//
//     the server splits on a space and drops it; the addon rejoins with exactly
//     one space.
//
// So Split(desc) joined back with " " must equal desc, for every desc. That is
// one line to assert and it is what tests/WireTest.cpp does.

namespace Gauntlet
{
    // "GNT\tODESC\t<index>\t" is thirteen bytes at three digits of index, and
    // Frame refuses anything over MaxPayload (255), so this leaves a wide
    // margin rather than sitting on the limit.
    constexpr std::size_t DESC_CHUNK = 200;

    // One description as however many chunks it takes.
    //
    // Never splits inside a word when it can avoid it, never emits an empty
    // chunk, and never splits inside a UTF-8 sequence. The last of those is not
    // reachable today -- every player-facing string in the module is ASCII --
    // and is here because the first em-dash somebody types would otherwise
    // arrive as two replacement characters, which is a bug that would be
    // reported as "the addon shows garbage" and found nowhere near this file.
    std::vector<std::string> SplitDescription(std::string const& desc,
                                              std::size_t chunk = DESC_CHUNK);

    // What the addon does with the pieces, mirrored here so the round trip can
    // be asserted rather than assumed. addon/GauntletUI/Panel.lua's ODESC and
    // ADESC handlers are the real implementation; this is a copy of a one-line
    // rule and the test that uses it is what stops the two drifting.
    std::string JoinDescription(std::vector<std::string> const& parts);
}

#endif // MOD_GAUNTLET_WIRE_H
