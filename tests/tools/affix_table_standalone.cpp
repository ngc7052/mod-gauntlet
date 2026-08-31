/*
 * mod-gauntlet - the README's affix table, generated from the registry
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The table in README.md lists sixty-nine mechanics with what each one does and
 * what it pays. Written by hand it would be wrong within a phase: Phase 6 alone
 * changed eighty rank values and three maxRanks, and Phase 8 changed a boon.
 * A table that disagrees with the registry is worse than no table, because it
 * is the first thing anyone reads.
 *
 * So it is generated, the way addon/GauntletUI/Data.lua is, from the same
 * registry the offer builder draws from -- including the boon magnitudes, which
 * come from GauntletGenerator's own BoonMagnitude rather than a second copy of
 * the numbers.
 *
 * Build and regenerate (see README-affix-table.md):
 *
 *   g++ -std=c++2a -O2 -I src -I "$CORE/src/common" \
 *       tests/tools/affix_table_standalone.cpp src/GauntletGenerator.cpp \
 *       src/GauntletRegistry.cpp src/GauntletNames.cpp -o build/affix_table
 *   build/affix_table
 */

#include "Gauntlet.h"
#include "GauntletGenerator.h"
#include "GauntletRegistry.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace Gauntlet;

namespace
{
    // 3.3.5a class ids, in the order the registry's CM_ constants use.
    struct ClassBit { uint32 mask; char const* name; };
    constexpr ClassBit CLASSES[] = {
        { 1u << 0,  "Warrior"      }, { 1u << 1,  "Paladin" },
        { 1u << 2,  "Hunter"       }, { 1u << 3,  "Rogue"   },
        { 1u << 4,  "Priest"       }, { 1u << 5,  "Death Knight" },
        { 1u << 6,  "Shaman"       }, { 1u << 7,  "Mage"    },
        { 1u << 8,  "Warlock"      }, { 1u << 10, "Druid"   },
    };

    std::string ClassesOf(uint32 mask)
    {
        if (mask == 0)
            return "any";

        std::vector<char const*> hit;
        for (ClassBit const& c : CLASSES)
            if (mask & c.mask)
                hit.push_back(c.name);

        // Ten of ten is "any"; the mana-user set is worth naming rather than
        // listing, because it is a rule and not a coincidence.
        if (hit.size() == 10)
            return "any";
        if (hit.size() == 8)
            return "mana users";
        if (hit.size() == 6)
            return "melee";

        std::string out;
        for (char const* n : hit)
        {
            if (!out.empty())
                out += ", ";
            out += n;
        }
        return out;
    }

    // Short enough for a table cell. BoonClause writes a sentence, which is the
    // right shape for an offer card and the wrong one for a column.
    char const* BoonNoun(Boon b)
    {
        switch (b)
        {
            case Boon::BonusDamage:     return "damage dealt";
            case Boon::BonusHealing:    return "healing received";
            case Boon::BonusMoveSpeed:  return "move speed";
            case Boon::BonusExperience: return "experience";
            case Boon::BonusMoney:      return "gold";
            case Boon::BonusMaxHealth:  return "maximum health";
            case Boon::BonusRegen:      return "resource regeneration";
            case Boon::BonusAvoidance:  return "a chance to avoid a blow outright";
            case Boon::BonusCooldown:   return "a shorter cooldown on the ability it names";
            case Boon::BonusAbility:    return "a bespoke buff to the ability it names";
            case Boon::BonusPetDamage:  return "your pet's damage";
            case Boon::SecondLife:      return "a second life";
            default:                    return nullptr;
        }
    }

    std::string PaysOf(MechanicDef const& def)
    {
        char const* noun = BoonNoun(def.boon);
        if (!noun)
            return "&mdash;";

        uint8 const top = std::min<uint8>(def.maxRank, MAX_RANK);
        uint32 const lo = BoonMagnitude(def.id, def.boon, 1);
        uint32 const hi = BoonMagnitude(def.id, def.boon, top);

        // A boon with no magnitude is one the mechanic delivers itself and the
        // blurb describes -- a second life, a named ability's cooldown. Naming
        // it without inventing a number is the honest cell.
        if (lo == 0 && hi == 0)
            return noun;

        char buf[128];
        if (lo == hi)
            std::snprintf(buf, sizeof(buf), "+%u%% %s", lo, noun);
        else
            std::snprintf(buf, sizeof(buf), "+%u&ndash;%u%% %s", lo, hi, noun);
        return buf;
    }

    // Markdown table cells cannot contain a bare pipe.
    std::string Cell(std::string s)
    {
        std::string out;
        for (char c : s)
        {
            if (c == '|')
                out += "\\|";
            else
                out += c;
        }
        return out;
    }
}

int main()
{
    std::printf("| # | Affix | Family | Rarity | Who | Levels | Ranks | What it does to you | What it pays |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|\n");

    for (MechanicDef const& def : AllMechanics())
    {
        std::printf("| %u | **%s** | %s | %s | %s | %u&ndash;%u | %u | %s | %s |\n",
                    unsigned(def.id),
                    Cell(def.name).c_str(),
                    Cell(FamilyName(def.family)).c_str(),
                    Cell(RarityName(def.rarity)).c_str(),
                    Cell(ClassesOf(def.classMask)).c_str(),
                    unsigned(def.minTier), unsigned(def.maxTier),
                    unsigned(std::min<uint8>(def.maxRank, MAX_RANK)),
                    Cell(def.blurb).c_str(),
                    Cell(PaysOf(def)).c_str());
    }
    return 0;
}
