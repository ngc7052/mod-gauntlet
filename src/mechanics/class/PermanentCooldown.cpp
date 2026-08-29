/*
 * mod-gauntlet - "you cannot use that", without a client patch
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "PermanentCooldown.h"

#include "Player.h"

namespace Gauntlet
{
    namespace PermanentCooldown
    {
        namespace
        {
            // Anything longer than this can only be ours; the longest ordinary
            // cooldown in 3.3.5 is well under an hour.
            constexpr uint32 OURS_ABOVE_MS = 24u * 60u * 60u * 1000u;
        }

        void Deny(Player* player, uint32 spellId)
        {
            if (!player || spellId == 0)
                return;

            // needSendToClient = true is the parameter that greys the button
            // (Player.h:1825). Without it the server refuses the cast and the
            // client shows a lit button that does nothing, which is the worst
            // of both: the player cannot use it and cannot see why.
            player->AddSpellCooldown(spellId, 0, DURATION_MS, /*needSendToClient*/ true);
        }

        void Allow(Player* player, uint32 spellId)
        {
            if (!player || spellId == 0)
                return;

            // update = true so the client is told as well; a button left grey
            // after the affix is gone is the same fault as one left lit while
            // it is denied.
            player->RemoveSpellCooldown(spellId, /*update*/ true);
        }

        bool IsDenied(Player* player, uint32 spellId)
        {
            if (!player || spellId == 0)
                return false;

            return player->GetSpellCooldownDelay(spellId) > OURS_ABOVE_MS;   // Player.h:1823
        }

        void Hold(Player* player, uint32 spellId)
        {
            if (!player || spellId == 0)
                return;

            if (!IsDenied(player, spellId))
                Deny(player, spellId);
        }
    }
}
