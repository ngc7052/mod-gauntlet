/*
 * mod-gauntlet - per-character run state
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MGR_H
#define MOD_GAUNTLET_MGR_H

#include "Gauntlet.h"
#include "GauntletAggregate.h"
#include "Player.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Gauntlet
{
    // RunState moved to Gauntlet.h with the switchover: plan section 2.6 puts
    // it there, and GauntletMechanic.h's Ctx needs it without dragging
    // Player.h into every mechanic. Its member functions are defined in
    // GauntletMgr.cpp, which is the only place IMechanic is complete.

    class Mgr
    {
    public:
        static Mgr* instance();

        void Load(Player* player);
        void Save(Player* player);
        void Forget(ObjectGuid guid);

        RunState* Get(Player* player);

        void OfferTier(Player* player, uint32 tier);
        bool Pick(Player* player, uint32 index);
        void EndRun(Player* player, std::string const& cause);

        // Fires IMechanic::OnDetach over the carried set without destroying
        // anything; ~RunState does the freeing when Forget drops the run.
        void DetachAll(Player* player);

        // The product of every active affix's factor, clamped by the config's
        // caps. Replaces Multiplier: the old one summed percentages and
        // floored the result, this multiplies factors and clamps the product.
        // The conditions are evaluated here, against the live player, and
        // handed to the Player-free maths in GauntletAggregate.h.
        float Aggregate(Player* player, AggregateKind kind) const;

        // The death sequence (plan section 6, decision 5). Dying arms a timer
        // instead of ending the run outright, so a Phase 3 bargain charge has
        // somewhere to intervene; releasing or letting the timer expire ends
        // it. Phase 0 has no charge to spend, so every armed death ends the
        // run exactly as it did before.
        void BeginPendingDeath(Player* player);
        bool CancelPendingDeath(Player* player);
        void UpdateDeathTimer(Player* player, uint32 diffMs);
        bool AnyPendingDeath() const { return _pendingDeaths != 0; }
        bool IsPendingDeath(Player* player) const;

        // One-shot conversion of a pre-redesign install, run from the
        // worldserver's OnStartup. Detects the legacy gauntlet_affix.roll
        // column, unrolls every row through LegacyRoll into the new columns,
        // then drops roll and tier. A no-op once the column is gone.
        void MigrateLegacyRuns();

        // Bots are Player objects too; the challenge is for real players only.
        bool IsEligible(Player* player) const;

        bool Enabled() const { return _enabled; }
        void LoadConfig();

        uint32 Interval() const  { return _interval; }
        uint32 Choices() const   { return _choices; }
        bool   Hardcore() const  { return _hardcore; }

        AggregateCaps const& Caps() const { return _caps; }

        // The offer's player-facing name, built the way Affix::Name() built
        // one: the condition's adjective in front of the mechanic's name,
        // with the boon's adjective in front of that. Public because
        // GauntletScripts.cpp prints carried affixes too.
        std::string NameOf(uint16 mechanic, Condition condition, Boon boon) const;
        std::string DescribeOf(AffixInstance const& instance) const;

    private:
        bool ConditionActive(Player* player, Condition c) const;

        // Fills every slot of AggregateInput::conditionActive for `player`.
        void FillConditions(Player* player, AggregateInput& in) const;

        std::unordered_map<ObjectGuid, RunState> _runs;
        AggregateCaps _caps;
        bool   _enabled  = true;
        bool   _hardcore = true;
        uint32 _interval = 5;
        uint32 _choices  = 3;
        bool   _announce = true;
        bool   _playersOnly = true;
        uint32 _graceMs  = 60000;

        // How many loaded runs are counting down. OnPlayerUpdate runs for
        // every player on every tick, so the common case has to be an integer
        // test rather than a hash lookup.
        uint32 _pendingDeaths = 0;
    };
}

#define sGauntlet Gauntlet::Mgr::instance()

#endif
