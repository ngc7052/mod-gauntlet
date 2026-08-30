/*
 * mod-gauntlet - A1 Deep Wounds: what you take becomes a wound only rest heals
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAddon.h"
#include "GauntletMechanic.h"
#include "GauntletState.h"
#include "Chat.h"
#include "GauntletSummons.h"
#include "Creature.h"
#include "Player.h"
#include "../Boons.h"

#include <algorithm>
#include <limits>
#include <string>
#include <iterator>

// Registry id 19. Design section 5 replaces Withering with this one: the same
// attrition, but the answer stops being "have more healing" and becomes "get
// hit less", which is a skill and therefore a decision. A fraction of every
// blow that actually lands is kept as a wound, the wound is taken off the
// character's maximum health, and only sitting in an inn or a city gives it
// back. That is what makes the town trip something a player chooses.
//
// Three things this mechanic deliberately is not:
//
//   - It is not a modifier. It reads the damage after the world has finished
//     deciding how much of it landed and changes nothing about it.
//   - It owns no clock. It arms no scheduler event, so it does not spend the
//     event budget and none of the suppression rules (mounted, in flight, in a
//     sanctuary, dead, in the grace window, an offer on the table) apply to it;
//     they gate events, and a wound is not an event. Its registry row must
//     therefore never gain MF_Timed.
//   - It holds nothing that can outlive it: no Player pointer, no GUID, no
//     summon, no timer. Every member is a number, and OnDetach hands the
//     health pool back before the object goes away.
//
// What integration must wire for it is listed beside each callback below.

namespace Gauntlet
{
    namespace
    {
        // The design card's severity ladder: 30 -> 40 -> 50% of the damage
        // taken becomes wound, at ranks I, II and III.
        // Rank IV is 60%, past the card. Gauntlet.Caps.MaxHealth still floors
        // what the pool can fall to, so what the last rank buys is reaching
        // that floor in fewer blows rather than a deeper hole: the ladder is
        // about how often you have to go and rest, which is the decision the
        // affix exists to create.
        constexpr int32 WOUND_PCT[] = { 30, 40, 50, 60 };
        static_assert(std::size(WOUND_PCT) >= MAX_RANK, "WOUND_PCT is short a rank");

        // ...capped at 40% of the pool, which is the same number plan section
        // 2.5 states as the floor on maximum health ("max health >= 0.6 x base
        // after wounds"). The two are one rule seen from either end, so a run
        // carrying only this affix can never make the aggregate's clamp bite.
        constexpr int32 WOUND_CAP_PCT = 40;

        // What one kill closes, as a percentage of the unwounded pool.
        //
        // This card used to heal only while sitting in an inn or a city, and
        // that was its worst feature by a distance: the counterplay to a combat
        // affix was a journey. Travelling somewhere to sit down is a chore
        // rather than gameplay, and it stopped the run instead of shaping it.
        //
        // Kills close wounds now. The verb is already in the player's hands, it
        // happens where the damage happened, and it puts this card on the same
        // currency as Killing Floor and Frenzy: carry two of the three and the
        // run has one instruction, which is to keep winning fights. See
        // docs/tempo-redesign.md.
        //
        // The ladder tightens from both ends -- higher ranks wound more (above)
        // and close less (here).
        constexpr int32 KILL_CLOSE_PCT[] = { 12, 10, 8, 6 };
        static_assert(std::size(KILL_CLOSE_PCT) >= MAX_RANK, "KILL_CLOSE_PCT is short a rank");

        // Maximum health is a replicated field and a full stat recompute, so
        // the wound is written onto it on a boundary rather than on every hit:
        // at most twice a second, and then only when it has moved by a percent
        // of the pool -- or when it has just reached zero or the cap, where
        // the exact number is the whole point of the affix.
        constexpr uint32 APPLY_INTERVAL_MS = 500;   // TODO(design)
        constexpr int32  APPLY_DELTA_PCT   = 1;     // TODO(design)

        // The readout is sent again on this interval even when the wound has
        // not moved. A character logs in wounded and the addon's handshake
        // arrives a moment later, by which time the only STAT of the session
        // has already been dropped by Addon::Enqueue for want of a session --
        // so a value that never changes again would never be drawn. Ten
        // seconds is one message every twenty flushes, against a budget of
        // eight a second.
        constexpr uint32 REPUBLISH_INTERVAL_MS = 10000;   // TODO(design)

        // How far the wound has to move, as a percentage of the pool, before
        // the player without an addon is told again. Five is small enough that
        // a real fight reports two or three times and large enough that
        // chip damage does not report at all.
        constexpr int32 ANNOUNCE_STEP_PCT = 5;   // TODO(design)

        // gauntlet_state key. Plan section 3.3, CONTRACT-P1 section 5.2 and
        // GauntletState.h all spell it "deepwounds.wound", which is what is
        // used here; note that the registry key for id 19 is "deep_wounds", so
        // this one key does not follow the "<mechanic key>.<field>" rule the
        // same documents state. See the report: the literal wins over the rule
        // because three documents and a test already carry it.
        constexpr char const* KEY_WOUND = "deepwounds.wound";

        // The addon key, which *is* the registry key: STAT shares a namespace
        // with EVT and SUMMON, and those are keyed by the mechanic.
        constexpr char const* STAT_KEY = "deep_wounds";

        class DeepWounds final : public IMechanic
        {
        public:
            // Pick or login. The wound is persistent state (plan section 3.3):
            // it is an amount of health taken out of a character over hours of
            // play, and losing it at the login screen would make logging out
            // the counterplay.
            void OnAttach(Ctx& ctx) override
            {
                _detached = false;

                if (ctx.state)
                    _wound = std::max<int32>(0, ctx.state->Get(KEY_WOUND, 0));
                _savedWound = _wound;

                if (Player* player = ctx.player)
                {
                    _level = player->GetLevel();

                    // Nothing has been subtracted yet, so whatever the
                    // character is carrying at this moment is the unwounded
                    // pool. The wound is never written into the stat chain,
                    // so this stays true across a login.
                    _base = player->GetMaxHealth();
                }

                // The loaded wound goes back onto the pool on the first tick
                // rather than here: a stat recompute inside Mgr::Load buys
                // nothing, and half a second later is invisible.
                _applied      = 0;
                _requested    = 0;
                _sinceApplyMs = APPLY_INTERVAL_MS;
            }

            // Swap, logout or death. The wound survives in gauntlet_state, but
            // the subtraction must not: an affix that is no longer carried has
            // no business holding a character's health down, and a swap is a
            // player spending a tier to be rid of exactly this.
            void OnDetach(Ctx& ctx) override
            {
                if (_detached)
                    return;
                _detached = true;

                Mirror(ctx);

                if (_applied == 0)
                    return;

                _applied = 0;

                // The flag above is what makes this safe: the recompute calls
                // back into OnMaxHealth over the carried set, and a detached
                // instance -- which on a swap is still in that set, because
                // Mgr::Pick detaches before it removes the slot -- subtracts
                // nothing.
                if (Player* player = ctx.player)
                    if (player->IsInWorld())
                        player->UpdateMaxHealth();
            }

            // Observer, and nothing else. INTEGRATION: dispatch this from
            // UnitScript::DealDamage (the hook at Unit.cpp:984, which runs
            // before the health is applied and after absorbs and resists have
            // already been taken off), for the victim only, once per blow.
            // It must not also be dispatched from ModifyMeleeDamage /
            // ModifySpellDamageTaken / ModifyPeriodicDamageAurasTick, or every
            // hit is counted twice.
            // A kill closes part of the wound. Pet kills count: the card is
            // about the fight being won, not about who landed the blow.
            void OnKill(Ctx& ctx, Creature* killed) override    { Close(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Close(ctx, killed); }

            void OnDamageTaken(Ctx& ctx, Unit* /*attacker*/, uint32 amount) override
            {
                ++_sawDamage;

                Player* player = ctx.player;
                if (_detached || !player || amount == 0 || !player->IsAlive())
                    return;

                uint32 const base = Base(player);
                if (base == 0)
                    return;

                // Overkill is not a wound. The hook runs before the health is
                // applied, so GetHealth() is still the pool the blow is about
                // to come out of, and the part of a killing blow that lands on
                // nothing would otherwise leave a body with a wound bigger
                // than itself.
                uint32 const landed = std::min<uint32>(amount, player->GetHealth());

                int64 const add = int64(landed) * Severity(ctx) / 100;
                if (add <= 0)
                    return;

                // Deliberately no UpdateMaxHealth here. This runs inside
                // Unit::DealDamage; changing the maximum health mid-blow would
                // clamp the health the same call is about to reduce.
                SetWound(std::min<int64>(int64(_wound) + add, CapFor(base)));
            }

            // INTEGRATION: dispatch this to *every* carried affix on the
            // module's 500 ms cadence, not only to the MF_Timed ones. MF_Timed
            // means "spends the event budget", which this mechanic must not;
            // the tick is what the card's decay, the batched write onto
            // maximum health and the addon readout all hang from. `diffMs` may
            // be a raw world diff or an accumulated 500 ms one; both work.
            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                ++_sawTick;

                Player* player = ctx.player;
                if (_detached || !player)
                    return;

                CheckLevel(ctx, player);

                _sinceApplyMs += diffMs;
                _sinceStatMs  += diffMs;
                if (_sinceApplyMs < APPLY_INTERVAL_MS)
                    return;
                _sinceApplyMs = 0;

                Mirror(ctx);
                Publish(ctx, player, _sinceStatMs >= REPUBLISH_INTERVAL_MS);
                ApplyIfDue(player);
            }

            // INTEGRATION: dispatch this from
            // PlayerScript::OnPlayerAfterUpdateMaxHealth
            // ($CORE/src/server/game/Scripting/ScriptDefines/PlayerScript.h:476),
            // and clamp the result against the aggregate's floor afterwards --
            // see the report; the two clamps are the same number and must not
            // be allowed to compound into 0.36 x base.
            void OnMaxHealth(Ctx& ctx, float& value) override
            {
                ++_sawMaxHealth;

                if (_detached || value <= 0.0f)
                    return;

                // What arrives here is the pool with no wound in it:
                // Player::UpdateMaxHealth rebuilds `value` from the stat chain
                // on every call (StatSystem.cpp:313-324) and nothing below is
                // ever written back into that chain. That is what stops the
                // subtraction compounding, and it is why this is the honest
                // base for both the cap here and the cap in OnDamageTaken.
                _base = uint32(value);

                // Player::GiveLevel moves the level (Player.cpp:2523) before
                // it recomputes the stats (Player.cpp:2538), so a level-up
                // reaches this callback with the new level already set -- one
                // whole recompute before OnPlayerLevelChanged fires at
                // Player.cpp:2585. The card's "clears on level-up" is
                // therefore honoured here, at the earliest moment it can be,
                // and needs no hook of its own.
                if (ctx.player)
                    CheckLevel(ctx, ctx.player);

                int32 const sub = std::min(_wound, CapFor(_base));
                _applied = sub;

                // Never to zero: SetMaxHealth would read that as 1 anyway
                // (Unit.cpp:12410) and a pool of zero divides by zero in every
                // health percentage in the core. The cap keeps this at 60% of
                // what arrived, so the clamp is a belt on top of braces.
                value = std::max(value - float(sub), 1.0f);
            }

            // Every number this mechanic holds, and -- the point of it -- how
            // many times each of its three callbacks has actually been reached
            // this session. The three are the whole dispatch chain, and a zero
            // in any of them names the broken link outright:
            //
            //   ticks 0        Mgr::Tick is not reaching OnTick, so nothing
            //                  decays, nothing is published and nothing is ever
            //                  written onto the health pool.
            //   blows 0        UnitScript::DealDamage is not reaching
            //                  OnDamageTaken, so no wound is ever accumulated.
            //   recomputes 0   OnPlayerAfterUpdateMaxHealth is not reaching
            //                  OnMaxHealth, so the wound exists and is
            //                  subtracted from nothing. `asked` above `applied`
            //                  with recomputes at 0 is exactly this case, and
            //                  ApplyIfDue stops asking after the first one.
            //
            // All three non-zero with `applied` tracking `wound` means the
            // mechanic is working and the health bar is the place to look.
            std::string Diagnose(Ctx& ctx) const override
            {
                uint32 const base = ctx.player ? const_cast<DeepWounds*>(this)->Base(ctx.player) : _base;
                int32 const  pct  = base != 0 ? int32(int64(_wound) * 100 / base) : 0;

                return "wound " + std::to_string(_wound) + " (" + std::to_string(pct)
                     + "% of " + std::to_string(base) + ") | applied " + std::to_string(_applied)
                     + " | asked " + std::to_string(_requested)
                     + " | saved " + std::to_string(_savedWound)
                     + " | cap " + std::to_string(CapFor(base))
                     + " | level " + std::to_string(uint32(_level))
                     + " | a kill closes " + std::to_string(CloseFor(ctx.self ? ctx.self->rank : 1)) + "%"
                     + (_detached ? " | DETACHED" : "")
                     + " | ticks " + std::to_string(_sawTick)
                     + " blows " + std::to_string(_sawDamage)
                     + " recomputes " + std::to_string(_sawMaxHealth);
            }

            std::string Describe(AffixInstance const& self) const override
            {
                std::string out = std::to_string(PctFor(self.rank))
                                + "% of the damage you take becomes a wound. Only a kill closes one.";
                out += BoonClause(self.boon, self.boonMag);
                return out;
            }

        private:
            static int32 CloseFor(uint8 rank)
            {
                return KILL_CLOSE_PCT[std::clamp<uint8>(rank, 1, MAX_RANK) - 1];
            }

            static int32 PctFor(uint8 rank)
            {
                return WOUND_PCT[std::clamp<uint8>(rank, 1, MAX_RANK) - 1];
            }

            static int32 Severity(Ctx const& ctx)
            {
                return PctFor(ctx.self ? ctx.self->rank : 1);
            }

            static int32 CapFor(uint32 base)
            {
                return int32(int64(base) * WOUND_CAP_PCT / 100);
            }

            // The unwounded pool. `_base` is refreshed on every recompute, so
            // it is normally exact; the fallback reconstructs it for the one
            // moment before the first recompute of a session.
            uint32 Base(Player* player) const
            {
                if (_base != 0)
                    return _base;

                return player->GetMaxHealth() + uint32(_applied);
            }

            // A level-80 pool is tens of thousands of health and the wound is
            // at most two fifths of it -- five figures at the very worst,
            // where an int32 and gauntlet_state's signed SQL INT both hold ten.
            // The clamp below is for the pathological case (a GM-set pool),
            // not for a real character.
            void SetWound(int64 value)
            {
                _wound = int32(std::clamp<int64>(value, 0, std::numeric_limits<int32>::max()));
            }

            void CheckLevel(Ctx& ctx, Player* player)
            {
                uint8 const level = player->GetLevel();
                if (_level == 0)
                {
                    _level = level;
                    return;
                }

                if (level == _level)
                    return;

                // Any change, not only an increase: a level taken away by a GM
                // shrinks the pool the wound is measured against, and the card
                // gives no reason to keep a wound across either direction.
                _level = level;
                SetWound(0);
                Mirror(ctx);
            }

            void Close(Ctx& ctx, Creature* killed)
            {
                Player* player = ctx.player;
                if (_detached || !player || !killed || _wound <= 0)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                // Nothing this module summoned closes a wound. One card must
                // not be able to farm the cure for another card's cost.
                if (sGauntletSummons->IsGauntletSummon(killed))
                    return;

                uint32 const base = Base(player);
                if (base == 0)
                    return;

                int32 const closed = std::max<int32>(
                    1, int32(int64(base) * CloseFor(ctx.self ? ctx.self->rank : 1) / 100));

                SetWound(std::max<int64>(0, int64(_wound) - closed));
                Mirror(ctx);
                Publish(ctx, player, /*force*/ true);
                ApplyIfDue(player);
            }

            void ApplyIfDue(Player* player)
            {
                if (!player->IsInWorld() || !player->IsAlive())
                    return;

                uint32 const base   = Base(player);
                int32 const  cap    = CapFor(base);
                int32 const  target = std::min(_wound, cap);
                int32 const  delta  = target > _applied ? target - _applied : _applied - target;
                if (delta == 0)
                    return;

                int32 const threshold = std::max<int32>(1, int32(int64(base) * APPLY_DELTA_PCT / 100));
                if (delta < threshold && target != 0 && target != cap)
                    return;

                // Asked for this number once and the pool did not move, so the
                // OnMaxHealth dispatch is not wired: stop asking. Without this
                // an unwired hook would mean a stat recompute twice a second
                // for as long as the character is wounded, which is a worse
                // failure than the missing subtraction.
                if (target == _requested && target != _applied)
                    return;
                _requested = target;

                // The recompute is what calls back into OnMaxHealth, which is
                // where `_applied` is set. Nothing here writes it.
                player->UpdateMaxHealth();
            }

            // STAT deep_wounds <percent of the pool>. Design section 4.8: an
            // affix the player cannot see is an affix they cannot learn from,
            // and the wound is invisible on the health bar -- what a player
            // sees there is a maximum that is simply smaller than it was.
            void Publish(Ctx& ctx, Player* player, bool force)
            {
                uint32 const base = Base(player);
                int32 const  pct  = base != 0 ? int32(int64(_wound) * 100 / base) : 0;
                if (pct == _lastPct && !force)
                    return;

                int32 const before = _lastPct;

                _lastPct     = pct;
                _sinceStatMs = 0;

                // And a chat line for a player with no addon, which is the
                // whole of what this affix looks like to them. Deep Wounds is
                // the only mechanic in the module with no moment, no creature
                // and no aura -- what it does is make a number smaller -- so
                // without this it is genuinely invisible, and an affix a player
                // cannot see is one they cannot learn from (design section 5,
                // rule 4).
                //
                // Every five percent of the pool, not every change: the wound
                // moves on every blow and a line per blow is a combat log
                // nobody reads. Crossing back down through a step reports too,
                // because the trip to the inn is the counterplay and it has to
                // be visible to be worth taking.
                Announce(player, before, pct);

                // INTEGRATION: fill Ctx::addon wherever a Ctx is built. Addon
                // is a singleton, so the fallback below is the same object and
                // not a second channel; it is here so the readout survives a
                // Ctx that predates the wiring, and QueueStat is safe for an
                // ineligible player or one with no addon (it stops in
                // Addon::CanSend and Addon::Enqueue).
                if (Addon* addon = ctx.addon ? ctx.addon : sGauntletAddon)
                    addon->QueueStat(player, STAT_KEY, pct);
            }

            void Announce(Player* player, int32 before, int32 now) const
            {
                if (!player || !player->GetSession())
                    return;

                int32 const wasStep = before < 0 ? 0 : before / ANNOUNCE_STEP_PCT;
                int32 const nowStep = now / ANNOUNCE_STEP_PCT;
                if (wasStep == nowStep)
                    return;

                if (now <= 0)
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Your wounds have closed.");
                    return;
                }

                ChatHandler(player->GetSession()).PSendSysMessage(
                    nowStep > wasStep
                        ? "|cffff2020[Gauntlet]|r Your wounds deepen: {}% of your health is gone until you rest."
                        : "|cffff2020[Gauntlet]|r Your wounds are closing: {}% of your health is still gone.",
                    now);
            }

            void Mirror(Ctx& ctx)
            {
                if (!ctx.state || _wound == _savedWound)
                    return;

                ctx.state->Set(KEY_WOUND, _wound);
                _savedWound = _wound;
            }

            int32  _wound        = 0;    // absolute health; persisted as deepwounds.wound
            int32  _savedWound   = 0;    // what ctx.state was last told
            int32  _applied      = 0;    // what OnMaxHealth last subtracted
            int32  _requested    = 0;    // what the last recompute was asked for
            uint32 _base         = 0;    // the pool without the wound in it
            uint32 _sinceApplyMs = 0;
            uint32 _sinceStatMs  = 0;   // since the addon was last told
            int32  _lastPct      = -1;   // last percentage sent to the addon
            uint8  _level        = 0;
            bool   _detached     = false;

            // Diagnostics only, and never read by the mechanic itself. They
            // exist so that `.gauntlet debug dump` can say which callback is
            // not arriving; see Diagnose().
            uint32 _sawTick      = 0;
            uint32 _sawDamage    = 0;
            uint32 _sawMaxHealth = 0;
        };
    }

    GAUNTLET_MECHANIC(19, DeepWounds);
}
