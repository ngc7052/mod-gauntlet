/*
 * mod-gauntlet - affix generator
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Gauntlet.h"
#include <array>
#include <cctype>

namespace Gauntlet
{
    namespace
    {
        // splitmix64: small, fast, and identical on every platform, so a seed
        // reproduces the same run anywhere. Deliberately NOT std::rand.
        uint64 Mix(uint64 x)
        {
            x += 0x9E3779B97F4A7C15ULL;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
            return x ^ (x >> 31);
        }

        uint32 RollIn(uint64& state, uint32 lo, uint32 hi)
        {
            state = Mix(state);
            return lo + static_cast<uint32>(state % (hi - lo + 1));
        }

        // Curse magnitude bands per severity: {min%, max%}
        constexpr std::array<std::pair<uint32, uint32>, 6> SEVERITY_BANDS = { {
            {  2,  4 },   // Trivial
            {  5,  9 },   // Minor
            { 10, 16 },   // Moderate
            { 17, 25 },   // Major
            { 26, 36 },   // Severe
            { 37, 50 },   // Dire
        } };

        // Conditional affixes hit less often, so they roll harder numbers.
        uint32 ConditionWeight(Condition c)
        {
            switch (c)
            {
                case Condition::Always:          return 100;
                case Condition::InCombat:        return 130;
                case Condition::OutOfCombat:     return 190;
                case Condition::BelowHalfHealth: return 210;
                case Condition::AboveHalfHealth: return 140;
                case Condition::WhileSolo:       return 150;
                case Condition::WhileGrouped:    return 170;
                case Condition::InDungeon:       return 180;
                case Condition::InOpenWorld:     return 120;
                case Condition::VersusElites:    return 200;
                case Condition::VersusPlayers:   return 220;
                case Condition::AtNight:         return 175;
                case Condition::AtDay:           return 175;
                case Condition::WhileMoving:     return 145;
                case Condition::WhileStationary: return 195;
                case Condition::WhileMounted:    return 230;
                default:                         return 100;
            }
        }
    }


    bool IsImplemented(Effect e)
    {
        switch (e)
        {
            case Effect::DamageTaken:
            case Effect::DamageDone:
            case Effect::HealingReceived:
            case Effect::ExperienceGain:
                return true;
            default:
                return false;   // vocabulary reserved, not yet wired to a hook
        }
    }

    bool IsImplemented(Condition c)
    {
        // VersusElites needs the target, which ambient stat queries do not have.
        return c != Condition::VersusElites;
    }

    std::string EffectName(Effect e)
    {
        switch (e)
        {
            case Effect::MaxHealth:        return "Brittle";
            case Effect::DamageTaken:      return "Exposed";
            case Effect::DamageDone:       return "Feeble";
            case Effect::HealingReceived:  return "Withering";
            case Effect::HealingDone:      return "Faithless";
            case Effect::MoveSpeed:        return "Leaden";
            case Effect::AttackSpeed:      return "Sluggish";
            case Effect::CastSpeed:        return "Stammering";
            case Effect::ManaPool:         return "Hollow";
            case Effect::HealthRegen:      return "Festering";
            case Effect::ExperienceGain:   return "Forgetful";
            case Effect::MoneyGain:        return "Impoverished";
            case Effect::DurabilityLoss:   return "Corroding";
            case Effect::ThreatGeneration: return "Hunted";
            default:                       return "Unknown";
        }
    }

    std::string ConditionName(Condition c)
    {
        switch (c)
        {
            case Condition::Always:          return "Everlasting";
            case Condition::InCombat:        return "Embattled";
            case Condition::OutOfCombat:     return "Restless";
            case Condition::BelowHalfHealth: return "Desperate";
            case Condition::AboveHalfHealth: return "Complacent";
            case Condition::WhileSolo:       return "Solitary";
            case Condition::WhileGrouped:    return "Codependent";
            case Condition::InDungeon:       return "Delving";
            case Condition::InOpenWorld:     return "Wandering";
            case Condition::VersusElites:    return "Outmatched";
            case Condition::VersusPlayers:   return "Rivalrous";
            case Condition::AtNight:         return "Nocturnal";
            case Condition::AtDay:           return "Sunlit";
            case Condition::WhileMoving:     return "Fleeting";
            case Condition::WhileStationary: return "Rooted";
            case Condition::WhileMounted:    return "Saddlesore";
            default:                         return "Unknown";
        }
    }

    std::string BoonName(Boon b)
    {
        switch (b)
        {
            case Boon::None:            return "";
            case Boon::BonusDamage:     return "Wrathful";
            case Boon::BonusHealing:    return "Mending";
            case Boon::BonusMoveSpeed:  return "Fleetfooted";
            case Boon::BonusExperience: return "Enlightened";
            case Boon::BonusMoney:      return "Avaricious";
            case Boon::BonusMaxHealth:  return "Stalwart";
            case Boon::BonusRegen:      return "Renewing";
            default:                    return "";
        }
    }

    std::string SeverityName(Severity s)
    {
        switch (s)
        {
            case Severity::Trivial:  return "Trivial";
            case Severity::Minor:    return "Minor";
            case Severity::Moderate: return "Moderate";
            case Severity::Major:    return "Major";
            case Severity::Severe:   return "Severe";
            case Severity::Dire:     return "Dire";
            default:                 return "Unknown";
        }
    }

    std::string Affix::Name() const
    {
        std::string n = ConditionName(condition) + " " + EffectName(effect);
        if (boon != Boon::None)
            n = BoonName(boon) + " " + n;
        return n;
    }

    std::string Affix::Describe() const
    {
        std::string what;
        switch (effect)
        {
            case Effect::DamageTaken:     what = "you take " + std::to_string(magnitude) + "% more damage"; break;
            case Effect::DamageDone:      what = "you deal " + std::to_string(magnitude) + "% less damage"; break;
            case Effect::HealingReceived: what = "healing on you is " + std::to_string(magnitude) + "% weaker"; break;
            case Effect::ExperienceGain:  what = "you gain " + std::to_string(magnitude) + "% less experience"; break;
            default:                      what = EffectName(effect) + " " + std::to_string(magnitude) + "%"; break;
        }

        std::string when;
        switch (condition)
        {
            case Condition::Always:          when = ""; break;
            case Condition::InCombat:        when = " while in combat"; break;
            case Condition::OutOfCombat:     when = " while out of combat"; break;
            case Condition::BelowHalfHealth: when = " below half health"; break;
            case Condition::AboveHalfHealth: when = " above half health"; break;
            case Condition::WhileSolo:       when = " while alone"; break;
            case Condition::WhileGrouped:    when = " while in a group"; break;
            case Condition::InDungeon:       when = " inside dungeons"; break;
            case Condition::InOpenWorld:     when = " in the open world"; break;
            case Condition::VersusPlayers:   when = " in battlegrounds and arenas"; break;
            case Condition::AtNight:         when = " at night"; break;
            case Condition::AtDay:           when = " during the day"; break;
            case Condition::WhileMoving:     when = " while moving"; break;
            case Condition::WhileStationary: when = " while standing still"; break;
            case Condition::WhileMounted:    when = " while mounted"; break;
            default:                         when = ""; break;
        }

        std::string out = what + when;
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
        out += ".";

        if (boon != Boon::None)
        {
            switch (boon)
            {
                case Boon::BonusDamage:     out += " In exchange, you deal " + std::to_string(boonMagnitude) + "% more damage."; break;
                case Boon::BonusHealing:    out += " In exchange, healing on you is " + std::to_string(boonMagnitude) + "% stronger."; break;
                case Boon::BonusExperience: out += " In exchange, you gain " + std::to_string(boonMagnitude) + "% more experience."; break;
                default:                    out += " In exchange, " + BoonName(boon) + " " + std::to_string(boonMagnitude) + "%."; break;
            }
        }

        return "[" + SeverityName(severity) + "] " + out;
    }

    Affix Roll(uint32 seed, uint32 tier, uint32 rollIndex)
    {
        // Distinct stream per (seed, tier, choice slot).
        uint64 state = Mix((static_cast<uint64>(seed) << 32)
                         ^ (static_cast<uint64>(tier) << 8)
                         ^ static_cast<uint64>(rollIndex));

        Affix a;
        do { a.effect = static_cast<Effect>(RollIn(state, 0, static_cast<uint32>(Effect::MAX) - 1)); }
        while (!IsImplemented(a.effect));
        do { a.condition = static_cast<Condition>(RollIn(state, 0, static_cast<uint32>(Condition::MAX) - 1)); }
        while (!IsImplemented(a.condition));

        // Severity drifts upward with tier but never becomes fully predictable.
        uint32 const floorSev = std::min<uint32>(tier / 4u, 3u);
        uint32 const sev      = std::min<uint32>(RollIn(state, floorSev, floorSev + 2u),
                                                 static_cast<uint32>(Severity::MAX) - 1);
        a.severity = static_cast<Severity>(sev);

        auto const& band = SEVERITY_BANDS[sev];
        uint32 base = RollIn(state, band.first, band.second);

        // Rarely-active affixes hit harder to stay relevant.
        a.magnitude = std::max<uint32>(1u, base * ConditionWeight(a.condition) / 100u);

        // Roughly one in three affixes carries a boon.
        if (RollIn(state, 0, 99) < 34)
        {
            a.boon = static_cast<Boon>(RollIn(state, 1, static_cast<uint32>(Boon::MAX) - 1));
            a.boonMagnitude = std::max<uint32>(1u, a.magnitude * RollIn(state, 40, 80) / 100u);
        }

        a.id = static_cast<uint32>(Mix(state) & 0x7FFFFFFFu);
        return a;
    }

    uint32 VariationCount()
    {
        return static_cast<uint32>(Effect::MAX)
             * static_cast<uint32>(Condition::MAX)
             * static_cast<uint32>(Severity::MAX)
             * static_cast<uint32>(Boon::MAX);
    }
}
