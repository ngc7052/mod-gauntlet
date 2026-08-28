/*
 * mod-gauntlet - the aggregate product and its caps
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Plan section 2.5 is one sentence -- "multiplies every active instance's
// factor, then clamps" -- and every number a Gauntlet character sees passes
// through it. The two things worth proving are that the clamp lands on the
// product rather than on each contribution, which is the difference between
// three affixes reaching the ceiling together and each being trimmed on the
// way in, and that an affix this build cannot run contributes nothing at all
// rather than crashing or leaking its boon.
//
// The contributions come from test stubs rather than from real mechanics. Two
// reasons: every mechanic in the tree now needs Player.h and so is not linked
// into the local harness at all, and a cap test written against a mechanic's
// own percentages would fail every time somebody tuned them. What is under
// test here is the arithmetic Aggregate() does with whatever factors it is
// handed.

#include "GauntletAggregate.h"
#include "GauntletMechanic.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace
{
    using namespace Gauntlet;

    constexpr size_t CONDITION_COUNT = static_cast<size_t>(Condition::MAX);

    // One factor on one kind, and 1.0 on every other, which is the shape of
    // every mechanic that is nothing but a coefficient.
    class FixedFactor : public IMechanic
    {
    public:
        FixedFactor(AggregateKind kind, float factor) : _kind(kind), _factor(factor) { }

        float AggregateFactor(AffixInstance const& /*self*/, AggregateKind kind) const override
        {
            return kind == _kind ? _factor : 1.0f;
        }

        std::string Describe(AffixInstance const& /*self*/) const override { return "a test stub"; }

    private:
        AggregateKind _kind;
        float         _factor;
    };

    // Reads the instance, so that "Aggregate forwards the affix it is
    // iterating" is checked rather than assumed.
    class RankFactor : public IMechanic
    {
    public:
        explicit RankFactor(AggregateKind kind) : _kind(kind) { }

        float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
        {
            return kind == _kind ? 1.0f + 0.1f * static_cast<float>(self.rank) : 1.0f;
        }

        std::string Describe(AffixInstance const& /*self*/) const override { return "a test stub"; }

    private:
        AggregateKind _kind;
    };

    AggregateInput Everything(AggregateKind kind)
    {
        AggregateInput in;
        in.kind = kind;
        for (size_t i = 0; i < CONDITION_COUNT; ++i)
            in.conditionActive[i] = true;
        return in;
    }

    AffixInstance Carrying(IMechanic* impl, Condition condition = Condition::Always)
    {
        AffixInstance instance;
        // Any live id will do; 6 is Champions. It used to be 21, Exposed, which
        // Phase 2 deleted -- and the id matters only in that Aggregate must not
        // consult the registry about it.
        instance.mechanic  = 6;
        instance.condition = condition;
        instance.impl      = impl;
        return instance;
    }

    constexpr std::array<AggregateKind, 6> KINDS = {
        AggregateKind::DamageTaken, AggregateKind::DamageDone, AggregateKind::HealTaken,
        AggregateKind::MaxHealth,   AggregateKind::EnemySpeed, AggregateKind::Experience
    };

    // The plan's caps, restated here rather than read off AggregateCaps, so
    // that a default silently moved in Gauntlet.h fails this file.
    float ExpectedClamp(float product, AggregateKind kind)
    {
        switch (kind)
        {
            case AggregateKind::DamageTaken:
                return product < 1.0f ? 1.0f : (product > 2.0f ? 2.0f : product);
            case AggregateKind::DamageDone:  return product < 0.6f ? 0.6f : product;
            case AggregateKind::HealTaken:   return product < 0.5f ? 0.5f : product;
            case AggregateKind::MaxHealth:   return product < 0.6f ? 0.6f : product;
            case AggregateKind::EnemySpeed:  return product > 1.4f ? 1.4f : product;
            case AggregateKind::Experience:  return product;
            default:                         return product;
        }
    }

    char const* KindName(AggregateKind kind)
    {
        switch (kind)
        {
            case AggregateKind::DamageTaken: return "DamageTaken";
            case AggregateKind::DamageDone:  return "DamageDone";
            case AggregateKind::HealTaken:   return "HealTaken";
            case AggregateKind::MaxHealth:   return "MaxHealth";
            case AggregateKind::EnemySpeed:  return "EnemySpeed";
            case AggregateKind::Experience:  return "Experience";
            default:                         return "?";
        }
    }
}

TEST(Aggregate, EmptySetIsExactlyOne)
{
    AggregateCaps const caps;
    std::vector<AffixInstance> const none;

    for (AggregateKind kind : KINDS)
        EXPECT_FLOAT_EQ(Aggregate(none, Everything(kind), caps), 1.0f) << KindName(kind);
}

