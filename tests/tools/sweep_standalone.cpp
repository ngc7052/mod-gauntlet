/*
 * mod-gauntlet - the offer-density sweep, as a tool rather than a test
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * tests/OfferInvariantsTest.cpp runs this same simulation to assert that the
 * generator's invariants hold, and prints a census on the way past. That is the
 * right home for the assertions and the wrong home for tuning: the test fixes
 * every knob at its shipped value, because a test whose inputs move is not a
 * test.
 *
 * Tuning needs the opposite. Phase 4 ended with a question its report could not
 * answer -- tiers 78-80 come back empty for every class and every seed, and the
 * three candidate fixes are a bigger carried set, a higher rank ceiling, or a
 * shorter tier axis -- and answering it means running the same sweep with those
 * knobs at values the module does not ship. So the simulation is here a second
 * time, deliberately: it is fifty lines, it depends on nothing but the
 * generator and the registry, and the alternative is a test that takes its
 * bounds from the command line.
 *
 * It stays in the tree because the question recurs. Every phase that adds rows
 * to the table wants to know what they did to the density, and "compare against
 * the numbers in the last report" is the whole method.
 *
 * Build (no core build and no gtest needed; see README-sweep.md):
 *
 *   g++ -std=c++2a -O2 -I src -I "$CORE/src/common" \
 *       tests/tools/sweep_standalone.cpp src/GauntletGenerator.cpp \
 *       src/GauntletRegistry.cpp src/GauntletNames.cpp -o build/sweep
 */

#include "Gauntlet.h"
#include "GauntletGenerator.h"
#include "GauntletRegistry.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Gauntlet;

namespace
{
    // CLASS_WARRIOR .. CLASS_DRUID. Class 10 does not exist in 3.3.5, which is
    // why the list is spelled out rather than being 1..10.
    constexpr uint8 CLASSES[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 };
    constexpr size_t CLASS_COUNT = sizeof(CLASSES) / sizeof(CLASSES[0]);

    class StubView : public IPlayerView
    {
    public:
        StubView(uint8 cls, uint8 tree) : _class(cls), _tree(tree) { }

        uint8 GetClass() const override { return _class; }
        uint8 GetLevel() const override { return 80; }
        bool  HasSpell(uint32) const override { return true; }
        uint8 GetTalentTree() const override { return _tree; }

    private:
        uint8 _class;
        uint8 _tree;
    };

    // Both copied from tests/OfferInvariantsTest.cpp so the two sweeps walk the
    // same runs and their numbers are comparable. If either moves, both move.
    size_t PickIndex(uint32 seed, uint8 tier, size_t classIndex, size_t offers)
    {
        uint64 const mixed = Stream::Mix((static_cast<uint64>(seed) << 20)
                                       ^ (static_cast<uint64>(tier) << 8)
                                       ^ static_cast<uint64>(classIndex));
        return static_cast<size_t>(mixed % offers);
    }

    void ApplyPick(std::vector<AffixInstance>& carried, Offer const& offer, uint8 tier)
    {
        if (offer.mechanic == MECHANIC_NONE)
            return;

        if (offer.kind == OfferKind::Swap)
            for (size_t i = 0; i < carried.size(); ++i)
                if (carried[i].slot == offer.swapSlot)
                {
                    carried.erase(carried.begin() + static_cast<long>(i));
                    break;
                }

        for (AffixInstance& a : carried)
            if (a.mechanic == offer.mechanic)
            {
                a.rank      = offer.rank;
                a.condition = offer.condition;
                a.boon      = offer.boon;
                a.boonMag   = offer.boonMag;
                return;
            }

        AffixInstance instance;
        instance.mechanic   = offer.mechanic;
        instance.rank       = offer.rank;
        instance.condition  = offer.condition;
        instance.boon       = offer.boon;
        instance.boonMag    = offer.boonMag;
        instance.slot       = tier;
        instance.genVersion = GeneratorVersion;
        carried.push_back(instance);
    }

