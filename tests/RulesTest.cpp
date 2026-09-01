/*
 * mod-gauntlet - the redesigned cards' arithmetic, and the shapes it must keep
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Six cards were redesigned and shipped without a single test. This is that
// debt paid, and the thing it is trying not to repeat is the one the bench
// already got caught by: an assertion that passes because it is not really
// asking anything.
//
// So these are mostly *shape* tests rather than value tests. "Hubris' duel
// multiplies by 0.75" is a restatement of the table and fails only when
// someone edits the table deliberately. "The duel is always a shelter and the
// rest of the pull is always an exposure" is a claim about the card being the
// card, and it fails when a digit is transposed -- which is the fault that
// actually happens. They were written over rank ladders; the ladders went
// (docs/rarity-plan.md section 5b) and every claim that survived is a claim
// about the one value each card has now.

#include "GauntletRules.h"

#include <gtest/gtest.h>

namespace
{
    using namespace Gauntlet;
    using namespace Gauntlet::Rules;

    // ------------------------------------------------------------------
    // Hubris. The card is "the duel shelters you, everything else does not".
    // ------------------------------------------------------------------

    TEST(Rules, HubrisDuelIsAShelterAndTheRestIsAnExposure)
    {
        EXPECT_LT(HubrisTakenMult(true), 1.0f) << "the duel must take less, or the card is inside out";
        EXPECT_GT(HubrisTakenMult(false), 1.0f) << "everything else must take more";
    }

    // ------------------------------------------------------------------
    // Overextended. Facing is the whole card.
    // ------------------------------------------------------------------

    TEST(Rules, OverextendedChargesOnlyForYourBack)
    {
        EXPECT_FLOAT_EQ(OverextendedTakenMult(false), 1.0f)
            << "facing an enemy must cost nothing, or facing is not the verb";
        EXPECT_GT(OverextendedTakenMult(true), 1.0f);
    }

    // ------------------------------------------------------------------
    // Falling Sky. The verb is movement, so the threshold must be reachable
    // and moving must always answer it.
    // ------------------------------------------------------------------

    TEST(Rules, FallingSkyDoesNotArmBeforeItsWindow)
    {
        EXPECT_FALSE(FallingSkyArms(0));
        EXPECT_FALSE(FallingSkyArms(STILL_MS - 1));
        EXPECT_TRUE(FallingSkyArms(STILL_MS));
    }

    TEST(Rules, FallingSkyStaysPlayable)
    {
        // The failure mode is a card that no caster can play at all. Two
        // seconds is shorter than a great many casts in 3.3.5; if a tuning
        // pass ever goes below it, that is a decision someone should have to
        // make on purpose.
        EXPECT_GE(STILL_MS, 2000u) << "below two seconds this stops being a verb and becomes a class ban";
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
        EXPECT_FLOAT_EQ(FrenzyDoneMult(0, 0), 1.0f);
    }

    TEST(Rules, FrenzyRisesWithEveryStack)
    {
        for (uint32 n = 1; n <= MAX_STACKS; ++n)
            EXPECT_GT(FrenzyDoneMult(n, 0), FrenzyDoneMult(n - 1, 0));
    }

    TEST(Rules, FrenzyIsCappedAndCannotBeFarmedPastIt)
    {
        // The cap is the card's only bound. Without it a long enough chain is
        // an unbounded damage multiplier.
        float const atCap = FrenzyDoneMult(MAX_STACKS, 0);
        EXPECT_FLOAT_EQ(FrenzyDoneMult(MAX_STACKS + 1, 0), atCap);
        EXPECT_FLOAT_EQ(FrenzyDoneMult(9999, 0), atCap);
    }

    TEST(Rules, FrenzyPrefersTheOfferCardsNumberOverTheTable)
    {
        // The offer promises boonMag per stack; the multiplier must pay that
        // and not the table, or the card lies on the card.
        EXPECT_FLOAT_EQ(FrenzyDoneMult(2, 25), 1.0f + 0.25f * 2.0f);
    }

    // ------------------------------------------------------------------
    // Deep Wounds. A kill must always close something.
    // ------------------------------------------------------------------

    TEST(Rules, DeepWoundsAlwaysClosesAtLeastAPoint)
    {
        // The rounding case that matters: a level-one health pool where the
        // percentage floors to nothing. A kill that closes zero reads as the
        // card being broken.
        EXPECT_GE(DeepWoundsClose(0), 1);
        EXPECT_GE(DeepWoundsClose(1), 1);
        EXPECT_GE(DeepWoundsClose(20), 1);
    }

    TEST(Rules, DeepWoundsOpensMoreThanAKillCloses)
    {
        // The card's whole tension: a kill closes *some* of the wound, never
        // all of what the fight opened, or the wound is not a wound.
        EXPECT_GT(WOUND_PCT, KILL_CLOSE_PCT);
    }

    TEST(Rules, DeepWoundsCannotOutrunTheWoundItCloses)
    {
        // A kill must never close more than a full pool: that would hand the
        // player health they never lost.
        uint32 const pool = 10000;
        EXPECT_LT(uint32(DeepWoundsClose(pool)), pool);
    }

    // ------------------------------------------------------------------
    // Killing Floor. Held, not refused -- so something must always come back.
    // ------------------------------------------------------------------

    TEST(Rules, KillingFloorAlwaysHandsSomethingBackForAKill)
    {
        EXPECT_GT(KillingFloorPayout(1000), 0u) << "a kill that pays nothing is a refusal, not a delay";
    }

    TEST(Rules, KillingFloorNeverPaysMoreThanWasHeld)
    {
        EXPECT_LE(KillingFloorPayout(1000), 1000u);
        EXPECT_LE(KillingFloorLeaveBack(1000), 1000u);
    }

    TEST(Rules, KillingFloorMakesWinningWorthMoreThanWalkingAway)
    {
        // The whole decision the card offers. If breaking off ever paid at
        // least as well as the kill, "walk away" would be strictly correct and
        // the card would have no choice in it at all. The old rank I was pure
        // delay by design -- the teaching rank -- and went with the ranks; the
        // card's one value has to carry the decision.
        EXPECT_GT(KillingFloorPayout(1000), KillingFloorLeaveBack(1000)) << "disengaging must cost something";
        EXPECT_LT(KillingFloorLeaveBack(1000), 1000u) << "breaking off must lose something, or there is no decision";
    }

    TEST(Rules, KillingFloorHandlesAnEmptyBank)
    {
        EXPECT_EQ(KillingFloorPayout(0), 0u);
        EXPECT_EQ(KillingFloorLeaveBack(0), 0u);
    }

    // ------------------------------------------------------------------
    // The reroll purse. Section 7.5 of the plan owns the numbers; what is
    // held here is that the economy is one at all.
    // ------------------------------------------------------------------

    TEST(Rules, TheRerollPurseIsWorthHaving)
    {
        // A fresh run can afford the button, or it is decoration on every
        // chooser until the first skip.
        EXPECT_GT(REROLL_STARTING_CHARGES, 0u);

        // Skipping pays, or it is a trap: a tier given up for nothing.
        EXPECT_GT(SKIP_EARNS_CHARGES, 0u);
        EXPECT_EQ(ChargesAfterSkip(0), SKIP_EARNS_CHARGES);
        EXPECT_GT(ChargesAfterSkip(REROLL_STARTING_CHARGES), REROLL_STARTING_CHARGES);

        // The purse travels as one byte; hoarding saturates rather than wraps.
        EXPECT_EQ(ChargesAfterSkip(REROLL_MAX_CHARGES), REROLL_MAX_CHARGES);
        EXPECT_EQ(ChargesAfterSkip(255), REROLL_MAX_CHARGES);
    }

    // ------------------------------------------------------------------
    // Rarity weights. The plan calls the numbers invented; what is held here
    // is the shape they must keep whatever they are tuned to.
    // ------------------------------------------------------------------

    constexpr Rarity EVERY_RARITY[] = { Rarity::Common, Rarity::Uncommon, Rarity::Rare,
                                        Rarity::Epic, Rarity::Legendary };
    static_assert(std::size(EVERY_RARITY) == RARITY_COUNT, "a rarity is missing from the test");

    // The first and last tier of each band, so a claim about a band is made at
    // both of its edges rather than only where the table happens to be read.
    template <typename Fn>
    void ForEachBandEdge(Fn&& fn)
    {
        for (size_t band = 0; band < RARITY_BANDS; ++band)
        {
            uint8 const first = static_cast<uint8>(band * RARITY_BAND_TIERS + 1);
            uint8 const last  = static_cast<uint8>((band + 1) * RARITY_BAND_TIERS);
            fn(band, first);
            fn(band, last);
        }
    }

    TEST(Rules, RarityBandsCoverTheAxisAndClampPastIt)
    {
        ForEachBandEdge([](size_t band, uint8 tier)
        {
            EXPECT_EQ(RarityBand(tier), band) << "tier " << int(tier);
        });

        // A tier the axis does not have. Zero is below FIRST_TIER and 255 is
        // past every window in the table; both must land inside the table.
        EXPECT_EQ(RarityBand(0), 0u);
        EXPECT_EQ(RarityBand(255), RARITY_BANDS - 1);
        EXPECT_EQ(RarityBand(81), RARITY_BANDS - 1)
            << "a realm with a longer axis gets the endgame mix, not a read past the end";
    }

    TEST(Rules, RarityWeightsAreASharePerBand)
    {
        // Each band is a distribution: the five shares are percentages of the
        // slots at that tier and must add to a hundred. A band that added to
        // ninety would silently under-weight whatever was listed last.
        ForEachBandEdge([](size_t band, uint8 tier)
        {
            uint32 sum = 0;
            for (Rarity r : EVERY_RARITY)
                sum += RarityWeight(tier, r);
            EXPECT_EQ(sum, 100u) << "band " << band << " at tier " << int(tier);
        });
    }

    TEST(Rules, CommonsFadeAndRarerCardsRiseAcrossTheRun)
    {
        // The plan's whole claim about the ladder: "early tiers are nearly all
        // commons, legendaries only appear late". Monotonic in each direction,
        // band over band. Uncommon is left out on purpose -- it is the bridge
        // between the two and is meant to hump; the sentinel on its table in
        // GauntletRules.h says so.
        for (size_t band = 1; band < RARITY_BANDS; ++band)
        {
            uint8 const now  = static_cast<uint8>(band * RARITY_BAND_TIERS + 1);
            uint8 const then = static_cast<uint8>(band * RARITY_BAND_TIERS);

            EXPECT_LT(RarityWeight(now, Rarity::Common), RarityWeight(then, Rarity::Common))
                << "commons must thin out with every band";
            EXPECT_GT(RarityWeight(now, Rarity::Rare), RarityWeight(then, Rarity::Rare));
            EXPECT_GT(RarityWeight(now, Rarity::Epic), RarityWeight(then, Rarity::Epic));
            EXPECT_GE(RarityWeight(now, Rarity::Legendary), RarityWeight(then, Rarity::Legendary));
        }
    }

    TEST(Rules, TheOpeningIsCommonAndTheCloseIsNot)
    {
        // At both ends the shape has to be unmistakable, or the axis is not
        // doing the job the ranks did. The first band is a common majority; the
        // last is anything but.
        EXPECT_GT(RarityWeight(1, Rarity::Common), 50u);
        EXPECT_LT(RarityWeight(80, Rarity::Common), 25u);
        EXPECT_GT(RarityWeight(80, Rarity::Rare) + RarityWeight(80, Rarity::Epic)
                  + RarityWeight(80, Rarity::Legendary), 50u)
            << "the endgame must be mostly rare or better";
    }

    TEST(Rules, NothingRunDefiningInTheFirstHalf)
    {
        // The plan's dashes. A legendary is "run-defining, one per run" and an
        // epic "changes how a whole system plays"; neither belongs in front of
        // a character still learning what the module does. Epics wait for the
        // second band, legendaries for the third.
        for (uint8 tier = 1; tier <= RARITY_BAND_TIERS; ++tier)
        {
            EXPECT_EQ(RarityWeight(tier, Rarity::Epic), 0u) << "tier " << int(tier);
            EXPECT_EQ(RarityWeight(tier, Rarity::Legendary), 0u) << "tier " << int(tier);
        }
        for (uint8 tier = RARITY_BAND_TIERS + 1; tier <= 2 * RARITY_BAND_TIERS; ++tier)
            EXPECT_EQ(RarityWeight(tier, Rarity::Legendary), 0u) << "tier " << int(tier);

        // And both do arrive, or the top of the ladder is decoration.
        EXPECT_GT(RarityWeight(80, Rarity::Epic), 0u);
        EXPECT_GT(RarityWeight(80, Rarity::Legendary), 0u);
    }

    TEST(Rules, EveryRarityIsReachableSomewhere)
    {
        // A rarity with a zero weight in every band is a rarity the run never
        // sees; the sixty cards written for it would be dead rows.
        for (Rarity r : EVERY_RARITY)
        {
            uint32 anywhere = 0;
            for (uint8 tier = 1; tier <= 80; ++tier)
                anywhere += RarityWeight(tier, r);
            EXPECT_GT(anywhere, 0u) << RarityName(r) << " is never rolled at any tier";
        }
    }

    TEST(Rules, KillingFloorDoesNotOverflowOnAHugeBank)
    {
        // The bank is a uint64 accumulated over a whole fight. The arithmetic
        // multiplies before it divides, so a large bank is the case where it
        // would wrap.
        uint64 const huge = uint64(1) << 40;
        EXPECT_LE(KillingFloorPayout(huge), huge);
        EXPECT_LE(KillingFloorLeaveBack(huge), huge);
    }
}

// ---------------------------------------------------------------------------
// The three reward-shaped cards of docs/commons.md -- Scavenger's Eye (88),
// Blood for Bread (89), Waste Not (90).
//
// The claims worth making about them are the ones that are about the cards
// being those cards. Their percentages will be tuned by play; that one card
// takes more than another and must therefore pay more than it is the design,
// and a transposed digit breaks it.
// ---------------------------------------------------------------------------

TEST(Rules, TheCardThatTakesMoreOfYourDowntimePaysMore)
{
    // Blood for Bread gives up eating and drinking -- both bars, every rest.
    // Waste Not gives up potions only. If the smaller sacrifice ever paid as
    // well as the larger one, the larger card would be strictly worse than the
    // smaller and there would be no reason to ever take it.
    for (uint32 pool : { 100u, 743u, 4200u, 16795u, 100000u })
        EXPECT_GT(Rules::BloodForBreadRestore(pool), Rules::WasteNotRestore(pool))
            << "pool " << pool;
}

TEST(Rules, AKillAlwaysRestoresSomethingAndNeverThePlayersWholePool)
{
    // Both halves matter. Nothing restored reads to a player as the card being
    // broken -- which is the fault Deafening Roar shipped with -- and a
    // level-5 health pool rounds a percentage to zero long before the player
    // stops noticing. A whole pool would make either card a full heal on every
    // kill, which is not a hardcore curse at all.
    for (uint32 pool : { 1u, 20u, 100u, 743u, 4200u, 16795u, 100000u })
    {
        EXPECT_GT(Rules::BloodForBreadRestore(pool), 0u) << "pool " << pool;
        EXPECT_GT(Rules::WasteNotRestore(pool), 0u) << "pool " << pool;

        if (pool > 2u)
        {
            EXPECT_LT(Rules::BloodForBreadRestore(pool), pool) << "pool " << pool;
            EXPECT_LT(Rules::WasteNotRestore(pool), pool) << "pool " << pool;
        }
    }
}

TEST(Rules, TheUncommonNoticesYouFromLessFarThanTheRareDoes)
{
    // Scavenger's Eye is Keen-nosed's curse at a lower rarity, and rarity is
    // "how much of the run the card changes" (docs/rarity-plan.md section 2).
    // If the uncommon ever reached further than the rare, the rare would be
    // the weaker card at the higher rarity and the ladder would read backwards.
    EXPECT_GT(Rules::SCAVENGER_YARDS, 0.0f) << "a curse of zero yards is not a curse";
    EXPECT_LT(Rules::SCAVENGER_YARDS, Rules::KEEN_NOSED_YARDS);
}

TEST(Rules, ACleanFightIsPaidAWholeExtraRollAndNotAFraction)
{
    // The card's upside has to be something the player earned by fighting a
    // particular way, so it is a second roll of the creature's own table --
    // its blues included -- rather than a percentage laid over the first roll.
    // A percentage would be a boon, and a boon is not what MF_RewardShaped is
    // for.
    EXPECT_GT(Rules::SCAVENGER_ROLLS, 1u);
    EXPECT_EQ(Rules::ScavengerExtraRolls(), Rules::SCAVENGER_ROLLS - 1u);
    EXPECT_GE(Rules::ScavengerExtraRolls(), 1u);
}

// ---------------------------------------------------------------------------
// The first loot cards -- Fresh Kill (110), Blood Price (111), Trophy Hunter
// (112). docs/greed-redesign.md section 7.3.
// ---------------------------------------------------------------------------

TEST(Rules, ACorpseLootedInTimeIsWorthMoreThanOneLootedLate)
{
    // Fresh Kill is a clock, and the clock only means something if the two
    // ends of it differ. If "late" ever paid as well as "in time" the card
    // would be a free double-loot with a sentence about hurrying attached, and
    // the decision it exists to create -- stop mid-pull, or finish the fight --
    // would not exist. That is the fault Killing Floor shipped with once.
    EXPECT_GT(Rules::FRESH_KILL_ROLLS, Rules::FRESH_KILL_LATE_ROLLS);
    EXPECT_GT(Rules::FRESH_KILL_ROLLS, 1u) << "in time must actually double something";
    EXPECT_EQ(Rules::FRESH_KILL_LATE_ROLLS, 0u) << "late holds nothing but the quest";
    EXPECT_GT(Rules::FRESH_KILL_WINDOW_MS, 0u);
}

TEST(Rules, BloodPriceCanNeverBeTheThingThatKillsTheRun)
{
    // The card charges health to open a corpse, in a mode where dying is the
    // end of the character. A loot click that can kill is not a cost, it is a
    // trap, so the floor is part of the arithmetic rather than a clamp someone
    // has to remember at the call site -- and this is the test that says so.
    for (uint32 maxHealth : { 40u, 743u, 4200u, 16795u, 100000u })
        for (uint32 current : { 1u, 2u, 7u, 50u, 743u, 4200u, 16795u, 100000u })
        {
            if (current > maxHealth)
                continue;

            uint32 const cost = Rules::BloodPriceCost(maxHealth, current);
            EXPECT_LT(cost, current)
                << "max " << maxHealth << " current " << current << ": the cost took the last point";
            EXPECT_GE(current - cost, 1u) << "max " << maxHealth << " current " << current;
        }

    // And at one health it takes nothing at all rather than rounding to zero
    // by luck.
    EXPECT_EQ(Rules::BloodPriceCost(10000u, 1u), 0u);
}

TEST(Rules, BloodPricePaysForLootingHurtRatherThanForLootingSafely)
{
    // The whole card is that the greedy line and the safe line disagree. If
    // looting whole ever paid as well as looting hurt, the cost would be a tax
    // with no decision attached.
    EXPECT_GT(Rules::BLOOD_PRICE_ROLLS_HURT, Rules::BLOOD_PRICE_ROLLS_WHOLE);
    EXPECT_GE(Rules::BLOOD_PRICE_ROLLS_WHOLE, 1u) << "a corpse always holds its own loot";
    EXPECT_GT(Rules::BLOOD_PRICE_PCT, 0u) << "a cost of nothing is not a cost";
}

TEST(Rules, TrophyHunterIsADangerBeforeItIsAPayday)
{
    // The curse has to be real for the reward to be a decision, and the chest
    // has to exist for every level a character can be -- a card that offered
    // no chest to a level-80 player would be the boon that prints and does
    // nothing.
    EXPECT_GT(Rules::TROPHY_TAKEN_PCT, 0u);
    EXPECT_GT(Rules::TrophyTakenMult(), 1.0f);
    EXPECT_GT(Rules::TROPHY_YARDS, 0.0f);

    for (uint8 level = 1; level <= 80; ++level)
        EXPECT_NE(Rules::TrophyChestFor(level), 0u) << "no chest for level " << unsigned(level);

    // The bands are a ladder, not a lookup that repeats itself: a low-level
    // character and a level-80 one must not be handed the same chest, or the
    // level banding is decoration.
    EXPECT_NE(Rules::TrophyChestFor(10), Rules::TrophyChestFor(80));
}

TEST(Rules, ScavengeAndBloodPriceDisagreeAboutWhatLootingCosts)
{
    // The two cards are each other's opposite on purpose (docs/commons.md
    // section 4b): opening a corpse costs health under Blood Price and
    // restores it under Scavenge. If they ever pointed the same way, one of
    // them would be the other with different words, and the choice a run makes
    // between them would not exist.
    //
    // Held at several pools because both are percentages of a maximum and the
    // claim is about their directions, not their sizes.
    for (uint32 pool : { 100u, 743u, 4200u, 16795u })
    {
        EXPECT_GT(Rules::ScavengeHeal(pool), 0u) << "pool " << pool;
        EXPECT_GT(Rules::BloodPriceCost(pool, pool), 0u) << "pool " << pool;
    }

    // And the curse is a real one: a card that restored health on loot and
    // took nothing would be a boon with a sentence attached.
    EXPECT_GT(Rules::SCAVENGE_TAKEN_PCT, 0);
    EXPECT_GT(Rules::SCAVENGE_HEAL_PCT, 0u);
}

TEST(Rules, GravediggerIsTheQuieterCardBesideCarrion)
{
    // Both stand something up when a corpse is opened, and the common has to
    // be the smaller of the two or the rare beside it is pointless. Carrion
    // draws its pack every fourth corpse and pays nothing for them;
    // Gravedigger raises one every eighth and pays that corpse's own table.
    //
    // The number Carrion uses lives inside its own translation unit, which
    // includes Player.h and is therefore out of this build's reach -- so what
    // is asserted here is the half that can be: the rhythm is slower than
    // every-other-corpse, and the payout is a whole extra roll rather than a
    // fraction of one.
    EXPECT_GT(Rules::GRAVEDIGGER_EVERY, 4)
        << "a common that raises something more often than Carrion's rare does is the louder card";
    EXPECT_GT(Rules::GRAVEDIGGER_ROLLS, 1u);
    EXPECT_GT(Rules::GRAVEDIGGER_LIFE_MS, 0u);
}

