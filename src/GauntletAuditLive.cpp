/*
 * mod-gauntlet - reading a live character's footprint
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAudit.h"

#include "GauntletMgr.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"

#include "Player.h"
#include "Unit.h"

#include <algorithm>

// The half of the audit that needs a world. It is a straight read -- nothing
// here writes anything, and that matters more than usual: Capture is called
// three times per mechanic, and a snapshot that perturbed what it measured
// would make every verdict after the first one a lie.

namespace Gauntlet
{
    Footprint Capture(Player* player, RunState const* run)
    {
        Footprint fp;
        if (!player)
            return fp;

        // GetAppliedAuras is a multimap keyed by spell id, so an aura applied
        // twice appears twice, which is exactly the multiset Diff wants.
        for (auto const& applied : player->GetAppliedAuras())
            fp.auras.push_back(applied.first);
        std::sort(fp.auras.begin(), fp.auras.end());

        // Ids only. The remaining duration is not recorded on purpose: it
        // decreases between two readings taken in the same command, so every
        // cooldown the character already had would report as a change.
        for (auto const& cd : player->GetSpellCooldownMap())
            fp.cooldowns.push_back(cd.first);
        std::sort(fp.cooldowns.begin(), fp.cooldowns.end());

        fp.maxHealth   = player->GetMaxHealth();
        fp.maxPower    = player->GetMaxPower(player->getPowerType());
        fp.freeTalents = player->GetFreeTalentPoints();
        fp.shapeshift  = static_cast<uint8>(player->GetShapeshiftForm());
        fp.speedRun    = player->GetSpeedRate(MOVE_RUN);
        fp.speedSwim   = player->GetSpeedRate(MOVE_SWIM);
        fp.summons     = sGauntletSummons->AliveFor(player);
        fp.carried     = run ? static_cast<uint32>(run->affixes.size()) : 0u;

        if (Scheduler const* clock = sGauntlet->ClockFor(player))
            fp.armed = static_cast<uint32>(clock->Queue().size());

        // The products, not the raw factors: what the caps let through is what
        // the character actually lives with, and a mechanic that leaves a
        // factor behind under a cap that was already clamping is not leaking
        // anything a player could feel.
        for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            fp.aggregate[k] = sGauntlet->Aggregate(player, static_cast<AggregateKind>(k));

        return fp;
    }
}