    struct Options
    {
        uint32 seeds       = 2000;
        uint8  tiers       = 80;
        uint32 choices     = 3;
        uint8  maxCarried  = MAX_CARRIED;
        bool   fullTable   = false;   // include MF_NotImplemented rows
        bool   perTier     = true;
        uint8  familyMask  = FAMILY_MASK_ALL;
    };

    struct Row
    {
        uint64 sets = 0;
        uint64 relaxed = 0;
        uint64 empty = 0;
        uint64 noReward = 0;
        uint64 carried = 0;   // summed, for the mean

        // The relaxation bits, counted apart, because "relaxed" alone says
        // nothing about which of three different problems a run is hitting.
        // This tool is what separated them, and what earned GR_NoRewardShaped
        // its own bit: the first says the table has run out, the second says
        // ten rows out of sixty-nine carry MF_RewardShaped.
        uint64 bitFamily = 0;
        uint64 bitMechanic = 0;
        uint64 bitNoCandidate = 0;
        uint64 bitNoReward = 0;

        // Sets whose only relaxation was the reward-shaped guarantee: no empty
        // slot, no repeated family, no repeated mechanic.
        uint64 rewardOnly = 0;
    };
}

int main(int argc, char** argv)
{
    Options opt;

    for (int i = 1; i < argc; ++i)
    {
        auto next = [&](char const* what) -> char const* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "%s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };

        if (!std::strcmp(argv[i], "--seeds"))            opt.seeds      = uint32(std::atoi(next("--seeds")));
        else if (!std::strcmp(argv[i], "--tiers"))       opt.tiers      = uint8(std::atoi(next("--tiers")));
        else if (!std::strcmp(argv[i], "--choices"))     opt.choices    = uint32(std::atoi(next("--choices")));
        else if (!std::strcmp(argv[i], "--max-carried")) opt.maxCarried = uint8(std::atoi(next("--max-carried")));
        else if (!std::strcmp(argv[i], "--family-mask")) opt.familyMask = uint8(std::strtoul(next("--family-mask"), nullptr, 0));
        else if (!std::strcmp(argv[i], "--full"))        opt.fullTable  = true;
        else if (!std::strcmp(argv[i], "--summary"))     opt.perTier    = false;
        else
        {
            std::fprintf(stderr,
                "usage: %s [--seeds N] [--tiers N] [--choices N] [--max-carried N]\n"
                "          [--family-mask 0xNN] [--full] [--summary]\n"
                "\n"
                "  --full     include MF_NotImplemented rows (the table as designed,\n"
                "             rather than the table as built)\n"
                "  --summary  the key tiers and the totals only\n", argv[0]);
            return 2;
        }
    }

    if (opt.tiers < 1 || opt.maxCarried < 1 || opt.choices < 1)
    {
        std::fprintf(stderr, "--tiers, --max-carried and --choices must all be at least 1\n");
        return 2;
    }

    RegistryView reg;
    reg.includeUnimplemented = opt.fullTable;
    reg.familyMask           = opt.familyMask;

    std::vector<Row> rows(size_t(opt.tiers) + 1);
    Row total;

    for (uint32 seed = 1; seed <= opt.seeds; ++seed)
        for (size_t ci = 0; ci < CLASS_COUNT; ++ci)
        {
            uint8 const cls  = CLASSES[ci];
            uint8 const tree = uint8(1 + ((seed + ci) % 3));
            StubView const view(cls, tree);

            std::vector<AffixInstance> carried;
            carried.reserve(opt.tiers);

            for (uint8 tier = FIRST_TIER; tier <= opt.tiers; ++tier)
            {
                OfferSet const set = BuildOffers(seed, tier, view, carried, opt.choices,
                                                 reg, opt.maxCarried);

                Row& row = rows[tier];
                ++row.sets;
                ++total.sets;
                row.carried += carried.size();
                total.carried += carried.size();

                if (set.relaxations != GR_None)
                {
                    ++row.relaxed;
                    ++total.relaxed;
                }
                if (set.relaxations & GR_RepeatedFamily)   { ++row.bitFamily;      ++total.bitFamily; }
                if (set.relaxations & GR_RepeatedMechanic) { ++row.bitMechanic;    ++total.bitMechanic; }
                if (set.relaxations & GR_NoCandidate)      { ++row.bitNoCandidate; ++total.bitNoCandidate; }
                if (set.relaxations & GR_NoRewardShaped)   { ++row.bitNoReward;    ++total.bitNoReward; }

                bool rewardShaped = false;
                for (Offer const& o : set.offers)
                {
                    if (o.mechanic == MECHANIC_NONE)
                    {
                        ++row.empty;
                        ++total.empty;
                        continue;
                    }
                    if (MechanicDef const* def = FindMechanic(o.mechanic))
                        if (def->flags & MF_RewardShaped)
                            rewardShaped = true;
                }
                if (!rewardShaped)
                {
                    ++row.noReward;
                    ++total.noReward;
                }

                bool anyEmpty = false;
                for (Offer const& o : set.offers)
                    if (o.mechanic == MECHANIC_NONE)
                        anyEmpty = true;
                if (!rewardShaped && !anyEmpty
                    && !(set.relaxations & (GR_RepeatedFamily | GR_RepeatedMechanic)))
                {
                    ++row.rewardOnly;
                    ++total.rewardOnly;
                }

                if (!set.offers.empty())
                    ApplyPick(carried, set.offers[PickIndex(seed, tier, ci, set.offers.size())], tier);
            }
        }

    std::printf("sweep: %u seeds x %zu classes x %u tiers = %llu sets"
                "  (choices %u, maxCarried %u, %s table, familyMask 0x%02X)\n",
                opt.seeds, CLASS_COUNT, unsigned(opt.tiers),
                static_cast<unsigned long long>(total.sets),
                opt.choices, unsigned(opt.maxCarried),
                opt.fullTable ? "full" : "live", unsigned(opt.familyMask));

    auto pct = [](uint64 n, uint64 d) { return d ? 100.0 * double(n) / double(d) : 0.0; };

    auto printRow = [&pct](unsigned tier, Row const& r) {
        std::printf("  %4u  %8llu  %7.2f%%  %8llu  %7.2f%%  %6.2f  %7.2f%%  %7.2f%%  %7.2f%%\n",
                    tier,
                    static_cast<unsigned long long>(r.sets),
                    pct(r.relaxed, r.sets),
                    static_cast<unsigned long long>(r.empty),
                    pct(r.noReward, r.sets),
                    r.sets ? double(r.carried) / double(r.sets) : 0.0,
                    pct(r.bitFamily, r.sets),
                    pct(r.bitMechanic, r.sets),
                    pct(r.rewardOnly, r.sets));
    };

    std::printf("  tier      sets   relaxed     empty  noReward  carried"
                "   family  mechanic  rwdOnly\n");
    if (opt.perTier)
    {
        for (uint8 t = FIRST_TIER; t <= opt.tiers; ++t)
            printRow(t, rows[t]);
    }
    else
    {
        for (uint8 t = FIRST_TIER; t <= opt.tiers; ++t)
            if (t == FIRST_TIER || t % 10 == 1 || t == opt.tiers)
                printRow(t, rows[t]);
    }

    std::printf("  ----  --------  --------  --------  --------  -------  --------  --------  --------\n");
    printRow(0, total);
    std::printf("total: %llu empty slots, %.2f%% of sets with no reward-shaped offer,\n"
                "       %.2f%% relaxed, of which %.2f points are the reward-shaped guarantee alone\n",
                static_cast<unsigned long long>(total.empty),
                pct(total.noReward, total.sets),
                pct(total.relaxed, total.sets),
                pct(total.rewardOnly, total.sets));
    return 0;
}
