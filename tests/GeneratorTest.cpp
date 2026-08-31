/*
 * mod-gauntlet - the offer builder is a pure function of its inputs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// One job. The offers are never stored -- they are rebuilt from the seed every
// time the tier prompt is shown -- so a query that is not a pure function of
// its inputs shows a player one set of offers and gives them another.
//
// This file also used to carry the legacy golden test, which held LegacyRoll to
// a 288-row fixture captured before the Phase 0 rewrite so that a live
// character's affixes could be rebuilt during the one-shot migration. Phase 2
// deleted the migration, LegacyRoll, the fixture and the tool that wrote it:
// there is nothing left on any realm that was rolled by generator 1.

#include "GauntletGenerator.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Gauntlet;

    constexpr std::array<uint8, 10> CLASSES = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 };

    class StubView : public IPlayerView
    {
    public:
        StubView(uint8 cls, uint8 tree) : _class(cls), _tree(tree) { }

        uint8 GetClass() const override { return _class; }
        uint8 GetLevel() const override { return 80; }
        bool  HasSpell(uint32 /*spellId*/) const override { return true; }
        uint8 GetTalentTree() const override { return _tree; }

    private:
        uint8 _class;
        uint8 _tree;
    };

    bool SameOffer(Offer const& a, Offer const& b)
    {
        // Field by field rather than memcmp: Offer has padding, and padding
        // bytes are not part of the value this test is about.
        return a.mechanic == b.mechanic && a.rank == b.rank && a.rarity == b.rarity
            && a.condition == b.condition && a.boon == b.boon && a.boonMag == b.boonMag
            && a.kind == b.kind && a.swapSlot == b.swapSlot;
    }

    bool SameSet(OfferSet const& a, OfferSet const& b)
    {
        if (a.relaxations != b.relaxations || a.offers.size() != b.offers.size())
            return false;
        for (size_t i = 0; i < a.offers.size(); ++i)
            if (!SameOffer(a.offers[i], b.offers[i]))
                return false;
        return true;
    }

    std::string Describe(OfferSet const& set)
    {
        std::ostringstream out;
        out << "relaxations=0x" << std::hex << set.relaxations << std::dec;
        for (Offer const& offer : set.offers)
            out << " | id=" << offer.mechanic << " rank=" << unsigned(offer.rank)
                << " rarity=" << unsigned(static_cast<uint8>(offer.rarity))
                << " cond=" << unsigned(static_cast<uint8>(offer.condition))
                << " boon=" << unsigned(static_cast<uint8>(offer.boon))
                << " boonMag=" << unsigned(offer.boonMag)
                << " kind=" << unsigned(static_cast<uint8>(offer.kind))
                << " swapSlot=" << unsigned(offer.swapSlot);
        return out.str();
    }

    // A carried set derived from the seed, so a failure is reproducible from
    // the numbers the message prints.
    std::vector<AffixInstance> CarriedFor(uint32 seed, uint8 tier)
    {
        std::vector<AffixInstance> carried;
        uint64 state = Stream::Mix((static_cast<uint64>(seed) << 16) ^ tier);
        uint32 const count = Stream::RollIn(state, 0, tier < 5 ? tier : 5);

        auto const& all = AllMechanics();
        for (uint32 i = 0; i < count; ++i)
        {
            MechanicDef const& def = all[Stream::RollIn(state, 0, static_cast<uint32>(all.size()) - 1)];

            bool held = false;
            for (AffixInstance const& a : carried)
                held = held || a.mechanic == def.id;
            if (held)
                continue;

            AffixInstance instance;
            instance.mechanic   = def.id;
            instance.rank       = static_cast<uint8>(Stream::RollIn(state, 1, def.maxRank));
            instance.slot       = static_cast<uint8>(Stream::RollIn(state, 1, tier));
            instance.genVersion = GeneratorVersion;
            carried.push_back(instance);
        }
        return carried;
    }
}

// =====================================================================
// Determinism.
// =====================================================================

