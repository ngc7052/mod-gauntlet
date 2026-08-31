/*
 * mod-gauntlet - the movement-speed boon, actually applied
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_BOON_SPEED_H
#define MOD_GAUNTLET_MECHANICS_BOON_SPEED_H

#include "Define.h"

class Player;

namespace Gauntlet
{
    struct RunState;

    // Boon::BonusMoveSpeed, delivered.
    //
    // It was not. Falling Sky implemented it for itself -- a five-second aura
    // paid for a clean dodge -- and nothing else did, so every other row that
    // named the boon printed "In exchange, you move 5% faster." on the offer
    // card and then did nothing whatsoever. Reported from play as "movement
    // speed doesn't work, it's the same", and it was made worse rather than
    // caused by the reward pass: Carrion and Keen-nosed were moved off
    // Boon::BonusMoney, which at least paid out, onto a boon that was
    // decorative.
    //
    // The delivery is the one Falling Sky worked out and documented in full --
    // spell 65828 "Surge of Speed", with the amount overwritten on the aura
    // effect -- lifted here so there is one implementation rather than a second
    // copy of that reasoning.
    //
    // Summed across the run rather than applied per card, because two cards
    // both granting speed would otherwise fight over the same aura and the
    // player would get whichever wrote last. One aura, one number, recomputed
    // from what is carried.
    namespace BoonSpeed
    {
        // Recomputes the total from `run` and puts the aura in step with it.
        // Removes it when the total is zero, so a swap that drops the last
        // speed boon takes the buff with it.
        //
        // Called from Mgr::RefreshStats, which already runs on attach, pick,
        // detach and login -- so a card added later that declares the boon is
        // paid without writing any code for it.
        void Sync(Player* player, RunState const* run);

        // Unconditional removal, for the run ending.
        void Release(Player* player);
    }
}

#endif // MOD_GAUNTLET_MECHANICS_BOON_SPEED_H
