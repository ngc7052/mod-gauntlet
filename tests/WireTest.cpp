/*
 * mod-gauntlet - the server splits and the addon rejoins; they must agree
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// One job, and it is the property both sides depend on:
//
//     JoinDescription(SplitDescription(text)) == text
//
// The server splits a description on a space and drops the space; the addon
// rejoins the pieces with exactly one. That rule is kept in two languages in two
// repositories' worth of files, and it has already been broken once in a way a
// player saw -- "in dungeons.In exchange" -- because a trailing space does not
// survive the trip to CHAT_MSG_ADDON.
//
// It had no test until Phase 8, and could not have had one: the splitter lived
// in GauntletAddon.cpp, which needs the core to build. It is in GauntletWire.cpp
// now for exactly this.

#include "GauntletWire.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using namespace Gauntlet;

    // The property, stated once and used by every case below.
    void RoundTrips(std::string const& text, std::size_t chunk, char const* what)
    {
        std::vector<std::string> const parts = SplitDescription(text, chunk);
        EXPECT_EQ(JoinDescription(parts), text)
            << what << ": split into " << parts.size() << " chunk(s) at " << chunk
            << " did not rejoin to what went in";

        for (std::string const& p : parts)
        {
            EXPECT_FALSE(p.empty()) << what << ": an empty chunk rejoins as a doubled space";
            EXPECT_LE(p.size(), chunk) << what << ": a chunk is over the limit and would be refused";
        }
    }

    // Real text, taken from the mechanics that produce the longest sentences.
    char const* const REAL[] = {
        "A Shade rises behind you every 15 minutes and hunts you until you kill it or leave it"
        " behind. It is slower than you are, and much slower than a mount. It is one named"
        " creature: every time you leave it behind it returns stronger, and killing it keeps it"
        " down for two tiers. Killing it grants Vindication: 25% more experience for five minutes.",

        "While you are in a fight with something you have wounded, no healing reaches you. Every"
        " enemy you kill gives back 5% of your health instead. The block holds for 15 seconds"
        " after the fight ends, so running is no longer an answer. Food and drink still work.",

        "Standing still in the open world for 20 seconds brings footsteps, and four seconds later"
        " an Ambusher. Moving cancels it, right up to the last moment. Nothing happens in inns, in"
        " cities or in dungeons. In exchange, you have 10% more health.",
    };

    // The longest run of non-space bytes. The round trip can only hold while a
    // chunk is at least this big: below it there is no space to cut at, the cut
    // is hard, and the join then inserts a space that was never there.
    std::size_t LongestWord(std::string const& text)
    {
        std::size_t best = 0, run = 0;
        for (char c : text)
        {
            if (c == ' ') { run = 0; continue; }
            best = std::max(best, ++run);
        }
        return best;
    }
}

TEST(Wire, RealDescriptionsSurviveTheRoundTrip)
{
    for (char const* text : REAL)
        RoundTrips(text, DESC_CHUNK, "a real description");
}


TEST(Wire, TheRoundTripHoldsAtEveryChunkSizeThatFitsTheLongestWord)
{
    // Every boundary, not just the shipped one: an off-by-one in the cut shows
    // up at one specific length and nowhere else.
    //
    // Swept from the longest word rather than from 1, because that is where the
    // property actually starts holding, and a test that sweeps below it is
    // asserting something the splitter never claimed. The shipped chunk is 200
    // and the longest word in any description is nowhere near it -- see
    // NoDescriptionHasAWordTooLongToSplitCleanly.
    for (char const* text : REAL)
    {
        std::size_t const floorSize = LongestWord(text);
        for (std::size_t chunk = floorSize; chunk <= 260; ++chunk)
            RoundTrips(text, chunk, "a real description at a swept chunk size");
    }
}

TEST(Wire, NoDescriptionHasAWordTooLongToSplitCleanly)
{
    // The guard that keeps the hard cut theoretical. A single word longer than
    // DESC_CHUNK is the one shape the round trip cannot survive, so the answer
    // is not to make the splitter cleverer -- it is to never write one. Two
    // hundred characters without a space is not a sentence anyone would write
    // on purpose, and this says so out loud.
    //
    // The live check over all sixty-nine mechanics at every rank is
    // `.gauntlet debug cards`, which needs a Player and cannot live here.
    for (char const* text : REAL)
        EXPECT_LT(LongestWord(text), DESC_CHUNK) << text;
}

TEST(Wire, TheAwkwardShapes)
{
    RoundTrips("", DESC_CHUNK, "empty");
    RoundTrips("one", DESC_CHUNK, "shorter than a chunk");
    RoundTrips(std::string(DESC_CHUNK, 'x'), DESC_CHUNK, "exactly one chunk, no space");
    RoundTrips("a b", 1, "a chunk that fits one byte");
    RoundTrips(" leading and trailing ", DESC_CHUNK, "spaces at the ends");

    // A single word longer than a chunk. There is no space to cut at, so the
    // cut is hard -- and the join then inserts a space that was never there.
    // This is the one shape the round trip cannot hold, and the test states the
    // real behaviour rather than pretending otherwise. The guard that keeps it
    // theoretical is NoDescriptionHasAWordTooLongToSplitCleanly.
    std::vector<std::string> const parts = SplitDescription(std::string(450, 'x'), DESC_CHUNK);
    ASSERT_EQ(parts.size(), 3u);

    std::string concatenated;
    for (std::string const& p : parts)
        concatenated += p;
    EXPECT_EQ(concatenated, std::string(450, 'x')) << "a hard cut must not lose bytes";

    EXPECT_NE(JoinDescription(parts), std::string(450, 'x'))
        << "if this starts passing, the hard cut has learned to rejoin cleanly and the "
           "comment above it is out of date";
}

TEST(Wire, RunsOfSpacesDoNotBecomeEmptyChunks)
{
    // A doubled space in a description would otherwise split into a chunk, an
    // empty chunk, and a chunk -- and the empty one rejoins as a second space,
    // so the text a player reads grows a gap every time it crosses the wire.
    for (std::size_t chunk = 4; chunk <= 40; ++chunk)
    {
        std::vector<std::string> const parts = SplitDescription("aa  bb   cc dd", chunk);
        for (std::string const& p : parts)
            EXPECT_FALSE(p.empty()) << "chunk size " << chunk;
    }
}

TEST(Wire, AHardCutNeverLandsInsideACharacter)
{
    // Not reachable today -- every player-facing string in the module is ASCII
    // -- and here because the first em-dash somebody types would otherwise
    // arrive as replacement characters, which gets reported as "the addon shows
    // garbage" and investigated nowhere near this file.
    //
    // One long unbroken word of three-byte characters, so there is no space to
    // cut at and the hard cut is forced.
    std::string word;
    for (int i = 0; i < 120; ++i)
        word += "\xE2\x80\x94";   // em dash

    for (std::size_t chunk = 8; chunk <= 64; ++chunk)
    {
        std::vector<std::string> const parts = SplitDescription(word, chunk);
        std::string rebuilt;
        for (std::string const& p : parts)
        {
            rebuilt += p;
            ASSERT_EQ(p.size() % 3u, 0u)
                << "chunk size " << chunk << ": a chunk ends part-way through a character";
        }
        EXPECT_EQ(rebuilt, word) << "chunk size " << chunk << ": bytes were lost or added";
    }
}

// ---------------------------------------------------------------------------
// The inbound field parser.
//
// This is the module's entire inbound surface: the addon sends PICK <i>, and
// `i` arrives as text a client chose. Everything downstream indexes a vector
// with it. It had no test until Phase 8 and could not have had one, because it
// lived in GauntletAddon.cpp.
// ---------------------------------------------------------------------------

TEST(WireParse, AcceptsExactlyTheDecimalsInRange)
{
    uint32 out = 0xDEADBEEF;

    EXPECT_TRUE(ParseUInt("0", 255, out));   EXPECT_EQ(out, 0u);
    EXPECT_TRUE(ParseUInt("7", 255, out));   EXPECT_EQ(out, 7u);
    EXPECT_TRUE(ParseUInt("255", 255, out)); EXPECT_EQ(out, 255u);
    EXPECT_TRUE(ParseUInt("0000000255", 255, out)) << "leading zeros are digits";
    EXPECT_EQ(out, 255u);
}

TEST(WireParse, RefusesEverythingElse)
{
    uint32 out = 0;

    // The shapes a hand-written or hostile client actually sends.
    for (char const* bad : { "", " ", "\t", "+1", "-1", "1 ", " 1", "1.0", "0x10",
                             "1e3", "abc", "1a", "a1", "\xFF", "1\n", "٣" })
        EXPECT_FALSE(ParseUInt(bad, 255, out)) << "accepted \"" << bad << "\"";

    EXPECT_FALSE(ParseUInt("256", 255, out)) << "over the limit";
    EXPECT_FALSE(ParseUInt("99999999999", 0xFFFFFFFFu, out)) << "eleven digits";
}

TEST(WireParse, CannotBeMadeToOverflow)
{
    // The reason the length cap and the running limit check both exist: ten
    // digits fit in uint64 without wrapping, and the per-digit test stops the
    // accumulator before it can reach a value the caller did not allow.
    uint32 out = 0;

    EXPECT_TRUE(ParseUInt("4294967295", 0xFFFFFFFFu, out));
    EXPECT_EQ(out, 0xFFFFFFFFu);
    EXPECT_FALSE(ParseUInt("4294967296", 0xFFFFFFFFu, out));
    EXPECT_FALSE(ParseUInt("9999999999", 0xFFFFFFFFu, out));
}

TEST(WireParse, LeavesTheOutputAloneWhenItRefuses)
{
    // Callers test the bool. One that did not would otherwise read a value
    // half-parsed from a rejected field.
    uint32 out = 4242;
    EXPECT_FALSE(ParseUInt("12x", 255, out));
    EXPECT_EQ(out, 4242u) << "a refused parse wrote to its output";
}

// ---------------------------------------------------------------------------
// Truncation: a name and a run's epitaph both have to fit a 255-byte message.
// ---------------------------------------------------------------------------

TEST(WireTrim, AListShorterThanItsBudgetIsUntouched)
{
    EXPECT_EQ(TrimList("Red Mist III, Cold Feet II", 255), "Red Mist III, Cold Feet II");
}

TEST(WireTrim, ALongListIsCutAtAnEntryBoundaryAndMarked)
{
    std::string const list = "Red Mist III, Cold Feet II, Mana Burn III, Fickle Sheep I";

    // Strictly below the list's own length: at or above it nothing is cut, and
    // an uncut list must not be marked as one. That boundary is checked
    // separately below.
    for (std::size_t budget = 6; budget < list.size(); ++budget)
    {
        std::string const cut = TrimList(list, budget);
        ASSERT_LE(cut.size(), budget) << "budget " << budget << " produced " << cut.size();
        EXPECT_NE(cut.find(", ..."), std::string::npos)
            << "budget " << budget << ": a cut list must say it was cut";
    }

    EXPECT_EQ(TrimList(list, list.size()), list) << "exactly fits: untouched and unmarked";
    EXPECT_EQ(TrimList(list, list.size() + 1), list);
}

TEST(WireTrim, ABudgetTooSmallForTheMarkerGivesNothingRatherThanNonsense)
{
    for (std::size_t budget = 0; budget <= 5; ++budget)
        EXPECT_TRUE(TrimList("Red Mist III, Cold Feet II", budget).empty()) << "budget " << budget;
}

TEST(WireTrim, OneEnormousEntryIsCutOnACharacterBoundary)
{
    // No ", " to cut at, so the cut is hard -- and it must still not leave half
    // a character on the wire.
    std::string entry;
    for (int i = 0; i < 60; ++i)
        entry += "\xE2\x80\x94";

    for (std::size_t budget = 8; budget <= 120; ++budget)
    {
        std::string const cut = TrimList(entry, budget);
        ASSERT_LE(cut.size(), budget) << "budget " << budget;

        std::string const body = cut.substr(0, cut.size() - 5);   // drop the marker
        EXPECT_EQ(body.size() % 3u, 0u)
            << "budget " << budget << ": cut part-way through a character";
    }
}

TEST(WireFloor, BacksOffToACharacterBoundaryAndNeverPastTheEnd)
{
    std::string const s = "ab\xE2\x80\x94" "cd";   // a b <em dash> c d

    EXPECT_EQ(Utf8Floor(s, s.size() + 10), s.size()) << "a budget past the end is the end";
    EXPECT_EQ(Utf8Floor(s, 0), 0u);
    EXPECT_EQ(Utf8Floor(s, 2), 2u) << "already on a boundary";
    EXPECT_EQ(Utf8Floor(s, 3), 2u) << "inside the em dash, back off";
    EXPECT_EQ(Utf8Floor(s, 4), 2u) << "still inside it";
    EXPECT_EQ(Utf8Floor(s, 5), 5u) << "the byte after it is a boundary";

    EXPECT_EQ(Utf8Floor("", 4), 0u);
}