TEST(GeneratorDeterminism, SameQueryTwiceInOneProcess)
{
    // What this catches: a rand() or a clock read on the path, an unordered
    // container whose iteration order depends on insertion history, a static
    // accumulated across calls, and a read of an uninitialised field that the
    // allocator happened to fill differently the second time.
    //
    // What it cannot catch: a value that is stable within one process but not
    // across processes or platforms -- a pointer ordering that is consistent
    // in a single run, or a hash seeded once per process. The whole-table
    // sweep in OfferInvariantsTest.cpp is what makes those unlikely by
    // exercising every code path; only a cross-process diff of the offer
    // stream would settle it, and that belongs in the coordinator's build,
    // not in a unit test.
    RegistryView full;
    full.includeUnimplemented = true;

    for (uint32 seed = 1; seed <= 200; ++seed)
        for (size_t ci = 0; ci < CLASSES.size(); ++ci)
        {
            StubView const view(CLASSES[ci], static_cast<uint8>(1 + (ci % 3)));
            for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
            {
                std::vector<AffixInstance> const carried = CarriedFor(seed, tier);

                OfferSet const first  = BuildOffers(seed, tier, view, carried, 3, full);
                OfferSet const second = BuildOffers(seed, tier, view, carried, 3, full);

                ASSERT_TRUE(SameSet(first, second))
                    << "seed=" << seed << " tier=" << unsigned(tier)
                    << " class=" << unsigned(CLASSES[ci]) << " carried=" << carried.size()
                    << "\n  first:  " << Describe(first)
                    << "\n  second: " << Describe(second);
            }
        }
}

TEST(GeneratorDeterminism, DoesNotDependOnWhereTheInputsLive)
{
    // The same query, but with the carried vector at a different address and
    // with unrelated calls interleaved between the two. A generator that read
    // a pointer value, or that kept state between calls, would answer
    // differently here while still passing the back-to-back test above.
    RegistryView full;
    full.includeUnimplemented = true;

    for (uint32 seed = 1; seed <= 100; ++seed)
        for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
        {
            StubView const view(CLASSES[seed % CLASSES.size()], static_cast<uint8>(1 + (tier % 3)));

            std::vector<AffixInstance> const carried = CarriedFor(seed, tier);
            OfferSet const first = BuildOffers(seed, tier, view, carried, 3, full);

            // Churn the heap and interleave a different query.
            std::vector<std::vector<AffixInstance>> ballast(8, carried);
            StubView const other(CLASSES[(seed + 3) % CLASSES.size()], 2);
            (void)BuildOffers(seed + 1, static_cast<uint8>(1 + (tier % 16)), other, ballast[0], 3, full);

            std::vector<AffixInstance> const relocated(carried.begin(), carried.end());
            if (!carried.empty())
            {
                // Braced because ASSERT_NE expands to an if/else and an
                // unbraced one under an if is the -Wdangling-else warning.
                ASSERT_NE(relocated.data(), carried.data())
                    << "the copy landed on the same allocation, so this iteration proves nothing";
            }

            OfferSet const second = BuildOffers(seed, tier, view, relocated, 3, full);
            ASSERT_TRUE(SameSet(first, second))
                << "seed=" << seed << " tier=" << unsigned(tier)
                << "\n  first:  " << Describe(first)
                << "\n  second: " << Describe(second);
        }
}

