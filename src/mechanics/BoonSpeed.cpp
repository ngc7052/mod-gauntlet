/*
 * mod-gauntlet - the movement-speed boon, actually applied
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "BoonSpeed.h"

#include "Gauntlet.h"

#include "Log.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"

namespace Gauntlet
{
    namespace
    {
        // "Surge of Speed". Read field by field out of Spell.dbc in the long
        // comment above FallingSky::Reward, which is the reasoning for this id
        // and is not repeated here: aura 31 SPELL_AURA_MOD_INCREASE_SPEED,
        // caster-targeted, class-neutral, nothing to dispel, and visible on the
        // buff frame -- which matters, because a speed change with nothing on
        // the frames is not something a player can learn from.
        constexpr uint32 SPELL_SURGE_OF_SPEED = 65828;

        // Deliberately short, and re-asserted from the tick.
        //
        // The obvious choice was PermanentCooldown's seven days, and it is
        // wrong here: the core saves a long-lived aura to `character_aura` on
        // logout, so a seven-day speed buff would survive the run that granted
        // it and follow the character around afterwards. A minute cannot -- the
        // worst case is a player who keeps the boon for up to sixty seconds
        // after losing the affix, and only if the server stops in between.
        //
        // The tooltip will read 70% because the client builds it from its own
        // copy of Spell.dbc and nothing but a client patch could change that.
        // The icon is the telegraph; the tooltip is not. Falling Sky already
        // makes this trade and says so.
        constexpr int32 HELD_MS = 60000;
    }

    namespace BoonSpeed
    {
        void Release(Player* player)
        {
            if (player)
                player->RemoveAura(SPELL_SURGE_OF_SPEED);
        }

        void Sync(Player* player, RunState const* run)
        {
            if (!player)
                return;

            uint32 total = 0;
            if (run)
                for (AffixInstance const& a : run->affixes)
                    if (a.boon == Boon::BonusMoveSpeed)
                        total += a.boonMag;

            if (total == 0)
            {
                Release(player);
                return;
            }

            if (Aura* live = player->GetAura(SPELL_SURGE_OF_SPEED))
                if (AuraEffect const* eff = live->GetEffect(EFFECT_0))
                {
                    // Falling Sky pays the same boon as a short reward for a
                    // clean dodge, and writes the same aura at a bigger number.
                    // Leave it alone while it runs rather than stamping a
                    // smaller standing value over a larger one the player has
                    // just earned; it expires and the next tick restores ours.
                    if (uint32(eff->GetAmount()) > total)
                        return;

                    // Already correct and not close to expiring. This runs at
                    // 2 Hz, so the cheap case has to be genuinely cheap and has
                    // to not re-apply the aura every time -- reapplying would
                    // reset the buff icon's sweep twice a second.
                    if (uint32(eff->GetAmount()) == total && live->GetDuration() > HELD_MS / 2)
                        return;
                }

            Aura* aura = player->AddAura(SPELL_SURGE_OF_SPEED, player);
            if (!aura)
            {
                // AddAura answers null for a spell the world does not know, a
                // dead target, or a target immune to it. The first of those is
                // a deployment fault rather than a gameplay one and is silent
                // from inside the game, so it is said once: a boon that cannot
                // be applied is a card promising something it never pays, which
                // is the exact fault this file was written to fix.
                static bool warned = false;
                if (!warned)
                {
                    warned = true;
                    LOG_ERROR("module", "Gauntlet: the movement-speed boon could not apply spell {}. "
                                        "Every card paying Boon::BonusMoveSpeed is promising a bonus "
                                        "it will not deliver.", SPELL_SURGE_OF_SPEED);
                }
                return;
            }

            // Order matters and is the order Falling Sky establishes: a second
            // application of an aura that is still up goes down the refresh
            // path, which reinstates the DBC amount and the maximum duration,
            // so anything written before AddAura would be undone.
            aura->SetMaxDuration(HELD_MS);
            aura->SetDuration(HELD_MS);

            if (AuraEffect* eff = aura->GetEffect(EFFECT_0))
                eff->ChangeAmount(static_cast<int32>(total));
        }
    }
}
