/*
 * mod-gauntlet - "you cannot use that", without a client patch
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "PermanentCooldown.h"

#include "GameTime.h"
#include "ObjectGuid.h"
#include "Player.h"

#include <unordered_map>

namespace Gauntlet
{
    namespace PermanentCooldown
    {
        namespace
        {
            // Anything longer than this can only be ours; the longest ordinary
            // cooldown in 3.3.5 is well under an hour.
            constexpr uint32 OURS_ABOVE_MS = 24u * 60u * 60u * 1000u;

            // What the spell's own cooldown had left when we buried it.
            //
            // AddSpellCooldown replaces rather than stacks, so denying a spell
            // that was already on a real cooldown destroys the original end --
            // and for a long time the only honest thing to say was that
            // releasing the denial granted one early use. `.gauntlet debug
            // bench` reported it as a leak against Berserker's Bargain on every
            // warrior that had actually used Shield Wall, which is often
            // enough that "bounded over-grant" stopped being a good answer.
            //
            // So Deny remembers and Allow puts it back. Keyed by player and
            // spell; an entry lives only between one Deny and its Allow.
            std::unordered_map<ObjectGuid, std::unordered_map<uint32, uint32>> g_buried;

            uint32 NowMs() { return GameTime::GetGameTimeMS().count(); }
        }

        void Deny(Player* player, uint32 spellId)
        {
            if (!player || spellId == 0)
                return;

            // Remember the real cooldown before burying it, and only the
            // first time: Hold() calls this again whenever something clears
            // the denial, and a second pass would record our own seven days as
            // if it were the spell's.
            if (!IsDenied(player, spellId))
            {
                uint32 const remaining = player->GetSpellCooldownDelay(spellId);
                auto& forPlayer = g_buried[player->GetGUID()];
                if (remaining != 0)
                    forPlayer[spellId] = NowMs() + remaining;
                else
                    forPlayer.erase(spellId);
            }

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
            player->RemoveSpellCooldown(spellId, /*update*/ true);

            // And put back whatever the spell's own cooldown had left when Deny
            // buried it, if it has not since run out on its own.
            auto const forPlayer = g_buried.find(player->GetGUID());
            if (forPlayer == g_buried.end())
                return;

            auto const buried = forPlayer->second.find(spellId);
            if (buried == forPlayer->second.end())
                return;

            uint32 const end = buried->second;
            forPlayer->second.erase(buried);
            if (forPlayer->second.empty())
                g_buried.erase(forPlayer);

            // Signed, because the millisecond clock wraps.
            int32 const left = static_cast<int32>(end - NowMs());
            if (left > 0)
                player->AddSpellCooldown(spellId, 0, static_cast<uint32>(left), /*needSendToClient*/ true);
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