TEST(GeneratorDeterminism, DifferentSeedsGiveDifferentOffers)
{
    // 16 tiers x 10 classes x 500 adjacent seed pairs, empty carried set, so
    // the only thing that differs between the two calls is the seed.
    //
    // This is not a statistical test and cannot flake: BuildOffers is a pure
    // function, the sample is a fixed cross product, and the count below is a
    // fixed integer for a given table and algorithm. It is stated as a ceiling
    // rather than an equality only so that a benign registry edit does not
    // fail it. A genuine collision is not a bug either -- two seeds may land
    // on the same three cards, and the low tiers, where the eligible pool is
    // smallest, are where that happens -- so what is asserted is that the seed
    // is doing real work, not that it is injective.
    RegistryView full;
    full.includeUnimplemented = true;

    std::vector<AffixInstance> const empty;
    size_t pairs = 0;
    size_t identical = 0;
    std::array<size_t, 81> identicalPerTier = {};   // one slot per tier, and a tier is a level

    for (uint32 seed = 1; seed <= 500; ++seed)
        for (size_t ci = 0; ci < CLASSES.size(); ++ci)
        {
            StubView const view(CLASSES[ci], static_cast<uint8>(1 + (ci % 3)));
            for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
            {
                OfferSet const a = BuildOffers(seed, tier, view, empty, 3, full);
                OfferSet const b = BuildOffers(seed + 1, tier, view, empty, 3, full);
                ++pairs;
                if (SameSet(a, b))
                {
                    ++identical;
                    identicalPerTier[tier]++;
                }
            }
        }

    // 500 seeds x 10 classes x (80 - FIRST_TIER + 1) tiers.
    ASSERT_EQ(pairs, 500u * 10u * (80u - FIRST_TIER + 1u));

    // Measured after Phase 2: 428 of the 80,000 pairs collide, 0.535%, against
    // 78 (0.0975%) before it. The rise is arithmetic and has two named causes,
    // both of them the deletion rather than the generator.
    //
    // The four flat scalars are gone, and with them the condition axis. A
    // Scalar offer used to carry one of thirteen conditions rolled from the
    // stream, which multiplied the number of distinct sets a tier could
    // produce by more than an order of magnitude; nothing rolls a condition
    // now. And the four rows themselves had windows of 1-16, so every tier lost
    // four candidates.
    //
    // The collisions concentrate exactly where the pool is thinnest: 180 of the
    // 428 at tier 1, where the whole eligible table is seven mechanics across
    // four families, and 166 at tiers 15 and 16, where seventeen rows reach the
    // window and a run carries most of them. Tiers 4, 6, 7, 8, 9 and 12 collide
    // not at all.
    //
    // Two bounds rather than one. The global ceiling is 1% -- twice the
    // measured rate -- and no single tier may pass 10%, so a band that
    // collapsed to a handful of possible sets fails here even if the total
    // stayed small. A generator that stopped reading the seed lands near 100%
    // on both and fails loudly.
    constexpr size_t CEILING_PER_MILLE  = 10;
    constexpr double CEILING_TIER_PCT   = 10.0;
    size_t const ceiling = pairs * CEILING_PER_MILLE / 1000;

    EXPECT_LE(identical, ceiling)
        << identical << " of " << pairs << " adjacent seed pairs produced identical offers";

    size_t const perTier = pairs / (80u - FIRST_TIER + 1u);
    for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
    {
        double const rate = 100.0 * double(identicalPerTier[tier]) / double(perTier);
        EXPECT_LE(rate, CEILING_TIER_PCT)
            << "tier " << unsigned(tier) << ": " << identicalPerTier[tier] << " of " << perTier
            << " adjacent seed pairs (" << rate << "%) produced identical offers";
    }

    if (identical > ceiling)
        for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
            if (identicalPerTier[tier] != 0)
                ADD_FAILURE() << "  tier " << unsigned(tier) << ": " << identicalPerTier[tier]
                              << " identical pairs of " << perTier;
}

