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

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>
#include <string_view>

namespace
{
    using namespace Gauntlet;

    constexpr size_t TABLE_SIZE = 73;
    constexpr uint8  MAX_TIER   = 16;

    // The mechanics the generator may offer: Phase 0's two scalars, Exposed
    // (21) and Feeble (22), and Phase 1's vertical slice -- The Shade (1),
    // Champions (6), Falling Sky (14) and Deep Wounds (19). Withering (72) and
    // Forgetful (73) have working implementations but are MF_NotImplemented on
    // purpose -- IsImplemented() answers "may the generator offer this", not
    // "does code exist" -- so they are absent here. CONTRACT.md section 8.
    //
    // This list is the switch a phase is finished by, so it is asserted exactly
    // rather than as a count: a row that gains the flag by accident, or loses
    // it before its dispatch is wired, is an affix offered to a live hardcore
    // character that silently does nothing.
    constexpr std::array<uint16, 6> OFFERABLE = { 1, 6, 14, 19, 21, 22 };

    // CONTRACT.md section 8's id ranges, which are fixed forever: an id is
    // never reused, so a retired mechanic keeps its number and gains
    // MF_NotImplemented rather than leaving a hole. Withering and Forgetful
    // are attrition scalars, which is why that family has six entries and not
    // the four the A1-A4 range alone would give.
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
        { 19, 22, Family::Attrition,  6 },   // 19-22 plus Withering (72) and Forgetful (73)
        { 23, 25, Family::Rules,      3 },
        { 26, 27, Family::Bargain,    2 },
        { 28, 71, Family::Class,     44 }
    } };
}

TEST(Registry, HoldsSeventyThreeEntriesInIdOrder)
{
    auto const& all = AllMechanics();
    ASSERT_EQ(all.size(), TABLE_SIZE);

    for (size_t i = 0; i < all.size(); ++i)
        EXPECT_EQ(all[i].id, static_cast<uint16>(i + 1))
            << "entry " << i << " breaks the 1..73 id sequence; ids are contiguous and never reused";
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

    // Withering and Forgetful are legacy attrition scalars sitting above the
    // C-range, so they are checked by name rather than by a range row.
    for (Range const& range : RANGES)
        for (uint16 id = range.first; id <= range.last; ++id)
        {
            MechanicDef const* def = FindMechanic(id);
            ASSERT_NE(def, nullptr) << "id " << id << " is missing from the table";
            EXPECT_EQ(def->family, range.family)
                << "id " << id << " (" << def->key << ") is outside the family its id range fixes";
        }

    for (uint16 id : { MECHANIC_WITHERING, MECHANIC_FORGETFUL })
    {
        MechanicDef const* def = FindMechanic(id);
        ASSERT_NE(def, nullptr) << "id " << id;
        EXPECT_EQ(def->family, Family::Attrition) << "id " << id;
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
        << "CONTRACT.md section 9 sizes the whole offer-pool argument on this set, and "
           "OfferInvariants.LiveRegistryView measures against it; changing it changes what "
           "the invariant sweep can assert";

    for (MechanicDef const& def : AllMechanics())
        EXPECT_EQ(IsImplemented(def), (def.flags & MF_NotImplemented) == 0)
            << "id " << def.id << ": IsImplemented must be exactly !(flags & MF_NotImplemented)";
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
    EXPECT_EQ(FindMechanic(static_cast<uint16>(TABLE_SIZE + 1)), nullptr);
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
    EXPECT_EQ(FindMechanic(uint16(21)), FindMechanic(std::string_view("exposed")));
}