TEST(Aggregate, DefaultCapsAreThePlansNumbers)
{
    // Plan section 2.5: damage taken in [1.0, 2.0], damage done >= 0.6,
    // healing received >= 0.5, max health >= 0.6, creature run speed <= 1.4.
    AggregateCaps const caps;
    EXPECT_FLOAT_EQ(caps.damageTakenMin, 1.0f);
    EXPECT_FLOAT_EQ(caps.damageTakenMax, 2.0f);
    EXPECT_FLOAT_EQ(caps.damageDoneMin, 0.6f);
    EXPECT_FLOAT_EQ(caps.healTakenMin, 0.5f);
    EXPECT_FLOAT_EQ(caps.maxHealthMin, 0.6f);
    EXPECT_FLOAT_EQ(caps.enemySpeedMax, 1.4f);
}

TEST(Aggregate, AnInactiveConditionContributesNothing)
{
    AggregateCaps const caps;
    FixedFactor exposed(AggregateKind::DamageTaken, 1.35f);

    std::vector<AffixInstance> const carried = { Carrying(&exposed, Condition::BelowHalfHealth) };

    AggregateInput active;
    active.kind = AggregateKind::DamageTaken;
    active.conditionActive[static_cast<size_t>(Condition::BelowHalfHealth)] = true;
    EXPECT_FLOAT_EQ(Aggregate(carried, active, caps), 1.35f);

    AggregateInput idle;
    idle.kind = AggregateKind::DamageTaken;   // every condition false
    EXPECT_FLOAT_EQ(Aggregate(carried, idle, caps), 1.0f);

    // A different condition being active is not this affix's condition.
    AggregateInput other;
    other.kind = AggregateKind::DamageTaken;
    other.conditionActive[static_cast<size_t>(Condition::AboveHalfHealth)] = true;
    EXPECT_FLOAT_EQ(Aggregate(carried, other, caps), 1.0f);
}

TEST(Aggregate, AConditionOutsideTheEnumIsIgnored)
{
    // A stored row is only as trustworthy as the database it came from, and
    // the condition column is a tinyint. Reading conditionActive[] with an
    // out-of-range index would be a buffer overrun on the damage path.
    AggregateCaps const caps;
    FixedFactor exposed(AggregateKind::DamageTaken, 1.5f);

    AffixInstance corrupt = Carrying(&exposed);
    corrupt.condition = static_cast<Condition>(static_cast<uint8>(Condition::MAX) + 7);

    std::vector<AffixInstance> const carried = { corrupt };
    EXPECT_FLOAT_EQ(Aggregate(carried, Everything(AggregateKind::DamageTaken), caps), 1.0f);
}

TEST(Aggregate, AnUnimplementedMechanicIsIgnoredEntirely)
{
    // A run migrated from a newer registry, or a family switched off in
    // config, legitimately carries an id this build has no code for:
    // MakeMechanic answers nullptr and the instance keeps a null impl. It
    // must not crash, and it must not pay out its boon either -- a boon whose
    // curse is not running would be a free upside.
    AggregateCaps const caps;

    AffixInstance unimplemented;
    unimplemented.mechanic = 4242;
    unimplemented.impl     = nullptr;
    unimplemented.boon     = Boon::BonusDamage;
    unimplemented.boonMag  = 40;

    std::vector<AffixInstance> const alone = { unimplemented };
    for (AggregateKind kind : KINDS)
        EXPECT_FLOAT_EQ(Aggregate(alone, Everything(kind), caps), 1.0f) << KindName(kind);

    // And it does not disturb the affixes around it.
    FixedFactor feeble(AggregateKind::DamageDone, 0.8f);
    std::vector<AffixInstance> const mixed = { unimplemented, Carrying(&feeble), unimplemented };
    EXPECT_FLOAT_EQ(Aggregate(mixed, Everything(AggregateKind::DamageDone), caps), 0.8f);
}

TEST(Aggregate, TheAffixIsForwardedToItsImplementation)
{
    AggregateCaps const caps;
    RankFactor scaling(AggregateKind::DamageTaken);

    for (uint8 rank = 1; rank <= MAX_RANK; ++rank)
    {
        AffixInstance instance = Carrying(&scaling);
        instance.rank = rank;
        std::vector<AffixInstance> const carried = { instance };
        EXPECT_FLOAT_EQ(Aggregate(carried, Everything(AggregateKind::DamageTaken), caps),
                        1.0f + 0.1f * float(rank))
            << "rank " << unsigned(rank);
    }
}