TEST(GeneratorDeterminism, GeneratorVersionIsFoldedIntoTheStream)
{
    // Stated plainly, because the spec asks for it: this test cannot vary the
    // real thing. GeneratorVersion is a `constexpr uint16` in the frozen
    // Gauntlet.h and BuildOffers takes no version parameter, so there is no
    // way from a test to ask the generator for the offers of version 3. What
    // can be checked is the arithmetic BuildOffers actually performs -- the
    // expression below is the one at the head of BuildOffers, copied -- and
    // that the version reaches the stream at all rather than being folded into
    // a bit another input already occupies.
    //
    // If GeneratorVersion is ever bumped, the assertion that matters is not
    // here: it is that every offer in the game moves, which the sweep in
    // OfferInvariantsTest.cpp will show and no committed run will feel,
    // because a pick is stored in columns and never regenerated.
    auto streamSeed = [](uint32 seed, uint8 tier, uint16 version, uint8 rerolls = 0)
    {
        return Stream::Mix((static_cast<uint64>(seed) << 32)
                         ^ (static_cast<uint64>(rerolls) << 16)
                         ^ (static_cast<uint64>(tier) << 8)
                         ^ static_cast<uint64>(version));
    };

    for (uint32 seed : { 1u, 7u, 1337u, 987654321u })
        for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
        {
            uint64 const current = streamSeed(seed, tier, GeneratorVersion);
            for (uint16 version = 1; version <= 8; ++version)
            {
                if (version == GeneratorVersion)
                    continue;
                EXPECT_NE(streamSeed(seed, tier, version), current)
                    << "generator version " << version << " shares a stream with version "
                    << GeneratorVersion << " at seed=" << seed << " tier=" << unsigned(tier);
            }
        }

    // The version occupies the low byte, the tier the next one up and the
    // reroll count the one above that, so no value below 256 can be mistaken
    // for a neighbour. That is the property that makes the XOR above safe; a
    // version of 256 would silently collide with tier 1 and this is where
    // that would be noticed.
    EXPECT_LT(GeneratorVersion, 256)
        << "GeneratorVersion has grown past the byte the stream seed gives it and now overlaps "
           "the tier";

    // And the reroll byte is genuinely its own: every count gets its own
    // stream, and no count collides with any (tier, version) pair below 256.
    for (uint32 seed : { 1u, 1337u })
        for (uint8 tier : { uint8(1), uint8(40), uint8(80) })
            for (uint8 a = 0; a < 8; ++a)
                for (uint8 b = uint8(a + 1); b < 8; ++b)
                    EXPECT_NE(streamSeed(seed, tier, GeneratorVersion, a),
                              streamSeed(seed, tier, GeneratorVersion, b))
                        << "rerolls " << unsigned(a) << " and " << unsigned(b)
                        << " share a stream at seed=" << seed << " tier=" << unsigned(tier);
}

// ---------------------------------------------------------------------------
// Rerolls (docs/rarity-plan.md section 4): the counter folded into the stream
// must buy a different set, deterministically -- the same reroll twice is the
// same set, because a relog rebuilds the offers and must show the player what
// the charge bought rather than what it replaced.
// ---------------------------------------------------------------------------

TEST(GeneratorRerolls, ARerollBuysADifferentSetAndTheSameRerollBuysTheSameOne)
{
    RegistryView full;
    full.includeUnimplemented = true;

    size_t pairs = 0;
    size_t identical = 0;

    for (uint32 seed = 1; seed <= 200; ++seed)
        for (size_t ci = 0; ci < CLASSES.size(); ++ci)
        {
            StubView const view(CLASSES[ci], static_cast<uint8>(1 + (ci % 3)));
            for (uint8 tier = 5; tier <= 80; tier += 5)
            {
                std::vector<AffixInstance> const carried = CarriedFor(seed, tier);

                OfferSet const plain    = BuildOffers(seed, tier, view, carried, 3, full,
                                                      MAX_CARRIED, 0);
                OfferSet const rerolled = BuildOffers(seed, tier, view, carried, 3, full,
                                                      MAX_CARRIED, 1);
                OfferSet const again    = BuildOffers(seed, tier, view, carried, 3, full,
                                                      MAX_CARRIED, 1);

                // Deterministic: the rerolled set is a set, not a shuffle.
                ASSERT_TRUE(SameSet(rerolled, again))
                    << "seed=" << seed << " tier=" << unsigned(tier)
                    << "\n  first:  " << Describe(rerolled)
                    << "\n  second: " << Describe(again);

                // And the default is spelled zero: the parameter's absence and
                // its zero are the same stream, or every set in the game moved.
                OfferSet const defaulted = BuildOffers(seed, tier, view, carried, 3, full);
                ASSERT_TRUE(SameSet(plain, defaulted))
                    << "seed=" << seed << " tier=" << unsigned(tier);

                ++pairs;
                if (SameSet(plain, rerolled))
                    ++identical;
            }
        }

    // A reroll that hands back the same three cards is legal -- the pools are
    // finite and thin tiers are thin -- but it must be the exception, or the
    // charge buys nothing. The ceiling is the seed-collision test's shape: a
    // generator that stopped folding the counter lands at 100% and fails
    // loudly.
    double const rate = 100.0 * double(identical) / double(pairs);
    EXPECT_LE(rate, 10.0)
        << identical << " of " << pairs << " rerolls (" << rate
        << "%) handed back the identical offer set";
}

