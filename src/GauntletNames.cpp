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

    std::string OfferKindName(OfferKind k)
    {
        switch (k)
        {
            case OfferKind::New:     return "New";
            case OfferKind::RankUp:  return "Rank up";
            case OfferKind::Swap:    return "Swap";
            case OfferKind::Bargain: return "Bargain";
            default:                 return "Unknown";
        }
    }
}
