/*
 * mod-gauntlet - taking the character away from the player, briefly
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_SELF_CONTROL_H
#define MOD_GAUNTLET_MECHANICS_SELF_CONTROL_H

#include "Define.h"

class Player;
class Unit;

namespace Gauntlet
{
    // Confuse, root, stun and flee, applied to the module's own player, on a
    // timer this module keeps.
    //
    // Several class curses in design section 3 turn a resource the player
    // manages into a moment where they briefly stop driving: C1 Red Mist at
    // full rage, C29 Cold Feet, C22 Grave Call. All of them want the same four
    // verbs and the same timer, and none of them wants an aura -- an aura would
    // need a spell id whose DBC tooltip would then describe something else.
    //
    // Unit::SetControlled(apply, state, source, isFear)
    // ($CORE/src/server/game/Entities/Unit/Unit.h:1768) is the core's own entry
    // point for all four, and it is what a real fear or stun goes through, so
    // the movement, the client's state flags and the interrupt all behave the
    // way the player already expects.
    //
    // The timer is the caller's, not this header's: a mechanic already has a
    // 500 ms OnTick and a Ctx, and a second clock hidden in a helper is a
    // second thing that can be out of step with the first. Apply(), then Tick()
    // each tick, then Release() when it runs out or when the affix detaches.
    class SelfControl
    {
    public:
        enum class Kind : uint8 { None, Confuse, Root, Stun, Flee };

        // Takes control for `ms`. Applying while something else is already
        // applied releases that one first -- two states at once is a character
        // the player cannot get back.
        void Apply(Player* player, Kind kind, uint32 ms);

        // Counts down and releases when it reaches zero. Returns true on the
        // tick that released, so a mechanic can say so.
        bool Tick(Player* player, uint32 diffMs);

        // Gives it straight back. Safe when nothing is applied, and it must be
        // called from OnDetach: an affix that is swapped away while it holds
        // the character would otherwise hold it forever.
        void Release(Player* player);

        bool   Held() const { return _kind != Kind::None; }
        Kind   Current() const { return _kind; }
        uint32 RemainingMs() const { return _leftMs; }

        // What the player should be told is happening to them, in words. Every
        // one of these is a moment the player did not choose, so every one of
        // them owes an explanation.
        static char const* Describe(Kind kind);

    private:
        Kind   _kind   = Kind::None;
        uint32 _leftMs = 0;
    };
}

#endif // MOD_GAUNTLET_MECHANICS_SELF_CONTROL_H
