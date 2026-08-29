/*
 * mod-gauntlet - taking the character away from the player, briefly
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SelfControl.h"

#include "Player.h"
#include "UnitDefines.h"

namespace Gauntlet
{
    namespace
    {
        UnitState StateOf(SelfControl::Kind kind)
        {
            switch (kind)
            {
                case SelfControl::Kind::Confuse: return UNIT_STATE_CONFUSED;   // UnitDefines.h:184
                case SelfControl::Kind::Root:    return UNIT_STATE_ROOT;
                case SelfControl::Kind::Stun:    return UNIT_STATE_STUNNED;
                case SelfControl::Kind::Flee:    return UNIT_STATE_FLEEING;    // UnitDefines.h:180
                default:                         return UNIT_STATE_DIED;       // never reached
            }
        }
    }

    void SelfControl::Apply(Player* player, Kind kind, uint32 ms)
    {
        if (!player || kind == Kind::None || ms == 0)
            return;

        if (_kind != Kind::None)
            Release(player);

        _kind   = kind;
        _leftMs = ms;

        // isFear is true only for the flee state, because the core uses it to
        // decide the movement generator rather than the aura: a "fear" that is
        // not a fear walks the character in the wrong way.
        player->SetControlled(true, StateOf(kind), nullptr, kind == Kind::Flee);
    }

    bool SelfControl::Tick(Player* player, uint32 diffMs)
    {
        if (_kind == Kind::None)
            return false;

        if (_leftMs > diffMs)
        {
            _leftMs -= diffMs;
            return false;
        }

        Release(player);
        return true;
    }

    void SelfControl::Release(Player* player)
    {
        if (_kind == Kind::None)
            return;

        Kind const was = _kind;
        _kind   = Kind::None;
        _leftMs = 0;

        if (player)
            player->SetControlled(false, StateOf(was), nullptr, was == Kind::Flee);
    }

    char const* SelfControl::Describe(Kind kind)
    {
        switch (kind)
        {
            case Kind::Confuse: return "you lose your bearings";
            case Kind::Root:    return "your feet will not move";
            case Kind::Stun:    return "you are stunned";
            case Kind::Flee:    return "you turn and run";
            default:            return "";
        }
    }
}
