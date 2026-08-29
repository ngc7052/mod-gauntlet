/*
 * mod-gauntlet - "you cannot use that", without a client patch
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_PERMANENT_COOLDOWN_H
#define MOD_GAUNTLET_MECHANICS_PERMANENT_COOLDOWN_H

#include "Define.h"

class Player;

// Core types by pointer only, like Nearby.h, so a class curse that includes
// this does not drag Player.h in behind it.

namespace Gauntlet
{
    // The primitive behind every class curse whose ladder ends in "you can no
    // longer use X" — design section 3's family C rule is "tax before deny",
    // and this is the deny.
    //
    // How it works, and why it needs no client patch. A spell the player has
    // trained cannot be untrained without the client noticing, but a spell on
    // cooldown is a spell the client already knows how to draw: greyed, with a
    // sweep. So the denial is a cooldown of seven days.
    // Player::AddSpellCooldown's third parameter is a *duration* in
    // milliseconds, added to the current game time
    // ($CORE/src/server/game/Entities/Player/Player.cpp:11250), and its fourth
    // is `needSendToClient` (Player.h:1825) — which is the whole trick, because
    // a cooldown the server keeps to itself greys nothing.
    //
    // Two things it is not. It is not a silence: the spell is refused by the
    // ordinary cooldown check, so the player gets the client's own "not ready
    // yet" rather than a script error. And it is not permanent in the database
    // sense — nothing is written to `character_spell_cooldown` by this module,
    // so a denial has to be re-applied on login and re-asserted while it is
    // meant to hold, which is what Hold() is for.
    namespace PermanentCooldown
    {
        // Seven days. Long enough that no run outlives it, short enough that it
        // is nowhere near overflowing the uint32 milliseconds the core stores:
        // 604,800,000 against a ceiling of 4,294,967,295.
        //
        // It is deliberately not "forever". A number the client can render is
        // worth more than an impossible one — a player who inspects the button
        // sees a cooldown, which is a thing the game already explains, rather
        // than something inexplicable.
        constexpr uint32 DURATION_MS = 7u * 24u * 60u * 60u * 1000u;

        // Denies the spell, telling the client so the button greys. Safe to
        // call when it is already denied; it simply refreshes.
        void Deny(Player* player, uint32 spellId);

        // Gives it back. Called from OnDetach, and from a rank-down if one ever
        // exists — a curse the player no longer carries must not go on biting.
        void Allow(Player* player, uint32 spellId);

        // Whether this module's denial is currently in force.
        //
        // It cannot distinguish our seven days from the spell's own cooldown,
        // and does not try: what it answers is "is there a cooldown long enough
        // that it can only be ours". A real cooldown in WotLK tops out in the
        // tens of minutes, so the threshold is a day.
        bool IsDenied(Player* player, uint32 spellId);

        // Re-applies the denial if something has cleared it, and does nothing
        // otherwise. Meant for a mechanic's OnTick, so the cheap case is one
        // map lookup.
        //
        // It is needed because plenty of ordinary play clears cooldowns:
        // Preparation, Cold Snap, a GM command, and the login path, which
        // restores only what the database holds and this module never writes.
        // Without it "you cannot use Vanish" lasts until the first Preparation.
        void Hold(Player* player, uint32 spellId);
    }
}

#endif // MOD_GAUNTLET_MECHANICS_PERMANENT_COOLDOWN_H
