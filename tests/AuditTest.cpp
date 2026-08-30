/*
 * mod-gauntlet - what the attach/detach audit reports, and what it stays quiet about
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Diff is the half of `.gauntlet debug leaks` with a decision in it. Capture
// needs a Player and this harness has no world, but Capture is a straight read
// of nine getters; the part that can be wrong in a way nobody notices is the
// comparison, and that is here.
//
// Two properties matter more than the rest:
//
//   * a footprint compared with itself reports nothing -- otherwise every
//     mechanic reads as a leak and the command is noise;
//   * an aura applied twice and removed once *is* a leak -- a plain set
//     difference sees nothing there, and that is the bug this file was written
//     to make impossible.

#include "GauntletAudit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using namespace Gauntlet;

    // A footprint that could plausibly be a real character: some auras, a
    // cooldown, and every ratio at rest.
    Footprint Baseline()
    {
        Footprint fp;
        fp.auras       = { 1126, 1126, 21562 };   // two stacks of one, one of another
        fp.cooldowns   = { 1856, 5384 };
        fp.maxHealth   = 4200;
        fp.maxPower    = 3100;
        fp.freeTalents = 5;
        fp.summons     = 0;
        fp.armed       = 0;
        fp.carried     = 0;
        fp.shapeshift  = 0;
        fp.speedRun    = 1.0f;
        fp.speedSwim   = 1.0f;
        for (float& f : fp.aggregate)
            f = 1.0f;
        return fp;
    }

    bool Mentions(std::vector<std::string> const& lines, std::string const& needle)
    {
        return std::any_of(lines.begin(), lines.end(), [&](std::string const& l)
        {
            return l.find(needle) != std::string::npos;
        });
    }

    // ------------------------------------------------------------------
    // The quiet case. It is first because it is the one that decides whether
    // the command is usable at all: sixty-nine mechanics run through this, and
    // a single spurious line per mechanic is sixty-nine lines of chat saying
    // nothing.
    // ------------------------------------------------------------------

    TEST(Audit, AFootprintComparedWithItselfReportsNothing)
    {
        Footprint const fp = Baseline();
        EXPECT_TRUE(Diff(fp, fp).empty());
    }

    TEST(Audit, TwoSeparatelyBuiltButEqualFootprintsReportNothing)
    {
        EXPECT_TRUE(Diff(Baseline(), Baseline()).empty());
    }

    TEST(Audit, AnEmptyFootprintComparedWithItselfReportsNothing)
    {
        Footprint const fp;
        EXPECT_TRUE(Diff(fp, fp).empty());
    }

    // ------------------------------------------------------------------
    // Auras, and the multiset property.
    // ------------------------------------------------------------------

    TEST(Audit, AnAuraLeftBehindIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.auras.push_back(99999);
        std::sort(after.auras.begin(), after.auras.end());

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "aura 99999 still applied"));
    }

    TEST(Audit, AnAuraStrippedAndNotRestoredIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.auras.erase(std::find(after.auras.begin(), after.auras.end(), 21562u));

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "aura 21562 was removed and not restored"));
    }

    // The reason Extra is a multiset difference and not a set difference. An
    // affix that stacks something the character already had, and removes one
    // stack on detach, leaves the character changed -- and both readings
    // contain the same *set* of ids.
    TEST(Audit, OneStackOfTwoLeftBehindIsALeakEvenThoughTheIdWasAlreadyThere)
    {
        Footprint const before = Baseline();          // 1126 twice
        Footprint after = before;
        after.auras.push_back(1126);                  // now three
        std::sort(after.auras.begin(), after.auras.end());

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "aura 1126 still applied"));
    }

    TEST(Audit, OneStackOfTwoRemovedIsReportedTheOtherWay)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.auras.erase(std::find(after.auras.begin(), after.auras.end(), 1126u));

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "aura 1126 was removed and not restored"));
    }

    TEST(Audit, AnAuraAppliedAndProperlyRemovedIsNotReported)
    {
        Footprint const before = Baseline();
        Footprint held = before;
        held.auras.push_back(99999);
        std::sort(held.auras.begin(), held.auras.end());

        // held is what the character looked like while carrying it; after is
        // the same as before, which is the whole point.
        EXPECT_FALSE(Diff(before, held).empty());
        EXPECT_TRUE(Diff(before, before).empty());
    }

    TEST(Audit, SeveralLeakedAurasAreEachReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.auras.push_back(700);
        after.auras.push_back(800);
        std::sort(after.auras.begin(), after.auras.end());

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_TRUE(Mentions(lines, "aura 700"));
        EXPECT_TRUE(Mentions(lines, "aura 800"));
    }

    // ------------------------------------------------------------------
    // Cooldowns. The four PermanentCooldown users are the reason this field
    // exists: a swap that holds a cooldown and never releases it leaves a
    // client button greyed out for seven days.
    // ------------------------------------------------------------------

    TEST(Audit, ACooldownStillHeldIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.cooldowns.push_back(1766);
        std::sort(after.cooldowns.begin(), after.cooldowns.end());

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "spell 1766 still on cooldown"));
    }

    TEST(Audit, ACooldownClearedAndNotRestoredIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.cooldowns.erase(std::find(after.cooldowns.begin(), after.cooldowns.end(), 1856u));

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "spell 1856 had its cooldown cleared and not restored"));
    }

    // ------------------------------------------------------------------
    // The counts.
    // ------------------------------------------------------------------

    TEST(Audit, ASummonLeftStandingIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.summons = 1;

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "summons owned 0 -> 1"));
    }

    TEST(Audit, AnEventLeftArmedIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.armed = 2;

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "scheduler entries queued 0 -> 2"));
    }

    TEST(Audit, AnAffixThatDidNotDetachIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.carried = 1;

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "carried affixes 0 -> 1"));
    }

    TEST(Audit, MaxHealthAndTalentPointsAreReportedWithBothValues)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.maxHealth   = 3990;
        after.freeTalents = 3;

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_TRUE(Mentions(lines, "max health 4200 -> 3990"));
        EXPECT_TRUE(Mentions(lines, "free talent points 5 -> 3"));
    }

    TEST(Audit, AShapeshiftLeftOnIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.shapeshift = 5;

        EXPECT_TRUE(Mentions(Diff(before, after), "shapeshift form 0 -> 5"));
    }

    // ------------------------------------------------------------------
    // The ratios, and the epsilon. Both directions of this are a real fault:
    // too tight and a float product reports as a leak on every mechanic, too
    // loose and a genuine 5% factor left behind reads as clean.
    // ------------------------------------------------------------------

    TEST(Audit, FloatNoiseBelowTheEpsilonIsNotALeak)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.aggregate[static_cast<std::size_t>(AggregateKind::DamageTaken)] = 1.0f + 0.0001f;
        after.speedRun = 1.0f - 0.0001f;

        EXPECT_TRUE(Diff(before, after).empty());
    }

    TEST(Audit, AFactorLeftBehindIsReportedWithItsOwnName)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.aggregate[static_cast<std::size_t>(AggregateKind::DamageTaken)] = 1.25f;

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, AggregateKindName(AggregateKind::DamageTaken)));
        EXPECT_TRUE(Mentions(lines, "x1.00 -> x1.25"));
    }

    TEST(Audit, EveryAggregateKindHasItsOwnLine)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        for (float& f : after.aggregate)
            f = 1.5f;

        std::vector<std::string> const lines = Diff(before, after);
        EXPECT_EQ(lines.size(), static_cast<std::size_t>(AggregateKind::MAX));

        // Each one names itself, so a reader can tell which cap is still bent.
        for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            EXPECT_TRUE(Mentions(lines, AggregateKindName(static_cast<AggregateKind>(k))))
                << "aggregate kind " << static_cast<uint32>(k) << " has no line of its own";
    }

    TEST(Audit, ASpeedLeftBentIsReported)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.speedRun = 0.85f;

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(Mentions(lines, "run speed x1.00 -> x0.85"));
    }

    // ------------------------------------------------------------------
    // Order. The two things loose in the world are read before the numbers,
    // because they are the ones that go on being wrong after the player walks
    // away.
    // ------------------------------------------------------------------

    TEST(Audit, TheWorldStateIsReportedBeforeTheNumbers)
    {
        Footprint const before = Baseline();
        Footprint after = before;
        after.carried   = 1;
        after.summons   = 1;
        after.armed     = 1;
        after.maxHealth = 1;
        after.auras.push_back(99999);
        std::sort(after.auras.begin(), after.auras.end());

        std::vector<std::string> const lines = Diff(before, after);
        ASSERT_EQ(lines.size(), 5u);
        EXPECT_NE(lines[0].find("carried affixes"), std::string::npos);
        EXPECT_NE(lines[1].find("summons owned"), std::string::npos);
        EXPECT_NE(lines[2].find("scheduler entries queued"), std::string::npos);
        EXPECT_NE(lines[3].find("aura 99999"), std::string::npos);
        EXPECT_NE(lines[4].find("max health"), std::string::npos);
    }
}
