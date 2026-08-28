/*
 * mod-gauntlet - "once per level", counted somewhere it survives a logout
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_CHARGES_H
#define MOD_GAUNTLET_MECHANICS_CHARGES_H

#include "GauntletState.h"

// Free of Player.h and of every core game header, like Boons.h beside it: a
// charge is arithmetic over one integer in the state store, and the unit tests
// must be able to exercise it without a game world. The caller supplies the
// level; nothing here has heard of a Player.

namespace Gauntlet
{
    // Design section 3, card B1: "Once per level, a killing blow leaves you at
    // 1 health instead", laddering to once per two levels and once per three.
    // Phase 4's Ankh Pact and Stone of the Damned are the same sentence with a
    // different verb, which is why this is a shared helper and not three
    // integers inside Last Rites -- the implementation plan's Phase 3 entry
    // says the cheat-death path "must land before Ankh Pact / Stone of the
    // Damned reuse it", and this is the half of it they actually share.
    //
    // The whole state is one value: the level at which the last charge was
    // spent, 0 for "never spent". A charge is therefore not a thing that is
    // granted and stored -- which would need a grant hook on every level-up and
    // would go wrong the first time one was missed -- but a fact derived from
    // the player's current level whenever it is asked for.
    namespace Charges
    {
        // "<mechanic key>.spent". Kept under State::MaxKeyLen for every key in
        // the registry; the longest is "reinforcements", and even that plus
        // this suffix is 20 characters.
        std::string SpentKey(char const* mechanicKey);

        // Whether a charge is up. `everyLevels` is the rank's ladder: 1 for
        // once per level, 2 for once per two, 3 for once per three. Zero is
        // read as 1 rather than dividing by it.
        //
        // A stored level *above* the player's current one is treated as never
        // spent, and this is not defensive padding: `.gauntlet debug` and a GM
        // `.levelup`/`.character level` both move a character down, and Phase 2
        // spent an evening on Deep Wounds behaving strangely for exactly this
        // reason -- a value accumulated at a high level sitting permanently
        // against a low-level test. A charge spent at 60 must not be
        // unreachable forever on a character taken back to 40.
        bool Available(State const* state, char const* mechanicKey, uint8 level, uint8 everyLevels);

        // Records the spend. Idempotent within a level: spending twice at the
        // same level stores the same number.
        void Spend(State* state, char const* mechanicKey, uint8 level);

        // The level at which the next charge arrives, or 0 if one is up now.
        // For the addon counter and for `.gauntlet debug dump`, so a player
        // who has just spent one can be told when it comes back rather than
        // being left to guess.
        uint8 ReturnsAtLevel(State const* state, char const* mechanicKey, uint8 level, uint8 everyLevels);

        // Forgets the spend. Used when the mechanic is detached, so a swap
        // followed by a re-pick does not hand back a charge the run already
        // used -- and deliberately NOT called on logout, which is why detach is
        // the only caller.
        void Clear(State* state, char const* mechanicKey);
    }
}

#endif // MOD_GAUNTLET_MECHANICS_CHARGES_H
