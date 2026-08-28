/*
 * mod-gauntlet - the shape shared by the four legacy scalars, and the factory
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Scalars.h"
#include <algorithm>
#include <cctype>

namespace Gauntlet
{
    namespace
    {
        // Mgr::Multiplier floored the whole aggregate at 0.05 so that a curse
        // above 100% could not invert the multiplier. The redesign's caps
        // replace that floor for four of the six kinds, but Experience is
        // uncapped by plan section 2.5 and a generator-1 magnitude reaches 115
        // (a Dire roll on a rarely-active condition), so the floor is kept here
        // per contribution. Without it a migrated Saddlesore Forgetful would
        // drop from today's 5% experience to none at all, which ends a run.
        // The rank ladder never comes near it.
        constexpr float LEGACY_FACTOR_FLOOR = 0.05f;   // TODO(design)
    }

    float ScalarMechanic::AggregateFactor(AffixInstance const& self, AggregateKind kind) const
    {
        if (kind != _kind)
            return 1.0f;

        float const pct = static_cast<float>(ScalarMagnitude(self)) / 100.0f;
        return _raises ? (1.0f + pct) : std::max(LEGACY_FACTOR_FLOOR, 1.0f - pct);
    }

    std::string ScalarMechanic::Describe(AffixInstance const& self) const
    {
        std::string out = std::string(_lead) + std::to_string(ScalarMagnitude(self)) + _tail
                        + ConditionClause(self.condition) + ".";
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));

        out += BoonClause(self.boon, self.boonMag);
        return out;
    }

    std::string ConditionClause(Condition c)
    {
        switch (c)
        {
            case Condition::Always:          return "";
            case Condition::InCombat:        return " while in combat";
            case Condition::OutOfCombat:     return " while out of combat";
            case Condition::BelowHalfHealth: return " below half health";
            case Condition::AboveHalfHealth: return " above half health";
            case Condition::WhileSolo:       return " while alone";
            case Condition::WhileGrouped:    return " while in a group";
            case Condition::InDungeon:       return " inside dungeons";
            case Condition::InOpenWorld:     return " in the open world";
            case Condition::VersusElites:    return " from elites";
            case Condition::VersusPlayers:   return " in battlegrounds and arenas";
            case Condition::AtNight:         return " at night";
            case Condition::AtDay:           return " during the day";
            case Condition::WhileMoving:     return " while moving";
            case Condition::WhileStationary: return " while standing still";
            case Condition::WhileMounted:    return " while mounted";
            default:                         return "";
        }
    }

    std::string BoonClause(Boon b, uint8 magnitude)
    {
        if (b == Boon::None || magnitude == 0)
            return "";

        std::string const mag = std::to_string(static_cast<uint32>(magnitude));
        switch (b)
        {
            case Boon::BonusDamage:     return " In exchange, you deal " + mag + "% more damage.";
            case Boon::BonusHealing:    return " In exchange, healing on you is " + mag + "% stronger.";
            case Boon::BonusMoveSpeed:  return " In exchange, you move " + mag + "% faster.";
            case Boon::BonusExperience: return " In exchange, you gain " + mag + "% more experience.";
            case Boon::BonusMoney:      return " In exchange, you loot " + mag + "% more money.";
            case Boon::BonusMaxHealth:  return " In exchange, you have " + mag + "% more health.";
            case Boon::BonusRegen:      return " In exchange, you recover " + mag + "% faster.";
            default:                    return "";
        }
    }

    // Registry id to implementation. A switch rather than a table because the
    // ids are sparse and this is read once per affix on attach, never on the
    // damage path. Everything the registry marks MF_NotImplemented answers
    // nullptr, and so does an id from a registry this build has never seen.
    //
    // Withering and Forgetful are here despite being MF_NotImplemented: the
    // flag stops the generator offering them again, it does not stop a
    // migrated run carrying one, and such a run must keep working.
    //
    // Phase 1 moves this to its own translation unit once mechanics exist
    // outside this family; in Phase 0 all four are attrition scalars, so it
    // sits beside them.
    IMechanic* MakeMechanic(uint16 id)
    {
        switch (id)
        {
            case 21:                 return MakeExposed();
            case 22:                 return MakeFeeble();
            case MECHANIC_WITHERING: return MakeWithering();
            case MECHANIC_FORGETFUL: return MakeForgetful();
            default:                 return nullptr;
        }
    }
}
