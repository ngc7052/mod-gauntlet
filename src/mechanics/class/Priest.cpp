/*
 * mod-gauntlet - the priest's two: Frail Soul, Whispers of the Deep
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "AuraDurationEdit.h"
#include "SelfControl.h"

#include "Chat.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <string>

// Design section 3, family C, priest. Both of these are about the moment
// before the mistake rather than the mistake: one makes the shield a decision
// about *when*, the other makes the low-health line a place you must not reach
// rather than a place you recover from.

namespace Gauntlet
{
    namespace
    {
        constexpr uint32 SPELL_WEAKENED_SOUL = 6788;
        constexpr uint32 SPELL_RENEW         = 139;
        constexpr uint32 SPELL_FEAR_WARD     = 6346;

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
        // C17 - Frail Soul (44)
        //
        // "Weakened Soul lasts 30 seconds."
        //
        // The shortcut verb, and the smallest change in wave A: one aura's
        // duration. What it costs is the reflex -- Power Word: Shield before
        // every pull -- and what it buys is a decision, which is pre-shield and
        // pull fast, or save it for the second mob and open with Renew.
        // ==================================================================
        constexpr uint16 MECHANIC_FRAIL_SOUL = 44;

        // The card's ladder: 20 -> 30 -> 45 seconds.
        constexpr int32 WEAKENED_MS[MAX_RANK] = { 20000, 30000, 45000 };

        class FrailSoul final : public IMechanic
        {
        public:
            void OnAuraApplied(Ctx& ctx, Unit* target, Aura* aura) override
            {
                if (!AuraDurationEdit::Matches(target, aura, ctx.player, SPELL_WEAKENED_SOUL))
                    return;

                AuraDurationEdit::Edit(aura, WEAKENED_MS[RankIndexOf(ctx.self)]);
                ++_stretched;

                AddonFor(ctx)->SendEvent(ctx.player, KeyOf(MECHANIC_FRAIL_SOUL, "c17_frail_soul"),
                                         uint32(WEAKENED_MS[RankIndexOf(ctx.self)] / 1000),
                                         "Weakened Soul");
            }

            // Boon::BonusHealing, and the card is specific: "Renew 20% stronger
            // on yourself". So it is not the generic heal-taken multiplier the
            // aggregate would apply to everything -- it is one spell, on one
            // target, which is why it is here rather than in AggregateFactor.
            float HealTakenMult(Ctx& ctx, Unit* healer, SpellInfo const* info) override
            {
                if (!ctx.self || ctx.self->boonMag == 0 || !info)
                    return 1.0f;
                if (healer != ctx.player)
                    return 1.0f;
                if (sSpellMgr->GetFirstSpellInChain(info->Id) != SPELL_RENEW)
                    return 1.0f;

                return 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = uint32(WEAKENED_MS[RankIndexOf(&self)] / 1000);
                uint32 const pct  = self.boonMag;

                std::string out = "Weakened Soul lasts " + std::to_string(secs)
                                + " seconds instead of fifteen, whatever its tooltip says. Shield"
                                  " when it matters, not before every pull.";

                if (pct != 0)
                    out += " In exchange, your own Renew heals you " + std::to_string(pct)
                         + "% more.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "frail soul: " + std::to_string(_stretched) + " shield(s) stretched";
            }

        private:
            uint32 _stretched = 0;
        };

        // ==================================================================
        // C20 - Whispers of the Deep (47)
        //
        // "Below 20% health you lose your mind and flee for three seconds, once
        // per fight."
        //
        // The threshold verb, and the card's note is the reason it is in wave
        // A: "lose your mind for the class whose lore is losing it."
        //
        // The counterplay is a button priests forget they have. Fear Ward on
        // yourself prevents it outright, which is the card's own rule and the
        // best kind of counterplay: not a reaction, a preparation.
        // ==================================================================
        constexpr uint16 MECHANIC_WHISPERS = 47;

        // The card's ladder for the line: 15 -> 20 -> 30% health.
        constexpr uint32 WHISPER_LINE_PCT[MAX_RANK] = { 15, 20, 30 };

        constexpr uint32 WHISPER_MS = 3000;

        class WhispersOfTheDeep final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override { _control.Release(ctx.player); }

            // Once per fight, so the flag resets when the fight does.
            void OnEnterCombat(Ctx&, Unit*, bool) override { _spent = false; }
            void OnLeaveCombat(Ctx&) override { _spent = false; }

            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // Boon::BonusCooldown: Fear Ward comes back sooner -- which is the
            // same button that prevents the curse, so the boon is an argument
            // for the counterplay rather than a consolation for the curse.
            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                Player* player = ctx.player;
                if (!player || !spell || !ctx.self || ctx.self->boonMag == 0)
                    return;

                SpellInfo const* info = spell->GetSpellInfo();
                if (!info || sSpellMgr->GetFirstSpellInChain(info->Id) != SPELL_FEAR_WARD)
                    return;

                uint32 const now = player->GetSpellCooldownDelay(info->Id);
                if (now != 0)
                    player->ModifySpellCooldown(info->Id,
                                                -int32(uint64(now) * ctx.self->boonMag / 100u));
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "whispers: line at "
                                + std::to_string(WHISPER_LINE_PCT[RankIndexOf(ctx.self)]) + "%";
                if (ctx.player)
                    out += ", health " + std::to_string(uint32(ctx.player->GetHealthPct())) + "%";
                out += _spent ? ", spent this fight" : ", ready";
                out += _control.Held() ? ", FLEEING" : "";
                return out;
            }

        private:
            SelfControl _control;
            bool        _spent = false;
            uint32      _times = 0;
        };

        void WhispersOfTheDeep::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            if (_control.Tick(player, diffMs) && player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r The whispers fade. You have yourself again.");

            if (_spent || _control.Held())
                return;
            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
                return;
            if (ctx.run && ctx.run->dead)
                return;

            if (player->GetHealthPct() >= float(WHISPER_LINE_PCT[RankIndexOf(ctx.self)]))
                return;

            // Fear Ward prevents it outright, and the card says so. It is the
            // whole counterplay and the reason the boon shortens its cooldown:
            // the affix teaches a button and then makes it cheaper.
            if (player->HasAura(SPELL_FEAR_WARD))
            {
                _spent = true;
                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff20ff20[Gauntlet]|r The whispers rise, and Fear Ward holds them off.");
                return;
            }

            _spent = true;
            ++_times;

            _control.Apply(player, SelfControl::Kind::Flee, WHISPER_MS);

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_WHISPERS, "c20_whispers_of_the_deep"),
                                     WHISPER_MS / 1000u, "Whispers");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Whispers of the Deep: {}.",
                    SelfControl::Describe(SelfControl::Kind::Flee));
        }

        std::string WhispersOfTheDeep::Describe(AffixInstance const& self) const
        {
            uint32 const line = WHISPER_LINE_PCT[RankIndexOf(&self)];
            uint32 const pct  = self.boonMag;

            std::string out = "The first time your health drops below " + std::to_string(line)
                            + "% in a fight, you flee for 3 seconds. Once per fight. Fear Ward on"
                              " yourself stops it completely.";

            if (pct != 0)
                out += " In exchange, Fear Ward comes back " + std::to_string(pct) + "% sooner.";

            return out;
        }
    }

    GAUNTLET_MECHANIC(44, FrailSoul);
    GAUNTLET_MECHANIC(47, WhispersOfTheDeep);
}
