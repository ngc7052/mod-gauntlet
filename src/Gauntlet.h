/*
 * mod-gauntlet - procedurally generated hardcore affix challenge for AzerothCore
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_H
#define MOD_GAUNTLET_H

#include "Define.h"
#include "Player.h"
#include <string>
#include <vector>

namespace Gauntlet
{
    // ---------------------------------------------------------------------
    // Effects: WHAT an affix does. Magnitude is rolled per-affix, so each
    // effect yields many distinct affixes rather than one fixed entry.
    // ---------------------------------------------------------------------
    enum class Effect : uint8
    {
        MaxHealth,          // -% maximum health
        DamageTaken,        // +% damage taken
        DamageDone,         // -% damage dealt
        HealingReceived,    // -% healing received
        HealingDone,        // -% healing dealt
        MoveSpeed,          // -% movement speed
        AttackSpeed,        // -% attack speed
        CastSpeed,          // -% cast speed
        ManaPool,           // -% maximum mana
        HealthRegen,        // -% out-of-combat health regeneration
        ExperienceGain,     // -% experience gained
        MoneyGain,          // -% money looted
        DurabilityLoss,     // +% durability loss on death/hit
        ThreatGeneration,   // +% threat generated
        MAX
    };

    // ---------------------------------------------------------------------
    // Conditions: WHEN an affix applies. This is the main multiplier on
    // variety - the same effect feels entirely different when it only bites
    // at low health, or only in dungeons, or only when you are alone.
    // ---------------------------------------------------------------------
    enum class Condition : uint8
    {
        Always,
        InCombat,
        OutOfCombat,
        BelowHalfHealth,
        AboveHalfHealth,
        WhileSolo,
        WhileGrouped,
        InDungeon,
        InOpenWorld,
        VersusElites,
        VersusPlayers,
        AtNight,
        AtDay,
        WhileMoving,
        WhileStationary,
        WhileMounted,
        MAX
    };

    // ---------------------------------------------------------------------
    // Boons: an optional upside paired with the curse. Turns a pick from
    // "least painful" into a genuine trade-off.
    // ---------------------------------------------------------------------
    enum class Boon : uint8
    {
        None,
        BonusDamage,
        BonusHealing,
        BonusMoveSpeed,
        BonusExperience,
        BonusMoney,
        BonusMaxHealth,
        BonusRegen,
        MAX
    };

    enum class Severity : uint8 { Trivial, Minor, Moderate, Major, Severe, Dire, MAX };

    struct Affix
    {
        Effect    effect      = Effect::MaxHealth;
        Condition condition   = Condition::Always;
        Boon      boon        = Boon::None;
        Severity  severity    = Severity::Minor;
        uint32    magnitude   = 0;   // percent, curse side
        uint32    boonMagnitude = 0; // percent, boon side
        uint32    id          = 0;   // deterministic id derived from the roll

        std::string Name() const;
        std::string Describe() const;
    };

    // Deterministic: the same (seed, tier) always yields the same affix, so a
    // run can be reproduced and shared by its seed.
    Affix Roll(uint32 seed, uint32 tier, uint32 rollIndex);

    // How many distinct affixes the generator can produce with current tuning.
    uint32 VariationCount();

    std::string EffectName(Effect e);
    std::string ConditionName(Condition c);
    std::string BoonName(Boon b);
    std::string SeverityName(Severity s);
}

#endif // MOD_GAUNTLET_H
