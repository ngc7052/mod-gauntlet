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
    // One value per card, not a ladder. The rank system is gone (docs/
    // rarity-plan.md section 5b): every number below is what the card is worth
    // at its rarity, and it is the number the registry blurb states -- the
    // design's middle value, where the blurb states none. A card escalates a
    // run by being rarer, not by being the same card bigger.

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
    // The reroll purse (docs/rarity-plan.md section 4).
    //
    // A reroll rebuilds the pending tier's three offers; a skip declines the
    // tier outright and banks a charge, which is what makes skipping a real
    // choice rather than a trap. The plan's section 7.5 says plainly that
    // these numbers have no evidence behind them at all -- they are the shape
    // to start arguing from, and the sweep and play are the argument.
    // ------------------------------------------------------------------

    constexpr uint8 REROLL_STARTING_CHARGES = 2;    // TODO(design): "two or three", section 4
    constexpr uint8 SKIP_EARNS_CHARGES      = 1;    // TODO(design): section 7.5's open number

    // A bound, not a design: the purse travels as one byte on the wire and in
    // the state store's int32, and eighty tiers of skipping should not be able
    // to wrap either. Reaching it means a run declined most of its picks,
    // which is a stranger problem than a full purse.
    constexpr uint8 REROLL_MAX_CHARGES      = 250;

    constexpr uint8 ChargesAfterSkip(uint8 held)
    {
        uint32 const next = static_cast<uint32>(held) + SKIP_EARNS_CHARGES;
        return next > REROLL_MAX_CHARGES ? REROLL_MAX_CHARGES : static_cast<uint8>(next);
    }

    // ------------------------------------------------------------------
    // Hubris (18) -- the first enemy in a fight is your duel.
    // ------------------------------------------------------------------

    constexpr uint32 DUEL_TAKEN_PCT = 75;

    constexpr uint32 OTHER_TAKEN_PCT = 130;

    // The shelter must always be a shelter and the exposure always an exposure.
    // Getting this backwards would turn the card inside out, which is the
    // hardest kind of tuning bug to notice in play.
    constexpr float HubrisTakenMult(bool isDuel)
    {
        return float(isDuel ? DUEL_TAKEN_PCT : OTHER_TAKEN_PCT) / 100.f;
    }

    // ------------------------------------------------------------------
    // Overextended (16) -- your back is what costs.
    // ------------------------------------------------------------------

    constexpr uint32 BEHIND_PCT = 30;

    constexpr float OverextendedTakenMult(bool behind)
    {
        return behind ? 1.f + float(BEHIND_PCT) / 100.f : 1.f;
    }

    // ------------------------------------------------------------------
    // Falling Sky (14) -- it marks ground you refused to leave.
    // ------------------------------------------------------------------

    constexpr uint32 STILL_MS = 6000;

    constexpr float MOVED_YARDS = 5.0f;

    constexpr bool FallingSkyArms(uint32 stillMs)
    {
        return stillMs >= STILL_MS;
    }

    constexpr bool FallingSkyMoved(float travelled)
    {
        return travelled > MOVED_YARDS;
    }

    // ------------------------------------------------------------------
    // Frenzy (15) -- damage only, and the chain is fragile.
    // ------------------------------------------------------------------

    constexpr uint32 PCT_PER_STACK = 6;

    constexpr uint32 MAX_STACKS = 5;

    // `boonMag` is the generator's per-stack figure for this row when it has
    // one, so the offer card and the multiplier cannot disagree; zero falls
    // back to the table.
    constexpr float FrenzyDoneMult(uint32 stacks, uint32 boonMag)
    {
        uint32 const pct = boonMag != 0 ? boonMag : PCT_PER_STACK;
        uint32 const n   = stacks > MAX_STACKS ? MAX_STACKS : stacks;
        return 1.f + float(pct) / 100.f * float(n);
    }

    // ------------------------------------------------------------------
    // Deep Wounds (19) -- a kill closes what damage opened.
    // ------------------------------------------------------------------

    constexpr int32 WOUND_PCT = 30;

    constexpr int32 KILL_CLOSE_PCT = 12;

    // At least one point, always: a kill that closes nothing would read to the
    // player as the card being broken, and at low levels the percentage rounds
    // to zero long before the wound does.
    constexpr int32 DeepWoundsClose(uint32 basePool)
    {
        int32 const share = int32(int64(basePool) * KILL_CLOSE_PCT / 100);
        return share > 1 ? share : 1;
    }

    // ------------------------------------------------------------------
    // Killing Floor (74) -- healing is held, not refused.
    // ------------------------------------------------------------------

    // These two were first written as complements -- payout 100/85/70/50
    // against loss 0/15/30/50 -- which makes `bank * payout` and
    // `bank * (100 - loss)` the *same number*. Breaking off paid exactly what
    // winning paid, so the choice the card is built around did not exist, and
    // the card was back to being the tax it was redesigned out of.
    //
    // RulesTest.KillingFloorMakesWinningWorthMoreThanWalkingAway caught it on
    // the first run it was ever given. They are independent numbers now and
    // the test holds them apart.
    constexpr uint32 KILL_PAYOUT_PCT = 95;

    constexpr uint32 LEAVE_LOSS_PCT = 25;

    // What a kill hands back out of the bank.
    constexpr uint64 KillingFloorPayout(uint64 bank)
    {
        return bank * KILL_PAYOUT_PCT / 100u;
    }

    // What breaking off hands back instead.
    constexpr uint64 KillingFloorLeaveBack(uint64 bank)
    {
        return bank * (100u - LEAVE_LOSS_PCT) / 100u;
    }

    // ------------------------------------------------------------------
    // The three reward-shaped cards of docs/commons.md -- Scavenger's Eye
    // (88), Blood for Bread (89), Waste Not (90).
    //
    // They exist because of a measurement rather than a theme: every offer set
    // must contain one card flagged MF_RewardShaped, and every row that
    // carried the flag was Rare, so one slot in three was a rare before any
    // weight was consulted. These three are the first that are not. The flag
    // is not decoration -- it means the card's own mechanic hands the player
    // something when they engage with it, which is why each of them pays on a
    // kill or on a fight fought a particular way, and not through a boon.
    // ------------------------------------------------------------------

    // Keen-nosed's (13) bonus, moved here from the mechanic so the claim below
    // can be made at all. It is the rare version of the same curse.
    constexpr float KEEN_NOSED_YARDS = 8.0f;

    // Scavenger's Eye's. Five, against Keen-nosed's eight: the uncommon is
    // the smaller version of the rare's curse, and the test says so rather
    // than either file saying a number twice.
    constexpr float SCAVENGER_YARDS = 5.0f;

    // How many times a clean fight's corpse is rolled. Two is "twice", which
    // is what the card says; the point the test holds is that it is a whole
    // extra roll of the creature's own table and not a percentage on top of
    // one -- a fraction would be a boon, and this card's upside has to be
    // something the player earned by fighting a particular way.
    constexpr uint32 SCAVENGER_ROLLS = 2;

    constexpr uint32 ScavengerExtraRolls()
    {
        return SCAVENGER_ROLLS - 1u;
    }

    // Blood for Bread gives up eating and drinking -- both bars, all
    // downtime -- and pays on every kill. Waste Not gives up potions only and
    // pays less. The ordering is the decision: a card that takes more must
    // pay more, or the smaller card is strictly better and the bigger one is
    // never worth taking.
    constexpr uint32 BLOOD_FOR_BREAD_PCT = 8;
    constexpr uint32 WASTE_NOT_PCT       = 5;

    // At least one point, for Deep Wounds' reason: a kill that restores
    // nothing reads as the card being broken, and a percentage of a level-5
    // health pool rounds to zero long before the player stops noticing.
    constexpr uint32 KillRestore(uint32 pool, uint32 pct)
    {
        uint32 const share = uint32(uint64(pool) * pct / 100u);
        return share > 1u ? share : 1u;
    }

    constexpr uint32 BloodForBreadRestore(uint32 pool)
    {
        return KillRestore(pool, BLOOD_FOR_BREAD_PCT);
    }

    constexpr uint32 WasteNotRestore(uint32 pool)
    {
        return KillRestore(pool, WASTE_NOT_PCT);
    }

    // ------------------------------------------------------------------
    // The loot cards of docs/greed-redesign.md section 7.3: Fresh Kill (110),
    // Blood Price (111), Trophy Hunter (112).
    //
    // All three are reward-shaped, which is the point of building these three
    // first: section 7 of docs/handoff.md measures slot B's guarantee drawing
    // from a pool of a dozen cards thinned by the distinct-family rule, and
    // handing whatever survives the whole rarity share. More reward-shaped
    // cards, in more families, is the only lever that moves it -- and Blood
    // Price is the Attrition family's first that is not an epic.
    // ------------------------------------------------------------------

    // Fresh Kill: loot it now or it holds nothing but the quest item. Eight
    // seconds is long enough to open a corpse mid-pull and short enough that
    // doing so is a decision -- which is the whole card, since the pull is
    // still going on.
    constexpr uint32 FRESH_KILL_WINDOW_MS = 8000;
    constexpr uint32 FRESH_KILL_ROLLS     = 2;
    constexpr uint32 FRESH_KILL_LATE_ROLLS = 0;

    // Blood Price: opening a corpse costs blood, and a corpse opened while
    // hurt pays double. The two halves point the same way on purpose -- the
    // card wants you to loot at the worst moment.
    constexpr uint32 BLOOD_PRICE_PCT         = 3;
    constexpr uint32 BLOOD_PRICE_ROLLS_HURT  = 2;
    constexpr uint32 BLOOD_PRICE_ROLLS_WHOLE = 1;

    // What opening a corpse costs, in health, already floored so it can never
    // be the thing that kills the run: Blood Magic's cost is written the same
    // way. The mechanic applies exactly this, so "never lethal" is a property
    // of the arithmetic rather than of remembering to clamp at the call site.
    constexpr uint32 BloodPriceCost(uint32 maxHealth, uint32 currentHealth)
    {
        if (currentHealth <= 1)
            return 0;

        uint32 const share = maxHealth * BLOOD_PRICE_PCT / 100u;
        uint32 const most  = currentHealth - 1u;
        return share < most ? share : most;
    }

    // Trophy Hunter: a silver dragon within a hundred yards is a payday and a
    // threat at once. The range is what the tick scans, so it is also the
    // widest net Nearby::CreaturesNear is asked to cast for this card.
    constexpr float  TROPHY_YARDS     = 100.0f;
    constexpr uint32 TROPHY_TAKEN_PCT = 15;

    constexpr float TrophyTakenMult()
    {
        return 1.0f + float(TROPHY_TAKEN_PCT) / 100.0f;
    }

    // A chest of the player's own level band. The world database's classic
    // chest ladder, verified present with its loot tables: Battered (2849,
    // loot 2280, 712 rows), Solid (2850, loot 2281), Solid (2855, loot 2283,
    // 1093 rows) and Northrend's Large Solid (153462, loot 9934, 1411 rows).
    //
    // Section 7.1's honest limit applies and is worth repeating where the
    // number is: a chest's contents are the zone's, so this is greens and
    // greys at level with the occasional blue. The card promises a chest, not
    // a jackpot.
    //
    // LADDER-SENTINEL: entry ids, not magnitudes -- they ascend by accident of
    // when Blizzard added them, and 153462 is only larger because Northrend
    // came last.
    constexpr uint32 TROPHY_CHEST[] = { 2849, 2850, 2855, 153462 };
    constexpr uint8  TROPHY_CHEST_LEVEL[] = { 25, 45, 65, 255 };

    constexpr uint32 TrophyChestFor(uint8 level)
    {
        for (size_t i = 0; i < std::size(TROPHY_CHEST_LEVEL); ++i)
            if (level <= TROPHY_CHEST_LEVEL[i])
                return TROPHY_CHEST[i];
        return TROPHY_CHEST[std::size(TROPHY_CHEST) - 1];
    }

    // How long a summoned chest stands before it goes. Long enough to walk
    // back for after the fight it came out of, short enough not to litter.
    constexpr uint32 TROPHY_CHEST_SECONDS = 300;

    // ------------------------------------------------------------------
    // The reward-shaped low end -- Scavenge (113) and Gravedigger (114),
    // docs/commons.md section 4b.
    //
    // Both are commons, both classless, both open at tier 1, and both are in a
    // family whose only reward-shaped card was a rare. That is the whole
    // reason they exist: slot B's guarantee draws from the reward-shaped cards
    // of a family the other two slots did not use, and at tier 1 only Rules
    // and Enemy could answer with anything but a rare.
    // ------------------------------------------------------------------

    // Scavenge: looting is how you recover, and the price is paid all the time.
    // Deliberately the opposite of Blood Price (111), which makes the same act
    // cost health -- a run offered both is being asked what kind of looter it
    // is.
    constexpr int32  SCAVENGE_TAKEN_PCT = 10;
    constexpr uint32 SCAVENGE_HEAL_PCT  = 4;

    constexpr uint32 ScavengeHeal(uint32 maxHealth)
    {
        return KillRestore(maxHealth, SCAVENGE_HEAL_PCT);
    }

    // Gravedigger: every eighth corpse gets up. Eight is often enough to be a
    // rhythm the player plans around and rare enough that looting stays worth
    // doing; Carrion's every-fourth is the neighbouring card and this is
    // deliberately slower, because Carrion's scavengers arrive *beside* you
    // and this one is the corpse you are already standing on.
    constexpr int32  GRAVEDIGGER_EVERY   = 8;
    constexpr uint32 GRAVEDIGGER_ROLLS   = 2;
    constexpr uint32 GRAVEDIGGER_LIFE_MS = 120000;

    // ------------------------------------------------------------------
    // The greed redesign's brakes (docs/greed-redesign.md section 3). Each of
    // these cards already made the run slower and then charged for the
    // slowness; the redesign leaves the curse alone and gives the player
    // something to win by beating it.
    // ------------------------------------------------------------------

    // Craven (7): a runner cut down before it reaches its camp is a bounty.
    // The chase was always the brake; now it is a race worth running.
    constexpr uint32 CRAVEN_BOUNTY_XP_MULT = 2;
    constexpr uint32 CRAVEN_BOUNTY_ROLLS   = 2;

    constexpr uint32 CravenBountyXP(uint32 amount)
    {
        uint64 const paid = uint64(amount) * CRAVEN_BOUNTY_XP_MULT;
        return paid > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32(paid);
    }

    // Grudge (10): the spirit rises four seconds after the kill instead of at
    // once, and looting the corpse first stops it forming and pays a roll.
    // The drain rises with the window -- the card is no longer punishing a
    // player for playing quickly, so it can afford to hurt the one who stands
    // still.
    constexpr uint32 GRUDGE_RISE_MS   = 4000;
    constexpr uint32 GRUDGE_DRAIN_PCT = 5;
    constexpr uint32 GRUDGE_LOOT_ROLLS = 2;

    // Falter (17): the stumble ends and the next thing you do is a Reprisal.
    constexpr uint32 FALTER_REPRISAL_PCT   = 50;
    constexpr uint32 FALTER_REPRISAL_MS    = 5000;

    constexpr float FalterReprisalMult()
    {
        return 1.0f + float(FALTER_REPRISAL_PCT) / 100.0f;
    }

    // Cunning (12): a cast that completes with a kicker in melee range is the
    // high roll. The puzzle is unchanged; this is the reason to stop solving
    // it and commit.
    constexpr uint32 CUNNING_PAYOFF_PCT = 40;

    constexpr float CunningPayoffMult()
    {
        return 1.0f + float(CUNNING_PAYOFF_PCT) / 100.0f;
    }

    // Ambush (5): killing the Ambusher finishes the rest it interrupted.
    constexpr uint32 AMBUSH_RESTORE_PCT = 100;

    // Call to Arms (8): the kin that answer are worth more than the kill that
    // called them.
    constexpr uint32 CALL_TO_ARMS_XP_PCT = 25;

    constexpr uint32 CallToArmsXP(uint32 amount)
    {
        return amount + uint32(uint64(amount) * CALL_TO_ARMS_XP_PCT / 100u);
    }

    // Tribute (115): every 25th kill leaves a chest, and opening it draws the
    // scavengers that make it a decision rather than a gift. Twenty-five is
    // Echo's cadence deliberately -- a number the player can feel coming
    // without counting.
    constexpr int32  TRIBUTE_EVERY      = 25;
    constexpr uint32 TRIBUTE_SCAVENGERS = 2;

    // The Tenth Corpse (116): nine corpses hold nothing and the tenth holds
    // all of it. The run is faster by nine loot windows never opened, and the
    // risk is dying on the ninth.
    constexpr int32  TENTH_EVERY = 10;

    constexpr int32 TenthCorpseLeft(int32 opened)
    {
        int32 const into = opened % TENTH_EVERY;
        return into == 0 ? 0 : TENTH_EVERY - into;
    }

    // Blood Magic (20): below the line the cost stops and the payoff starts.
    constexpr uint32 BLOOD_MAGIC_LINE_PCT   = 35;
    constexpr uint32 BLOOD_MAGIC_PAYOFF_PCT = 25;

    constexpr float BloodMagicPayoffMult()
    {
        return 1.0f + float(BLOOD_MAGIC_PAYOFF_PCT) / 100.0f;
    }
}
}

#endif // MOD_GAUNTLET_RULES_H
