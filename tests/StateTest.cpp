/*
 * mod-gauntlet - GauntletState's in-memory map, without a database
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// LoadFrom and SaveTo are not exercised here: both need CharacterDatabase,
// which needs the core's full include set and is unavailable to this local
// build (see GauntletState.cpp). What is tested is everything the plan's
// dirtiness contract actually depends on: the map that Get/Set/Add read and
// write, and Dirty()/Clear() over it.

#include "GauntletState.h"
#include <gtest/gtest.h>

#include <string>

namespace
{
    using namespace Gauntlet;

    std::string KeyOfLength(std::size_t n)
    {
        return std::string(n, 'k');
    }
}

TEST(State, GetReturnsZeroFallbackByDefaultWhenAbsent)
{
    State s;
    EXPECT_EQ(s.Get("champions.count"), 0);
}

TEST(State, GetReturnsExplicitFallbackWhenAbsent)
{
    State s;
    EXPECT_EQ(s.Get("shade.deadUntilTier", -1), -1);
}

TEST(State, SetThenGetRoundTrips)
{
    State s;
    s.Set("champions.count", 5);
    EXPECT_EQ(s.Get("champions.count"), 5);
}

TEST(State, SetOverwritesAPreviousValue)
{
    State s;
    s.Set("shade.rank", 1);
    s.Set("shade.rank", 2);
    EXPECT_EQ(s.Get("shade.rank"), 2);
}

TEST(State, DistinctKeysDoNotCollide)
{
    State s;
    s.Set("champions.count", 3);
    s.Set("shade.rank", 7);
    EXPECT_EQ(s.Get("champions.count"), 3);
    EXPECT_EQ(s.Get("shade.rank"), 7);
}

TEST(State, AddStartsFromTheFallbackOfZeroWhenAbsent)
{
    State s;
    EXPECT_EQ(s.Add("deepwounds.wound", 150), 150);
    EXPECT_EQ(s.Get("deepwounds.wound"), 150);
}

TEST(State, AddAccumulatesOnAnExistingValue)
{
    State s;
    s.Set("deepwounds.wound", 100);
    EXPECT_EQ(s.Add("deepwounds.wound", 50), 150);
    EXPECT_EQ(s.Get("deepwounds.wound"), 150);
}

TEST(State, AddAcceptsANegativeDeltaAndCanGoNegative)
{
    // Deep Wounds' wound and a Shade's deadUntilTier both count down.
    State s;
    s.Set("shade.deadUntilTier", 3);
    EXPECT_EQ(s.Add("shade.deadUntilTier", -1), 2);
}

TEST(State, DirtyIsFalseWithNothingSet)
{
    State s;
    EXPECT_FALSE(s.Dirty());
}

TEST(State, SettingAValueMakesItDirty)
{
    State s;
    EXPECT_FALSE(s.Dirty());
    s.Set("champions.count", 1);
    EXPECT_TRUE(s.Dirty());
}

TEST(State, AddingMakesItDirty)
{
    State s;
    s.Add("champions.count", 1);
    EXPECT_TRUE(s.Dirty());
}

TEST(State, ClearWipesValuesAndDirtiness)
{
    State s;
    s.Set("champions.count", 5);
    ASSERT_TRUE(s.Dirty());

    s.Clear();

    EXPECT_FALSE(s.Dirty());
    EXPECT_EQ(s.Get("champions.count", -1), -1);
}

TEST(State, ClearOnAnAlreadyCleanStoreIsANoOp)
{
    State s;
    s.Clear();
    EXPECT_FALSE(s.Dirty());
}

TEST(State, KeyAtExactlyThirtyTwoCharactersIsStored)
{
    State s;
    std::string const key = KeyOfLength(State::MaxKeyLen);
    ASSERT_EQ(key.size(), 32u);

    s.Set(key, 42);

    EXPECT_EQ(s.Get(key, -1), 42);
    EXPECT_TRUE(s.Dirty());
}

TEST(State, KeyOneOverTheLimitIsRefusedNotTruncated)
{
    State s;
    std::string const key = KeyOfLength(State::MaxKeyLen + 1);
    ASSERT_EQ(key.size(), 33u);

    s.Set(key, 42);

    // Never stored -- not stored under a shortened key either, which is the
    // failure mode that would silently merge two different long keys.
    EXPECT_EQ(s.Get(key, -1), -1);
    EXPECT_FALSE(s.Dirty());
}

TEST(State, AddOnAnOverlongKeyReportsTheSumButDoesNotPersistIt)
{
    State s;
    std::string const key = KeyOfLength(State::MaxKeyLen + 1);

    int32 const result = s.Add(key, 10);

    EXPECT_EQ(result, 10);         // computed as if starting from the fallback of 0
    EXPECT_FALSE(s.Dirty());       // but never actually written
    EXPECT_EQ(s.Get(key, -1), -1);
}
