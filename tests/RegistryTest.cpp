/*
 * mod-gauntlet - the mechanic table's own invariants
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// The registry is the single description of an affix that the offer builder,
// the addon exporter, the debug commands and the storage all read, so a bad
// row is not a local mistake: it is an affix the addon cannot name, an id a
// migrated run cannot resolve, or a tier window the generator will honour
// forever. Everything below is a property the rest of the module assumes
// without checking.

#include "GauntletRegistry.h"
#include "GauntletGenerator.h"
#include "GauntletTrades.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>

namespace
{
    using namespace Gauntlet;

    // Seventy-nine rows carrying ids 1..84 with five holes in them. Phase 2
    // deleted Exposed (21), Feeble (22), Withering (72) and Forgetful (73) --
    // the four flat scalars -- and Phase 6 deleted Unspent (69). An id is never
    // reused, so the holes stay. Killing Floor (74) took Unspent's place in the
    // table and not its number, and 75-84 are the first ten commons of
    // docs/rarity-plan.md step 2.
    constexpr size_t TABLE_SIZE = 104;
    // A tier is a level now, not five of them. Every registry window was
    // multiplied by five with the axis, so the *level* each affix unlocks at
    // is exactly what it was; only the number naming it changed.
    constexpr uint8  MAX_TIER   = 80;

    // The ids that were removed and may never come back. Asserting their
    // absence is what stops a later phase quietly filling a hole: a stored
    // gauntlet_affix row from any past run must never resolve to a different
    // mechanic than the one it was written for.
    //
    // 69 is Unspent, and it is the first retirement that was not a scalar: the
    // affix worked exactly as written and the design was wrong. See
    // docs/unspent-replacement-plan.md.
    constexpr std::array<uint16, 5> DELETED = { 21, 22, 69, 72, 73 };

    // The mechanics the generator may offer: Phase 1's vertical slice -- The
    // Shade (1), Champions (6), Falling Sky (14) and Deep Wounds (19) --
    // Phase 2's fifteen, which is everything else in families S, E and T, and
    // Phase 3's six: Blood Magic, three Rules rows and both Bargains. Family C is Phase 4 and stays
    // MF_NotImplemented; so do the two Bargains until step 4 of this phase.
    //
    // This list is the switch a phase is finished by, so it is asserted exactly
    // rather than as a count: a row that gains the flag by accident, or loses
    // it before its dispatch is wired, is an affix offered to a live hardcore
    // character that silently does nothing.
    constexpr std::array<uint16, 104> OFFERABLE = {
        1, 2, 3, 4, 5,           // S1 Shade, S2 Echo, S3 Carrion, S4 Reinforcements, S5 Ambush
        6, 7, 8, 9, 10, 11, 12, 13,  // E1 Champions .. E8 Keen-nosed
        14, 15, 16, 17, 18,      // T1 Falling Sky .. T5 Hubris
        19, 20, 74,              // A1 Deep Wounds, A2 Blood Magic, A5 Killing Floor
        23, 24, 25,              // R1 Self-found, R2 Lone Wolf, R3 Iron Purse
        26, 27,                  // B1 Last Rites, B2 Cursed Hoard
        28, 29, 31,              // C1 Red Mist, C2 Berserker's Bargain, C4 Deafening Roar
        32, 33,                  // C5 Long Forbearance, C6 Consecrated Ground
        36, 37, 38,              // C9 Half-Tamed, C10 Dead Weight, C11 Wide Dead Zone
        40, 42,                  // C13 Cold Trail, C15 Exposed Back
        44, 47,                  // C17 Frail Soul, C20 Whispers of the Deep
        48, 49,                  // C21 Rune-starved, C22 Grave Call
        52, 53,                  // C25 One Totem, C26 Totemic Anchor
        56, 58,                  // C29 Cold Feet, C31 Mana Burn
        60,                      // C33 Fel Pact
        64,                      // C37 Bound Skin
        68,                      // C41 Faint (all mana users)
        70, 71,                  // C43 Ankh Pact, C44 Stone of the Damned

        // Wave B.
        30, 34, 35,              // C3 Iron Discipline, C7 No Sanctuary, C8 Commitment
        39, 41, 43,              // C12 Blood Bond, C14 Poisoned Blades, C16 Slow Hands
        45, 46,                  // C18 Faithless Form, C19 Penance of Silence
        50, 51,                  // C23 Cold Presence, C24 One Ward
        54, 55,                  // C27 Elemental Overload, C28 Spirit Debt
        57, 59,                  // C30 Fickle Sheep, C32 Arcane Frailty
        61, 62, 63,              // C34 Affliction, C35 Shard Economy, C36 Shared Blood
        65, 66, 67,              // C38 Nature's Toll, C39 Commitment of Roots, C40 Two Faces

        // The trades: seven denials filed as Rules, three coefficient trades
        // filed as Attrition, then the three paid in loot -- Magpie (Rules),
        // Butterfingers and Night Owl (Attrition), the last the first uncommon.
        // Every one is a line in src/GauntletTrades.h.
        75, 76, 77, 78, 79, 80, 81,
        82, 83, 84,
        85, 86, 87,

        // docs/commons.md's reward-shaped three. Not trades: each is a
        // mechanic, and each is classless and open at tier 1 because that
        // availability is the entire reason they were written.
        88, 89, 90,

        // And its nine uncommons, which are trades with a condition.
        91, 92, 93, 94, 95, 96, 97, 98, 99,

        // And its ten commons, which settle the mix those nine tipped.
        100, 101, 102, 103, 104, 105, 106, 107, 108, 109
    };

    // CONTRACT.md section 8's id ranges, which are fixed forever. The Attrition
    // range is 19-22 and holds two rows, not four, because 21 and 22 are gone.
    struct Range
    {
        uint16 first;
        uint16 last;
        Family family;
        size_t count;
    };

    constexpr std::array<Range, 22> RANGES = { {
        {  1,  5, Family::Spawn,      5 },
        {  6, 13, Family::Enemy,     11 },
        { 14, 18, Family::Tempo,      8 },
        { 19, 22, Family::Attrition, 12 },   // 21 and 22 deleted; 19, 20, 74 and five trades remain
        { 23, 25, Family::Rules,     23 },   // three rules, sixteen denials, two reward-shaped, two uncommons
        { 26, 27, Family::Bargain,    2 },
        { 28, 71, Family::Class,     43 },   // 69 deleted with Unspent
        // A5 Killing Floor, outside its family's original band because 21 and
        // 22 are spent and the next free id was 74. The band describes how the
        // table was first laid out; the no-reuse rule outranks it.
        { 74, 74, Family::Attrition, 12 },
        // The trades, appended in id order as every row after 74 is: the
        // denials are Rules, the coefficient trades Attrition. The loot trades
        // (85-87) interleave the two, which is why the bands split again.
        { 75, 81, Family::Rules,     23 },
        { 82, 84, Family::Attrition, 12 },
        { 85, 85, Family::Rules,     23 },
        { 86, 87, Family::Attrition, 12 },
        // docs/commons.md's three reward-shaped rows: the first two Rules
        // cards since the denials, and the Enemy family's first non-rare.
        { 88, 88, Family::Enemy,     11 },
        { 89, 90, Family::Rules,     23 },
        // The nine uncommons, interleaving four families in id order.
        { 91, 91, Family::Attrition, 12 },
        { 92, 94, Family::Tempo,      8 },
        { 95, 96, Family::Rules,     23 },
        { 97, 98, Family::Attrition, 12 },
        { 99, 99, Family::Enemy,     11 },
        // The ten commons: eight denials filed Rules (four of them
        // class-masked, as Axeless is), and the last two coefficient kinds.
        { 100, 107, Family::Rules,     23 },
        { 108, 108, Family::Enemy,     11 },
        { 109, 109, Family::Attrition, 12 }
    } };
}

TEST(Registry, HoldsEveryEntryInAscendingIdOrder)
{
    auto const& all = AllMechanics();
    ASSERT_EQ(all.size(), TABLE_SIZE);

    // Ascending and unique, but no longer contiguous: the table has four holes
    // where the deleted scalars were, and every lookup, the addon exporter and
    // the debug commands all walk it in this order.
    for (size_t i = 1; i < all.size(); ++i)
        EXPECT_LT(all[i - 1].id, all[i].id)
            << "entry " << i << " (id " << all[i].id << ") does not follow entry " << (i - 1)
            << " (id " << all[i - 1].id << ") in ascending order";

    EXPECT_EQ(all.front().id, 1u);
    EXPECT_EQ(all.back().id, 109u) << "the table must end at the last card, Slow Learner, id 109";
}

TEST(Registry, TheDeletedScalarIdsAreGoneAndStayGone)
{
    for (uint16 id : DELETED)
        EXPECT_EQ(FindMechanic(id), nullptr)
            << "id " << id << " was retired -- one of Phase 2's four flat scalars, or Unspent. An "
               "id is never reused: a stored gauntlet_affix row written for it must never resolve "
               "to something else. If a mechanic needs this number, it needs a different number.";

    for (std::string_view key : { "exposed", "feeble", "withering", "forgetful", "c42_unspent" })
        EXPECT_EQ(FindMechanic(key), nullptr) << "key '" << key << "' is still in the table";
}

TEST(Registry, KeysAreUniqueAndPrintable)
{
    std::set<std::string_view> keys;
    for (MechanicDef const& def : AllMechanics())
    {
        ASSERT_NE(def.key, nullptr) << "id " << def.id << " has a null key";
        EXPECT_FALSE(std::string_view(def.key).empty()) << "id " << def.id << " has an empty key";
        EXPECT_TRUE(keys.insert(def.key).second)
            << "id " << def.id << " repeats the key \"" << def.key << "\"";
    }
    EXPECT_EQ(keys.size(), TABLE_SIZE);
}

TEST(Registry, EveryEntryCanBeShownToAPlayer)
{
    for (MechanicDef const& def : AllMechanics())
    {
        ASSERT_NE(def.name, nullptr) << "id " << def.id << " has a null name";
        ASSERT_NE(def.blurb, nullptr) << "id " << def.id << " has a null blurb";
        EXPECT_FALSE(std::string_view(def.name).empty()) << "id " << def.id << " has an empty name";
        EXPECT_FALSE(std::string_view(def.blurb).empty()) << "id " << def.id << " has an empty blurb";

        // The offer builder walks these with std::string_view and would read
        // past the end of a null pointer rather than treating it as "no keys".
        EXPECT_NE(def.exclusiveKeys, nullptr) << "id " << def.id << " has a null exclusiveKeys";
    }
}

TEST(Registry, TierWindowsAreInRange)
{
    for (MechanicDef const& def : AllMechanics())
    {
        EXPECT_GE(def.minTier, 1) << "id " << def.id;
        EXPECT_LE(def.maxTier, MAX_TIER) << "id " << def.id;
        EXPECT_LE(def.minTier, def.maxTier)
            << "id " << def.id << " has an empty tier window [" << unsigned(def.minTier)
            << ", " << unsigned(def.maxTier) << "]";
    }
}

TEST(Registry, RarityIsInsideTheEnum)
{
    for (MechanicDef const& def : AllMechanics())
        EXPECT_LT(static_cast<uint8>(def.rarity), static_cast<uint8>(Rarity::MAX))
            << "id " << def.id << " carries a rarity Data.lua cannot name";
}

// docs/rarity-plan.md, steps 1, 2 and 5. The sixty-nine cards that existed
// when rarity landed are all Rare, and stay Rare until the plan's section 7.4
// -- one pass over the whole list deciding which are epics -- rewrites this
// into a list; a row promoted on its own before then is exactly what the plan
// says not to do. Everything after them is a line in the trade table, which is
// what makes it a table row rather than a file -- and which of the two trade
// tiers it is, the line says: an uncommon is "a trade with a condition"
// (section 2), so a line's condition and its row's rarity have to agree. The
// first real-mechanic uncommon (Scavenger's Eye) would turn the trade-line
// half of this into a list too; that day came, and MECHANIC_ROWS is the list.
TEST(Registry, TheOriginalCardsAreRareAndEverythingAfterIsATradeLineOrANamedMechanic)
{
    constexpr uint16 LAST_ORIGINAL = 74;   // A5 Killing Floor

    // docs/commons.md's three. A row lands here rather than in the trade table
    // only by being something a single line cannot say -- these three pay out
    // on a kill or on a fight fought a particular way, which is what earns
    // them MF_RewardShaped. Adding a fourth is a decision, and this list is
    // where it gets made.
    constexpr std::array<uint16, 3> MECHANIC_ROWS = { 88, 89, 90 };

    for (MechanicDef const& def : AllMechanics())
    {
        if (def.id <= LAST_ORIGINAL)
        {
            EXPECT_EQ(def.rarity, Rarity::Rare)
                << "id " << def.id << " (" << def.key << ") is " << RarityName(def.rarity)
                << "; the epic pass is one decision over the whole table, not per row";
            continue;
        }

        if (std::find(MECHANIC_ROWS.begin(), MECHANIC_ROWS.end(), def.id) != MECHANIC_ROWS.end())
        {
            EXPECT_EQ(FindTrade(def.id), nullptr)
                << def.key << " is listed as a mechanic and also has a trade line";
            EXPECT_TRUE(def.flags & MF_RewardShaped)
                << def.key << " is one of the three written to carry MF_RewardShaped and does not";
            EXPECT_EQ(def.classMask, 0u)
                << def.key << " is class-masked; the whole point of these three is that any "
                   "character can be offered one at tier 1";
            EXPECT_EQ(def.minTier, 1u) << def.key << " does not open at tier 1";
            EXPECT_TRUE(def.rarity == Rarity::Common || def.rarity == Rarity::Uncommon)
                << def.key << " is " << RarityName(def.rarity)
                << "; a rare carrying the flag is what these were written to stop being the only kind";
            continue;
        }

        TradeDef const* line = FindTrade(def.id);
        ASSERT_NE(line, nullptr)
            << "id " << def.id << " (" << def.key << ") is past the originals, is not in "
            << "MECHANIC_ROWS, and has no trade line";
        if (line->condition == Condition::Always)
            EXPECT_EQ(def.rarity, Rarity::Common)
                << def.key << ": a trade with no condition is a common, not " << RarityName(def.rarity);
        else
            EXPECT_EQ(def.rarity, Rarity::Uncommon)
                << def.key << ": a trade with a condition is an uncommon, not " << RarityName(def.rarity);
    }
}

TEST(Registry, TheRewardShapedGuaranteeCanBePaidWithoutARare)
{
    // The measurement docs/commons.md was written around, kept as an
    // assertion. Every offer set must contain a row flagged MF_RewardShaped
    // (GauntletGenerator.cpp:810). While every such row was Rare, one slot in
    // three was a rare before any rarity weight was read, and the early mix
    // could not reach the plan's targets however the weights were cut.
    //
    // What has to stay true is not a count but the existence of a way to pay
    // the guarantee that is not a rare, at the tier where it bites hardest.
    uint32 nonRareAtTierOne = 0;
    for (MechanicDef const& def : AllMechanics())
        if ((def.flags & MF_RewardShaped) && def.rarity != Rarity::Rare
            && def.classMask == 0 && def.minTier <= 1)
            ++nonRareAtTierOne;

    EXPECT_GT(nonRareAtTierOne, 0u)
        << "every reward-shaped row is rare, class-masked or opens late again, so slot B at "
           "tier 1 is a rare in every set the generator can build";
}

TEST(Registry, FamiliesAreInRangeAndMatchTheIdRanges)
{
    for (MechanicDef const& def : AllMechanics())
        EXPECT_LT(static_cast<uint8>(def.family), static_cast<uint8>(Family::MAX)) << "id " << def.id;

    // An id inside a range is either absent -- one of the four deleted scalars
    // -- or in the family its range fixes. Nothing else is allowed.
    for (Range const& range : RANGES)
        for (uint16 id = range.first; id <= range.last; ++id)
        {
            MechanicDef const* def = FindMechanic(id);
            bool const deleted = std::find(DELETED.begin(), DELETED.end(), id) != DELETED.end();

            if (deleted)
            {
                EXPECT_EQ(def, nullptr) << "id " << id << " was deleted and must stay deleted";
                continue;
            }

            ASSERT_NE(def, nullptr) << "id " << id << " is missing from the table";
            EXPECT_EQ(def->family, range.family)
                << "id " << id << " (" << def->key << ") is outside the family its id range fixes";
        }

    std::array<size_t, static_cast<size_t>(Family::MAX)> counted = {};
    for (MechanicDef const& def : AllMechanics())
        counted[static_cast<size_t>(def.family)]++;

    for (Range const& range : RANGES)
        EXPECT_EQ(counted[static_cast<size_t>(range.family)], range.count)
            << "family " << unsigned(static_cast<uint8>(range.family)) << " has the wrong entry count";

    // Design section 4.7's forty-four class curses less Unspent, which was
    // never a class curse in anything but its filing -- it had no class mask.
    // Spelled out because the addon's family filter and the offer builder's
    // family weights both assume the class family is the large one.
    EXPECT_EQ(counted[static_cast<size_t>(Family::Class)], 43u);
}

TEST(Registry, OnlyTheImplementedMechanicsMayBeOffered)
{
    std::set<uint16> implemented;
    for (MechanicDef const& def : AllMechanics())
        if (IsImplemented(def))
            implemented.insert(def.id);

    std::set<uint16> const expected(OFFERABLE.begin(), OFFERABLE.end());
    EXPECT_EQ(implemented, expected)
        << "this set is what the offer builder may draw from, and "
           "OfferInvariants.LiveRegistryView measures against it; changing it changes what "
           "a live player is offered";

    for (MechanicDef const& def : AllMechanics())
        EXPECT_EQ(IsImplemented(def), (def.flags & MF_NotImplemented) == 0)
            << "id " << def.id << ": IsImplemented must be exactly !(flags & MF_NotImplemented)";
}

TEST(Registry, BargainsOpenWhereTheGeneratorSaysTheyDo)
{
    // Two places named a bargain's earliest tier and they disagreed from Phase
    // 0 to Phase 3: Cursed Hoard's row said 4, GauntletGenerator's
    // BARGAIN_MIN_TIER said 6, and because the generator checks both, the
    // constant won and two tiers of the card's window were dead letter. That
    // is the quietest possible kind of wrong -- nothing fails, the affix is
    // simply never offered where its own row says it should be.
    for (MechanicDef const& def : AllMechanics())
    {
        if (def.family != Family::Bargain)
            continue;

        EXPECT_GE(def.minTier, BARGAIN_MIN_TIER)
            << "id " << def.id << " (" << def.key << ") opens at tier " << unsigned(def.minTier)
            << ", but the offer builder refuses every bargain below tier "
            << unsigned(BARGAIN_MIN_TIER) << ", so those tiers of its window can never be reached";
    }
}

TEST(Registry, LookupsAgreeWithTheTable)
{
    for (MechanicDef const& def : AllMechanics())
    {
        EXPECT_EQ(FindMechanic(def.id), &def)
            << "id " << def.id << " does not resolve to its own row";
        EXPECT_EQ(FindMechanic(std::string_view(def.key)), &def)
            << "key \"" << def.key << "\" does not resolve to its own row";
    }

    // Out of range on both sides, and a key the table does not carry. Every
    // one of these is the normal answer for a run migrated from a registry
    // this build has never seen, so nullptr is the contract, not a crash.
    EXPECT_EQ(FindMechanic(static_cast<uint16>(MECHANIC_NONE)), nullptr);
    // 110, one past the highest id the table carries. Not TABLE_SIZE + 1: the
    // ids are no longer contiguous, so the count and the highest id are
    // different numbers and only the second one bounds a lookup.
    EXPECT_EQ(FindMechanic(static_cast<uint16>(110)), nullptr);
    EXPECT_EQ(FindMechanic(static_cast<uint16>(72)), nullptr);
    EXPECT_EQ(FindMechanic(static_cast<uint16>(0xFFFF)), nullptr);
    EXPECT_EQ(FindMechanic(std::string_view("")), nullptr);
    EXPECT_EQ(FindMechanic(std::string_view("no_such_mechanic")), nullptr);
}

TEST(Registry, TableIsStableAcrossCalls)
{
    // Both lookups hand out MechanicDef pointers that callers hold across
    // frames, and AffixInstance::impl is created from one. A table rebuilt per
    // call would dangle every one of them.
    EXPECT_EQ(&AllMechanics(), &AllMechanics());
    EXPECT_EQ(FindMechanic(uint16(6)), FindMechanic(std::string_view("champions")));
}
