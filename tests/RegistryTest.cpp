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

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>

namespace
{
    using namespace Gauntlet;

    // Sixty-nine rows carrying ids 1..71 with four holes in them. Phase 2
    // deleted Exposed (21), Feeble (22), Withering (72) and Forgetful (73) --
    // the four flat scalars -- and an id is never reused, so the holes stay.
    constexpr size_t TABLE_SIZE = 69;
    // A tier is a level now, not five of them. Every registry window was
    // multiplied by five with the axis, so the *level* each affix unlocks at
    // is exactly what it was; only the number naming it changed.
    constexpr uint8  MAX_TIER   = 80;

    // The four ids that were removed and may never come back. Asserting their
    // absence is what stops a later phase quietly filling a hole: a stored
    // gauntlet_affix row from any past run must never resolve to a different
    // mechanic than the one it was written for.
    constexpr std::array<uint16, 4> DELETED = { 21, 22, 72, 73 };

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
    constexpr std::array<uint16, 33> OFFERABLE = {
        1, 2, 3, 4, 5,           // S1 Shade, S2 Echo, S3 Carrion, S4 Reinforcements, S5 Ambush
        6, 7, 8, 9, 10, 11, 12, 13,  // E1 Champions .. E8 Keen-nosed
        14, 15, 16, 17, 18,      // T1 Falling Sky .. T5 Hubris
        19, 20,                  // A1 Deep Wounds, A2 Blood Magic
        23, 24, 25,              // R1 Self-found, R2 Lone Wolf, R3 Iron Purse
        26, 27,                  // B1 Last Rites, B2 Cursed Hoard
        28, 29, 31,              // C1 Red Mist, C2 Berserker's Bargain, C4 Deafening Roar
        32, 33,                  // C5 Long Forbearance, C6 Consecrated Ground
        36, 37, 38               // C9 Half-Tamed, C10 Dead Weight, C11 Wide Dead Zone
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

    constexpr std::array<Range, 7> RANGES = { {
        {  1,  5, Family::Spawn,      5 },
        {  6, 13, Family::Enemy,      8 },
        { 14, 18, Family::Tempo,      5 },
        { 19, 22, Family::Attrition,  2 },   // 21 and 22 deleted; 19 and 20 remain
        { 23, 25, Family::Rules,      3 },
        { 26, 27, Family::Bargain,    2 },
        { 28, 71, Family::Class,     44 }
    } };
}

TEST(Registry, HoldsSixtyNineEntriesInAscendingIdOrder)
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
    EXPECT_EQ(all.back().id, 71u) << "the table must still end at C44, id 71";
}

TEST(Registry, TheDeletedScalarIdsAreGoneAndStayGone)
{
    for (uint16 id : DELETED)
        EXPECT_EQ(FindMechanic(id), nullptr)
            << "id " << id << " was one of the four flat scalars Phase 2 deleted. An id is never "
               "reused: a stored gauntlet_affix row written for it must never resolve to something "
               "else. If a mechanic needs this number, it needs a different number.";

    for (std::string_view key : { "exposed", "feeble", "withering", "forgetful" })
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

TEST(Registry, TierWindowsAndRanksAreInRange)
{
    for (MechanicDef const& def : AllMechanics())
    {
        EXPECT_GE(def.minTier, 1) << "id " << def.id;
        EXPECT_LE(def.maxTier, MAX_TIER) << "id " << def.id;
        EXPECT_LE(def.minTier, def.maxTier)
            << "id " << def.id << " has an empty tier window [" << unsigned(def.minTier)
            << ", " << unsigned(def.maxTier) << "]";

        EXPECT_GE(def.maxRank, 1) << "id " << def.id << " can never be offered at any rank";
        EXPECT_LE(def.maxRank, MAX_RANK) << "id " << def.id << " exceeds MAX_RANK";
    }
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

    // Design section 4.7's forty-four class curses, spelled out because the
    // addon's family filter and the offer builder's family weights both
    // assume the class family is the large one.
    EXPECT_EQ(counted[static_cast<size_t>(Family::Class)], 44u);
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
    // 72, one past the highest id the table carries. Not TABLE_SIZE + 1: the
    // ids are no longer contiguous, so the count and the highest id are
    // different numbers and only the second one bounds a lookup.
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
