/*
 * mod-gauntlet - the paladin's two: Long Forbearance, Consecrated Ground
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"
#include "AuraDurationEdit.h"
#include "SelfControl.h"

#include "Chat.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <string>
#include <iterator>

// Design section 3, family C, paladin. Engine: Seal and Judgement, melee, Holy
// Light, Consecration. The classic death the design names is trusting the
// bubble-hearth, which Classic Hardcore blocks outright; these two tax it
// rather than blocking it, which is the family's "tax before deny" rule.

namespace Gauntlet
{
    namespace
    {
        // 3.3.5a ids. 642 and 26573 are the registry's own requiresSpell
        // values, so they are already load-bearing elsewhere in the module and
        // wrong ones would have shown up as a curse nobody is ever offered.
        constexpr uint32 SPELL_FORBEARANCE  = 25771;
        constexpr uint32 SPELL_DIVINE_SHIELD = 642;
        constexpr uint32 SPELL_CONSECRATION = 26573;
        constexpr uint32 SPELL_HOLY_LIGHT   = 635;


        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        bool IsChainOf(uint32 spellId, uint32 base)
        {
            return sSpellMgr->GetFirstSpellInChain(spellId) == base;   // SpellMgr.h:675
        }

        // Give a fraction of a spell's mana cost back, for the boons whose card
        // says an ability is cheaper.
        //
        // A refund rather than a discount, and not for want of trying: the cost
        // is taken inside Spell::TakePower, which runs before OnPlayerSpellCast
        // (Spell.cpp:3825 is where the hook fires, at the end of Spell::_cast),
        // so by the time this module hears about a cast the mana is already
        // gone. The player cannot tell the difference -- the bar lands where a
        // discount would have left it.
        void RefundShare(Player* player, SpellInfo const* info, uint32 pct)
        {
            if (!player || !info || pct == 0)
                return;

            uint32 const cost = info->ManaCost;                        // SpellInfo.h:387
            if (cost == 0)
                return;

            uint32 const back = uint32(uint64(cost) * pct / 100u);
            if (back == 0)
                return;

            player->SetPower(POWER_MANA, player->GetPower(POWER_MANA) + int32(back));
        }

        // ==================================================================
        // C5 - Long Forbearance (32)
        //
        // "Forbearance lasts three minutes, and Divine Shield empties your
        // mana."
        //
        // The shortcut verb. Divine Shield, Lay on Hands and Hand of Protection
        // stop being three buttons and become one decision: which, and when.
        // The bubble-hearth still works -- the design is explicit that it is
        // not blocked -- it just costs the run's next three minutes of
        // immunity and the mana to get home on.
        // ==================================================================
        constexpr uint16 MECHANIC_FORBEARANCE = 32;

        // The card's ladder: 2 -> 3 -> 5 minutes.
        // Eight minutes at rank IV, which is longer than most levelling fights
        // are apart: the bubble becomes a once-a-zone answer.
        constexpr int32 FORBEARANCE_MS = 180000;

        class LongForbearance final : public IMechanic
        {
        public:
            void OnAuraApplied(Ctx& ctx, Unit* target, Aura* aura) override
            {
                Player* player = ctx.player;
                if (!AuraDurationEdit::Matches(target, aura, player, SPELL_FORBEARANCE))
                    return;

                AuraDurationEdit::Edit(aura, FORBEARANCE_MS);
                ++_stretched;

                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_FORBEARANCE, "c05_long_forbearance"),
                                         uint32(FORBEARANCE_MS / 1000),
                                         "Forbearance");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Forbearance will hold for {} minutes.",
                        FORBEARANCE_MS / 60000);
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx&) const override
            {
                return "long forbearance: " + std::to_string(_stretched) + " stretched, "
                     + std::to_string(_emptied) + " bubble(s) paid for";
            }

        private:
            uint32 _stretched = 0;
            uint32 _emptied   = 0;
        };

        void LongForbearance::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return;

            // The boon: Holy Light costs less. Named on the card, and delivered
            // as a refund for the reason RefundShare explains.
            if (ctx.self && ctx.self->boonMag != 0 && IsChainOf(info->Id, SPELL_HOLY_LIGHT))
                RefundShare(player, info, ctx.self->boonMag);

            if (!IsChainOf(info->Id, SPELL_DIVINE_SHIELD))
                return;
            if (ctx.run && ctx.run->dead)
                return;

            player->SetPower(POWER_MANA, 0);
            ++_emptied;

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r The Light answers, and takes everything you had.");
        }

        std::string LongForbearance::Describe(AffixInstance const& self) const
        {
            int32 const  mins = FORBEARANCE_MS / 60000;
            uint32 const pct  = self.boonMag;

            std::string out = "Forbearance lasts " + std::to_string(mins)
                            + " minutes instead of one, whatever its tooltip says, and casting"
                              " Divine Shield leaves you with no mana.";

            if (pct != 0)
                out += " In exchange, Holy Light costs " + std::to_string(pct) + "% less.";

            return out;
        }

        // ==================================================================
        // C6 - Consecrated Ground (33)
        //
        // "You take 25% more damage while not standing in your own
        // Consecration."
        //
        // The anchor verb, and the card calls it the class fantasy as a rule.
        // Every fight opens with a placement decision and the eight-second
        // refresh becomes a rhythm; kiting off the circle is dangerous and
        // pulling *onto* it is the play.
        // ==================================================================
        constexpr uint16 MECHANIC_CONSECRATED = 33;

        // The card's ladder for the damage taken off the circle.
        constexpr float OFF_CIRCLE_MULT = 1.25f;

        // The card's own eight yards. Consecration's real radius is smaller;
        // this is the affix's circle, and it is deliberately the more generous
        // number so that "standing in it" means what it looks like.
        constexpr float SAFE_YARDS = 8.0f;

        class ConsecratedGround final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player,
                                         KeyOf(MECHANIC_CONSECRATED, "c06_consecrated_ground"), 0);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;

                bool const safe = OnCircle(player);
                if (safe == _safe && _published)
                    return;

                _safe      = safe;
                _published = true;

                // A flag the player can watch, because the whole affix is a
                // question about where they are standing and the answer has to
                // be visible without counting yards.
                AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_CONSECRATED, "c06_consecrated_ground"),
                                         safe ? 0 : 1);
            }

            float DamageTakenMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                return OnCircle(ctx.player) ? 1.0f : OFF_CIRCLE_MULT;
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "consecrated ground: ";
                out += _lastCast == 0 ? "no Consecration cast yet"
                                      : (OnCircle(ctx.player) ? "standing in it" : "OFF the circle");
                return out;
            }

        private:
            // The live Consecration this paladin cast, or nullptr.
            //
            // Looked up by the id they actually cast rather than by a table of
            // every Consecration rank. Unit::GetDynObject takes one spell id
            // (Unit.h:1693), and eight ranks would be eight numbers to get
            // right from memory with no way to check them on this machine --
            // whereas the id of the cast that made the circle is something the
            // module was told.
            DynamicObject* Circle(Player* player) const
            {
                return (player && _lastCast != 0) ? player->GetDynObject(_lastCast) : nullptr;
            }

            bool OnCircle(Player* player) const
            {
                DynamicObject* dyn = Circle(player);
                return dyn && player->GetExactDist2d(dyn) <= SAFE_YARDS;
            }

            uint32 _lastCast  = 0;
            bool   _safe      = false;
            bool   _published = false;
        };

        void ConsecratedGround::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info || !IsChainOf(info->Id, SPELL_CONSECRATION))
                return;

            _lastCast = info->Id;

            // The boon, both halves: twice as long and half the cost. The
            // duration is on the dynamic object rather than on an aura, because
            // Consecration is a patch of ground and not a buff
            // (DynamicObject.h:48).
            if (DynamicObject* dyn = player->GetDynObject(info->Id))
                dyn->SetDuration(dyn->GetDuration() * 2);

            RefundShare(player, info, 50);

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_CONSECRATED, "c06_consecrated_ground"),
                                     0, "Consecration");
        }

        std::string ConsecratedGround::Describe(AffixInstance const& /*self*/) const
        {
            uint32 const extra = uint32((OFF_CIRCLE_MULT - 1.0f) * 100.0f + 0.5f);

            return "You take " + std::to_string(extra) + "% more damage whenever you are more than "
                 + std::to_string(uint32(SAFE_YARDS)) + " yards from your own Consecration."
                   " In exchange it lasts twice as long and costs half. Fight where you consecrate.";
        }

        // ==================================================================
        // C7 - No Sanctuary (34)
        //
        // "Your Hearthstone will not answer under Divine Shield."
        //
        // The identity verb, and the card calls it what it is: "the famous
        // hardcore taboo, enforced". Bubble-hearth is the death every Classic
        // Hardcore realm has an opinion about; this one lets you keep both
        // buttons and refuses the combination.
        //
        // Rank III goes further and breaks the bubble on your first attack, so
        // it stops being an offensive cooldown as well as an escape.
        // ==================================================================

        constexpr uint32 SPELL_HEARTHSTONE        = 8690;
        constexpr uint32 SPELL_HAND_OF_PROTECTION = 1022;
        // 34 -- No Sanctuary -- was retired on 2026-09-01.
        //
        // docs/greed-redesign.md section 3: "No Sanctuary is retired
        // outright: a card that never acts is indistinguishable from a broken
        // one, and it has nothing to become." Its Hearthstone denial fired
        // only while Divine Shield or Hand of Protection was up, which is a
        // few seconds of a run, and the card spent the rest of it being
        // invisible. The id is spent forever; nothing may fill the hole.

        // Commitment's own constants, which sat between it and the retired No
        // Sanctuary and came out with it the first time.
        constexpr uint16 MECHANIC_COMMITMENT = 35;
        constexpr uint32 SPELL_HAMMER_OF_JUSTICE = 853;
        constexpr uint32 COMMIT_MS = 4000;

        class Commitment final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override { _control.Release(ctx.player); }

            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                if (_control.Tick(ctx.player, diffMs) && ctx.player && ctx.player->GetSession())
                    ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                        "|cff20ff20[Gauntlet]|r Your feet are yours again.");
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                Player* player = ctx.player;
                if (!player || !spell)
                    return;

                SpellInfo const* info = spell->GetSpellInfo();
                if (!info || !IsChainOf(info->Id, SPELL_HAMMER_OF_JUSTICE))
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const ms = COMMIT_MS;
                _control.Apply(player, SelfControl::Kind::Root, ms);

                // The boon: Hammer comes back sooner, cut from the cooldown the
                // core has just set.
                if (ctx.self && ctx.self->boonMag != 0)
                {
                    uint32 const now = player->GetSpellCooldownDelay(info->Id);
                    if (now != 0)
                        player->ModifySpellCooldown(info->Id,
                                                    -int32(uint64(now) * ctx.self->boonMag / 100u));
                }

                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_COMMITMENT, "c08_commitment"),
                                         ms / 1000u, "Committed");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Committed: {}. Finish it.",
                        SelfControl::Describe(SelfControl::Kind::Root));
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = COMMIT_MS / 1000u;

                std::string out = "Hammer of Justice roots you for " + std::to_string(secs)
                                + " seconds as well as your target. Stun and finish, not stun and"
                                  " run.";

                if (self.boonMag != 0)
                    out += " In exchange it comes back " + std::to_string(self.boonMag)
                         + "% sooner.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return std::string("commitment: ") + (_control.Held() ? "ROOTED" : "free");
            }

        private:
            SelfControl _control;
        };
    }

    GAUNTLET_MECHANIC(35, Commitment);
    GAUNTLET_MECHANIC(32, LongForbearance);
    GAUNTLET_MECHANIC(33, ConsecratedGround);
}
