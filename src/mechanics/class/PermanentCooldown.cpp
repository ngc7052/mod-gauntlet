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

            // Only our own denial, never a cooldown the spell came by
            // honestly. This used to clear unconditionally, and the cost was a
            // free Shield Wall -- or Vanish, or Blink -- every time the affix
            // came off while the spell happened to be on its real cooldown.
            //
            // `.gauntlet debug leaks` found it on a warrior bot that had been
            // fighting: Berserker's Bargain detached and reported
            // "spell 871 had its cooldown cleared and not restored". It is the
            // same fault Iron Discipline had, in the shared helper rather than
            // in one curse, and it is worth saying plainly that phase 9 read
            // all four PermanentCooldown users by hand and called them clean.
            // They *are* symmetric -- Deny pairs with Allow on every one of
            // them -- and reading could not see this, because what makes it a
            // bug is a cooldown that was already there.
            if (!IsDenied(player, spellId))
                return;

            // update = true so the client is told as well; a button left grey
            // after the affix is gone is the same fault as one left lit while
            // it is denied.
            //
            // What this cannot do is put back a real cooldown that Deny
            // overwrote. AddSpellCooldown replaces rather than stacks, so the
            // original end is gone the moment the denial lands. Releasing a
            // denial therefore still grants at most one early use of a spell
            // that was mid-cooldown when the curse arrived -- a bounded
            // over-grant, and a smaller one than clearing every real cooldown
            // on every detach.
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
