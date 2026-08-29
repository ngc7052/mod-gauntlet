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

    // Backs `n` off to the nearest UTF-8 boundary at or below itself, so
    // truncating a character name never leaves half a sequence on the wire.
    // Continuation bytes are 10xxxxxx, so a cut at any byte that is not one
    // lands on the start of a character and everything before it is whole.
    std::size_t Utf8Floor(std::string_view s, std::size_t n);

    // A comma-separated list cut to fit a budget, at an entry boundary where
    // there is one, and marked so the reader knows it was cut. The conducts on
    // a leaderboard row are the only thing that needs it -- VARCHAR(255) into a
    // 255-byte protocol message -- and cutting a run's epitaph mid-word would
    // be a poor way to end it.
    std::string TrimList(std::string_view list, std::size_t budget);

    // A parser for one client-supplied decimal field.
    //
    // This is the module's entire inbound surface: the addon sends PICK <i> and
    // SYNC, and `i` goes through here. It refuses an empty field, anything that
    // is not a digit, and anything above `limit`, so no inbound value can
    // overflow or index past the end of a vector -- which is why the module has
    // no atoi in it and why this is worth a test of its own.
    bool ParseUInt(std::string_view s, uint32 limit, uint32& out);

    // What the addon does with the pieces, mirrored here so the round trip can
    // be asserted rather than assumed. addon/GauntletUI/Panel.lua's ODESC and
    // ADESC handlers are the real implementation; this is a copy of a one-line
    // rule and the test that uses it is what stops the two drifting.
    std::string JoinDescription(std::vector<std::string> const& parts);
}

#endif // MOD_GAUNTLET_WIRE_H
