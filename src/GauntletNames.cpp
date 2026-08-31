/*
 * mod-gauntlet - the player-facing names of the shared vocabulary
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Gauntlet.h"

// Condition and Boon are the two axes both generators share, so their names
// outlived GauntletAffix.cpp and had to land somewhere that is free of
// Player.h: the offer and pick chat lines read them, the addon exporter will,
// tests/tools/dump_legacy_rolls.cpp does, and the strings below are already
// recorded in tests/fixtures/legacy_rolls.json. They are copied character for
// character from the file the switchover deleted; changing one renames an
// affix a live character is carrying.
//
// FamilyName and OfferKindName join them because they belong to the same kind
// of thing -- a name for an enum in Gauntlet.h that several translation units
// want -- and because Gauntlet.h has declared both since step 0 with nothing
// defining them.

namespace Gauntlet
{
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

    // A boon is a word the player wears in front of the affix's name --
    // "Wrathful Desperate Exposed" -- so every one of these is an adjective
    // and none of them is a category label. The five below BonusRegen were
    // added in Phase 1 for the cards the original seven could not express;
    // they name the shape of the gift rather than its size, because their
    // magnitude is bespoke and lives in the mechanic's blurb.
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
            case Boon::BonusAvoidance:  return "Evasive";
            case Boon::BonusCooldown:   return "Relentless";
            case Boon::BonusAbility:    return "Honed";
            case Boon::BonusPetDamage:  return "Savage";
            case Boon::SecondLife:      return "Deathless";
            default:                    return "";
        }
    }

    // The seven families of the redesign, in the order Data.lua's table
    // indexes them (CONTRACT section 11.1).
    std::string FamilyName(Family f)
    {
        switch (f)
        {
            case Family::Spawn:     return "Spawn";
            case Family::Enemy:     return "Enemy";
            case Family::Tempo:     return "Tempo";
            case Family::Attrition: return "Attrition";
            case Family::Rules:     return "Rules";
            case Family::Bargain:   return "Bargain";
            case Family::Class:     return "Class";
            default:                return "Unknown";
        }
    }

    // The client's own item-quality words, one step up: a common card is the
    // client's Common (white), not its Poor (grey). Every player already reads
    // these five colours off item links, which is the whole reason to borrow
    // them rather than invent a palette -- an offer card is telling the player
    // how much of the run it will change, and blue-means-rare needs no legend.
    std::string RarityName(Rarity r)
    {
        switch (r)
        {
            case Rarity::Common:    return "Common";
            case Rarity::Uncommon:  return "Uncommon";
            case Rarity::Rare:      return "Rare";
            case Rarity::Epic:      return "Epic";
            case Rarity::Legendary: return "Legendary";
            default:                return "Unknown";
        }
    }

    // ITEM_QUALITY_COLORS[1..5] of the 3.3.5 client, as the six hex digits a
    // "|cff" colour code takes. Common is white rather than the grey of a poor
    // item on purpose: white is the colour of a thing that is ordinary, grey is
    // the colour of a thing that is worthless, and a common card is the first
    // and not the second.
    char const* RarityColor(Rarity r)
    {
        switch (r)
        {
            case Rarity::Common:    return "ffffff";
            case Rarity::Uncommon:  return "1eff00";
            case Rarity::Rare:      return "0070dd";
            case Rarity::Epic:      return "a335ee";
            case Rarity::Legendary: return "ff8000";
            default:                return "ffffff";
        }
    }

    std::string OfferKindName(OfferKind k)
    {
        switch (k)
        {
            case OfferKind::New:     return "New";
            case OfferKind::Swap:    return "Swap";
            case OfferKind::Bargain: return "Bargain";
            default:                 return "Unknown";
        }
    }

    // Moved here from GauntletCommands.cpp, where a comment had been asking
    // for the move since the switchover: the six labels were a file-local
    // static in the command file, and the audit in GauntletAudit.cpp is the
    // second caller that would otherwise have had to copy them. Two copies of
    // a label the player reads is how the two drift.
    std::string AggregateKindName(AggregateKind kind)
    {
        switch (kind)
        {
            case AggregateKind::DamageTaken: return "damage taken";
            case AggregateKind::DamageDone:  return "damage done";
            case AggregateKind::HealTaken:   return "healing taken";
            case AggregateKind::MaxHealth:   return "max health";
            case AggregateKind::EnemySpeed:  return "enemy speed";
            case AggregateKind::Experience:  return "experience";
            default:                         return "unknown";
        }
    }
}
