/*
 * mod-gauntlet - the per-player scheduler, on a fake clock
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Plan section 5.1 asks for "scheduler with a fake clock: spacing, budget
// stretching, suppression, warn->fire ordering, cancel", and the accumulator is
// added because everything else here is measured in ticks and a wrong
// accumulator would move every one of those measurements at once.
//
// The fake clock is the Scheduler's own: Tick() takes a diff and a Suppression
// and touches no Player, no config and no wall clock, so driving it is a loop
// over Tick() with the diffs the test wants. Every event is recorded with the
// scheduler time it came out at, and the assertions are on those times rather
// than on tick counts, because a millisecond is what the design's numbers are
// written in.

#include "GauntletScheduler.h"

#include <gtest/gtest.h>

#include <map>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace Gauntlet;

    // One event and the scheduler-clock time it was released at.
    struct Released
    {
        uint32         atMs;
        ScheduledEvent ev;
    };

    // Drives `sched` for `totalMs` in `stepMs` slices, recording everything it
    // gives back. The suppression is constant for the whole run; a test that
    // needs it to change calls Advance twice.
    std::vector<Released> Advance(Scheduler& sched, uint32 totalMs, uint32 stepMs, Suppression const& sup)
    {
        std::vector<Released> out;
        for (uint32 elapsed = 0; elapsed < totalMs; elapsed += stepMs)
            for (ScheduledEvent const& ev : sched.Tick(stepMs, sup))
                out.push_back({ sched.NowMs(), ev });
        return out;
    }

    std::vector<Released> Advance(Scheduler& sched, uint32 totalMs, uint32 stepMs = Scheduler::TICK_MS)
    {
        Suppression const none;
        return Advance(sched, totalMs, stepMs, none);
    }

    std::vector<Released> Only(std::vector<Released> const& all, EventKind kind)
    {
        std::vector<Released> out;
        for (Released const& r : all)
            if (r.ev.kind == kind)
                out.push_back(r);
        return out;
    }

    std::vector<Released> OnlyMechanic(std::vector<Released> const& all, uint16 mechanic)
    {
        std::vector<Released> out;
        for (Released const& r : all)
            if (r.ev.mechanic == mechanic)
                out.push_back(r);
        return out;
    }

    // Readable failure output: "m1/id1 Fire@13000".
    std::string Describe(std::vector<Released> const& all)
    {
        std::string s;
        for (Released const& r : all)
        {
            s += "m" + std::to_string(r.ev.mechanic) + "/id" + std::to_string(r.ev.id);
            s += r.ev.kind == EventKind::Fire ? " Fire@" : " Warn@";
            s += std::to_string(r.atMs) + "  ";
        }
        return s.empty() ? std::string("<nothing>") : s;
    }

    constexpr uint16 MECH_A = 1;
    constexpr uint16 MECH_B = 2;
    constexpr uint16 MECH_C = 3;

    // -----------------------------------------------------------------------
    // The accumulator
    // -----------------------------------------------------------------------

    TEST(SchedulerAccumulator, NothingHappensBeforeTheFirstBoundary)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 500, 0);

        // 499 ms of diff, in any shape, is still inside the first tick.
        EXPECT_TRUE(s.Tick(400, Suppression{}).empty());
        EXPECT_TRUE(s.Tick(99, Suppression{}).empty());
        EXPECT_EQ(0u, s.NowMs());

        std::vector<ScheduledEvent> const due = s.Tick(1, Suppression{});
        ASSERT_EQ(1u, due.size());
        EXPECT_EQ(500u, s.NowMs());
        EXPECT_EQ(MECH_A, due[0].mechanic);
        EXPECT_EQ(EventKind::Fire, due[0].kind);
    }

    TEST(SchedulerAccumulator, TenFiftyMsTicksEqualOneFiveHundredMsTick)
    {
        Scheduler coarse;
        Scheduler fine;
        for (Scheduler* s : { &coarse, &fine })
        {
            s->SetTimedAffixCount(2);
            s->Arm(MECH_A, 1, 8000, 2000);
            s->Arm(MECH_B, 7, 9000, 0);
        }

        std::vector<Released> const a = Advance(coarse, 60000, 500);
        std::vector<Released> const b = Advance(fine, 60000, 50);

        ASSERT_EQ(a.size(), b.size()) << "coarse: " << Describe(a) << "\nfine:   " << Describe(b);
        for (size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_EQ(a[i].atMs, b[i].atMs) << "at index " << i;
            EXPECT_EQ(a[i].ev.mechanic, b[i].ev.mechanic) << "at index " << i;
            EXPECT_EQ(a[i].ev.id, b[i].ev.id) << "at index " << i;
            EXPECT_EQ(a[i].ev.kind, b[i].ev.kind) << "at index " << i;
        }
        EXPECT_EQ(coarse.NowMs(), fine.NowMs());
    }

    TEST(SchedulerAccumulator, TheLeftoverCarriesRatherThanBeingLost)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 1000, 0);

        // 300 ms nine times is 2700 ms: five boundaries, and 200 ms left over.
        std::vector<Released> const out = Advance(s, 2700, 300);
        ASSERT_EQ(1u, out.size()) << Describe(out);
        EXPECT_EQ(1000u, out[0].atMs);
        EXPECT_EQ(2500u, s.NowMs());
    }

    TEST(SchedulerAccumulator, ADiffLargerThanTheTickRunsEveryBoundaryItCovers)
    {
        Scheduler s;
        s.SetMinSpacingMs(0);
        s.Arm(MECH_A, 1, 500, 0);
        s.Arm(MECH_B, 1, 1000, 0);
        s.Arm(MECH_C, 1, 1500, 0);

        // One 2000 ms diff is four boundaries, so all three come out at once
        // with the times they were due at, not with one shared time.
        std::vector<ScheduledEvent> const due = s.Tick(2000, Suppression{});
        ASSERT_EQ(3u, due.size());
        EXPECT_EQ(MECH_A, due[0].mechanic);
        EXPECT_EQ(MECH_B, due[1].mechanic);
        EXPECT_EQ(MECH_C, due[2].mechanic);
        EXPECT_EQ(2000u, s.NowMs());
    }

    // -----------------------------------------------------------------------
    // Minimum spacing
    // -----------------------------------------------------------------------

    TEST(SchedulerSpacing, TwoFiresDueTogetherComeOutMinSpacingApartAndNeitherIsLost)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_A, 1, 1000, 0);
        s.Arm(MECH_B, 1, 1000, 0);

        std::vector<Released> const out = Advance(s, 40000);
        ASSERT_EQ(2u, out.size()) << Describe(out);

        EXPECT_EQ(MECH_A, out[0].ev.mechanic);
        EXPECT_EQ(1000u, out[0].atMs);

        EXPECT_EQ(MECH_B, out[1].ev.mechanic) << "the delayed event must be the same one, not a new one";
        EXPECT_EQ(13000u, out[1].atMs);
        EXPECT_GE(out[1].atMs - out[0].atMs, 12000u);

        EXPECT_TRUE(s.Queue().empty()) << "nothing may be left behind";
    }

    TEST(SchedulerSpacing, TheFirstFireOfASessionIsNeverHeldBack)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_A, 1, 500, 0);

        std::vector<Released> const out = Advance(s, 2000);
        ASSERT_EQ(1u, out.size()) << Describe(out);
        EXPECT_EQ(500u, out[0].atMs);
    }

    TEST(SchedulerSpacing, AQueueOfFiresDrainsInOrderOneEverySpacing)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_A, 1, 1000, 0);
        s.Arm(MECH_B, 1, 1000, 0);
        s.Arm(MECH_C, 1, 1000, 0);

        std::vector<Released> const out = Advance(s, 60000);
        ASSERT_EQ(3u, out.size()) << Describe(out);
        EXPECT_EQ(MECH_A, out[0].ev.mechanic);
        EXPECT_EQ(MECH_B, out[1].ev.mechanic);
        EXPECT_EQ(MECH_C, out[2].ev.mechanic);
        EXPECT_EQ(1000u, out[0].atMs);
        EXPECT_EQ(13000u, out[1].atMs);
        EXPECT_EQ(25000u, out[2].atMs);
    }

    TEST(SchedulerSpacing, ALaterEventDoesNotJumpADelayedOne)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_A, 1, 1000, 0);   // fires at 1000
        s.Arm(MECH_B, 1, 1500, 0);   // due at 1500, blocked until 13000
        s.Arm(MECH_C, 1, 14000, 0);  // due at 14000, and must still wait its turn

        std::vector<Released> const out = Advance(s, 60000);
        ASSERT_EQ(3u, out.size()) << Describe(out);
        EXPECT_EQ(MECH_B, out[1].ev.mechanic) << "first come, first served";
        EXPECT_EQ(13000u, out[1].atMs);
        EXPECT_EQ(MECH_C, out[2].ev.mechanic);
        EXPECT_EQ(25000u, out[2].atMs);
    }

    TEST(SchedulerSpacing, WarningsAreNotSpacedAgainstEachOther)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);

        // The two fires are 20 s apart, so neither is delayed and neither
        // earns a second telegraph; what is under test is only that the two
        // warnings, which fall in the same tick, both come out of it.
        s.Arm(MECH_A, 1, 10000, 5000);
        s.Arm(MECH_B, 1, 30000, 25000);

        std::vector<Released> const out = Advance(s, 60000);
        std::vector<Released> const warns = Only(out, EventKind::Warn);
        ASSERT_EQ(2u, warns.size()) << Describe(out);
        EXPECT_EQ(5000u, warns[0].atMs);
        EXPECT_EQ(5000u, warns[1].atMs) << "two warnings in the same tick are fine";

        std::vector<Released> const fires = Only(out, EventKind::Fire);
        ASSERT_EQ(2u, fires.size()) << Describe(out);
        EXPECT_EQ(10000u, fires[0].atMs);
        EXPECT_EQ(30000u, fires[1].atMs);
    }

    TEST(SchedulerSpacing, ZeroSpacingLetsEverythingDueOutAtOnce)
    {
        Scheduler s;
        s.SetMinSpacingMs(0);
        s.Arm(MECH_A, 1, 1000, 0);
        s.Arm(MECH_B, 1, 1000, 0);
        s.Arm(MECH_C, 1, 1000, 0);

        std::vector<Released> const out = Advance(s, 3000);
        ASSERT_EQ(3u, out.size()) << Describe(out);
        for (Released const& r : out)
            EXPECT_EQ(1000u, r.atMs);
    }

    // -----------------------------------------------------------------------
    // The event budget
    // -----------------------------------------------------------------------

    TEST(SchedulerBudget, TheArithmeticIsDesignFourTwo)
    {
        Scheduler s;
        EXPECT_FLOAT_EQ(1.0f, s.Budget()) << "no timed affix carried";

        s.SetTimedAffixCount(1);
        EXPECT_FLOAT_EQ(1.0f, s.Budget());
        s.SetTimedAffixCount(2);
        EXPECT_FLOAT_EQ(1.25f, s.Budget());
        s.SetTimedAffixCount(3);
        EXPECT_FLOAT_EQ(1.5f, s.Budget()) << "design 4.2: three timed affixes fire 1.5x less often";
        s.SetTimedAffixCount(4);
        EXPECT_FLOAT_EQ(1.75f, s.Budget());
    }

    TEST(SchedulerBudget, TheSameArmProducesDifferentDueTimesAtOneTwoAndFour)
    {
        struct Case { uint32 affixes; uint32 expectedMs; };
        Case const cases[] = { { 1, 20000 }, { 2, 25000 }, { 3, 30000 }, { 4, 35000 } };

        for (Case const& c : cases)
        {
            Scheduler s;
            s.SetTimedAffixCount(c.affixes);
            s.Arm(MECH_A, 1, 20000, 0);

            std::vector<Released> const out = Advance(s, 60000);
            ASSERT_EQ(1u, out.size()) << "affixes=" << c.affixes << " " << Describe(out);
            EXPECT_EQ(c.expectedMs, out[0].atMs) << "affixes=" << c.affixes;
        }
    }

    TEST(SchedulerBudget, AConfiguredStepReplacesTheDefault)
    {
        Scheduler s;
        s.SetBudgetStep(0.5f);
        s.SetTimedAffixCount(3);
        EXPECT_FLOAT_EQ(2.0f, s.Budget());

        s.Arm(MECH_A, 1, 10000, 0);
        std::vector<Released> const out = Advance(s, 40000);
        ASSERT_EQ(1u, out.size()) << Describe(out);
        EXPECT_EQ(20000u, out[0].atMs);
    }

    TEST(SchedulerBudget, ANegativeStepCannotMakeEventsFireFaster)
    {
        Scheduler s;
        s.SetBudgetStep(-1.0f);
        s.SetTimedAffixCount(4);
        EXPECT_FLOAT_EQ(1.0f, s.Budget());
    }

    TEST(SchedulerBudget, TheStretchIsAppliedAtArmAndNotAtTick)
    {
        Scheduler s;
        s.SetTimedAffixCount(1);
        s.Arm(MECH_A, 1, 20000, 0);

        // A second timed affix picked up after the arm does not move an event
        // that is already on the clock; it stretches the next one.
        s.SetTimedAffixCount(4);

        std::vector<Released> const out = Advance(s, 40000);
        ASSERT_EQ(1u, out.size()) << Describe(out);
        EXPECT_EQ(20000u, out[0].atMs);
    }

    TEST(SchedulerBudget, TheWarningsLeadIsNotStretched)
    {
        Scheduler s;
        s.SetTimedAffixCount(4);           // 1.75x
        s.Arm(MECH_A, 1, 20000, 5000);

        std::vector<Released> const out = Advance(s, 60000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(EventKind::Warn, out[0].ev.kind);
        EXPECT_EQ(35000u, out[1].atMs) << "the interval stretches";
        EXPECT_EQ(30000u, out[0].atMs) << "the telegraph stays five seconds";
    }

    // -----------------------------------------------------------------------
    // Suppression
    // -----------------------------------------------------------------------

    class SchedulerSuppression : public ::testing::TestWithParam<int>
    {
    protected:
        // Sets exactly the flag under test.
        static Suppression Flag(int which)
        {
            Suppression s;
            switch (which)
            {
                case 0: s.mounted      = true; break;
                case 1: s.inFlight     = true; break;
                case 2: s.inSanctuary  = true; break;
                case 3: s.dead         = true; break;
                case 4: s.inGrace      = true; break;
                case 5: s.offerPending = true; break;
                default: break;
            }
            return s;
        }
    };

    TEST_P(SchedulerSuppression, HoldsAFireBackAndThenReleasesTheSameEvent)
    {
        Suppression const held = Flag(GetParam());
        ASSERT_TRUE(held.Any());

        Scheduler s;
        s.Arm(MECH_A, 42, 1000, 0);

        std::vector<Released> const suppressed = Advance(s, 20000, Scheduler::TICK_MS, held);
        EXPECT_TRUE(suppressed.empty()) << "flag " << GetParam() << ": " << Describe(suppressed);
        ASSERT_EQ(1u, s.Queue().size()) << "the event waits; it is not dropped";
        EXPECT_EQ(1000u, s.Queue()[0].dueMs) << "and it waits with the due time it was armed with";

        std::vector<Released> const released = Advance(s, 2000);
        ASSERT_EQ(1u, released.size()) << Describe(released);
        EXPECT_EQ(MECH_A, released[0].ev.mechanic);
        EXPECT_EQ(42u, released[0].ev.id) << "the same event, not a replacement";
        EXPECT_EQ(EventKind::Fire, released[0].ev.kind);
        EXPECT_EQ(20500u, released[0].atMs) << "on the first tick after the flag clears";
    }

    TEST_P(SchedulerSuppression, HoldsAWarningBackToo)
    {
        Suppression const held = Flag(GetParam());

        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 5000);

        // The warning is due at 5000 and the fire at 10000; neither may go out
        // while a flag is up, because a countdown started on a griffin is a lie
        // about when the hit lands.
        std::vector<Released> const suppressed = Advance(s, 12000, Scheduler::TICK_MS, held);
        EXPECT_TRUE(suppressed.empty()) << "flag " << GetParam() << ": " << Describe(suppressed);
        EXPECT_EQ(2u, s.Queue().size());
    }

    INSTANTIATE_TEST_SUITE_P(EveryFlag, SchedulerSuppression, ::testing::Values(0, 1, 2, 3, 4, 5));

    TEST(SchedulerSuppressionCombined, AnyFlagIsEnoughAndNoneMeansNothingIsHeld)
    {
        Suppression none;
        EXPECT_FALSE(none.Any());

        Suppression all;
        all.mounted = all.inFlight = all.inSanctuary = all.dead = all.inGrace = all.offerPending = true;
        EXPECT_TRUE(all.Any());
    }

    TEST(SchedulerSuppressionCombined, SuppressionDoesNotStopTheClock)
    {
        Scheduler s;
        Suppression held;
        held.mounted = true;

        Advance(s, 10000, Scheduler::TICK_MS, held);
        EXPECT_EQ(10000u, s.NowMs()) << "the clock has to keep running or spacing loses its meaning";
    }

    TEST(SchedulerSuppressionCombined, APairReleasedTogetherStillGetsItsFullTelegraph)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 5000, 3000);      // warn at 2000, fire at 5000

        Suppression held;
        held.inGrace = true;
        std::vector<Released> const nothing = Advance(s, 20000, Scheduler::TICK_MS, held);
        ASSERT_TRUE(nothing.empty()) << Describe(nothing);

        // Both halves are now overdue. The warning goes out at once, and the
        // fire is pushed a full lead behind it rather than landing in the same
        // half second as its own warning.
        std::vector<Released> const out = Advance(s, 10000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(EventKind::Warn, out[0].ev.kind);
        EXPECT_EQ(20500u, out[0].atMs);
        EXPECT_EQ(EventKind::Fire, out[1].ev.kind);
        EXPECT_EQ(23500u, out[1].atMs);
        EXPECT_EQ(3000u, out[1].atMs - out[0].atMs);
    }

    // -----------------------------------------------------------------------
    // Warn -> fire ordering, and what happens to a warning whose fire slips
    // -----------------------------------------------------------------------

    TEST(SchedulerWarning, TheWarningPrecedesItsFireByTheLeadItWasArmedWith)
    {
        Scheduler s;
        s.Arm(MECH_A, 9, 10000, 4000);

        std::vector<Released> const out = Advance(s, 20000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(EventKind::Warn, out[0].ev.kind);
        EXPECT_EQ(6000u, out[0].atMs);
        EXPECT_EQ(EventKind::Fire, out[1].ev.kind);
        EXPECT_EQ(10000u, out[1].atMs);
        EXPECT_EQ(9u, out[1].ev.id);
    }

    TEST(SchedulerWarning, AZeroLeadArmsOnlyTheFire)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 0);
        EXPECT_EQ(1u, s.Queue().size());

        std::vector<Released> const out = Advance(s, 20000);
        ASSERT_EQ(1u, out.size()) << Describe(out);
        EXPECT_EQ(EventKind::Fire, out[0].ev.kind);
    }

    TEST(SchedulerWarning, ALeadLongerThanTheIntervalIsClampedRatherThanWrappingAround)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 3000, 9000);

        std::vector<Released> const out = Advance(s, 10000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(EventKind::Warn, out[0].ev.kind);
        EXPECT_EQ(500u, out[0].atMs) << "the warning goes out on the first tick, not in the past";

        // The warning was due at 0 and could not go out until the first
        // boundary, so the fire moves with it: a telegraph is worth its full
        // lead from the moment it is actually sent, which is the same rule
        // that saves a pair released together after a long suppression.
        EXPECT_EQ(EventKind::Fire, out[1].ev.kind);
        EXPECT_EQ(3500u, out[1].atMs);
        EXPECT_EQ(3000u, out[1].atMs - out[0].atMs);
    }

    // The decision this file exists to record. A fire whose warning has already
    // gone out and which then slips -- by spacing here, by a long suppression in
    // the test above -- is re-telegraphed rather than allowed to land after a
    // countdown that reached zero and stayed there. The player sees a second
    // warning and then the hit, a full lead later.
    TEST(SchedulerWarning, ASlippedFireIsReTelegraphedRatherThanLandingSilently)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_A, 1, 10000, 3000);   // warn at 7000, fire at 10000
        s.Arm(MECH_B, 1, 9500, 0);       // fires at 9500 and takes the spacing slot

        std::vector<Released> const out = Advance(s, 60000);

        std::vector<Released> const a = OnlyMechanic(out, MECH_A);
        ASSERT_EQ(3u, a.size()) << Describe(out);

        EXPECT_EQ(EventKind::Warn, a[0].ev.kind);
        EXPECT_EQ(7000u, a[0].atMs) << "the first warning went out on time";

        EXPECT_EQ(EventKind::Warn, a[1].ev.kind) << "the stale telegraph is replaced, not left to expire";
        EXPECT_EQ(18500u, a[1].atMs);

        EXPECT_EQ(EventKind::Fire, a[2].ev.kind);
        EXPECT_EQ(21500u, a[2].atMs);
        EXPECT_EQ(3000u, a[2].atMs - a[1].atMs) << "and the second warning is a full lead ahead of the fire";

        // The spacing is still what held it: mechanic B fired at 9500.
        std::vector<Released> const b = OnlyMechanic(out, MECH_B);
        ASSERT_EQ(1u, b.size());
        EXPECT_EQ(9500u, b[0].atMs);
        EXPECT_EQ(12000u, a[2].atMs - b[0].atMs);
    }

    // The other side of the same boundary. A short slip is not worth a second
    // countdown -- the player is still inside the telegraph they were given --
    // so the fire simply waits and lands with the warning it already had.
    TEST(SchedulerWarning, ASlipInsideTheSlackIsNotWorthASecondWarning)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_B, 1, 500, 0);        // fires at 500 and starts the spacing
        s.Arm(MECH_A, 1, 12000, 3000);   // warn at 9000, fire due at 12000

        std::vector<Released> const out = Advance(s, 60000);
        std::vector<Released> const a = OnlyMechanic(out, MECH_A);

        // The spacing holds the fire to 12500, which is 3500 after the warning
        // at 9000 -- inside the 3000 lead plus the 2000 slack.
        ASSERT_EQ(2u, a.size()) << Describe(out);
        EXPECT_EQ(EventKind::Warn, a[0].ev.kind);
        EXPECT_EQ(9000u, a[0].atMs);
        EXPECT_EQ(EventKind::Fire, a[1].ev.kind);
        EXPECT_EQ(12500u, a[1].atMs) << "delayed by the spacing, but only by half a second";
        EXPECT_LE(a[1].atMs - a[0].atMs, 3000u + Scheduler::WARN_STALE_SLACK_MS);
    }

    TEST(SchedulerWarning, AWarningIsNeverEmittedTwiceForAFireThatWasNotDelayed)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 3000);

        std::vector<Released> const warns = Only(Advance(s, 60000), EventKind::Warn);
        EXPECT_EQ(1u, warns.size()) << Describe(warns);
    }

    // -----------------------------------------------------------------------
    // Cancel
    // -----------------------------------------------------------------------

    TEST(SchedulerCancel, CancelRemovesBothHalvesAndLeavesOtherMechanicsAlone)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 3000);
        s.Arm(MECH_B, 5, 10000, 3000);
        ASSERT_EQ(4u, s.Queue().size());

        s.Cancel(MECH_A);
        ASSERT_EQ(2u, s.Queue().size());
        for (ScheduledEvent const& ev : s.Queue())
            EXPECT_EQ(MECH_B, ev.mechanic);

        std::vector<Released> const out = Advance(s, 60000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(MECH_B, out[0].ev.mechanic);
        EXPECT_EQ(MECH_B, out[1].ev.mechanic);
        EXPECT_EQ(5u, out[1].ev.id);
    }

    TEST(SchedulerCancel, CancelAfterTheWarningStillRemovesTheFire)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 3000);

        std::vector<Released> const first = Advance(s, 8000);
        ASSERT_EQ(1u, first.size()) << Describe(first);
        EXPECT_EQ(EventKind::Warn, first[0].ev.kind);

        s.Cancel(MECH_A);
        EXPECT_TRUE(s.Queue().empty());
        EXPECT_TRUE(Advance(s, 60000).empty());
    }

    TEST(SchedulerCancel, CancelAllEmptiesTheQueue)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 3000);
        s.Arm(MECH_B, 1, 12000, 0);
        s.Arm(MECH_C, 1, 14000, 0);
        ASSERT_EQ(4u, s.Queue().size());

        s.CancelAll();
        EXPECT_TRUE(s.Queue().empty());
        EXPECT_TRUE(Advance(s, 60000).empty());
    }

    TEST(SchedulerCancel, CancelAllKeepsTheClockAndTheSpacing)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.Arm(MECH_A, 1, 1000, 0);

        std::vector<Released> const first = Advance(s, 2000);
        ASSERT_EQ(1u, first.size()) << Describe(first);

        s.CancelAll();
        EXPECT_EQ(2000u, s.NowMs());

        // A fire armed straight after is still spaced against the one that just
        // went out: a player raised inside the death window does not get a free
        // burst because the queue was emptied.
        s.Arm(MECH_B, 1, 500, 0);
        std::vector<Released> const second = Advance(s, 30000);
        ASSERT_EQ(1u, second.size()) << Describe(second);
        EXPECT_EQ(13000u, second[0].atMs);
    }

    TEST(SchedulerCancel, CancellingAMechanicThatHasNothingQueuedIsHarmless)
    {
        Scheduler s;
        s.Arm(MECH_A, 1, 10000, 0);
        s.Cancel(MECH_B);
        EXPECT_EQ(1u, s.Queue().size());
    }

    // -----------------------------------------------------------------------
    // Arming
    // -----------------------------------------------------------------------

    TEST(SchedulerArm, ArmingTheSameTagAgainReschedulesRatherThanDuplicating)
    {
        Scheduler s;
        s.Arm(MECH_A, 7, 10000, 3000);
        ASSERT_EQ(2u, s.Queue().size());

        s.Arm(MECH_A, 7, 4000, 1000);
        ASSERT_EQ(2u, s.Queue().size()) << "one pair, not two";

        std::vector<Released> const out = Advance(s, 20000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(3000u, out[0].atMs);
        EXPECT_EQ(4000u, out[1].atMs);
    }

    TEST(SchedulerArm, DifferentTagsOnOneMechanicAreDifferentEvents)
    {
        Scheduler s;
        s.SetMinSpacingMs(0);
        s.Arm(MECH_A, 1, 2000, 0);
        s.Arm(MECH_A, 2, 4000, 0);
        ASSERT_EQ(2u, s.Queue().size());

        std::vector<Released> const out = Advance(s, 10000);
        ASSERT_EQ(2u, out.size()) << Describe(out);
        EXPECT_EQ(1u, out[0].ev.id);
        EXPECT_EQ(2u, out[1].ev.id);
    }

    TEST(SchedulerArm, ArmingMechanicNoneIsRefused)
    {
        Scheduler s;
        s.Arm(MECHANIC_NONE, 1, 1000, 0);
        EXPECT_TRUE(s.Queue().empty());
    }

    TEST(SchedulerArm, TheQueueIsCappedRatherThanGrowingWithoutBound)
    {
        Scheduler s;
        for (uint32 i = 0; i < 100; ++i)
            s.Arm(MECH_A, i, 10000 + i, 1000);

        EXPECT_LE(s.Queue().size(), Scheduler::MAX_QUEUED);
    }

    TEST(SchedulerArm, TheQueueIsOrderedByDueTimeWithWarningsAheadOfFires)
    {
        Scheduler s;
        s.Arm(MECH_C, 1, 30000, 1000);
        s.Arm(MECH_A, 1, 10000, 1000);
        s.Arm(MECH_B, 1, 20000, 20000);   // its warning is due immediately

        std::vector<ScheduledEvent> const& q = s.Queue();
        ASSERT_EQ(6u, q.size());
        for (size_t i = 1; i < q.size(); ++i)
        {
            EXPECT_LE(q[i - 1].dueMs, q[i].dueMs) << "at index " << i;
            if (q[i - 1].dueMs == q[i].dueMs)
            {
                EXPECT_LE(static_cast<uint8>(q[i - 1].kind), static_cast<uint8>(q[i].kind))
                    << "a warning goes before a fire that shares its due time, at index " << i;
            }
        }
        EXPECT_EQ(0u, q[0].dueMs);
        EXPECT_EQ(MECH_B, q[0].mechanic);
    }

    // -----------------------------------------------------------------------
    // The whole thing at once: four timed affixes armed together
    // -----------------------------------------------------------------------

    TEST(SchedulerIntegration, FourTimedAffixesStayTwelveSecondsApart)
    {
        Scheduler s;
        s.SetMinSpacingMs(12000);
        s.SetTimedAffixCount(4);

        uint16 const mechanics[] = { 1, 6, 14, 19 };
        for (uint16 m : mechanics)
            s.Arm(m, 1, 20000, 5000);      // 20 s stretched to 35 s, 5 s telegraph

        std::vector<Released> const out = Advance(s, 180000);
        std::vector<Released> const fires = Only(out, EventKind::Fire);
        ASSERT_EQ(4u, fires.size()) << Describe(out);

        for (size_t i = 1; i < fires.size(); ++i)
            EXPECT_GE(fires[i].atMs - fires[i - 1].atMs, 12000u)
                << "fires " << (i - 1) << " and " << i << ": " << Describe(fires);

        // Every fire is preceded by a warning for the same mechanic, and the
        // most recent one is no more than a lead plus the slack behind it.
        for (Released const& f : fires)
        {
            uint32 lastWarn = 0;
            bool   sawWarn  = false;
            for (Released const& r : out)
            {
                if (r.atMs > f.atMs)
                    break;
                if (r.ev.kind == EventKind::Warn && r.ev.mechanic == f.ev.mechanic)
                {
                    lastWarn = r.atMs;
                    sawWarn  = true;
                }
            }
            ASSERT_TRUE(sawWarn) << "mechanic " << f.ev.mechanic << " fired with no warning: " << Describe(out);
            EXPECT_LE(f.atMs - lastWarn, 5000u + Scheduler::WARN_STALE_SLACK_MS)
                << "mechanic " << f.ev.mechanic << " fired on a stale telegraph: " << Describe(out);
        }

        EXPECT_TRUE(s.Queue().empty()) << "everything armed came out";
    }
}

// =====================================================================
// Starvation (Phase 3)
// =====================================================================
//
// The bug this exists to prevent, in one sentence: a mechanic with a short
// warning lead could issue telegraph after telegraph and never once fire.
//
// Re-telegraphing moves a Fire's dueMs later, and the queue is sorted by
// dueMs, so a re-telegraphed Fire goes behind every event that has not slipped
// yet. Once enough timed affixes saturate the minimum spacing, whichever
// mechanic has the shortest lead is re-telegraphed every time and never
// reaches the front again.
//
// It was found in play, reported as "falling sky 3 rank says every 20 seconds
// in combat, but nothing burger happens". Falling Sky III asks for 20 s with a
// 3 s lead; the shipped spacing is 12 s, so it tolerates 5 s of delay against
// a 12 s wait and loses every contest. Measured before the fix: 23 warnings, 0
// fires, in five minutes of unbroken combat.
TEST(Scheduler, AShortLeadIsNotStarvedByTheSpacing)
{
    Scheduler s;
    s.SetTimedAffixCount(4);
    s.SetMinSpacingMs(12000);
    s.SetBudgetStep(0.25f);

    // A saturating set, not a typical one. Three of the four are real cadences;
    // the fast 4 s re-arm stands in for the mechanics that genuinely do it --
    // Death Rattle arms a 2 s fuse on every kill, Carrion re-arms on a 10 s
    // deferral whenever it cannot act -- because what starves an event is a
    // competitor that is always due, not a competitor that is frequent.
    //
    // Falling Sky is id 14 and every one of its rivals here has a lower id,
    // which is exactly the shape the old tie-break punished. Reverting the
    // ordering rule turns this set into 23 warnings and 0 fires for it.
    struct Timed { uint16 id; uint32 cadence; uint32 lead; };
    constexpr Timed CARRIED[] = {
        { 14, 20000, 3000 },   // Falling Sky III -- highest id, so it loses every tie
        {  5,  4000, 4000 },   // an always-due competitor
        {  2, 90000, 8000 },   // Echo
        {  4, 20000, 5000 },   // Reinforcements III
    };

    std::map<uint16, uint32> fires, warns, tag;
    for (Timed const& t : CARRIED)
    {
        tag[t.id] = 1;
        s.Arm(t.id, 1, t.cadence, t.lead);
    }

    Suppression const none;
    for (uint32 elapsed = 0; elapsed < 300000; elapsed += 500)
    {
        for (ScheduledEvent const& ev : s.Tick(500, none))
        {
            if (ev.kind == EventKind::Warn)
            {
                ++warns[ev.mechanic];
                continue;
            }

            ++fires[ev.mechanic];

            // Re-arm exactly as the mechanics do from their own OnEvent.
            for (Timed const& t : CARRIED)
                if (t.id == ev.mechanic)
                    s.Arm(t.id, ++tag[t.id], t.cadence, t.lead);
        }
    }

    for (Timed const& t : CARRIED)
    {
        EXPECT_GT(fires[t.id], 0u)
            << "mechanic " << t.id << " issued " << warns[t.id] << " warning(s) and fired "
            << fires[t.id] << " time(s) in five minutes of unbroken combat. A telegraph that "
               "never resolves is worse than no affix at all: the player learns to ignore it.";

        // A warning per fire is the promise. Some slippage is inherent in a
        // saturated queue -- that is what re-telegraphing is for -- but an
        // event that warns many times per fire is crying wolf, and a player
        // learns to ignore a countdown that usually resolves into nothing.
        //
        // The always-due competitor is exempt: it re-arms faster than the
        // spacing can ever release it, which is a shape no real mechanic has
        // for long, and it is here to saturate the queue rather than to be
        // measured.
        if (t.cadence >= 10000)
            EXPECT_LE(warns[t.id], fires[t.id] * 2 + 2)
                << "mechanic " << t.id << " warned " << warns[t.id] << " times for " << fires[t.id]
                << " fire(s); the countdown is crying wolf";
    }
}

// The ordering rule itself, in isolation: an event that has been waiting is
// ahead of one armed later when the two come due together. Before Phase 3 the
// tie went to the lower mechanic id, which is a stable ranking of the registry
// and not of anything about the events -- so the same mechanic won every tie
// and, under a tight spacing, the same mechanic never fired at all.
TEST(Scheduler, ATieGoesToWhicheverWasArmedFirst)
{
    Scheduler s;
    s.SetMinSpacingMs(0);     // spacing off: this is purely about order

    // Armed in descending id order, so an id tie-break would reverse them.
    s.Arm(19, 1, 5000, 0);
    s.Arm(14, 1, 5000, 0);
    s.Arm(6,  1, 5000, 0);

    std::vector<uint16> order;
    Suppression const none;
    for (uint32 t = 0; t < 10000 && order.size() < 3; t += 500)
        for (ScheduledEvent const& ev : s.Tick(500, none))
            if (ev.kind == EventKind::Fire)
                order.push_back(ev.mechanic);

    ASSERT_EQ(3u, order.size());
    EXPECT_EQ(19u, order[0]) << "armed first, so it goes first";
    EXPECT_EQ(14u, order[1]);
    EXPECT_EQ(6u,  order[2]) << "armed last, so it goes last -- not first for having the lowest id";
}