TEST(GeneratorDeterminism, CountIsHonoured)
{
    RegistryView full;
    full.includeUnimplemented = true;
    StubView const view(1, 1);
    std::vector<AffixInstance> const empty;

    EXPECT_TRUE(BuildOffers(1, 5, view, empty, 0, full).offers.empty());
    for (uint32 count = 1; count <= 5; ++count)
        EXPECT_EQ(BuildOffers(1, 5, view, empty, count, full).offers.size(), count)
            << "count=" << count;
}

// ---------------------------------------------------------------------------
// Rarity. docs/rarity-plan.md step 1: every slot rolls a rarity to draw from,
// weighted by tier, and the offer carries the rarity of the card it drew.
//
// With every card in the table Rare, the sweep cannot show the weights doing
// anything -- one candidate rarity, one answer -- so what BuildOffers is held
// to here is the part that does not depend on the table: an offer never says a
// rarity its card does not have. The roll itself is tested through RollRarity,
// which is public for exactly this reason.
// ---------------------------------------------------------------------------

TEST(GeneratorRarity, AnOfferCarriesItsCardsRarity)
{
    RegistryView full;
    full.includeUnimplemented = true;

    for (uint32 seed = 1; seed <= 100; ++seed)
        for (size_t ci = 0; ci < CLASSES.size(); ++ci)
        {
            StubView const view(CLASSES[ci], static_cast<uint8>(1 + (ci % 3)));
            for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
            {
                std::vector<AffixInstance> const carried = CarriedFor(seed, tier);
                OfferSet const set = BuildOffers(seed, tier, view, carried, 3, full);
                for (Offer const& o : set.offers)
                {
                    if (o.mechanic == MECHANIC_NONE)
                        continue;
                    MechanicDef const* def = FindMechanic(o.mechanic);
                    ASSERT_NE(def, nullptr);
                    EXPECT_EQ(o.rarity, def->rarity)
                        << "seed=" << seed << " tier=" << unsigned(tier) << " id=" << o.mechanic
                        << ": the offer says " << RarityName(o.rarity) << " and the card is "
                        << RarityName(def->rarity);
                }
            }
        }
}

namespace
{
    // The whole sample the distribution tests draw. Deterministic: the stream
    // is splitmix64 from a fixed seed, so these are counts, not statistics,
    // and cannot flake.
    constexpr uint32 ROLLS = 100000;

    std::array<uint32, Rules::RARITY_COUNT> CountRolls(uint8 tier, uint8 mask, uint64 seed = 0x5EED)
    {
        std::array<uint32, Rules::RARITY_COUNT> out = {};
        uint64 state = seed;
        for (uint32 i = 0; i < ROLLS; ++i)
            out[static_cast<size_t>(RollRarity(state, tier, mask))]++;
        return out;
    }
}

TEST(GeneratorRarity, TheRollNeverAnswersWithSomethingUnavailable)
{
    // Every non-empty mask, every band edge. A rarity with no candidate must
    // never come back however heavy its weight -- a Common at tier 1 carries
    // 70% and there are no commons in the table today.
    for (uint8 mask = 1; mask <= RARITY_MASK_ALL; ++mask)
        for (uint8 tier : { 1, 20, 21, 40, 41, 60, 61, 80 })
        {
            auto const counts = CountRolls(static_cast<uint8>(tier), mask);
            for (size_t r = 0; r < Rules::RARITY_COUNT; ++r)
                if (!(mask & RarityBit(static_cast<Rarity>(r))))
                    EXPECT_EQ(counts[r], 0u)
                        << RarityName(static_cast<Rarity>(r)) << " was rolled at tier " << tier
                        << " with mask 0x" << std::hex << unsigned(mask) << " and no candidate";
        }
}

TEST(GeneratorRarity, OneCandidateRarityIsAlwaysTheAnswer)
{
    // The all-Rare table, which is what the registry is until step 2 of the
    // plan lands: the roll has one option at every tier and must take it.
    auto const counts = CountRolls(1, RarityBit(Rarity::Rare));
    EXPECT_EQ(counts[static_cast<size_t>(Rarity::Rare)], ROLLS);

    // Including at a tier where its weight is the smallest in the band. Five
    // percent of nothing else is all of it.
    EXPECT_EQ(Rules::RarityWeight(1, Rarity::Rare), 5u) << "the premise of this test moved";
}

