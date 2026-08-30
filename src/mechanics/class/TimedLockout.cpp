/*
 * mod-gauntlet - a curse's own cooldown, which knows how to take itself back off
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "TimedLockout.h"

#include "GameTime.h"
#include "Player.h"

namespace Gauntlet
{
    void TimedLockout::Lock(Player* player, uint32 spellId, uint32 ms)
    {
        if (!player || spellId == 0 || ms == 0)
            return;

        // needSendToClient = true for the same reason PermanentCooldown gives:
        // a cooldown the server keeps to itself greys nothing, and the player
        // gets a lit button that refuses to work.
        player->AddSpellCooldown(spellId, 0, ms, /*needSendToClient*/ true);

        uint32 const until = GameTime::GetGameTimeMS().count() + ms;

        for (Held& h : _held)
            if (h.spellId == spellId)
            {
                h.untilMs = until;
                return;
            }

        _held.push_back({ spellId, until });
    }

    void TimedLockout::ReleaseAll(Player* player)
    {
        uint32 const now = GameTime::GetGameTimeMS().count();

        for (Held const& h : _held)
        {
            // Signed difference rather than `h.untilMs > now`: the core's
            // millisecond clock is a wrapping uint32, and an unsigned compare
            // reads every lock placed before a wrap as long expired -- which
            // would silently turn this back into the bug it exists to fix, once
            // every forty-nine days of uptime.
            if (player && static_cast<int32>(h.untilMs - now) > 0)
                player->RemoveSpellCooldown(h.spellId, /*update*/ true);
        }

        _held.clear();
    }
}
