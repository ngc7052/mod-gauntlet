/*
 * mod-gauntlet - dumps every Gauntlet::LegacyRoll() output for a fixed seed/tier/i
 * cross product as JSON, so the redesign's LegacyRoll can be checked against
 * what the shipped generator actually produced.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletLegacy.h"
#include <array>
#include <cstdio>
#include <string>

namespace
{
    std::string JsonEscape(std::string const& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                default:   out += c; break;
            }
        }
        return out;
    }
}

int main()
{
    // Every seed/tier/roll combination the fixture is expected to cover.
    // Kept in sync with tests/tools/README.md.
    constexpr std::array<uint32, 6> SEEDS = { 1, 7, 42, 1337, 65535, 2147483647 };
    constexpr uint32 MIN_TIER = 1;
    constexpr uint32 MAX_TIER = 16;
    constexpr uint32 ROLLS_PER_TIER = 3;

    bool firstRow = true;
    std::printf("[\n");
    for (uint32 seed : SEEDS)
    {
        for (uint32 tier = MIN_TIER; tier <= MAX_TIER; ++tier)
        {
            for (uint32 i = 0; i < ROLLS_PER_TIER; ++i)
            {
                Gauntlet::Affix a = Gauntlet::LegacyRoll(seed, tier, i);

                if (!firstRow)
                    std::printf(",\n");
                firstRow = false;

                std::printf("  {\n");
                std::printf("    \"seed\": %u,\n", seed);
                std::printf("    \"tier\": %u,\n", tier);
                std::printf("    \"i\": %u,\n", i);
                std::printf("    \"effect\": %u,\n", static_cast<unsigned>(a.effect));
                std::printf("    \"effectName\": \"%s\",\n", JsonEscape(Gauntlet::EffectName(a.effect)).c_str());
                std::printf("    \"condition\": %u,\n", static_cast<unsigned>(a.condition));
                std::printf("    \"conditionName\": \"%s\",\n", JsonEscape(Gauntlet::ConditionName(a.condition)).c_str());
                std::printf("    \"severity\": %u,\n", static_cast<unsigned>(a.severity));
                std::printf("    \"severityName\": \"%s\",\n", JsonEscape(Gauntlet::SeverityName(a.severity)).c_str());
                std::printf("    \"magnitude\": %u,\n", a.magnitude);
                std::printf("    \"boon\": %u,\n", static_cast<unsigned>(a.boon));
                std::printf("    \"boonName\": \"%s\",\n", JsonEscape(Gauntlet::BoonName(a.boon)).c_str());
                std::printf("    \"boonMagnitude\": %u,\n", a.boonMagnitude);
                std::printf("    \"id\": %u\n", a.id);
                std::printf("  }");
            }
        }
    }
    std::printf("\n]\n");
    return 0;
}