TEST(GeneratorRarity, TheRollFollowsTheTierWeights)
{
    // Every rarity available, at the first tier of each band: the share each
    // rarity gets must be the band's weight to within a point and a half of a
    // hundred thousand rolls. This is the whole point of the table -- the
    // escalation of the run is these numbers -- and it fails if a weight is
    // read from the wrong band or the roll is not consumed the way Draw
    // assumes.
    for (size_t band = 0; band < Rules::RARITY_BANDS; ++band)
    {
        uint8 const tier = static_cast<uint8>(band * Rules::RARITY_BAND_TIERS + 1);
        auto const counts = CountRolls(tier, RARITY_MASK_ALL);
        for (size_t r = 0; r < Rules::RARITY_COUNT; ++r)
        {
            double const share  = 100.0 * double(counts[r]) / double(ROLLS);
            double const weight = double(Rules::RarityWeight(tier, static_cast<Rarity>(r)));
            EXPECT_NEAR(share, weight, 1.5)
                << RarityName(static_cast<Rarity>(r)) << " at tier " << unsigned(tier)
                << ": rolled " << share << "% against a weight of " << weight;
        }
    }
}

TEST(GeneratorRarity, TheRollRenormalisesOverWhatIsThere)
{
    // Two rarities available out of five. The absent three give their share to
    // the present two in proportion, so the ratio between the two survives:
    // at tier 1 Common is 70 and Rare is 5, so with only those two the split
    // must be 70:5 -- fourteen commons to a rare -- not 70:30.
    uint8 const mask = static_cast<uint8>(RarityBit(Rarity::Common) | RarityBit(Rarity::Rare));
    auto const counts = CountRolls(1, mask);
    double const ratio = double(counts[static_cast<size_t>(Rarity::Common)])
                       / double(counts[static_cast<size_t>(Rarity::Rare)]);
    EXPECT_NEAR(ratio, 14.0, 1.0);
}

TEST(GeneratorRarity, ZeroWeightIsNeverWhileSomethingElseIsThere)
{
    // Epic weighs nothing at tier 1. With a common beside it, an epic must
    // never be drawn there -- that is what the dash in the plan's table means.
    uint8 const mask = static_cast<uint8>(RarityBit(Rarity::Common) | RarityBit(Rarity::Epic));
    auto const counts = CountRolls(1, mask);
    EXPECT_EQ(counts[static_cast<size_t>(Rarity::Epic)], 0u);
    EXPECT_EQ(counts[static_cast<size_t>(Rarity::Common)], ROLLS);
}

TEST(GeneratorRarity, AllZeroWeightsFallBackToUniformRatherThanNothing)
{
    // Only epics and legendaries eligible at tier 1, where both weigh zero.
    // The weights shape the mix, they do not veto a card the registry made
    // eligible, so the roll goes uniform over the two rather than emptying the
    // slot -- and uniform means both, about equally.
    uint8 const mask = static_cast<uint8>(RarityBit(Rarity::Epic) | RarityBit(Rarity::Legendary));
    auto const counts = CountRolls(1, mask);
    uint32 const epic = counts[static_cast<size_t>(Rarity::Epic)];
    uint32 const leg  = counts[static_cast<size_t>(Rarity::Legendary)];
    EXPECT_EQ(epic + leg, ROLLS);
    EXPECT_NEAR(double(epic) / double(ROLLS), 0.5, 0.015);
}

TEST(GeneratorRarity, TheRollConsumesExactlyOneDraw)
{
    // Draw and the tests above assume one RollIn per call, whichever branch
    // the roll takes. Checked against the stream directly: after a roll the
    // state must be exactly one Mix on from where it started.
    for (uint8 mask : { RARITY_MASK_ALL, uint8(RarityBit(Rarity::Rare)),
                        uint8(RarityBit(Rarity::Epic) | RarityBit(Rarity::Legendary)) })
        for (uint8 tier : { 1, 45, 80 })
        {
            uint64 state = 0xABCDEFu + tier;
            uint64 const expected = Stream::Mix(state);
            (void)RollRarity(state, tier, mask);
            EXPECT_EQ(state, expected) << "tier " << tier << " mask 0x" << std::hex << unsigned(mask);
        }

    // And an empty mask consumes nothing, so a caller that guards wrongly
    // does not shift every roll after it.
    uint64 state = 42;
    (void)RollRarity(state, 1, 0);
    EXPECT_EQ(state, 42u);
}

