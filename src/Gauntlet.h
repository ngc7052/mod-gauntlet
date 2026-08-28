/*
 * mod-gauntlet - procedurally generated hardcore affix challenge for AzerothCore
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_H
#define MOD_GAUNTLET_H

#include "Define.h"
#include <string>
#include <vector>

// This header is deliberately free of Player.h and every other core game
// header: the registry, the generator and the aggregate maths are compiled
// into unit_tests as well as into worldserver, and the test build has no
// game objects. Anything that needs a Player belongs in GauntletMgr.h or in
// a mechanic implementation.

namespace Gauntlet
{
    // =====================================================================
    // Legacy vocabulary (generator version 1).
    //
    // Kept verbatim so pre-redesign runs can be migrated. Effect/Severity
    // and the Affix struct move to GauntletLegacy.h once the new dispatch
    // replaces them; Condition and Boon below are shared by both models.
    // =====================================================================
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

    // ---------------------------------------------------------------------
    // Conditions: WHEN an affix applies. Shared by the legacy scalars and by
    // the redesign's Scalar mechanics.
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
    // Boons: the upside paired with a curse. One fixed type per mechanic in
    // the redesign; magnitude comes from the rank.
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

    // Only effects and conditions with a working implementation are rolled.
    bool IsImplemented(Effect e);
    bool IsImplemented(Condition c);

    // Deterministic: the same (seed, tier) always yields the same affix.
    Affix Roll(uint32 seed, uint32 tier, uint32 rollIndex);

    uint32 VariationCount();

    std::string EffectName(Effect e);
    std::string ConditionName(Condition c);
    std::string BoonName(Boon b);
    std::string SeverityName(Severity s);

    // =====================================================================
    // Redesign vocabulary (generator version 2).
    // =====================================================================

    // Families group mechanics for the offer builder's caps and for the
    // "three distinct families per offer" rule.
    enum class Family : uint8 { Spawn, Enemy, Tempo, Attrition, Rules, Bargain, Class, MAX };

    enum MechanicFlags : uint32
    {
        MF_None         = 0,
        MF_Timed        = 1u << 0,   // uses the scheduler clock; counts toward the event budget
        MF_OnKill       = 1u << 1,   // family cap "on-kill"
        MF_Stalker      = 1u << 2,   // family cap "stalker": one per run
        MF_RoleTax      = 1u << 3,   // cap: one per run (Cunning, Falter)
        MF_Scalar       = 1u << 4,   // takes a Condition from the condition axis
        MF_RewardShaped = 1u << 5,   // satisfies "one reward-shaped offer per tier"
        MF_NotImplemented = 1u << 31
    };

    // What an offer slot is asking the player to do.
    enum class OfferKind : uint8 { New, RankUp, Swap, Bargain };

    // Reserved: no mechanic. Registry ids start at 1.
    constexpr uint16 MECHANIC_NONE = 0;

    // Registry ids that survive only for migrated legacy runs.
    constexpr uint16 MECHANIC_WITHERING = 72;
    constexpr uint16 MECHANIC_FORGETFUL = 73;

    // The generator version folded into the offer stream. Bump whenever the
    // registry table, the family weights or the offer algorithm change; runs
    // created under an older version keep their stored columns untouched.
    constexpr uint16 GeneratorVersion = 2;

    constexpr uint8 MAX_RANK = 3;

    class IMechanic;   // GauntletMechanic.h

    // One carried affix. `impl` is owned by the RunState that holds this
    // instance and is created from the registry on attach.
    struct AffixInstance
    {
        uint16     mechanic  = MECHANIC_NONE;
        uint8      rank      = 1;
        Condition  condition = Condition::Always;
        Boon       boon      = Boon::None;
        uint8      boonMag   = 0;
        uint8      slot      = 0;          // the tier at which it was taken
        uint16     genVersion = 0;         // generator version that produced it

        // Generator 1 expressed a curse as a free percentage (2..115); the
        // redesign has only a three-step rank. A migrated affix keeps its exact
        // number here rather than being rounded onto a rank, which would change
        // a live player's run. 0 means "take the strength from the rank", which
        // is every generator 2 row. Mirrors gauntlet_affix.legacy_mag.
        uint16     legacyMag = 0;

        IMechanic* impl      = nullptr;    // owned by RunState
    };

    // One line of a tier offer.
    struct Offer
    {
        uint16    mechanic  = MECHANIC_NONE;
        uint8     rank      = 1;
        Condition condition = Condition::Always;
        Boon      boon      = Boon::None;
        uint8     boonMag   = 0;
        OfferKind kind      = OfferKind::New;
        uint8     swapSlot  = 0;           // meaningful only when kind == Swap
    };

    // What Mgr::Aggregate can be asked for. Every kind is a multiplier
    // applied to a base value; the caps in §2.5 of the plan clamp the
    // product, not the individual contributions.
    enum class AggregateKind : uint8
    {
        DamageTaken,
        DamageDone,
        HealTaken,
        MaxHealth,
        EnemySpeed,
        Experience,
        MAX
    };

    // Clamps on the aggregate product. Defaults are the plan's §2.5 values;
    // every field is overridable from mod_gauntlet.conf.
    struct AggregateCaps
    {
        float damageTakenMin = 1.0f;
        float damageTakenMax = 2.0f;
        float damageDoneMin  = 0.6f;
        float healTakenMin   = 0.5f;
        float maxHealthMin   = 0.6f;
        float enemySpeedMax  = 1.4f;
    };

    // The generator, the registry and the aggregate maths never see a
    // Player: they see this. GauntletMgr supplies the live implementation,
    // the unit tests supply a stub.
    class IPlayerView
    {
    public:
        virtual ~IPlayerView() = default;

        virtual uint8  GetClass() const = 0;               // CLASS_WARRIOR ... CLASS_DRUID
        virtual uint8  GetLevel() const = 0;
        virtual bool   HasSpell(uint32 spellId) const = 0; // Player::HasSpell
        virtual uint8  GetTalentTree() const = 0;          // Player::GetMostPointsTalentTree

        // 1 << (class - 1), the core's getClassMask() convention.
        uint32 GetClassMask() const { return 1u << (GetClass() - 1); }
    };

    std::string FamilyName(Family f);
    std::string OfferKindName(OfferKind k);
}

#endif // MOD_GAUNTLET_H
