/*
 * mod-gauntlet - the upside half of an affix, and who pays it
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_BOONS_H
#define MOD_GAUNTLET_MECHANICS_BOONS_H

#include "Gauntlet.h"
#include <string>

// Deliberately free of Player.h and of every core game header, like Gauntlet.h
// itself: tests/syntax-check.sh and tests/run-tests.sh compile it, and a
// mechanic that is nothing but a coefficient can pay its boon without ever
// touching the world.
//
// The rule this header exists to serve is one line long and is the reason
// commit 04570c9 exists: **a mechanic delivers its own boon**. The aggregate
// used to pay every carried affix's boon as well, which handed the Shade a
// permanent experience multiplier on top of the Vindication its card actually
// promises. With the scalars gone there is no generically-rolled boon left at
// all -- every boon is named by MechanicDef::boon -- so the aggregate pays
// none of them and each mechanic answers for its own.

namespace Gauntlet
{
    // " In exchange, you deal 12% more damage." -- empty for Boon::None or a
    // zero magnitude. Every Describe() ends with this, which is what puts the
    // upside in front of the player at the moment they are choosing.
    //
    // Moved here from mechanics/attrition/Scalars.cpp, which Phase 2 deletes
    // with the scalars themselves; the wording is unchanged, because it is
    // what the offer line has said since Phase 0.
    std::string BoonClause(Boon b, uint8 magnitude);

    // The three boons the aggregate already has an AggregateKind for, as a
    // factor a mechanic can return straight out of IMechanic::AggregateFactor:
    //
    //     float AggregateFactor(AffixInstance const& self, AggregateKind kind)
    //         const override { return BoonFactor(self, kind); }
    //
    // That is the whole delivery for a boon whose card promises a standing
    // upside rather than a reward for engaging. The framework multiplies it
    // into the same product the curse is in and clamps once, so a boon can
    // never push a run past plan section 2.5's ceilings, and a mechanic that
    // has a curse of its own to report simply multiplies the two together.
    //
    // Returns 1.0 for every other boon: BonusMoney is paid at the loot site,
    // BonusHealing through HealTakenMult, BonusMoveSpeed as an aura, and the
    // five fixed boons above LastGenericBoon belong to phases that have not
    // landed. A mechanic naming one of those must deliver it itself.
    float BoonFactor(AffixInstance const& self, AggregateKind kind);

    // The multiplier a BonusMoney boon puts on a purse, as 1.0 + magnitude%.
    // 1.0 when the instance's boon is anything else, so a loot hook can
    // multiply unconditionally.
    float BoonMoneyMult(AffixInstance const& self);

    // The same for BonusHealing, to be returned from HealTakenMult. Healing
    // has a floor in plan section 2.5 and no ceiling, so a boost here is
    // clamped by nothing -- which is correct: the cap exists to stop curses
    // stacking, not to stop an affix keeping its promise.
    float BoonHealMult(AffixInstance const& self);
}

#endif // MOD_GAUNTLET_MECHANICS_BOONS_H