// ---------------------------------------------------------------------------
// Gauntlet.Family.<X>.Enable, which reached Phase 5 as seven conf keys that
// nothing read. These are the tests that make them real: a family switched off
// is absent from every offer, at every tier, for every class -- including from
// the relaxed passes, which is the part that is easy to get wrong. A relaxation
// exists to fill a slot that would otherwise be empty, and the tempting shape
// is to drop every rule at the last rung; a family the realm has turned off is
// not a rule the generator may drop.
// ---------------------------------------------------------------------------

TEST(GeneratorFamilyMask, DisabledFamilyIsNeverOffered)
{
    for (uint8 f = 0; f < static_cast<uint8>(Family::MAX); ++f)
    {
        Family const off = static_cast<Family>(f);

        RegistryView view;
        view.includeUnimplemented = true;
        view.familyMask = static_cast<uint8>(FAMILY_MASK_ALL & ~FamilyBit(off));

        for (uint8 cls : CLASSES)
        {
            StubView const player(cls, 1);

            // The carried set is grown as the run would grow it, so the later
            // tiers are asked with a full set and reach the relaxed passes --
            // which is where a mask that is only checked once would leak.
            std::vector<AffixInstance> carried;
            for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
            {
                OfferSet const set = BuildOffers(7u + cls, tier, player, carried, 3, view);
                for (Offer const& o : set.offers)
                {
                    if (o.mechanic == MECHANIC_NONE)
                        continue;
                    MechanicDef const* def = FindMechanic(o.mechanic);
                    ASSERT_NE(def, nullptr);
                    EXPECT_NE(def->family, off)
                        << FamilyName(off) << " was offered at tier " << unsigned(tier)
                        << " for class " << unsigned(cls) << " with its bit clear";
                }

                if (!set.offers.empty() && set.offers[0].mechanic != MECHANIC_NONE
                    && carried.size() < MAX_CARRIED)
                {
                    AffixInstance inst;
                    inst.slot     = tier;
                    inst.mechanic = set.offers[0].mechanic;
                    inst.rank     = set.offers[0].rank;
                    inst.boon     = set.offers[0].boon;
                    inst.boonMag  = set.offers[0].boonMag;
                    if (!std::any_of(carried.begin(), carried.end(),
                                     [&](AffixInstance const& a) { return a.mechanic == inst.mechanic; }))
                        carried.push_back(inst);
                }
            }
        }
    }
}

TEST(GeneratorFamilyMask, EveryFamilyOffIsEmptyRatherThanUnfiltered)
{
    // The degenerate realm: every family off. The honest answer is nothing at
    // all, and the failure this guards against is a generator that treats an
    // empty candidate pool as "no filter" and offers the whole table.
    RegistryView view;
    view.includeUnimplemented = true;
    view.familyMask = 0;

    StubView const player(1, 1);
    std::vector<AffixInstance> const empty;

    for (uint8 tier = FIRST_TIER; tier <= 80; ++tier)
    {
        OfferSet const set = BuildOffers(99, tier, player, empty, 3, view);
        for (Offer const& o : set.offers)
            EXPECT_EQ(o.mechanic, MECHANIC_NONE)
                << "an offer survived a mask of zero at tier " << unsigned(tier);
        EXPECT_TRUE(set.relaxations & GR_NoCandidate)
            << "tier " << unsigned(tier) << " came back empty without saying so";
    }
}

TEST(GeneratorFamilyMask, TheDefaultViewAllowsEveryFamily)
{
    // FAMILY_MASK_ALL is derived from Family::MAX, so adding a family to the
    // enum widens it automatically -- and this is what catches a family added
    // to the enum but not to LoadConfig's key table, since that table has a
    // static_assert against the same enum.
    RegistryView const def;
    EXPECT_EQ(def.familyMask, FAMILY_MASK_ALL);
    for (uint8 f = 0; f < static_cast<uint8>(Family::MAX); ++f)
        EXPECT_TRUE(def.FamilyAllowed(static_cast<Family>(f)));
}
