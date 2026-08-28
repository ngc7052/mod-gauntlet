/*
 * mod-gauntlet - generator version 1, preserved for the one-shot migration
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_LEGACY_H
#define MOD_GAUNTLET_LEGACY_H

#include "Gauntlet.h"
#include <string>

namespace Gauntlet
{
    // Generator 1's whole vocabulary, moved here from Gauntlet.h by the
    // switchover so the redesign's header carries only what a live run uses.
    // Nothing but the storage migration and tests/tools/dump_legacy_rolls.cpp
    // may include this file. Condition and Boon are not here: both models
    // share them and they stayed in Gauntlet.h.
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

    enum class Severity : uint8 { Trivial, Minor, Moderate, Major, Severe, Dire, MAX };

    // One generator-1 affix. Name() and Describe() are gone with the move:
    // they composed a name out of Effect and a "[Severity]" prefix that the
    // redesign's schema does not store, and their only callers were the
    // offer and pick chat lines, which now read the registry instead.
    struct Affix
    {
        Effect    effect        = Effect::MaxHealth;
        Condition condition     = Condition::Always;
        Boon      boon          = Boon::None;
        Severity  severity      = Severity::Minor;
        uint32    magnitude     = 0;   // percent, curse side
        uint32    boonMagnitude = 0;   // percent, boon side
        uint32    id            = 0;   // deterministic id derived from the roll
    };

    std::string EffectName(Effect e);
    std::string SeverityName(Severity s);

    // Generator version 1's roll, moved here unchanged from GauntletAffix.cpp.
    //
    // Nothing in a live run calls this. Its only caller is the migration that
    // converts a pre-redesign gauntlet_affix row -- which stored no more than
    // (tier, roll index) -- back into the affix that character has been
    // playing with. A change of a single arithmetic step here silently
    // rewrites those runs, so the function is frozen: tests/fixtures/
    // legacy_rolls.json is the record of what it produced before the rewrite
    // and is the thing to trust if the two ever disagree.
    Affix LegacyRoll(uint32 seed, uint32 tier, uint32 rollIndex);
}

#endif // MOD_GAUNTLET_LEGACY_H
