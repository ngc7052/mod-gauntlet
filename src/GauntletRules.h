/*
 * mod-gauntlet - the tuning decisions of the redesigned cards, without a world
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_RULES_H
#define MOD_GAUNTLET_RULES_H

#include "Gauntlet.h"

#include <algorithm>
#include <iterator>

// The six cards redesigned in docs/tempo-redesign.md keep their ladders and
// their arithmetic here rather than inside their own translation units, and
// the rarity weights of docs/rarity-plan.md join them for the same reason.
//
// The reason is that the mechanics files all include Player.h, which puts them
// outside tests/run-tests.sh's Player-free set -- so every number in them was
// unreachable by any test, and six cards were redesigned without one. That is
// the same criticism that landed on the bench for passing a Reinforcements that
// did not work: an assertion about the thing that matters, or nothing.
//
// What is here is only the part with a decision in it. Applying the result
// still needs a Player and still lives in the mechanic.

namespace Gauntlet
{
namespace Rules
{
    // Clamps a stored rank to a table index. Every function below takes the
    // rank as the player carries it (1..MAX_RANK) rather than an index, because
    // an off-by-one at a call site is exactly the kind of thing this file
    // exists to make testable.
    constexpr uint8 Index(uint8 rank)
    {
        return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
    }

    // ------------------------------------------------------------------
    // Rarity (docs/rarity-plan.md section 2) -- which rarity an offer slot
    // draws from, weighted by tier.
    //
    // This is where a run's escalation lives now rather than in the rank
    // ladder: early tiers are nearly all commons, legendaries appear only late.
    // The numbers are the plan's, band for band, and the plan says plainly
    // (section 7.6) that they are invented -- a starting shape for the sweep
    // tool to argue with (tests/tools/README-sweep.md, --rarity), not a result.
    // Change them here; RulesTest holds the shape and not the values.
    //
    // One column per rarity across the four bands, rather than one row per
    // band, because a column is a claim the ladder audit can check: the common
    // share falls with tier and every rarer share rises. The rows summing to
    // a hundred is RulesTest's to hold, since no audit reads across arrays.
    //
    // The generator rolls over the rarities that actually have a candidate,
    // with these as the weights, so an all-Rare table -- which is what the
    // registry is until the first common is written -- draws exactly as it
    // did before the roll existed. A weight of zero means "never at this
    // tier" only while something else is available; see RollRarity.
    // ------------------------------------------------------------------

    constexpr uint8  RARITY_BAND_TIERS = 20;   // a band is twenty tiers
    constexpr size_t RARITY_BANDS      = 4;    // 1-20, 21-40, 41-60, 61-80
    constexpr size_t RARITY_COUNT      = static_cast<size_t>(Rarity::MAX);

    constexpr uint32 COMMON_PCT[]    = { 70, 45, 25, 10 };
    static_assert(std::size(COMMON_PCT) == RARITY_BANDS, "COMMON_PCT is short a band");

    // LADDER-SENTINEL: the uncommon share is meant to hump. It is the bridge
    // between a common-heavy opening and a rare-heavy close, and a bridge is
    // highest in the middle -- it rises through the midgame and gives its
    // share back to epics and legendaries at the end. Not a transposed digit.
    constexpr uint32 UNCOMMON_PCT[]  = { 25, 35, 35, 25 };
    static_assert(std::size(UNCOMMON_PCT) == RARITY_BANDS, "UNCOMMON_PCT is short a band");

    constexpr uint32 RARE_PCT[]      = {  5, 18, 30, 40 };
    static_assert(std::size(RARE_PCT) == RARITY_BANDS, "RARE_PCT is short a band");

    constexpr uint32 EPIC_PCT[]      = {  0,  2,  9, 20 };
    static_assert(std::size(EPIC_PCT) == RARITY_BANDS, "EPIC_PCT is short a band");

    constexpr uint32 LEGENDARY_PCT[] = {  0,  0,  1,  5 };
    static_assert(std::size(LEGENDARY_PCT) == RARITY_BANDS, "LEGENDARY_PCT is short a band");

    // Which band a tier falls in. Tier 0 does not exist -- FIRST_TIER is 1 --
    // and anything past the last band is the last band, so a realm whose axis
    // runs longer than eighty gets the endgame mix rather than a read past the
    // end of the table.
    constexpr size_t RarityBand(uint8 tier)
    {
        size_t const band = tier < 1 ? 0 : static_cast<size_t>(tier - 1) / RARITY_BAND_TIERS;
        return band < RARITY_BANDS ? band : RARITY_BANDS - 1;
    }

    constexpr uint32 RarityWeight(uint8 tier, Rarity rarity)
    {
        size_t const band = RarityBand(tier);
        switch (rarity)
        {
            case Rarity::Common:    return COMMON_PCT[band];
            case Rarity::Uncommon:  return UNCOMMON_PCT[band];
            case Rarity::Rare:      return RARE_PCT[band];
            case Rarity::Epic:      return EPIC_PCT[band];
            case Rarity::Legendary: return LEGENDARY_PCT[band];
            default:                return 0;
        }
    }

    // ------------------------------------------------------------------
    // Hubris (18) -- the first enemy in a fight is your duel.
    // ------------------------------------------------------------------

    constexpr uint32 DUEL_TAKEN_PCT[]  = { 85, 75, 65, 50 };
    static_assert(std::size(DUEL_TAKEN_PCT) >= MAX_RANK, "DUEL_TAKEN_PCT is short a rank");

    constexpr uint32 OTHER_TAKEN_PCT[] = { 115, 130, 150, 175 };
    static_assert(std::size(OTHER_TAKEN_PCT) >= MAX_RANK, "OTHER_TAKEN_PCT is short a rank");

    // The shelter must always be a shelter and the exposure always an exposure,
    // at every rank. Getting this backwards at one rank would turn the card
    // inside out there and nowhere else, which is the hardest kind of tuning
    // bug to notice in play.
    constexpr float HubrisTakenMult(uint8 rank, bool isDuel)
    {
        return float(isDuel ? DUEL_TAKEN_PCT[Index(rank)] : OTHER_TAKEN_PCT[Index(rank)]) / 100.f;
    }

    // ------------------------------------------------------------------
    // Overextended (16) -- your back is what costs.
    // ------------------------------------------------------------------

    constexpr uint32 BEHIND_PCT[] = { 20, 30, 45, 60 };
    static_assert(std::size(BEHIND_PCT) >= MAX_RANK, "BEHIND_PCT is short a rank");

    constexpr float OverextendedTakenMult(uint8 rank, bool behind)
    {
        return behind ? 1.f + float(BEHIND_PCT[Index(rank)]) / 100.f : 1.f;
    }

    // ------------------------------------------------------------------
    // Falling Sky (14) -- it marks ground you refused to leave.
    // ------------------------------------------------------------------

    constexpr uint32 STILL_MS[]  = { 8000, 6000, 4500, 3000 };
    static_assert(std::size(STILL_MS) >= MAX_RANK, "STILL_MS is short a rank");

    constexpr float MOVED_YARDS = 5.0f;

    constexpr bool FallingSkyArms(uint8 rank, uint32 stillMs)
    {
        return stillMs >= STILL_MS[Index(rank)];
    }

    constexpr bool FallingSkyMoved(float travelled)
    {
        return travelled > MOVED_YARDS;
    }

    // ------------------------------------------------------------------
    // Frenzy (15) -- damage only, and the chain is fragile.
    // ------------------------------------------------------------------

    constexpr uint32 PCT_PER_STACK[] = { 4, 6, 8, 10 };
    static_assert(std::size(PCT_PER_STACK) >= MAX_RANK, "PCT_PER_STACK is short a rank");

    constexpr uint32 MAX_STACKS = 5;

    // `boonMag` is the generator's per-stack figure for this row when it has
    // one, so the offer card and the multiplier cannot disagree; zero falls
    // back to the table.
    constexpr float FrenzyDoneMult(uint8 rank, uint32 stacks, uint32 boonMag)
    {
        uint32 const pct = boonMag != 0 ? boonMag : PCT_PER_STACK[Index(rank)];
        uint32 const n   = stacks > MAX_STACKS ? MAX_STACKS : stacks;
        return 1.f + float(pct) / 100.f * float(n);
    }

    // ------------------------------------------------------------------
    // Deep Wounds (19) -- a kill closes what damage opened.
    // ------------------------------------------------------------------

    constexpr int32 WOUND_PCT[]      = { 30, 40, 50, 60 };
    static_assert(std::size(WOUND_PCT) >= MAX_RANK, "WOUND_PCT is short a rank");

    constexpr int32 KILL_CLOSE_PCT[] = { 12, 10, 8, 6 };
    static_assert(std::size(KILL_CLOSE_PCT) >= MAX_RANK, "KILL_CLOSE_PCT is short a rank");

    // At least one point, always: a kill that closes nothing would read to the
    // player as the card being broken, and at low levels the percentage rounds
    // to zero long before the wound does.
    constexpr int32 DeepWoundsClose(uint8 rank, uint32 basePool)
    {
        int32 const share = int32(int64(basePool) * KILL_CLOSE_PCT[Index(rank)] / 100);
        return share > 1 ? share : 1;
    }

    // ------------------------------------------------------------------
    // Killing Floor (74) -- healing is held, not refused.
    // ------------------------------------------------------------------

    // These two were first written as complements -- payout 100/85/70/50
    // against loss 0/15/30/50 -- which makes `bank * payout` and
    // `bank * (100 - loss)` the *same number at every rank*. Breaking off paid
    // exactly what winning paid, so the choice the card is built around did not
    // exist, and the card was back to being the tax it was redesigned out of.
    //
    // RulesTest.KillingFloorMakesWinningWorthMoreThanWalkingAway caught it on
    // the first run it was ever given. They are independent ladders now and the
    // test holds them apart.
    constexpr uint32 KILL_PAYOUT_PCT[] = { 100, 95, 85, 70 };
    static_assert(std::size(KILL_PAYOUT_PCT) >= MAX_RANK, "KILL_PAYOUT_PCT is short a rank");

    constexpr uint32 LEAVE_LOSS_PCT[]  = { 0, 25, 45, 65 };
    static_assert(std::size(LEAVE_LOSS_PCT) >= MAX_RANK, "LEAVE_LOSS_PCT is short a rank");

    // What a kill hands back out of the bank.
    constexpr uint64 KillingFloorPayout(uint8 rank, uint64 bank)
    {
        return bank * KILL_PAYOUT_PCT[Index(rank)] / 100u;
    }

    // What breaking off hands back instead.
    constexpr uint64 KillingFloorLeaveBack(uint8 rank, uint64 bank)
    {
        return bank * (100u - LEAVE_LOSS_PCT[Index(rank)]) / 100u;
    }
}
}

#endif // MOD_GAUNTLET_RULES_H
