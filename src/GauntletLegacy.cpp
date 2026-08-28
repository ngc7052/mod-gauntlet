/*
 * mod-gauntlet - generator version 1, preserved for the one-shot migration
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletLegacy.h"
#include "GauntletGenerator.h"

#include <algorithm>
#include <array>
#include <utility>

namespace
{
    using namespace Gauntlet;

    // ------------------------------------------------------------------
    // Frozen bounds.
    //
    // Generator 1 rolled its boon and its condition as indices into the
    // shared enums, so appending a value to either one would move every
    // affix this file reproduces -- and this file reproduces the affixes
    // that live characters are already carrying. The bounds are therefore
    // literals with the generator-1 values baked in, and must never be made
    // to track Boon::MAX or Condition::MAX again: the redesign is expected
    // to want boons and conditions the shipped seven and sixteen do not
    // cover, and adding one must not rewrite a live run or invalidate
    // tests/fixtures/legacy_rolls.json.
    //
    // Effect and Severity are not frozen here: both are generator-1-only
    // vocabulary that the redesign replaces rather than extends, so they
    // have no reason to grow, and reading them from the enum keeps the copy
    // below closer to the original.
    // ------------------------------------------------------------------
    constexpr uint32 LEGACY_BOON_MAX      = 8;    // == uint8(Boon::MAX) as of generator 1
    constexpr uint32 LEGACY_CONDITION_MAX = 16;   // == uint8(Condition::MAX) as of generator 1

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

    // Local copies of Gauntlet::IsImplemented. They cannot call the shared
    // ones: those still live in GauntletAffix.cpp today and are deleted with
    // it, and what generator 1 rolled must not follow a later decision about
    // which effects and conditions are wired up.
    bool LegacyIsImplemented(Effect e)
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

    bool LegacyIsImplemented(Condition c)
    {
        // VersusElites needs the target, which ambient stat queries do not have.
        return c != Condition::VersusElites;
    }
}

namespace Gauntlet
{
    // Moved verbatim from GauntletAffix.cpp by the switchover. These two are
    // the only *Name functions that came with the legacy vocabulary; the
    // shared ConditionName and BoonName live in GauntletNames.cpp. The
    // strings are load-bearing -- tests/fixtures/legacy_rolls.json records
    // them -- so they are copied character for character.
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

    Affix LegacyRoll(uint32 seed, uint32 tier, uint32 rollIndex)
    {
        using Stream::Mix;
        using Stream::RollIn;

        // Distinct stream per (seed, tier, choice slot).
        uint64 state = Mix((static_cast<uint64>(seed) << 32)
                         ^ (static_cast<uint64>(tier) << 8)
                         ^ static_cast<uint64>(rollIndex));

        Affix a;
        do { a.effect = static_cast<Effect>(RollIn(state, 0, static_cast<uint32>(Effect::MAX) - 1)); }
        while (!LegacyIsImplemented(a.effect));
        do { a.condition = static_cast<Condition>(RollIn(state, 0, LEGACY_CONDITION_MAX - 1)); }
        while (!LegacyIsImplemented(a.condition));

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
            a.boon = static_cast<Boon>(RollIn(state, 1, LEGACY_BOON_MAX - 1));
            a.boonMagnitude = std::max<uint32>(1u, a.magnitude * RollIn(state, 40, 80) / 100u);
        }

        a.id = static_cast<uint32>(Mix(state) & 0x7FFFFFFFu);
        return a;
    }
}
