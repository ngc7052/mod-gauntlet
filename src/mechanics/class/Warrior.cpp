/*
 * mod-gauntlet - the warrior's three: Red Mist, Berserker's Bargain, Deafening Roar
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"
#include "../Nearby.h"
#include "AuraDurationEdit.h"
#include "PermanentCooldown.h"
#include "SelfControl.h"
#include "TimedLockout.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <array>
#include <string>
#include <iterator>

// Design section 3, family C, warrior. The family's own three rules are worth
// keeping in view while reading these:
//
//   Remove the shortcut, not the engine.  Auto-attack, rage, Heroic Strike,
//   Charge and Hamstring are the loop that must survive; stances, shouts,
//   Bloodrage, Shield Wall, Last Stand and Retaliation are fair game.
//
//   Tax before deny.  A cost creates a decision every time the button is
//   pressed; a removal creates one decision at pick time.
//
//   Threshold on a resource the class manages.  Rage caps and health lines are
//   states the player already watches.
//
// All three below leave every ability trained and every rotation intact.

namespace Gauntlet
{
    namespace
    {
        // ------------------------------------------------------------------
        // Spell ids
        //
        // 3.3.5a base ranks. GetFirstSpellInChain normalises every later rank
        // onto these (SpellMgr.h:675), so a level 70 warrior's Battle Shout is
        // recognised by the same number as a level 1 one.
        //
        // 5246 and 12975 are confirmed against the core's own warrior script
        // ($CORE/src/server/scripts/Spells/spell_warrior.cpp:543 and :231) and
        // 6673 against SpellInfoCorrections.cpp:4514. The other four are the
        // standard ids and could not be confirmed from source on this machine,
        // because 3.3.5 spells live in DBC rather than in SQL. The failure mode
        // if one is wrong is benign and visible: that one shout stops feeding
        // the curse, or that one panic button stops being held, and the
        // in-game checklist catches it.
        // ------------------------------------------------------------------
        constexpr uint32 SPELL_BATTLE_SHOUT       = 6673;
        constexpr uint32 SPELL_COMMANDING_SHOUT   = 469;
        constexpr uint32 SPELL_DEMORALIZING_SHOUT = 1160;
        constexpr uint32 SPELL_INTIMIDATING_SHOUT = 5246;

        constexpr uint32 SPELL_SHIELD_WALL          = 871;
        constexpr uint32 SPELL_LAST_STAND           = 12975;
        constexpr uint32 SPELL_ENRAGED_REGENERATION = 55694;

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
        // C1 - Red Mist (28)
        //
        // "At 100 rage you lose your mind for three seconds and your rage
        // empties." The threshold verb, and the card's own note names the
        // precedent: Darkest Dungeon's resolve check at 100 stress.
        //
        // What dies is the lazy levelling loop -- auto-attacking at cap with
        // nothing spent. Heroic Strike and Cleave exist to dump rage, and
        // Bloodrage and Berserker Rage stop being free buttons and become
        // timing decisions. Every ability stays trained.
        // ==================================================================
        constexpr uint16 MECHANIC_RED_MIST = 28;

        // The card's ladder is the cap it triggers at, falling as it worsens:
        // 100 -> 90 -> 80 rage. Rage is stored times ten
        // (Player::GetPower(POWER_RAGE) counts in tenths), so these are the
        // stored values.
        constexpr uint32 RAGE_TRIGGER[] = { 1000, 900, 800, 700 };
        static_assert(std::size(RAGE_TRIGGER) >= MAX_RANK, "RAGE_TRIGGER is short a rank");

        // The card's two fixed numbers.
        constexpr uint32 MIST_MS     = 3000;
        constexpr uint32 MIST_COOLDOWN_MS = 15000;

        class RedMist final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Publish(ctx); }

            void OnDetach(Ctx& ctx) override
            {
                // The character comes back whatever else happens. An affix
                // swapped away while it holds the player would hold them
                // forever.
                _control.Release(ctx.player);

                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_RED_MIST, "c01_red_mist"), 0);
            }

            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // The boon, and it is the core's own arithmetic rather than an
            // approximation of it: RewardRage(damage, speed, attacker=false)
            // is exactly what a blow taken runs through
            // ($CORE/src/server/game/Entities/Unit/Unit.cpp:16086), so handing
            // it a fraction of the damage yields precisely that fraction of the
            // rage. "Ten percent more rage from damage taken" means what it
            // says at every level, without this file knowing the formula.
            void OnDamageTaken(Ctx& ctx, Unit* /*attacker*/, uint32 amount) override
            {
                Player* player = ctx.player;
                if (!player || amount == 0 || !ctx.self || ctx.self->boonMag == 0)
                    return;

                uint32 const share = uint32(uint64(amount) * ctx.self->boonMag / 100u);
                if (share != 0)
                    player->RewardRage(share, 0, /*attacker*/ false);
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "red mist: trigger at "
                                + std::to_string(RAGE_TRIGGER[RankIndexOf(ctx.self)] / 10u) + " rage";
                if (ctx.player)
                    out += ", now " + std::to_string(ctx.player->GetPower(POWER_RAGE) / 10u);
                out += _control.Held() ? ", HELD" : ", free";
                out += ", cooldown " + std::to_string(_cooldownMs / 1000u) + "s";
                return out;
            }

        private:
            void Publish(Ctx& ctx);

            SelfControl _control;
            uint32      _cooldownMs = 0;
            uint32      _times      = 0;
        };

        void RedMist::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            if (_control.Tick(player, diffMs) && player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r The red mist clears.");

            if (_cooldownMs != 0)
                _cooldownMs = _cooldownMs > diffMs ? _cooldownMs - diffMs : 0;

            Publish(ctx);

            if (_control.Held() || _cooldownMs != 0)
                return;
            if (!player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && ctx.run->dead)
                return;

            if (player->GetPower(POWER_RAGE) < RAGE_TRIGGER[RankIndexOf(ctx.self)])
                return;

            // Confuse first, then empty the rage. The other order would give a
            // player who is watching closely a frame in which the bar is empty
            // and the character still theirs, which reads as the rage being
            // taken for nothing.
            _control.Apply(player, SelfControl::Kind::Confuse, MIST_MS);
            player->SetPower(POWER_RAGE, 0);

            _cooldownMs = MIST_COOLDOWN_MS;
            ++_times;

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_RED_MIST, "c01_red_mist"),
                                     MIST_MS / 1000u, "Red Mist");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Red Mist: the rage takes you. {}",
                    SelfControl::Describe(SelfControl::Kind::Confuse));
        }

        void RedMist::Publish(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            // How close the bar is to the line, as a percentage of the trigger
            // rather than of the cap -- because the trigger is what the player
            // has to stay under, and at rank III it is not the cap.
            uint32 const trigger = RAGE_TRIGGER[RankIndexOf(ctx.self)];
            uint32 const rage    = uint32(player->GetPower(POWER_RAGE));
            uint32 const pct     = trigger != 0 ? std::min<uint32>(100u, rage * 100u / trigger) : 0u;

            AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_RED_MIST, "c01_red_mist"), int32(pct));
        }

        std::string RedMist::Describe(AffixInstance const& self) const
        {
            uint8 const  i    = RankIndexOf(&self);
            uint32 const rage = RAGE_TRIGGER[i] / 10u;

            std::string out = "Reaching " + std::to_string(rage)
                            + " rage confuses you for 3 seconds and empties your rage. This can"
                              " happen once every 15 seconds. Spend rage before it fills.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }

        // ==================================================================
        // C2 - Berserker's Bargain (29)
        //
        // "Below 35% health you deal 25% more damage, but Shield Wall, Last
        // Stand and Enraged Regeneration will not answer."
        //
        // The shortcut verb, and the trade the card is built on: the panic
        // buttons have to be pressed *before* the line rather than after, which
        // is when they should be pressed anyway. Below it the class becomes a
        // race it is good at winning -- Execute, Victory Rush, Bloodrage -- and
        // Intercept and Hamstring still work, so leaving is always an option.
        //
        // The rank moves the line, not the damage: 30 -> 35 -> 40% health, so a
        // higher rank spends more of the fight inside the bargain.
        // ==================================================================
        constexpr uint16 MECHANIC_BERSERKERS = 29;

        // Rank IV puts the line at half health, which is where a warrior spends
        // most of a hard fight: the damage is free and the panic buttons are
        // gone for the half of the fight you most want them.
        constexpr uint32 LINE_PCT[] = { 30, 35, 40, 50 };

        static_assert(std::size(LINE_PCT) >= MAX_RANK, "LINE_PCT is short a rank");

        // The card's number, and it does not ladder.
        constexpr float BARGAIN_DAMAGE_MULT = 1.25f;

        class BerserkersBargain final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { _below = false; Sync(ctx); }

            void OnDetach(Ctx& ctx) override
            {
                // Every button back, whatever state the affix was in.
                Restore(ctx.player);

                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_BERSERKERS, "c02_berserkers_bargain"), 0);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            // The damage bonus *is* the boon -- the card says so in as many
            // words -- so it is delivered here and BoonFactor is deliberately
            // not also consulted. Paying it twice would be the aggregate
            // handing out an upside the mechanic has already given.
            float DamageDoneMult(Ctx& /*ctx*/, Unit*, SpellInfo const*) override
            {
                return _below ? BARGAIN_DAMAGE_MULT : 1.0f;
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "berserker's bargain: line at "
                                + std::to_string(LINE_PCT[RankIndexOf(ctx.self)]) + "%";
                if (ctx.player)
                    out += ", health " + std::to_string(uint32(ctx.player->GetHealthPct())) + "%";
                out += _below ? ", BELOW (panic buttons held)" : ", above";
                return out;
            }

        private:
            void Sync(Ctx& ctx);
            void Hold(Player* player);
            void Restore(Player* player);

            bool _below = false;
        };

        void BerserkersBargain::Sync(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld())
                return;

            bool const below = player->IsAlive()
                            && player->GetHealthPct() < float(LINE_PCT[RankIndexOf(ctx.self)]);

            // Only on the crossing. Denying and allowing every tick would send
            // the client two cooldown packets per second for three spells,
            // which is a lot of traffic to say nothing has changed.
            if (below == _below)
            {
                // Except this: while below, hold them against anything that
                // clears a cooldown. Hold() is a map lookup when the denial is
                // already in force.
                if (below)
                    Hold(player);
                return;
            }

            _below = below;

            if (below)
                Hold(player);
            else
                Restore(player);

            AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_BERSERKERS, "c02_berserkers_bargain"),
                                     below ? 1 : 0);

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    below ? "|cffff2020[Gauntlet]|r Berserker's Bargain: you hit harder now, and"
                            " nothing will save you. Win or leave."
                          : "|cff20ff20[Gauntlet]|r Above the line. Your defensives answer again.");
        }

        void BerserkersBargain::Hold(Player* player)
        {
            PermanentCooldown::Hold(player, SPELL_SHIELD_WALL);
            PermanentCooldown::Hold(player, SPELL_LAST_STAND);
            PermanentCooldown::Hold(player, SPELL_ENRAGED_REGENERATION);
        }

        void BerserkersBargain::Restore(Player* player)
        {
            PermanentCooldown::Allow(player, SPELL_SHIELD_WALL);
            PermanentCooldown::Allow(player, SPELL_LAST_STAND);
            PermanentCooldown::Allow(player, SPELL_ENRAGED_REGENERATION);
        }

        std::string BerserkersBargain::Describe(AffixInstance const& self) const
        {
            uint32 const line = LINE_PCT[RankIndexOf(&self)];

            // No BoonClause, for Frenzy's reason: the upside is the other half
            // of the same sentence, not a separate promise.
            return "Below " + std::to_string(line) + "% health you deal 25% more damage, and Shield"
                   " Wall, Last Stand and Enraged Regeneration will not answer. Above the line they"
                   " work normally. Use them early.";
        }

        // ==================================================================
        // C4 - Deafening Roar (31)
        //
        // "Your shouts wake every enemy within thirty yards."
        //
        // The anchor verb, and the card's own note is why it is in wave A:
        // cheap, thematic, and it creates a story every time it fires. A
        // warrior who shouts in the middle of a camp pulls the camp, and knows
        // exactly why.
        //
        // The counterplay is a habit rather than a reaction: buff at the edge
        // and walk in, re-shout on cleared ground. Demoralizing Shout stops
        // being a free debuff and becomes a deliberate multi-pull tool.
        // ==================================================================
        constexpr uint16 MECHANIC_ROAR = 31;

        // The card's ladder, in yards.
        constexpr float ROAR_YARDS[] = { 20.0f, 30.0f, 40.0f, 50.0f };
        static_assert(std::size(ROAR_YARDS) >= MAX_RANK, "ROAR_YARDS is short a rank");

        // The boon: "shouts free and long". Four minutes is the card's number.
        constexpr int32 SHOUT_DURATION_MS = 4 * 60 * 1000;

        bool IsShout(uint32 spellId)
        {
            uint32 const base = sSpellMgr->GetFirstSpellInChain(spellId);   // SpellMgr.h:675
            return base == SPELL_BATTLE_SHOUT
                || base == SPELL_COMMANDING_SHOUT
                || base == SPELL_DEMORALIZING_SHOUT
                || base == SPELL_INTIMIDATING_SHOUT;
        }

        class DeafeningRoar final : public IMechanic
        {
        public:
            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            // Half the boon: the shout costs nothing. Refunded rather than
            // prevented, because the cost is taken inside Spell::TakePower
            // before any hook this module has, and giving it back is
            // indistinguishable to the player.
            void OnAuraApplied(Ctx& ctx, Unit* target, Aura* aura) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx&) const override
            {
                return "deafening roar: " + std::to_string(_shouts) + " shout(s), "
                     + std::to_string(_woken) + " enemy/enemies woken";
            }

        private:
            uint32 _shouts = 0;
            uint32 _woken  = 0;
        };

        void DeafeningRoar::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info || !IsShout(info->Id))
                return;

            if (ctx.run && ctx.run->dead)
                return;
            if (!player->IsInWorld() || !player->IsAlive())
                return;

            ++_shouts;

            float const range = ROAR_YARDS[RankIndexOf(ctx.self)];
            uint32      woke  = 0;

            for (Creature* creature : CreaturesNear(player, range))
            {
                // Idle only. Something already fighting has heard you.
                if (!creature || creature->IsInCombat())
                    continue;

                // The card excludes elites and bosses by name, and IsOrdinaryFoe
                // is the same set every other on-kill and positional mechanic in
                // the module works from -- so "wakes the camp" never means
                // "wakes the dungeon boss two rooms away".
                if (!IsOrdinaryFoe(creature) || !IsFairGame(player, creature))
                    continue;

                if (CreatureAI* ai = creature->AI())
                    ai->AttackStart(player);

                creature->AddThreat(player, 1.0f);
                ++woke;
            }

            _woken += woke;

            if (woke == 0)
                return;

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_ROAR, "c04_deafening_roar"), 0,
                                     "Deafening Roar");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Your shout carries. {} of them heard it.", woke);
        }

        void DeafeningRoar::OnAuraApplied(Ctx& ctx, Unit* target, Aura* aura)
        {
            Player* player = ctx.player;
            if (!player || !aura || target != player)
                return;

            SpellInfo const* info = aura->GetSpellInfo();
            if (!info || !IsShout(info->Id))
                return;

            // The other half of the boon. The client's timer follows both
            // halves of the edit, so the bar reads four minutes -- but the
            // *tooltip* still reads the DBC's two, because nothing server-side
            // can change that without a client patch. Describe() says the real
            // number for exactly this reason.
            AuraDurationEdit::Edit(aura, SHOUT_DURATION_MS);

            // Free, by refund. The power is already gone by the time any hook
            // here runs.
            if (info->ManaCost != 0 || info->ManaCostPercentage != 0)
                player->SetPower(POWER_RAGE, player->GetPower(POWER_RAGE) + int32(info->ManaCost));
        }

        // ==================================================================
        // C3 - Iron Discipline (30)
        //
        // "Changing stance has a ten-second cooldown."
        //
        // The identity verb, and wave B's warrior. Stance-dancing is gone:
        // decide Battle, Defensive or Berserker before the pull, and live with
        // it. No mid-fight Shield Wall unless you were already in Defensive.
        //
        // The boon is the other half of the same idea. Changing stance normally
        // burns rage; here it does not, so committing to a stance early costs
        // nothing and the affix is entirely about the commitment rather than
        // about the cost of making it.
        // ==================================================================

        // The card's ladder, in seconds of lockout on the other two stances.
        // Thirty seconds at rank IV: a stance is a decision for the whole
        // fight rather than a rotation step.
        constexpr uint32 STANCE_LOCK_MS[] = { 6000, 10000, 20000, 30000 };
        static_assert(std::size(STANCE_LOCK_MS) >= MAX_RANK, "STANCE_LOCK_MS is short a rank");

        constexpr uint32 SPELL_BATTLE_STANCE     = 2457;
        constexpr uint32 SPELL_DEFENSIVE_STANCE  = 71;
        constexpr uint32 SPELL_BERSERKER_STANCE  = 2458;

        constexpr std::array<uint32, 3> STANCES = { {
            SPELL_BATTLE_STANCE, SPELL_DEFENSIVE_STANCE, SPELL_BERSERKER_STANCE
        } };

        bool IsStance(uint32 spellId)
        {
            return spellId == SPELL_BATTLE_STANCE
                || spellId == SPELL_DEFENSIVE_STANCE
                || spellId == SPELL_BERSERKER_STANCE;
        }

        class IronDiscipline final : public IMechanic
        {
        public:
            // Only the lockouts this affix placed, and only while they are
            // still running. See TimedLockout: releasing the whole group
            // unconditionally hands back a free stance change.
            void OnDetach(Ctx& ctx) override { _lock.ReleaseAll(ctx.player); }

            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                Player* player = ctx.player;
                if (!player || !spell)
                    return;

                SpellInfo const* info = spell->GetSpellInfo();
                if (!info || !IsStance(info->Id))
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const ms = STANCE_LOCK_MS[RankIndexOf(ctx.self)];

                // The other two only. The stance just entered is not put on
                // cooldown, because re-casting the stance you are already in is
                // a no-op the player may use to clear a debuff and there is no
                // reason to take that away.
                for (uint32 id : STANCES)
                    if (id != info->Id)
                        _lock.Lock(player, id, ms);

                // The boon: the rage a stance change normally burns is given
                // straight back. Refunded rather than prevented, because the
                // loss happens inside the stance aura's own effect, well before
                // any hook here.
                if (ctx.self && ctx.self->boonMag != 0)
                    player->SetPower(POWER_RAGE, _rageBefore);

                ++_changes;

                AddonFor(ctx)->SendEvent(player, KeyOf(30, "c03_iron_discipline"),
                                         ms / 1000u, "Stance locked");
            }

            // The rage is read every tick so the refund above has something
            // true to restore: by the time OnSpellCast runs the stance has
            // already taken it.
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override
            {
                if (ctx.player)
                    _rageBefore = ctx.player->GetPower(POWER_RAGE);
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = STANCE_LOCK_MS[RankIndexOf(&self)] / 1000u;

                std::string out = "Changing stance locks the other two for " + std::to_string(secs)
                                + " seconds. Choose before the pull.";

                if (self.boonMag != 0)
                    out += " In exchange you keep your rage through the change.";

                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return "iron discipline: " + std::to_string(STANCE_LOCK_MS[RankIndexOf(ctx.self)] / 1000u)
                     + "s lock, " + std::to_string(_changes) + " change(s)";
            }

        private:
            int32  _rageBefore = 0;
            uint32 _changes    = 0;
            TimedLockout _lock;
        };

        std::string DeafeningRoar::Describe(AffixInstance const& self) const
        {
            uint32 const yards = uint32(ROAR_YARDS[RankIndexOf(&self)]);

            return "Your shouts wake every idle enemy within " + std::to_string(yards)
                 + " yards and pull them onto you. Elites and bosses do not hear it. In exchange"
                   " your shouts cost no rage and last 4 minutes, whatever their tooltip says.";
        }
    }

    GAUNTLET_MECHANIC(28, RedMist);
    GAUNTLET_MECHANIC(30, IronDiscipline);
    GAUNTLET_MECHANIC(29, BerserkersBargain);
    GAUNTLET_MECHANIC(31, DeafeningRoar);
}