TEST(Aggregate, TheClampIsOnTheProductNotOnEachContribution)
{
    AggregateCaps const caps;

    // Two damage-taken curses at +50% each. The product is 2.25 and the
    // ceiling is 2.0. Clamping each contribution first would leave both at
    // 1.5 -- neither exceeds the ceiling on its own -- and give 2.25.
    FixedFactor half(AggregateKind::DamageTaken, 1.5f);
    std::vector<AffixInstance> const two = { Carrying(&half), Carrying(&half) };
    EXPECT_FLOAT_EQ(Aggregate(two, Everything(AggregateKind::DamageTaken), caps), 2.0f)
        << "per-contribution clamping would give 2.25 here";

    // The floor, the other way round. Two damage-done curses at -50% each
    // multiply to 0.25 and the floor is 0.6. Clamping each contribution to
    // 0.6 first would give 0.36, which is below the floor the cap exists to
    // hold and is a different number from either answer.
    FixedFactor crippling(AggregateKind::DamageDone, 0.5f);
    std::vector<AffixInstance> const both = { Carrying(&crippling), Carrying(&crippling) };
    EXPECT_FLOAT_EQ(Aggregate(both, Everything(AggregateKind::DamageDone), caps), 0.6f)
        << "per-contribution clamping would give 0.36 here, below the floor itself";

    // Three at -30% multiply to 0.343, still one clamp, still 0.6.
    FixedFactor third(AggregateKind::DamageDone, 0.7f);
    std::vector<AffixInstance> const three = { Carrying(&third), Carrying(&third), Carrying(&third) };
    EXPECT_FLOAT_EQ(Aggregate(three, Everything(AggregateKind::DamageDone), caps), 0.6f);
}

TEST(Aggregate, EveryCombinationOfContributorsLandsInsideItsCap)
{
    // Six contributors per kind, chosen so that subsets cross the cap from
    // both directions, and all 64 subsets of each. That is the plan's "from
    // every combination of the contributing mechanics" taken literally.
    AggregateCaps const caps;

    constexpr std::array<float, 6> FACTORS = { 1.25f, 1.5f, 0.8f, 0.5f, 1.1f, 0.65f };

    for (AggregateKind kind : KINDS)
    {
        std::vector<FixedFactor> stubs;
        stubs.reserve(FACTORS.size());
        for (float factor : FACTORS)
            stubs.emplace_back(kind, factor);

        for (uint32 subset = 0; subset < (1u << FACTORS.size()); ++subset)
        {
            std::vector<AffixInstance> carried;
            float expected = 1.0f;
            for (size_t bit = 0; bit < FACTORS.size(); ++bit)
                if (subset & (1u << bit))
                {
                    carried.push_back(Carrying(&stubs[bit]));
                    expected *= FACTORS[bit];
                }

            float const got = Aggregate(carried, Everything(kind), caps);
            EXPECT_FLOAT_EQ(got, ExpectedClamp(expected, kind))
                << KindName(kind) << " subset 0x" << std::hex << subset << std::dec
                << " raw product " << expected;

            // And the caps themselves, stated as the plan states them.
            switch (kind)
            {
                case AggregateKind::DamageTaken:
                    EXPECT_GE(got, 1.0f);
                    EXPECT_LE(got, 2.0f);
                    break;
                case AggregateKind::DamageDone:  EXPECT_GE(got, 0.6f); break;
                case AggregateKind::HealTaken:   EXPECT_GE(got, 0.5f); break;
                case AggregateKind::MaxHealth:   EXPECT_GE(got, 0.6f); break;
                case AggregateKind::EnemySpeed:  EXPECT_LE(got, 1.4f); break;
                case AggregateKind::Experience:  break;   // uncapped, by plan section 2.5
                default: break;
            }
        }
    }
}

TEST(Aggregate, AKindWithNoContributorsIsUntouchedByTheOthers)
{
    // A damage-taken curse must not move the damage-done product. This is what
    // makes it safe for Mgr to ask for one kind at a time on the damage path.
    AggregateCaps const caps;
    FixedFactor exposed(AggregateKind::DamageTaken, 1.75f);
    std::vector<AffixInstance> const carried = { Carrying(&exposed) };

    for (AggregateKind kind : KINDS)
    {
        float const expected = kind == AggregateKind::DamageTaken ? 1.75f : 1.0f;
        EXPECT_FLOAT_EQ(Aggregate(carried, Everything(kind), caps), expected) << KindName(kind);
    }
}

