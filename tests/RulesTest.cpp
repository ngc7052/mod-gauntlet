/*
 * mod-gauntlet - the redesigned cards' arithmetic, and the shapes it must keep
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Six cards were redesigned and shipped without a single test. This is that
// debt paid, and the thing it is trying not to repeat is the one the bench
// already got caught by: an assertion that passes because it is not really
// asking anything.
//
// So these are mostly *shape* tests rather than value tests. "Rank III of
// Hubris multiplies by 0.65" is a restatement of the table and fails only when
// someone edits the table deliberately. "The duel is always a shelter and the
// rest of the pull is always an exposure, at every rank" is a claim about the
// card being the card, and it fails when a digit is transposed -- which is the
// fault that actually happens.

#include "GauntletRules.h"

#include <gtest/gtest.h>

namespace
{
    using namespace Gauntlet;
    using namespace Gauntlet::Rules;

    // Every rank a player can actually carry.
    template <typename Fn>
    void ForEachRank(Fn&& fn)
    {
        for (uint8 rank = 1; rank <= MAX_RANK; ++rank)
            fn(rank);
    }

    // ------------------------------------------------------------------
    // Index: every function below trusts it, so it is checked first.
    // ------------------------------------------------------------------

    TEST(Rules, RankIsClampedToTheTable)
    {
        EXPECT_EQ(Index(1), 0u);
        EXPECT_EQ(Index(MAX_RANK), MAX_RANK - 1);

        // Out of range in both directions, because a stored rank comes off a
        // database row and nothing in the schema stops it being nonsense.
        EXPECT_EQ(Index(0), 0u);
        EXPECT_EQ(Index(200), MAX_RANK - 1);
    }

    // ------------------------------------------------------------------
    // Hubris. The card is "the duel shelters you, everything else does not".
    // ------------------------------------------------------------------

    TEST(Rules, HubrisDuelIsAlwaysAShelterAndTheRestIsAlwaysAnExposure)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_LT(HubrisTakenMult(rank, true), 1.0f)
                << "rank " << int(rank) << ": the duel must take less, or the card is inside out";
            EXPECT_GT(HubrisTakenMult(rank, false), 1.0f)
                << "rank " << int(rank) << ": everything else must take more";
        });
    }

    TEST(Rules, HubrisStakesRiseWithRank)
    {
        // Both halves escalate: the shelter deepens and the exposure sharpens.
        // A rank that made either one *softer* would be a rank-up that costs a
        // tier and makes the card easier.
        for (uint8 rank = 2; rank <= MAX_RANK; ++rank)
        {
            EXPECT_LT(HubrisTakenMult(rank, true), HubrisTakenMult(rank - 1, true));
            EXPECT_GT(HubrisTakenMult(rank, false), HubrisTakenMult(rank - 1, false));
        }
    }

    TEST(Rules, HubrisIsNeutralNowhere)
    {
        // There is no rank at which carrying this card changes nothing.
        ForEachRank([](uint8 rank)
        {
            EXPECT_NE(HubrisTakenMult(rank, true), 1.0f);
            EXPECT_NE(HubrisTakenMult(rank, false), 1.0f);
        });
    }

    // ------------------------------------------------------------------
    // Overextended. Facing is the whole card.
    // ------------------------------------------------------------------

    TEST(Rules, OverextendedChargesOnlyForYourBack)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_FLOAT_EQ(OverextendedTakenMult(rank, false), 1.0f)
                << "rank " << int(rank) << ": facing an enemy must cost nothing, or facing is not the verb";
            EXPECT_GT(OverextendedTakenMult(rank, true), 1.0f);
        });
    }

    TEST(Rules, OverextendedGetsWorseWithRank)
    {
        for (uint8 rank = 2; rank <= MAX_RANK; ++rank)
            EXPECT_GT(OverextendedTakenMult(rank, true), OverextendedTakenMult(rank - 1, true));
    }

    // ------------------------------------------------------------------
    // Falling Sky. The verb is movement, so the threshold must be reachable
    // and moving must always answer it.
    // ------------------------------------------------------------------

    TEST(Rules, FallingSkyDoesNotArmBeforeItsWindow)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_FALSE(FallingSkyArms(rank, 0));
            EXPECT_FALSE(FallingSkyArms(rank, STILL_MS[Index(rank)] - 1));
            EXPECT_TRUE(FallingSkyArms(rank, STILL_MS[Index(rank)]));
        });
    }

    TEST(Rules, FallingSkyWindowShrinksWithRankAndStaysPlayable)
    {
        for (uint8 rank = 2; rank <= MAX_RANK; ++rank)
            EXPECT_LT(STILL_MS[Index(rank)], STILL_MS[Index(rank - 1)]);

        // The top rank is the one most likely to be wrong, and the failure mode
        // is a card that no caster can play at all. Two seconds is shorter than
        // a great many casts in 3.3.5; if a tuning pass ever goes below it,
        // that is a decision someone should have to make on purpose.
        EXPECT_GE(STILL_MS[Index(MAX_RANK)], 2000u)
            << "below two seconds this stops being a verb and becomes a class ban";
    }

    TEST(Rules, MovingIsAlwaysAnAnswer)
    {
        EXPECT_FALSE(FallingSkyMoved(0.0f));
        EXPECT_FALSE(FallingSkyMoved(MOVED_YARDS));
        EXPECT_TRUE(FallingSkyMoved(MOVED_YARDS + 0.1f));

        // Melee shuffle must not read as movement, or the card never fires on
        // the players it is aimed at.
        EXPECT_FALSE(FallingSkyMoved(1.0f));
    }

    // ------------------------------------------------------------------
    // Frenzy. Damage only, and the stacks are capped.
    // ------------------------------------------------------------------

    TEST(Rules, FrenzyPaysNothingWithoutAChain)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_FLOAT_EQ(FrenzyDoneMult(rank, 0, 0), 1.0f);
        });
    }

    TEST(Rules, FrenzyRisesWithEveryStack)
    {
        ForEachRank([](uint8 rank)
        {
            for (uint32 n = 1; n <= MAX_STACKS; ++n)
                EXPECT_GT(FrenzyDoneMult(rank, n, 0), FrenzyDoneMult(rank, n - 1, 0));
        });
    }

    TEST(Rules, FrenzyIsCappedAndCannotBeFarmedPastIt)
    {
        // The cap is the card's only bound. Without it a long enough chain is
        // an unbounded damage multiplier.
        ForEachRank([](uint8 rank)
        {
            float const atCap = FrenzyDoneMult(rank, MAX_STACKS, 0);
            EXPECT_FLOAT_EQ(FrenzyDoneMult(rank, MAX_STACKS + 1, 0), atCap);
            EXPECT_FLOAT_EQ(FrenzyDoneMult(rank, 9999, 0), atCap);
        });
    }

    TEST(Rules, FrenzyPrefersTheOfferCardsNumberOverTheTable)
    {
        // The offer promises boonMag per stack; the multiplier must pay that
        // and not the table, or the card lies on the card.
        EXPECT_FLOAT_EQ(FrenzyDoneMult(1, 2, 25), 1.0f + 0.25f * 2.0f);
    }

    // ------------------------------------------------------------------
    // Deep Wounds. A kill must always close something.
    // ------------------------------------------------------------------

    TEST(Rules, DeepWoundsAlwaysClosesAtLeastAPoint)
    {
        // The rounding case that matters: a level-one health pool where the
        // percentage floors to nothing. A kill that closes zero reads as the
        // card being broken.
        ForEachRank([](uint8 rank)
        {
            EXPECT_GE(DeepWoundsClose(rank, 0), 1);
            EXPECT_GE(DeepWoundsClose(rank, 1), 1);
            EXPECT_GE(DeepWoundsClose(rank, 20), 1);
        });
    }

    TEST(Rules, DeepWoundsClosesLessAsRankRises)
    {
        // The ladder tightens from both ends: more of the damage becomes a
        // wound, and less of it closes per kill.
        for (uint8 rank = 2; rank <= MAX_RANK; ++rank)
        {
            EXPECT_LE(DeepWoundsClose(rank, 10000), DeepWoundsClose(rank - 1, 10000));
            EXPECT_GT(WOUND_PCT[Index(rank)], WOUND_PCT[Index(rank - 1)]);
        }
    }

    TEST(Rules, DeepWoundsCannotOutrunTheWoundItCloses)
    {
        // A kill must never close more than a full pool: that would hand the
        // player health they never lost.
        ForEachRank([](uint8 rank)
        {
            uint32 const pool = 10000;
            EXPECT_LT(uint32(DeepWoundsClose(rank, pool)), pool);
        });
    }

    // ------------------------------------------------------------------
    // Killing Floor. Held, not refused -- so something must always come back.
    // ------------------------------------------------------------------

    TEST(Rules, KillingFloorAlwaysHandsSomethingBackForAKill)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_GT(KillingFloorPayout(rank, 1000), 0u)
                << "rank " << int(rank) << ": a kill that pays nothing is a refusal, not a delay";
        });
    }

    TEST(Rules, KillingFloorNeverPaysMoreThanWasHeld)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_LE(KillingFloorPayout(rank, 1000), 1000u);
            EXPECT_LE(KillingFloorLeaveBack(rank, 1000), 1000u);
        });
    }

    TEST(Rules, KillingFloorMakesWinningWorthMoreThanWalkingAway)
    {
        // The whole decision the card offers. If breaking off ever paid at
        // least as well as the kill, "walk away" would be strictly correct and
        // the card would have no choice in it at all.
        for (uint8 rank = 2; rank <= MAX_RANK; ++rank)
            EXPECT_GT(KillingFloorPayout(rank, 1000), KillingFloorLeaveBack(rank, 1000))
                << "rank " << int(rank) << ": disengaging must cost something";
    }

    TEST(Rules, KillingFloorRankOneIsPureDelay)
    {
        // Rank I is deliberately not a tax: everything held comes back either
        // way, and only the timing changes. That is what makes the first rank
        // teachable.
        EXPECT_EQ(KillingFloorPayout(1, 1000), 1000u);
        EXPECT_EQ(KillingFloorLeaveBack(1, 1000), 1000u);
    }

    TEST(Rules, KillingFloorGetsHarsherWithRank)
    {
        for (uint8 rank = 2; rank <= MAX_RANK; ++rank)
        {
            EXPECT_LT(KillingFloorPayout(rank, 1000), KillingFloorPayout(rank - 1, 1000));
            EXPECT_LT(KillingFloorLeaveBack(rank, 1000), KillingFloorLeaveBack(rank - 1, 1000));
        }
    }

    TEST(Rules, KillingFloorHandlesAnEmptyBank)
    {
        ForEachRank([](uint8 rank)
        {
            EXPECT_EQ(KillingFloorPayout(rank, 0), 0u);
            EXPECT_EQ(KillingFloorLeaveBack(rank, 0), 0u);
        });
    }

    TEST(Rules, KillingFloorDoesNotOverflowOnAHugeBank)
    {
        // The bank is a uint64 accumulated over a whole fight. The arithmetic
        // multiplies before it divides, so a large bank is the case where it
        // would wrap.
        uint64 const huge = uint64(1) << 40;
        ForEachRank([huge](uint8 rank)
        {
            EXPECT_LE(KillingFloorPayout(rank, huge), huge);
            EXPECT_LE(KillingFloorLeaveBack(rank, huge), huge);
        });
    }
}
