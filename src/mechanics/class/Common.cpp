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
        constexpr uint32 FAINT_MS[MAX_RANK] = { 2000, 3000, 4000 };

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

        // ==================================================================
        // C42 - Unspent (69), all classes
        //
        // "You receive a talent point every second level. Each point you leave
        // unspent makes you 2% stronger."
        //
        // The only affix in the module that touches character building rather
        // than combat, which is why its window is early: it needs the run ahead
        // of it to mean anything.
        //
        // The decision it creates recurs every level. The 31-point capstone now
        // arrives at 60 rather than 40, so is it worth it -- or is a bank of
        // ten unspent points and the twenty percent they carry the better
        // character?
        // ==================================================================
        constexpr uint16 MECHANIC_UNSPENT = 69;

        // The card's ladder, as a fraction of the points a level would give:
        // two in three, one in two, one in three.
        constexpr uint32 GRANT_NUM[MAX_RANK] = { 2, 1, 1 };
        constexpr uint32 GRANT_DEN[MAX_RANK] = { 3, 2, 3 };

        // The card's own figure per unspent point.
        constexpr uint32 PER_POINT_PCT = 2;

        class Unspent final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_UNSPENT, "c42_unspent"), 0);
            }

            void OnTalentPoints(Ctx& ctx, uint32& points) override
            {
                uint8 const i = RankIndexOf(ctx.self);
                points = points * GRANT_NUM[i] / GRANT_DEN[i];
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;

                uint32 const bank = player->GetFreeTalentPoints();   // Player.h:1741
                if (bank == _shown)
                    return;

                _shown = bank;

                // The card asks for this by name -- "the addon shows the bank"
                // -- and it is the whole of the decision: a number the player
                // watches and chooses whether to spend.
                AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_UNSPENT, "c42_unspent"),
                                         int32(bank));
            }

            // The boon is the bank, and it is paid on everything: the card says
            // damage and healing, and healing is a heal the player receives
            // from themselves, so it goes through the two multipliers rather
            // than through AggregateFactor -- which is Player-free and cannot
            // read a talent bank.
            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                return 1.0f + Bonus(ctx);
            }

            float HealTakenMult(Ctx& ctx, Unit* healer, SpellInfo const*) override
            {
                return healer == ctx.player ? 1.0f + Bonus(ctx) : 1.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint8 const i = RankIndexOf(&self);

                return "You receive only " + std::to_string(GRANT_NUM[i]) + " talent point"
                     + (GRANT_NUM[i] == 1 ? "" : "s") + " every " + std::to_string(GRANT_DEN[i])
                     + " levels. Every point you leave unspent makes you "
                     + std::to_string(PER_POINT_PCT) + "% stronger.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                uint32 const bank = ctx.player ? ctx.player->GetFreeTalentPoints() : 0;
                return "unspent: " + std::to_string(bank) + " point(s) banked, +"
                     + std::to_string(bank * PER_POINT_PCT) + "%";
            }

        private:
            static float Bonus(Ctx& ctx)
            {
                if (!ctx.player)
                    return 0.0f;

                return float(ctx.player->GetFreeTalentPoints() * PER_POINT_PCT) / 100.0f;
            }

            uint32 _shown = 0;
        };
    }

    GAUNTLET_MECHANIC(69, Unspent);
    GAUNTLET_MECHANIC(68, Faint);
}
