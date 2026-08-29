/*
 * mod-gauntlet - the curses that are not one class's: Faint
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "SelfControl.h"

#include "Chat.h"
#include "Player.h"

#include <string>
#include <iterator>

// Design section 3, family C. Most of the family is one class's; these are the
// ones written against a resource rather than a kit, so they belong to whoever
// has that resource.

namespace Gauntlet
{
    namespace
    {
        uint8 RankIndexOf(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // ==================================================================
        // C41 - Faint (68), all mana users
        //
        // "When your mana hits zero in combat you black out for two seconds."
        //
        // The threshold verb, on the one resource every caster watches. The
        // card's argument for it is the best in the family: running dry is the
        // most common way a levelling caster dies anyway, and this gives it a
        // tell before the death instead of after.
        //
        // The counterplay is a habit rather than a button -- wand, melee or
        // Shoot before the bar is empty -- which is why this is a curse and
        // not a tax.
        // ==================================================================
        constexpr uint16 MECHANIC_FAINT = 68;

        // The card's ladder: 2 -> 3 -> 4 seconds.
        constexpr uint32 FAINT_MS[] = { 2000, 3000, 4000 };
        static_assert(std::size(FAINT_MS) >= MAX_RANK, "FAINT_MS is short a rank");

        // The card's own once-per-ten-seconds, so a caster who is genuinely out
        // is not stun-locked by their own empty bar.
        constexpr uint32 FAINT_COOLDOWN_MS = 10000;

        class Faint final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override { _control.Release(ctx.player); }

            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // Boon::BonusRegen: "+15% mana regeneration while casting". There
            // is no hook on the five-second rule, so this is delivered as a
            // small periodic top-up while in combat, which is the same effect
            // seen from the player's side: the bar falls more slowly while
            // they are working.
            //
            // Paid on the tick rather than continuously, and only in combat,
            // because out of combat the core's own regeneration is already
            // full and adding to it would be a boon nobody notices.
            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "faint: " + std::to_string(FAINT_MS[RankIndexOf(ctx.self)] / 1000u)
                                + "s blackout";
                if (ctx.player)
                    out += ", mana " + std::to_string(ctx.player->GetPower(POWER_MANA));
                out += _control.Held() ? ", OUT COLD" : "";
                out += ", cooldown " + std::to_string(_cooldownMs / 1000u) + "s";
                return out;
            }

        private:
            SelfControl _control;
            uint32      _cooldownMs = 0;
            uint32      _regenMs    = 0;
            uint32      _times      = 0;
        };

        void Faint::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            if (_control.Tick(player, diffMs) && player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r You come round.");

            if (_cooldownMs != 0)
                _cooldownMs = _cooldownMs > diffMs ? _cooldownMs - diffMs : 0;

            if (player->getPowerType() != POWER_MANA)
                return;

            // The boon, in one-second steps so the arithmetic is honest at any
            // tick rate.
            _regenMs += diffMs;
            if (_regenMs >= 1000)
            {
                _regenMs -= 1000;

                if (ctx.self && ctx.self->boonMag != 0 && player->IsInCombat() && player->IsAlive())
                {
                    int32 const max  = int32(player->GetMaxPower(POWER_MANA));
                    int32 const now  = player->GetPower(POWER_MANA);
                    int32 const step = max * int32(ctx.self->boonMag) / 100 / 20;

                    if (step > 0 && now < max)
                        player->SetPower(POWER_MANA, now + step > max ? max : now + step);
                }
            }

            if (_control.Held() || _cooldownMs != 0)
                return;
            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
                return;
            if (ctx.run && ctx.run->dead)
                return;
            if (player->GetPower(POWER_MANA) != 0)
                return;

            _control.Apply(player, SelfControl::Kind::Stun, FAINT_MS[RankIndexOf(ctx.self)]);
            _cooldownMs = FAINT_COOLDOWN_MS;
            ++_times;

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_FAINT, "c41_faint"),
                                     FAINT_MS[RankIndexOf(ctx.self)] / 1000u, "Faint");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Empty. {}.",
                    SelfControl::Describe(SelfControl::Kind::Stun));
        }

        std::string Faint::Describe(AffixInstance const& self) const
        {
            uint32 const secs = FAINT_MS[RankIndexOf(&self)] / 1000u;
            uint32 const pct  = self.boonMag;

            std::string out = "Reaching zero mana in combat stuns you for " + std::to_string(secs)
                            + " seconds, once every 10 seconds. Wand or melee before the bar is"
                              " empty.";

            if (pct != 0)
                out += " In exchange you regain mana " + std::to_string(pct)
                     + "% faster while fighting.";

            return out;
        }

    }

    GAUNTLET_MECHANIC(68, Faint);
}
