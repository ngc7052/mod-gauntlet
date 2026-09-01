/*
 * mod-gauntlet - the commons' table: one small trade per line, no code per line
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_TRADES_H
#define MOD_GAUNTLET_TRADES_H

#include "Gauntlet.h"

#include <cstddef>

// docs/rarity-plan.md section 3: a common is a table row, not a file. Every
// Rarity::Common row in the registry is backed by the same class --
// SimpleTrade, in src/mechanics/common/ -- reading its own line of the table
// below, which is where the curse's shape, what the refusal names and what
// the card pays all live. Sixty commons is sixty lines here, not sixty
// translation units.
//
// Deliberately free of Player.h and of every core game header, like
// GauntletRules.h and for the same reason: the generator's BoonTable reads the
// boon magnitude off this table so the offer card promises exactly what the
// mechanic pays, and tests/TradesTest.cpp holds the shape of every line --
// neither of which can happen in a file the Player-free build cannot see.

namespace Gauntlet
{
    // How a common takes. Exactly one per line; what it pays is the registry
    // row's Boon, delivered through the plumbing every card already uses.
    enum class TradeCurse : uint8
    {
        DenyInventoryType,    // "You cannot wear a helm": a mask over InventoryType
        DenyWeaponSubclass,   // "You cannot wield an axe": a mask over ItemSubclassWeapon
        Coefficient           // "You take 10% more damage": one AggregateKind, moved against you
    };

    struct TradeDef
    {
        uint16        id;       // the registry row this line backs
        TradeCurse    curse;
        uint32        mask;     // the two Deny kinds: which types, one bit each
        AggregateKind kind;     // Coefficient: which product
        int32         pct;      // Coefficient: signed percent -- the sign is the way the kind gets worse
        Boon          boon;     // must be the registry row's; TradesTest holds them together
        uint8         boonPct;  // what the card pays, as BoonClause prints it
        char const*   noun;     // what a refusal or a stripping names: "helm", "axe"
        char const*   text;     // the curse, present tense, as the card reads it

        // The uncommon shape, "lose X while Y, gain Z" (docs/rarity-plan.md
        // section 2): the curse holds only while this does.
        //
        // One kind of line must NOT take a condition: a coefficient on
        // MaxHealth. Max health is a standing stat rebuilt by
        // Player::UpdateMaxHealth, and the core fires the hook this module
        // hangs on only for level and stamina changes -- so a conditional
        // pool would move at the player's next level-up rather than when the
        // condition flipped. Lone Wolf is the one card that gets away with it,
        // and only because Mgr::OnGroupChanged calls RefreshStats by hand for
        // exactly that reason. Saddle-sore was written as "25% less health
        // while mounted", measured against this, and rewritten as damage
        // taken; the core has no mount hook to refresh on. The generator
        // copies it onto the offer, Pick onto the instance, and the aggregate
        // already gates every factor on AffixInstance::condition -- so a line
        // is conditional by saying so and nothing else. The boon is not
        // gated; it is the standing half of the trade. Always for a common,
        // and TradesTest holds rarity and condition to agree.
        Condition     condition = Condition::Always;
    };

    // ItemTemplate.h's InventoryType (lines 254-285) and ItemSubclassWeapon
    // (342-365), spelled out here because that is a game header and this is
    // not one. The registry does the same for the class masks. Only what a
    // line below uses is named; a new line names what it needs.
    namespace Inv
    {
        constexpr uint32 HEAD = 1, NECK = 2, SHOULDERS = 3, CHEST = 5, WAIST = 6, LEGS = 7,
                         FEET = 8, WRISTS = 9, HANDS = 10, FINGER = 11, TRINKET = 12,
                         SHIELD = 14, CLOAK = 16;
    }
    namespace Wpn
    {
        constexpr uint32 AXE = 0, AXE2 = 1, MACE = 4, MACE2 = 5, POLEARM = 6, SWORD = 7,
                         SWORD2 = 8, STAFF = 10, FIST = 13, DAGGER = 15;
    }

    // One bit per type. Nothing in either enum reaches 32, but a template's
    // field is a uint32 the database filled in, so a value past the word is
    // "denies nothing" rather than a shift the standard leaves undefined.
    constexpr uint32 InvTypeBit(uint32 type)     { return type < 32 ? 1u << type : 0u; }
    constexpr uint32 WeaponBit(uint32 subclass)  { return subclass < 32 ? 1u << subclass : 0u; }

    // Which way each product hurts. DamageTaken and EnemySpeed are the two the
    // player wants smaller; every other kind the player wants larger. A
    // coefficient line's `pct` must point the bad way, and TradesTest holds it.
    constexpr bool WorseWhenLarger(AggregateKind kind)
    {
        return kind == AggregateKind::DamageTaken || kind == AggregateKind::EnemySpeed;
    }

    // The multiplier a coefficient line contributes to its kind. A denial
    // contributes nothing here; its cost is paid at the equipment slot.
    constexpr float TradeFactor(TradeDef const& t)
    {
        return t.curse == TradeCurse::Coefficient ? 1.0f + static_cast<float>(t.pct) / 100.0f : 1.0f;
    }

    // Not a ladder, whatever the audit's regex thinks a braced list is: every
    // line is a card, and the lines are in registry id order because the
    // registry is.
    //
    // Filed in the registry by lever -- a denial is a Rule, "a restriction on
    // what you're allowed to do rather than a number", and a coefficient trade
    // is Attrition -- and spread over two families on purpose: the offer
    // builder still wants three distinct families in a set, and a family
    // holding every common could put only one common in it.
    //
    // The boons are modest and the class-gated ones are halved again by the
    // generator's relevance discount, because a weapon denial costs a shaman
    // who fights with maces nothing. Whether the commons want a cap of their
    // own so that eight of them do not stack into a free +60% damage is the
    // plan's open question 7.1, not this table's to answer.
    inline constexpr TradeDef TRADES[] =
    {
        // -- denials: Rules --------------------------------------------------
        { 75, TradeCurse::DenyInventoryType, InvTypeBit(Inv::HEAD),    AggregateKind::MAX, 0,
          Boon::BonusMaxHealth,  5,  "helm",     "You cannot wear a helm." },
        { 76, TradeCurse::DenyInventoryType, InvTypeBit(Inv::CLOAK),   AggregateKind::MAX, 0,
          Boon::BonusMoveSpeed,  5,  "cloak",    "You cannot wear a cloak." },
        { 77, TradeCurse::DenyInventoryType, InvTypeBit(Inv::FINGER),  AggregateKind::MAX, 0,
          Boon::BonusExperience, 10, "ring",     "You cannot wear rings." },
        { 78, TradeCurse::DenyInventoryType, InvTypeBit(Inv::TRINKET), AggregateKind::MAX, 0,
          Boon::BonusDamage,     8,  "trinket",  "You cannot carry a trinket." },
        { 79, TradeCurse::DenyInventoryType, InvTypeBit(Inv::NECK),    AggregateKind::MAX, 0,
          Boon::BonusHealing,    10, "necklace", "You cannot wear anything at your neck." },
        { 80, TradeCurse::DenyWeaponSubclass, WeaponBit(Wpn::AXE) | WeaponBit(Wpn::AXE2),     AggregateKind::MAX, 0,
          Boon::BonusDamage,     10, "axe",      "You cannot wield an axe." },
        { 81, TradeCurse::DenyWeaponSubclass, WeaponBit(Wpn::SWORD) | WeaponBit(Wpn::SWORD2), AggregateKind::MAX, 0,
          Boon::BonusDamage,     10, "sword",    "You cannot wield a sword." },

        // -- coefficients: Attrition ------------------------------------------
        { 82, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  10,
          Boon::BonusDamage,     8,  "",         "You take 10% more damage." },
        { 83, TradeCurse::Coefficient, 0, AggregateKind::MaxHealth,   -10,
          Boon::BonusExperience, 10, "",         "You have 10% less health." },
        { 84, TradeCurse::Coefficient, 0, AggregateKind::HealTaken,   -15,
          Boon::BonusDamage,     8,  "",         "Healing on you is 15% weaker." },

        // -- paid in loot (docs/greed-redesign.md section 7.2) ----------------
        // The magnitudes are the plan's; nothing has measured them yet.
        { 85, TradeCurse::DenyInventoryType, InvTypeBit(Inv::WAIST),   AggregateKind::MAX, 0,
          Boon::BonusLoot,       15, "belt",     "You cannot wear a belt." },
        { 86, TradeCurse::Coefficient, 0, AggregateKind::DamageDone,  -8,
          Boon::BonusLoot,       20, "",         "You deal 8% less damage." },
        // The first uncommon: the same 10% Glass takes all day, taken only by
        // night, and paid better for the hours it costs.
        { 87, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  10,
          Boon::BonusLoot,       25, "",         "By night you take 10% more damage.",
          Condition::AtNight },

        // -- the uncommon tier proper (docs/commons.md section 3.2) ----------
        // Nine trades with a condition, which is the whole of what separates
        // the tier from a common. They exist because the tier had one card:
        // eight hypothetical uncommons took tier 1's delivered uncommon share
        // from 9% to 27% against a 25% target, and nothing else moved it.
        //
        // Sunstruck is Night Owl's twin on purpose -- together they cover the
        // clock, and a run carrying both has sold the whole day for drops.
        { 91, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  10,
          Boon::BonusLoot,       25, "",         "By day you take 10% more damage.",
          Condition::AtDay },

        // Skittish and Rooted are the same trade pointed opposite ways, which
        // is the closest a table row gets to a tempo decision: one punishes
        // the kiting the other rewards.
        { 92, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  15,
          Boon::BonusMoveSpeed,   8, "",         "While you are moving you take 15% more damage.",
          Condition::WhileMoving },
        { 93, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  15,
          Boon::BonusDamage,     10, "",         "While you stand still you take 15% more damage.",
          Condition::WhileStationary },

        // Damage taken rather than a smaller pool, for the reason written
        // against TradeDef::condition above.
        { 94, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  25,
          Boon::BonusMoveSpeed,  10, "",         "While you are mounted you take 25% more damage.",
          Condition::WhileMounted },

        { 95, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  15,
          Boon::BonusExperience, 20, "",         "While you are in a group you take 15% more damage.",
          Condition::WhileGrouped },
        { 96, TradeCurse::Coefficient, 0, AggregateKind::DamageTaken,  15,
          Boon::BonusLoot,       30, "",         "In a dungeon you take 15% more damage.",
          Condition::InDungeon },

        // The two health-gated ones pull in opposite directions on purpose:
        // Cornered pays for fighting hurt and Fresh Legs taxes fighting whole,
        // so a run carrying both is one that wants to live at half health.
        { 97, TradeCurse::Coefficient, 0, AggregateKind::HealTaken,   -25,
          Boon::BonusDamage,     15, "",         "Below half health, healing on you is 25% weaker.",
          Condition::BelowHalfHealth },
        { 98, TradeCurse::Coefficient, 0, AggregateKind::DamageDone,  -10,
          Boon::BonusMaxHealth,  10, "",         "Above half health you deal 10% less damage.",
          Condition::AboveHalfHealth },

        { 99, TradeCurse::Coefficient, 0, AggregateKind::EnemySpeed,   15,
          Boon::BonusExperience, 15, "",         "In the open world everything chasing you is 15% faster.",
          Condition::InOpenWorld },
    };

    constexpr std::size_t TRADE_COUNT = sizeof(TRADES) / sizeof(TRADES[0]);

    // The line behind a registry id, or null for a row that is not a common.
    // A linear walk: ten lines today, sixty at most, and it is called on
    // attach and on the offer path, neither of which is hot.
    constexpr TradeDef const* FindTrade(uint16 id)
    {
        for (TradeDef const& t : TRADES)
            if (t.id == id)
                return &t;
        return nullptr;
    }
}

#endif // MOD_GAUNTLET_TRADES_H
