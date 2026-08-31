/*
 * mod-gauntlet - S2 Echo: every Nth enemy you kill returns as an echo of yourself
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "GauntletState.h"
#include "GauntletSummons.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <iterator>

// Registry id 2. Design section 3, card S2: "Every 25th enemy you kill returns
// as an echo of yourself."
//
// The Shade and the Echo are the same family and the same summon machinery, and
// the card says exactly what separates them: *who pulls the trigger*. The Shade
// arrives on a clock the player does not own; the Echo arrives on a counter the
// player advances one kill at a time and can see the whole way. So the whole of
// this file's counterplay is the counter being visible -- make the 25th kill
// land at full health, on easy ground, with cooldowns up -- and the counter is
// therefore persisted, telegraphed at every step, and warned about before it
// lands.
//
// Only one stalker may be active per run (design section 4.1), which the
// registry's "stalker" exclusive key enforces against the Shade, and the summon
// wrapper enforces again with SUMMON_CAP_STALKER.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_ECHO = 2;

        // The clock carries one event: the rise, armed the moment the counter
        // fills so the two-step telegraph the framework gives every timed affix
        // still happens. Arming the same tag again is a reschedule.
        constexpr uint32 EVENT_RISE = 1;

        // The card's ladder: every 30 -> 25 -> 18 kills, health x2 -> x2.5 -> x3.
        constexpr int32 KILLS_PER_ECHO = 25;

        // creature_template row 900005 already carries HealthModifier 2.0, so
        // what is left to code is the ratio to it -- the SQL says so in its own
        // comment. Damage has no ladder on the card and the template carries
        // the single figure.
        // Rank IV: every twelfth kill, at x3.5 a normal mob. The echo is a
        // copy of the player, so the health ladder is what decides whether it
        // is a fight or an execution, and 1.75 is the last step that still
        // loses to a character playing well.
        constexpr float HEALTH_RATIO = 1.25f;

        // The card's twenty yards, and its reward: "killing it grants five
        // kills' worth of XP".
        constexpr float  SPAWN_YARDS   = 20.0f;
        constexpr uint32 REWARD_KILLS  = 5;

        // Not on the card. The Echo is a fight the player chose, so it is given
        // the same two minutes the Shade gets to be fought or walked away from,
        // and the same lead to prepare in -- long enough to finish the pull you
        // are in, short enough that the warning is still a warning.
        constexpr uint32 LIFETIME_MS = 120000;   // TODO(design)
        constexpr uint32 WARN_LEAD_MS = 10000;   // TODO(design)

        // A rise deferred rather than cancelled, for the same reason the
        // Shade's is: the counter is full and the player is owed the fight.
        // Shorter than the Shade's, because the trigger was the player's own
        // kill and they are standing in the consequence of it.
        constexpr uint32 DEFER_MS = 10000;   // TODO(design)

        // Persistent, per plan section 3.3, which names "echo.kills" itself. A
        // player who logs out at 24 of 25 and comes back at 0 cannot plan
        // around the affix at all, and planning is the only decision it offers.
        constexpr char const* KEY_KILLS = "echo.kills";

        // "cloned appearance (cast spell 45204 'Clone Me' and 41055 copy-weapon
        // on it -- both used by the core's mirror-image script)".
        //
        // The two spells are cast in opposite directions and that is not a
        // slip. 45204 is cast by the *owner* at the copy, exactly as
        // npc_pet_mage_mirror_image does it
        // ($CORE/src/server/scripts/Pet/pet_mage.cpp:84,
        // `owner->CastSpell(me, SPELL_MAGE_CLONE_ME, true)`). 41055 has to go
        // the other way: spell_gen_clone_weapon makes the *hit unit* cast the
        // 41054 aura back at the caster (spell_generic.cpp:2286-2296), and
        // spell_gen_clone_weapon_aura::OnApply then reads the mainhand off the
        // aura's caster and writes it onto the aura's target
        // (spell_generic.cpp:2338-2360). So the Echo must cast 41055 at its
        // owner for the owner's weapon to end up in the Echo's hand.
        constexpr uint32 SPELL_CLONE_ME    = 45204;
        constexpr uint32 SPELL_COPY_WEAPON = 41055;


        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_ECHO);
            return def ? def->key : "echo";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        void Say(Player* player, std::string const& line)
        {
            if (!player || !player->GetSession())
                return;
            ChatHandler(player->GetSession()).PSendSysMessage("|cffff2020[Gauntlet]|r {}", line);
        }

        class Echo final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override;
            void OnDetach(Ctx& ctx) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;
            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;
            void OnKill(Ctx& ctx, Creature* killed) override;
            void OnPetKill(Ctx& ctx, Creature* killed) override { OnKill(ctx, killed); }
            void OnXP(Ctx& ctx, uint32& amount, Unit* victim) override;

            // BonusExperience, and the card is precise about it: the reward is
            // the kill, not the carrying. Nothing is returned here -- see
            // Reward() -- because a standing multiplier is exactly the bug
            // commit 04570c9 took out of the Shade.
            std::string Describe(AffixInstance const& self) const override;

        private:
            void  Bump(Ctx& ctx, Creature* killed);
            void  Arm(Ctx& ctx, uint32 inMs, uint32 warnMs);
            void  Rise(Ctx& ctx);
            void  Poll(Ctx& ctx);
            void  Reward(Ctx& ctx);
            void  ShowCounter(Ctx& ctx) const;
            int32 Threshold(Ctx const& /*ctx*/) const { return KILLS_PER_ECHO; }
            int32 Kills(Ctx& ctx) const;
            void  SetKills(Ctx& ctx, int32 value);

            // The Echo currently standing, and how much experience its death
            // is owed. `_owedXp` is filled at the moment the counter fills --
            // the average of the kills that filled it -- because by the time
            // the Echo dies the mobs that paid for it are long gone.
            ObjectGuid _guid;
            bool       _alive     = false;
            bool       _pending   = false;   // counter full, clock armed, nothing spawned yet
            uint64     _xpRunning = 0;       // experience seen since the last Echo
            uint32     _xpSamples = 0;
            uint32     _owedXp    = 0;
            uint32     _pollMs    = 0;
            int32      _transient = 0;       // only read when ctx.state is null
            bool       _paying    = false;   // inside Reward's own GiveXP; see there
        };

        // -------------------------------------------------------------------
        // The counter
        // -------------------------------------------------------------------

        int32 Echo::Kills(Ctx& ctx) const
        {
            int32 const stored = ctx.state ? ctx.state->Get(KEY_KILLS, 0) : _transient;
            return std::max(0, stored);
        }

        void Echo::SetKills(Ctx& ctx, int32 value)
        {
            _transient = value;
            if (ctx.state)
                ctx.state->Set(KEY_KILLS, value);
        }

        void Echo::ShowCounter(Ctx& ctx) const
        {
            if (ctx.player)
                AddonFor(ctx)->QueueCounter(ctx.player, MechanicKey(),
                                            uint32(Kills(ctx)), uint32(Threshold(ctx)));
        }

        void Echo::OnAttach(Ctx& ctx)
        {
            _guid.Clear();
            _alive     = false;
            _pending   = false;
            _xpRunning = 0;
            _xpSamples = 0;
            _owedXp    = 0;
            _pollMs    = 0;
            _transient = 0;

            if (ctx.state)
                _transient = std::max(0, ctx.state->Get(KEY_KILLS, 0));

            // A counter that is already full when the session starts is armed
            // straight away rather than waiting for one more kill: the player
            // earned it last session and the clock's own grace window is what
            // keeps it off them until they have taken control.
            if (Kills(ctx) >= Threshold(ctx))
                Arm(ctx, WARN_LEAD_MS, WARN_LEAD_MS);

            ShowCounter(ctx);
        }

        void Echo::OnDetach(Ctx& ctx)
        {
            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_ECHO);

            if (_alive)
                AddonFor(ctx)->SendSummon(ctx.player, MechanicKey(), false);

            if (ctx.player)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_ECHO);

            _guid.Clear();
            _alive   = false;
            _pending = false;
        }

        void Echo::OnKill(Ctx& ctx, Creature* killed)
        {
            if (!killed)
                return;

            // The Echo's own death is the reward, not a step toward the next
            // one: counting it would let a player farm the counter with the
            // thing the counter produced.
            if (_alive && killed->GetGUID() == _guid)
            {
                _alive = false;
                _guid.Clear();
                AddonFor(ctx)->SendSummon(ctx.player, MechanicKey(), false);
                Reward(ctx);
                return;
            }

            if (sGauntletSummons->IsGauntletSummon(killed))
                return;   // nothing this module spawned ever advances a counter

            Bump(ctx, killed);
        }

        void Echo::Bump(Ctx& ctx, Creature* /*killed*/)
        {
            // One Echo at a time. The counter keeps running underneath, so a
            // player who leaves the last one standing is not also banking a
            // second: it stops at the threshold, exactly as Champions' does.
            int32 const max  = Threshold(ctx);
            int32 const next = std::min(Kills(ctx) + 1, max);
            SetKills(ctx, next);
            ShowCounter(ctx);

            if (next < max || _alive || _pending)
                return;

            Arm(ctx, WARN_LEAD_MS, WARN_LEAD_MS);
        }

        // -------------------------------------------------------------------
        // The rise
        // -------------------------------------------------------------------

        void Echo::Arm(Ctx& ctx, uint32 inMs, uint32 warnMs)
        {
            if (!ctx.clock)
                return;
            _pending = true;
            ctx.clock->Arm(MECHANIC_ECHO, EVENT_RISE, inMs, warnMs);
        }

        void Echo::OnWarn(Ctx& ctx, uint32 eventId)
        {
            if (eventId != EVENT_RISE || !ctx.player || _alive)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            AddonFor(ctx)->SendEvent(ctx.player, MechanicKey(), WARN_LEAD_MS / 1000u, "Echo");
            Say(ctx.player, "Something of yours is coming loose. An Echo will step out of you in "
                            + std::to_string(WARN_LEAD_MS / 1000u) + " seconds.");
        }

        void Echo::OnEvent(Ctx& ctx, uint32 eventId)
        {
            if (eventId != EVENT_RISE)
                return;

            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            if (_alive)
            {
                _pending = false;
                return;
            }

            // The one condition this file owns. Everything else the scheduler
            // has already applied: mounted, in flight, in a sanctuary, dead,
            // inside the grace window, with an offer on the table.
            if (!player->IsInWorld() || !player->IsAlive())
            {
                Arm(ctx, DEFER_MS, 0);
                return;
            }

            Rise(ctx);
        }

        void Echo::Rise(Ctx& ctx)
        {
            Player* player = ctx.player;

            // Twenty yards, and in front rather than behind: the card's whole
            // point is that this one is not an ambush. GetFirstCollisionPosition
            // walks the offset rather than teleporting into geometry
            // (Object.cpp:2979-2992); the angle is relative to the player's own
            // facing, so 0 is straight ahead.
            Position const at = player->GetFirstCollisionPosition(SPAWN_YARDS, 0.0f);

            Creature* echo = sGauntletSummons->Summon(player, ENTRY_DOPPELGANGER, at, LIFETIME_MS,
                                                      /*countsAsStalker*/ true, MECHANIC_ECHO);
            if (!echo)
            {
                // The caps refused, which they may legitimately do while
                // another spawn affix has something in the world. Re-arm and
                // keep the counter full: the player is still owed the fight.
                Arm(ctx, DEFER_MS, 0);
                return;
            }

            sGauntletSummons->Scale(echo, HEALTH_RATIO, 1.0f);

            // The copy, in the two directions the core's own scripts use them.
            player->CastSpell(echo, SPELL_CLONE_ME, true);
            echo->CastSpell(player, SPELL_COPY_WEAPON, true);

            // The sound cue design section 2.10 singles out in Isaac's Dark
            // Esau: "he will pause for a brief moment with a sound cue". This
            // is the module's, and it is what a player with no addon hears.
            echo->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);                   // Unit.h:1943

            _guid   = echo->GetGUID();
            _alive  = true;
            _pending = false;

            // What the fight is worth, banked now. The card says "five kills'
            // worth of XP", and the honest measure of a kill is the average of
            // the ones that filled the counter -- which is only knowable here,
            // because by the time the Echo dies its victims are gone.
            _owedXp = _xpSamples != 0
                    ? uint32(std::min<uint64>(_xpRunning / _xpSamples * REWARD_KILLS,
                                              std::numeric_limits<uint32>::max()))
                    : 0;

            SetKills(ctx, 0);
            _xpRunning = 0;
            _xpSamples = 0;
            ShowCounter(ctx);

            Addon* addon = AddonFor(ctx);
            addon->SendEvent(player, MechanicKey(), 0, "Echo");
            addon->SendSummon(player, MechanicKey(), true);

            Say(player, "An Echo of you steps out and turns around.");
        }

        // -------------------------------------------------------------------
        // The end of one Echo
        // -------------------------------------------------------------------

        void Echo::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (!_alive)
                return;

            _pollMs += diffMs;
            if (_pollMs < Scheduler::TICK_MS)
                return;
            _pollMs = 0;

            Poll(ctx);
        }

        void Echo::Poll(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld())
                return;

            // Resolved through the player's own map, so this answers null the
            // moment the Echo is no longer in the world with its owner:
            // despawned on the leash, timed out, or left behind by a zone
            // change (ObjectAccessor.h:70). Every one of those is the card's
            // "or run -- it leashes like any mob", and none of them pays.
            Creature* echo = ObjectAccessor::GetCreature(*player, _guid);
            if (echo && echo->IsAlive())
                return;

            bool const killed = echo != nullptr;   // a corpse, so somebody landed it

            _alive = false;
            _guid.Clear();
            AddonFor(ctx)->SendSummon(ctx.player, MechanicKey(), false);

            // OnKill has usually got here first; this is the path for a
            // groupmate's killing blow, which the card allows ("it is
            // humanoid: sap, polymorph, fear all work" -- anyone may fight it).
            if (killed)
                Reward(ctx);
        }

        void Echo::Reward(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            uint32 const owed = _owedXp;
            _owedXp = 0;

            if (owed == 0)
            {
                Say(player, "The Echo falls silent.");
                return;
            }

            // Player::GiveXP fires OnPlayerGiveXP, which comes back into OnXP
            // below -- including this instance's. Without the flag the reward
            // would be folded into the average that sizes the *next* reward,
            // and each Echo would pay more than the last for no reason a player
            // could see. The flag is cleared before Say() so nothing after it
            // is inside the window.
            _paying = true;
            player->GiveXP(owed, nullptr);
            _paying = false;

            Say(player, "The Echo falls silent, and what it took comes back: "
                        + std::to_string(owed) + " experience.");
        }

        void Echo::OnXP(Ctx& /*ctx*/, uint32& amount, Unit* victim)
        {
            // An observer only, and this is where the "five kills' worth" is
            // measured. A creature this module summoned is excluded: its
            // experience is already halved by Gauntlet.Summons.XpRate and
            // letting it into the average would make each Echo cheaper than
            // the last.
            if (amount == 0 || !victim || _paying)
                return;

            Creature* creature = victim->ToCreature();
            if (creature && sGauntletSummons->IsGauntletSummon(creature))
                return;

            _xpRunning += amount;
            ++_xpSamples;
        }

        std::string Echo::Describe(AffixInstance const& self) const
        {

            std::string out = "Every " + std::to_string(KILLS_PER_ECHO)
                            + "th enemy you kill returns as an Echo of yourself: your face, your"
                              " weapon, and "
                            + "two and a half times"
                            + " a normal enemy's health. The counter is on your screen, so you"
                              " choose which kill is the one.";

            out += " Killing it pays five kills' worth of experience.";
            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(2, Echo);
}
