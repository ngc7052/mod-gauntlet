/*
 * mod-gauntlet - per-character run state
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MGR_H
#define MOD_GAUNTLET_MGR_H

#include "Gauntlet.h"
#include <unordered_map>
#include <vector>

namespace Gauntlet
{
    struct RunState
    {
        uint32 seed        = 0;
        uint32 tier        = 0;
        bool   dead        = false;   // hardcore: run is over
        std::vector<Affix> affixes;   // one per completed tier
        std::vector<Affix> pending;   // offered, awaiting a pick
    };

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

        // Aggregated multiplier for one effect, honouring conditions.
        float Multiplier(Player* player, Effect effect) const;

        // Bots are Player objects too; the challenge is for real players only.
        bool IsEligible(Player* player) const;

        bool Enabled() const { return _enabled; }
        void LoadConfig();

        uint32 Interval() const  { return _interval; }
        uint32 Choices() const   { return _choices; }
        bool   Hardcore() const  { return _hardcore; }

    private:
        bool ConditionActive(Player* player, Condition c) const;

        std::unordered_map<ObjectGuid, RunState> _runs;
        bool   _enabled  = true;
        bool   _hardcore = true;
        uint32 _interval = 5;
        uint32 _choices  = 3;
        bool   _announce = true;
        bool   _playersOnly = true;
    };
}

#define sGauntlet Gauntlet::Mgr::instance()

#endif
