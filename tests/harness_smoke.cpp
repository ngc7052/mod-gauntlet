/*
 * mod-gauntlet - harness smoke test
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Proves the local build loop actually works end to end: Gauntlet.h compiles
// standalone, links against gtest_main, and a stub IPlayerView behaves the
// way the header promises. The real Phase 0 tests land beside this one.

#include "Gauntlet.h"
#include <gtest/gtest.h>

namespace
{
    // Class ids are CLASS_WARRIOR/CLASS_MAGE from
    // $CORE/src/server/shared/SharedDefines.h:126,133. That header pulls in
    // the rest of the server tree, so the value is copied here rather than
    // included; GetClassMask() itself is Gauntlet::IPlayerView's own code.
    constexpr uint8 CLASS_WARRIOR = 1;
    constexpr uint8 CLASS_MAGE    = 8;

    class StubPlayerView : public Gauntlet::IPlayerView
    {
    public:
        explicit StubPlayerView(uint8 cls) : _class(cls) { }

        uint8 GetClass() const override { return _class; }
        uint8 GetLevel() const override { return 1; }
        bool  HasSpell(uint32 /*spellId*/) const override { return false; }
        uint8 GetTalentTree() const override { return 0; }

    private:
        uint8 _class;
    };
}

TEST(HarnessSmoke, CommittedConstants)
{
    EXPECT_EQ(Gauntlet::GeneratorVersion, 3);
    EXPECT_EQ(Gauntlet::MECHANIC_WITHERING, 72);
    EXPECT_EQ(Gauntlet::MECHANIC_FORGETFUL, 73);
    EXPECT_EQ(Gauntlet::MAX_RANK, 3);
}

TEST(HarnessSmoke, PlayerViewClassMask)
{
    StubPlayerView warrior(CLASS_WARRIOR);
    StubPlayerView mage(CLASS_MAGE);

    EXPECT_EQ(warrior.GetClassMask(), 1u << (CLASS_WARRIOR - 1));
    EXPECT_EQ(mage.GetClassMask(), 1u << (CLASS_MAGE - 1));
}
