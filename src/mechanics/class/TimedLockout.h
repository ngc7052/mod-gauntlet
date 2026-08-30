/*
 * mod-gauntlet - a curse's own cooldown, which knows how to take itself back off
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_TIMED_LOCKOUT_H
#define MOD_GAUNTLET_MECHANICS_TIMED_LOCKOUT_H

#include "Define.h"

#include <vector>

class Player;

// Core types by pointer only, like PermanentCooldown.h beside it, so a class
// curse that includes this does not drag Player.h in behind it.

namespace Gauntlet
{
    // A short cooldown a curse puts on a spell the player owns, paired with the
    // only correct way to lift it again.
    //
    // Five class curses lock a group of abilities against each other: warrior
    // stances, death knight presences and wards, druid forms, Shadowform. All
    // five were written the same way, and all five released by calling
    // RemoveSpellCooldown over the whole group in OnDetach -- which also clears
    // whatever cooldown the spell was on for its own reasons. Swapping the
    // affix away mid-fight handed back a free Shield Wall, a free Icebound
    // Fortitude, a free form swap.
    //
    // `.gauntlet debug leaks` found three of the five the first time it was
    // pointed at a live character, and it found them without any of those
    // curses ever triggering: the audit attaches and detaches, so the *only*
    // thing it exercised was the unconditional clear.
    //
    // The rule is the one PermanentCooldown::IsDenied already states in its own
    // comment -- undo what you can tell is yours. There it has to be a
    // threshold, because Deny has nothing to remember. Here the lock was placed
    // by this object, so the end is known exactly and the test is exact.
    class TimedLockout
    {
    public:
        // Locks `spellId` for `ms` and remembers when that lock runs out.
        // Locking a spell this object already holds moves the end rather than
        // recording it twice.
        void Lock(Player* player, uint32 spellId, uint32 ms);

        // Clears every lock this object placed that is still running, and
        // forgets all of them.
        //
        // A lock that has already expired is deliberately left alone: it is not
        // holding anything, so anything on cooldown now got there some other
        // way and is none of this object's business.
        void ReleaseAll(Player* player);

    private:
        struct Held
        {
            uint32 spellId = 0;
            uint32 untilMs = 0;
        };

        std::vector<Held> _held;
    };
}

#endif // MOD_GAUNTLET_MECHANICS_TIMED_LOCKOUT_H