TEST(Aggregate, TheAggregatePaysNoBoonOfItsOwn)
{
    // Until Phase 2 this function also paid a carried affix's boon, because a
    // Scalar's boon was rolled generically and had nowhere else to go. With the
    // Scalars deleted every boon is named by MechanicDef::boon and delivered by
    // the mechanic that names it -- several of them by returning BoonFactor()
    // from AggregateFactor, which is the one number this function sees.
    //
    // So a boon on the instance must move nothing at all. If it did, a mechanic
    // that already pays its own would be paying it twice: exactly the bug
    // commit 04570c9 took out of the Shade, which handed out a permanent
    // experience multiplier on top of the Vindication its card promises.
    AggregateCaps const caps;

    for (uint8 b = 1; b < static_cast<uint8>(Boon::MAX); ++b)
        for (AggregateKind kind : KINDS)
        {
            FixedFactor curse(kind, 0.9f);
            AffixInstance instance = Carrying(&curse);
            instance.boon    = static_cast<Boon>(b);
            instance.boonMag = 30;

            std::vector<AffixInstance> const carried = { instance };
            EXPECT_FLOAT_EQ(Aggregate(carried, Everything(kind), caps),
                            ExpectedClamp(0.9f, kind))
                << "boon " << unsigned(b) << " moved the product on " << KindName(kind)
                << "; the aggregate pays no boon, the mechanic does";
        }

    // And the same on a kind where a larger number is worse, where a boon
    // paid here would have had to pull the product down.
    for (AggregateKind kind : { AggregateKind::DamageTaken, AggregateKind::EnemySpeed })
        for (uint8 b = 1; b < static_cast<uint8>(Boon::MAX); ++b)
        {
            FixedFactor curse(kind, 1.2f);
            AffixInstance instance = Carrying(&curse);
            instance.boon    = static_cast<Boon>(b);
            instance.boonMag = 25;

            std::vector<AffixInstance> const carried = { instance };
            EXPECT_FLOAT_EQ(Aggregate(carried, Everything(kind), caps), 1.2f)
                << KindName(kind) << " was moved by boon " << unsigned(b);
        }
}

TEST(Aggregate, CapsAreConfigurable)
{
    // Every field is overridable from mod_gauntlet.conf (plan section 5.4's
    // Gauntlet.Caps.* keys), so the clamp must read the caps it is handed and
    // not a constant.
    AggregateCaps caps;
    caps.damageTakenMax = 3.0f;
    caps.damageDoneMin  = 0.1f;
    caps.enemySpeedMax  = 1.0f;

    FixedFactor heavy(AggregateKind::DamageTaken, 1.5f);
    std::vector<AffixInstance> const two = { Carrying(&heavy), Carrying(&heavy) };
    EXPECT_FLOAT_EQ(Aggregate(two, Everything(AggregateKind::DamageTaken), caps), 2.25f);

    FixedFactor crippling(AggregateKind::DamageDone, 0.5f);
    std::vector<AffixInstance> const both = { Carrying(&crippling), Carrying(&crippling) };
    EXPECT_FLOAT_EQ(Aggregate(both, Everything(AggregateKind::DamageDone), caps), 0.25f);

    FixedFactor swift(AggregateKind::EnemySpeed, 1.3f);
    std::vector<AffixInstance> const fast = { Carrying(&swift) };
    EXPECT_FLOAT_EQ(Aggregate(fast, Everything(AggregateKind::EnemySpeed), caps), 1.0f);
}

TEST(Aggregate, TheFloorWinsWhenAConfigurationCrossesIt)
{
    // GauntletAggregate.cpp applies the floor after the ceiling on purpose, so
    // a mod_gauntlet.conf whose min exceeds its max resolves to the min:
    // taking less than base damage is exactly the outcome the damage-taken cap
    // exists to prevent, so the floor has to win.
    AggregateCaps caps;
    caps.damageTakenMin = 1.5f;
    caps.damageTakenMax = 1.2f;

    std::vector<AffixInstance> const none;
    EXPECT_FLOAT_EQ(Aggregate(none, Everything(AggregateKind::DamageTaken), caps), 1.5f);

    FixedFactor heavy(AggregateKind::DamageTaken, 1.9f);
    std::vector<AffixInstance> const one = { Carrying(&heavy) };
    EXPECT_FLOAT_EQ(Aggregate(one, Everything(AggregateKind::DamageTaken), caps), 1.5f);
}

TEST(Aggregate, ExperienceIsUncapped)
{
    // Plan section 2.5 gives experience no ceiling and no floor, and inventing
    // one here would be a balance decision rather than an implementation.
    AggregateCaps const caps;

    FixedFactor drain(AggregateKind::Experience, 0.5f);
    std::vector<AffixInstance> const four = {
        Carrying(&drain), Carrying(&drain), Carrying(&drain), Carrying(&drain)
    };
    EXPECT_FLOAT_EQ(Aggregate(four, Everything(AggregateKind::Experience), caps), 0.0625f);

    FixedFactor bounty(AggregateKind::Experience, 1.5f);
    std::vector<AffixInstance> const rich = { Carrying(&bounty), Carrying(&bounty) };
    EXPECT_FLOAT_EQ(Aggregate(rich, Everything(AggregateKind::Experience), caps), 2.25f);
}
