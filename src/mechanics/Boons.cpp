/*
 * mod-gauntlet - the upside half of an affix, and who pays it
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Boons.h"

namespace Gauntlet
{
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
            case Boon::BonusLoot:       return " In exchange, things drop " + mag + "% more often.";
            default:                    return "";
        }
    }

    namespace
    {
        // Which AggregateKind a boon moves, and which way. Only three of the
        // seven generic boons name a kind the aggregate carries; the rest are
        // paid where the thing they name happens. The pairings are the ones
        // GauntletAggregate.cpp has used since Phase 0.
        bool Pays(Boon b, AggregateKind k)
        {
            switch (b)
            {
                case Boon::BonusDamage:     return k == AggregateKind::DamageDone;
                case Boon::BonusExperience: return k == AggregateKind::Experience;
                case Boon::BonusMaxHealth:  return k == AggregateKind::MaxHealth;
                default:                    return false;
            }
        }

        float Pct(uint8 magnitude) { return static_cast<float>(magnitude) / 100.0f; }
    }

    float BoonFactor(AffixInstance const& self, AggregateKind kind)
    {
        if (self.boon == Boon::None || self.boonMag == 0 || !Pays(self.boon, kind))
            return 1.0f;

        // All three of these kinds are ones a player wants larger, so the boon
        // multiplies up. DamageTaken and EnemySpeed are the two that read the
        // other way and no generic boon touches either.
        return 1.0f + Pct(self.boonMag);
    }

    float BoonMoneyMult(AffixInstance const& self)
    {
        if (self.boon != Boon::BonusMoney || self.boonMag == 0)
            return 1.0f;
        return 1.0f + Pct(self.boonMag);
    }

    float BoonHealMult(AffixInstance const& self)
    {
        if (self.boon != Boon::BonusHealing || self.boonMag == 0)
            return 1.0f;
        return 1.0f + Pct(self.boonMag);
    }

    float BoonLootMult(AffixInstance const& self)
    {
        if (self.boon != Boon::BonusLoot || self.boonMag == 0)
            return 1.0f;
        return 1.0f + Pct(self.boonMag);
    }
}
