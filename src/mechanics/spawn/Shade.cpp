/*
 * mod-gauntlet - S1 The Shade: it rises behind you and it does not stop
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "GauntletState.h"
#include "GauntletSummons.h"

#include "Chat.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

// Registry id 1. Design section 3, card S1: "A Shade rises behind you every few
// minutes and hunts you until you kill it or leave it behind."
//
// This is the out-of-combat pressure affix, and the whole decision it offers is
// *where* you meet it: it is slower than you and far slower than a mount, so
// you either outpace it or pick cleared ground and turn. That decision only
// exists if the player can see it coming, which is why the telegraph and the
// stalker-alive indicator below are not decoration -- they are the mechanic.
//
// The creature itself belongs to GauntletSummons: the caps, the owner binding,
// the levelling, the leash and every despawn path live there, and nothing here
// calls SummonCreature. What is left for this file is the clock, the choice of
// ground, the nemesis bookkeeping and the reward.
//
// Everything is inside namespace Gauntlet, so the two names that collide with
// core globals -- Condition and MECHANIC_NONE -- resolve to the module's own.
// Neither is spelled here; this mechanic's id is a named constant.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_SHADE = 1;

        // The clock carries exactly one event: the next rise. `id` is the
        // mechanic's own tag for it, and arming the same tag again is a
        // reschedule (GauntletScheduler.cpp:113-123), which is what every
        // deferral below relies on.
        constexpr uint32 EVENT_RISE = 1;

        // The card's severity ladder: interval 15 -> 10 -> 7 minutes.
        constexpr uint32 INTERVAL_MS[MAX_RANK] = { 900000u, 600000u, 420000u };

        // The card's other ladder is health x1.5 -> x2 -> x2.5 of a normal mob.
        // creature_template row 900001 already carries HealthModifier 1.5, so
        // what is left for code is the ratio to it -- the SQL says so in its
        // own comment. Damage is a single figure on the card (~1.2x) with no
        // ladder, and the template carries that too, so rank alone changes
        // nothing about how hard it hits.
        constexpr float HEALTH_RATIO[MAX_RANK] = { 1.0f, 4.0f / 3.0f, 5.0f / 3.0f };

        // The card's numbers, unaltered. The 150 yd / 15 s leash is absent
        // because it is not this mechanic's to enforce: GauntletSummons.h
        // applies SUMMON_LEASH_YARDS and SUMMON_LEASH_MS to every summon, and
        // those are this card's numbers already.
        constexpr float  SPAWN_BEHIND_YARDS = 35.0f;
        constexpr uint32 LIFETIME_MS        = 120000;

        // How much warning the player gets. The card says the addon shows a
        // countdown and that you must never let it land mid-pull, but not how
        // long the countdown is. Thirty seconds is long enough to finish the
        // pull you are in and pick your ground, short enough to still be a
        // warning rather than a schedule.
        constexpr uint32 WARN_LEAD_MS = 30000;   // TODO(design)

        // The clock landing in combat, in a rest area, on a mount or inside a
        // boss encounter defers; it never cancels. Two retries rather than one,
        // because a deferred fire is still a fire: Scheduler::Step stamps
        // _lastFireMs on every one it releases (GauntletScheduler.cpp:329-331),
        // so a deferral that polls too eagerly spends the 12 s minimum spacing
        // that the other timed affixes are queueing behind.
        //
        // Twenty seconds for a state the player is about to leave -- a fight,
        // an encounter -- so the Shade arrives while the fight that delayed it
        // is still the reason they are standing where they are, and a
        // chain-puller is not polled between every mob. A minute for a state
        // they have settled into: a mount, a flight path, an inn, a
        // battleground. Neither re-arm carries a fresh telegraph; the countdown
        // has already reached zero and the player is looking at what held it.
        constexpr uint32 DEFER_BUSY_MS    = 20000;   // TODO(design)
        constexpr uint32 DEFER_SETTLED_MS = 60000;   // TODO(design)

        // Vindication, from the card: +25% experience for five minutes.
        constexpr uint32 VINDICATION_MS  = 300000;
        constexpr uint32 VINDICATION_PCT = 25;

        // The nemesis rule, rank III. Each escape returns it one rank stronger;
        // each kill buys two tiers of quiet. "One rank stronger" is read as one
        // more step of the card's own ladder -- +0.5x health on the template's
        // 1.5x base, which is +1/3 of what the creature already has -- because
        // that is the only step size the card states. Damage has no ladder to
        // continue, so a step adds a tenth. The ceiling is a choice: four steps
        // puts it at 4.5x a normal mob's health and 1.68x its damage, which is
        // still a fight a level-appropriate character can win on ground of
        // their own choosing, which is the counterplay the card promises.
        constexpr int32 NEMESIS_MAX_STEPS   = 4;             // TODO(design)
        constexpr float NEMESIS_HEALTH_STEP = 1.0f / 3.0f;
        constexpr float NEMESIS_DAMAGE_STEP = 0.1f;          // TODO(design)
        constexpr int32 NEMESIS_QUIET_TIERS = 2;

        // Persistent, in gauntlet_state, under the registry key. Plan section
        // 3.3 names the first two itself.
        //
        //   shade.rank           how many times it has been left behind
        //   shade.deadUntilTier  the tier at which it may rise again
        //   shade.alive          a Shade was standing when the session ended
        //
        // The third is the only one the plan does not name, and it exists
        // because "it despawned without dying" has to survive the one despawn
        // this mechanic cannot watch happen: the owner logging out, crashing,
        // or the realm going down mid-hunt. It is written when a Shade rises
        // and cleared when that hunt is settled, so a session that ends in the
        // middle of one leaves a 1 behind and the next OnAttach reads it as the
        // escape it was.
        //
        // The clock is deliberately *not* persisted. Plan section 3.3 lists the
        // transient state -- "Frenzy stacks, Falling Sky clock, Falter clock"
        // -- and a countdown is the same kind of thing: a fact about the
        // current session. A stored due time would have to be reconciled
        // against time spent logged out, which the design never asks for and
        // which would let a character bank fifteen minutes of pressure while
        // offline. The consequence, stated in the report, is that logging out
        // restarts the countdown.
        constexpr char const* KEY_RANK  = "shade.rank";
        constexpr char const* KEY_DEAD  = "shade.deadUntilTier";
        constexpr char const* KEY_ALIVE = "shade.alive";

        // One named creature per run, its name taken from the seed. The name
        // reaches the player through the module's own chat lines and the addon
        // and not through the nameplate: SMSG_CREATURE_QUERY_RESPONSE answers
        // out of CreatureTemplate::Name for the entry rather than out of the
        // creature (QueryHandler.cpp:88-101), so a live rename would leave the
        // client still calling it "Shade". Renaming it anyway would put two
        // different names on one creature, which is worse than having one.
        constexpr char const* NEMESIS_NAMES[] =
        {
            "Ashgrieve",  "Coldwake",   "Dunmara",    "Emberthrall",
            "Gallowmind", "Hollowfen",  "Ithrenn",    "Karloth",
            "Lamenthal",  "Mournsend",  "Nachtvold",  "Orrimane",
            "Pyrewake",   "Rathgloom",  "Sablemourn", "Thessaly",
            "Umbriel",    "Vhaldrec",   "Wraithkell", "Yorrund"
        };

        std::string NemesisName(uint32 seed)
        {
            return NEMESIS_NAMES[seed % uint32(std::size(NEMESIS_NAMES))];
        }

        uint8 RankOf(AffixInstance const* self)
        {
            if (!self)
                return 1;
            return self->rank < 1 ? 1 : (self->rank > MAX_RANK ? MAX_RANK : self->rank);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_SHADE);
            return def ? def->key : "shade";
        }

        // Ctx::addon is filled by integration and is null everywhere Phase 0
        // builds a Ctx. The singleton is the same object, so falling back to it
        // means the telegraph works whether or not that wiring has landed, and
        // there is exactly one Addon either way. Every entry point on it
        // tolerates a null Player (GauntletAddon.cpp:165-168, :263-266).
        Addon* AddonFor(Ctx& ctx)
        {
            return ctx.addon ? ctx.addon : sGauntletAddon;
        }

        void Say(Player* player, std::string const& line)
        {
            if (!player || !player->GetSession())
                return;
            ChatHandler(player->GetSession()).PSendSysMessage("|cffff2020[Gauntlet]|r {}", line);
        }

        // Whether a Shade may rise *now*, and if not, what kind of "not":
        // Busy is a state the player is about to leave, Settled is one they
        // have chosen to be in. The two get different retries, above.
        enum class Ground : uint8 { Ready, Busy, Settled };

        // The scheduler already holds events while the owner is mounted, in
        // flight, dead, in a sanctuary, inside the login grace window or
        // looking at an offer (GauntletScheduler.h, Suppression), and holding
        // is not dropping -- so what is left here is the conditions it cannot
        // see, plus the ones it can, checked again because a mechanic that puts
        // a creature into the world should not rest on somebody else's flag
        // having been filled in.
        //
        // The settled tests come first on purpose: a player dazed off a mount
        // mid-journey is still travelling, and polling them every twenty
        // seconds for the rest of the road is what the two intervals exist to
        // avoid.
        Ground ReadGround(Player* player)
        {
            if (!player->IsInWorld() || !player->IsAlive())
                return Ground::Settled;

            // The card: the clock runs "whenever the owner is alive, out of a
            // rest area and not mounted".
            if (player->IsMounted() || player->IsInFlight())                // Unit.h:1887, :1709
                return Ground::Settled;
            if (player->HasRestFlag(REST_FLAG_IN_TAVERN) ||                 // Player.h:1221, :806-809
                player->HasRestFlag(REST_FLAG_IN_CITY))
                return Ground::Settled;

            Map* map = player->GetMap();
            if (!map)
                return Ground::Settled;

            // Not on the card and not in the plan, so this one is a choice: a
            // creature only its owner may fight, standing in a battleground or
            // an arena, is scenery in somebody else's match and cannot be
            // "left behind" in any sense the card's counterplay recognises.
            if (map->IsBattlegroundOrArena())                               // Map.h:307
                return Ground::Settled;

            // And it lands only when the owner is out of combat.
            if (player->IsInCombat())                                       // Unit.h:936
                return Ground::Busy;

            // Plan section 6, decision 4: it spawns in group dungeons too,
            // never while a boss encounter is in progress.
            // WorldObject::GetInstanceScript answers null outside an instance
            // (Object.cpp:1242-1246).
            if (InstanceScript const* instance = player->GetInstanceScript())   // Object.h:527
                if (instance->IsEncounterInProgress())                          // InstanceScript.h:169
                    return Ground::Busy;

            return Ground::Ready;
        }

        class Shade final : public IMechanic
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

            std::string Describe(AffixInstance const& self) const override;

        private:
            // Where one hunt has got to. Dead is not the same as None: the
            // corpse lingers for the normal decay, and the difference between
            // "you killed it" and "it left" is the whole nemesis rule, so both
            // are recorded when they happen rather than inferred afterwards.
            enum class Hunt : uint8 { None, Alive, Dead };

            void  ArmNext(Ctx& ctx);
            void  Rise(Ctx& ctx);
            void  Poll(Ctx& ctx);
            void  Settle(Ctx& ctx, bool killed);
            void  Vindicate(Ctx& ctx);
            void  CountEscape(Ctx& ctx);
            bool  NemesisAsleep(Ctx& ctx) const;
            int32 NemesisStep(Ctx& ctx) const;
            bool  IsNemesis(Ctx& ctx) const { return RankOf(ctx.self) >= MAX_RANK; }
            std::string Label(Ctx& ctx) const;

            ObjectGuid _guid;                 // the Shade currently hunting
            Hunt       _hunt        = Hunt::None;
            uint32     _vindicateMs = 0;      // transient: five minutes, this session only
            uint32     _pollMs      = 0;      // accumulator in front of Poll
        };

        // -------------------------------------------------------------------
        // Lifecycle
        // -------------------------------------------------------------------

        void Shade::OnAttach(Ctx& ctx)
        {
            _guid.Clear();
            _hunt        = Hunt::None;
            _vindicateMs = 0;
            _pollMs      = 0;

            // A hunt that was still running when the last session ended. The
            // Shade was taken out of the world by the summon wrapper's logout
            // path and never died, which is precisely the card's "despawns
            // without dying" -- so it is credited here, where the answer is
            // durable, rather than in OnDetach, where a swap and a logout look
            // identical and a crash looks like neither.
            if (ctx.state && ctx.state->Get(KEY_ALIVE, 0) != 0)
            {
                ctx.state->Set(KEY_ALIVE, 0);
                CountEscape(ctx);
            }

            ArmNext(ctx);
        }

        void Shade::OnDetach(Ctx& ctx)
        {
            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_SHADE);

            // Swap or logout. Either way this affix stops existing and nothing
            // it put into the world may outlive it. DespawnFor takes out the
            // Shade specifically rather than everything the player owns, so a
            // second spawn affix keeps its own creature.
            if (_hunt != Hunt::None)
                AddonFor(ctx)->SendSummon(ctx.player, MechanicKey(), false);

            if (ctx.player)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_SHADE);

            // shade.alive is deliberately left standing. If a Shade was up, the
            // next OnAttach reads it as an escape; clearing it here would make
            // logging out the way to walk away from the nemesis for nothing.
            _guid.Clear();
            _hunt = Hunt::None;
        }

        // -------------------------------------------------------------------
        // The clock
        // -------------------------------------------------------------------

        void Shade::ArmNext(Ctx& ctx)
        {
            if (!ctx.clock)
                return;

            uint32 const interval = INTERVAL_MS[RankOf(ctx.self) - 1];

            // A telegraph for something that is not going to happen is a lie,
            // so a sleeping nemesis arms the fire alone. The fire still runs,
            // because it is what notices the tier catching up.
            uint32 const warn = NemesisAsleep(ctx) ? 0u : WARN_LEAD_MS;

            ctx.clock->Arm(MECHANIC_SHADE, EVENT_RISE, interval, warn);
        }

        void Shade::OnWarn(Ctx& ctx, uint32 eventId)
        {
            if (eventId != EVENT_RISE || !ctx.player)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            // A countdown to a rise that cannot happen because one is already
            // hunting would be the same lie as above. The clock stays armed;
            // only the announcement is skipped.
            if (_hunt != Hunt::None)
                return;

            std::string const label = Label(ctx);

            // EVT key secs label. This is the countdown the card's counterplay
            // depends on: without it the player cannot choose the ground, which
            // is the only decision the affix offers.
            AddonFor(ctx)->SendEvent(ctx.player, MechanicKey(), WARN_LEAD_MS / 1000u, label);

            // And the same thing for a player with no addon, because the module
            // has to be playable without one.
            Say(ctx.player, label + " will rise behind you in " +
                            std::to_string(WARN_LEAD_MS / 1000u) + " seconds.");
        }

        void Shade::OnEvent(Ctx& ctx, uint32 eventId)
        {
            if (eventId != EVENT_RISE)
                return;

            Player* player = ctx.player;
            if (!player)
                return;

            // With no clock nothing can re-arm, and a Shade that rises once and
            // never again is worse than one that never rises: the player would
            // learn a cadence that does not exist. `.gauntlet debug` can build
            // a Ctx outside a run, and this is that case.
            if (!ctx.clock)
                return;

            // A retired run has no more events left in it.
            if (ctx.run && ctx.run->dead)
                return;

            if (_hunt != Hunt::None)
            {
                // One at a time, but check rather than assume: if OnTick is
                // ever not wired, this is the only place that would notice the
                // last hunt had ended, and the affix must not stall on it.
                // Settle() re-arms, so only the still-hunting case does.
                Poll(ctx);
                if (_hunt != Hunt::None)
                    ArmNext(ctx);
                return;
            }

            if (NemesisAsleep(ctx))
            {
                ArmNext(ctx);
                return;
            }

            Ground const ground = ReadGround(player);
            if (ground != Ground::Ready)
            {
                // Defer, never cancel, and with no fresh telegraph: the
                // countdown has already run out and the player is looking at
                // the reason it has not landed.
                ctx.clock->Arm(MECHANIC_SHADE, EVENT_RISE,
                               ground == Ground::Busy ? DEFER_BUSY_MS : DEFER_SETTLED_MS, 0);
                return;
            }

            Rise(ctx);
        }

        // -------------------------------------------------------------------
        // The rise
        // -------------------------------------------------------------------

        void Shade::Rise(Ctx& ctx)
        {
            Player* player = ctx.player;

            // 35 yd behind, from the card. The angle is relative to the
            // player's own facing (Object.cpp:2979-2992), so pi is directly
            // behind them, and the collision walk keeps the Shade out of the
            // geometry a plain offset would drop it inside. The position keeps
            // the player's orientation, which for something standing behind
            // them means it is already looking at their back.
            Position const at = player->GetFirstCollisionPosition(SPAWN_BEHIND_YARDS, float(M_PI));

            // The only way this module puts a creature into the world. It
            // levels the Shade to its owner, records the guid so every despawn
            // path can find it, and refuses past the caps by returning null --
            // at which point the wrapper's contract is that the caller re-arms
            // rather than retrying (GauntletSummons.h).
            Creature* shade = sGauntletSummons->Summon(player, ENTRY_SHADE, at, LIFETIME_MS,
                                                       /*countsAsStalker*/ true, MECHANIC_SHADE);
            if (!shade)
            {
                ArmNext(ctx);
                return;
            }

            int32 const step = NemesisStep(ctx);
            sGauntletSummons->Scale(shade,
                                    HEALTH_RATIO[RankOf(ctx.self) - 1] + float(step) * NEMESIS_HEALTH_STEP,
                                    1.0f + float(step) * NEMESIS_DAMAGE_STEP);

            // The card's emote. A one-shot animation rather than a line of
            // monster chat, because the creature's nameplate says "Shade" and
            // its chat would have to say the same; the name belongs to the
            // module's own lines below, where it is consistent.
            shade->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);                  // Unit.h:1943

            _guid = shade->GetGUID();
            _hunt = Hunt::Alive;

            if (ctx.state)
                ctx.state->Set(KEY_ALIVE, 1);

            std::string const label = Label(ctx);

            Addon* addon = AddonFor(ctx);
            addon->SendEvent(player, MechanicKey(), 0, label);   // secs == 0: it landed
            addon->SendSummon(player, MechanicKey(), true);

            Say(player, label + " rises behind you.");

            // Keep the clock armed even while a hunt runs. Settle() re-arms
            // from the resolution, which is the cadence that matters, but a
            // clock that is only ever armed there would stop dead if a hunt
            // somehow never resolved.
            ArmNext(ctx);
        }

        // -------------------------------------------------------------------
        // The end of one hunt
        // -------------------------------------------------------------------

        void Shade::Poll(Ctx& ctx)
        {
            if (_hunt == Hunt::None)
                return;

            Player* player = ctx.player;
            if (!player || !player->IsInWorld())
                return;

            // Resolved through the player's own map (ObjectAccessor.h:70), so
            // this answers null the moment the Shade is no longer in the world
            // *with its owner*: despawned on the leash, timed out at 120 s,
            // taken away with the grid, or left behind by a zone change. Every
            // one of those is the card's "despawns without dying".
            Creature* shade = ObjectAccessor::GetCreature(*player, _guid);

            if (shade && shade->IsAlive())
                return;

            if (_hunt == Hunt::Alive)
            {
                // A corpse, and not necessarily one this player made: the card
                // says anyone may damage it, so the kill is credited to the
                // owner however it landed. OnKill has usually got here first
                // and left _hunt at Dead; this is the path for a groupmate's
                // killing blow.
                Settle(ctx, /*killed*/ shade != nullptr);
                return;
            }

            // _hunt == Dead: the corpse has finally gone. Nothing left to
            // settle, only the guid to forget.
            if (!shade)
            {
                _hunt = Hunt::None;
                _guid.Clear();
            }
        }

        void Shade::Settle(Ctx& ctx, bool killed)
        {
            if (ctx.state)
                ctx.state->Set(KEY_ALIVE, 0);

            AddonFor(ctx)->SendSummon(ctx.player, MechanicKey(), false);

            if (killed)
            {
                _hunt = Hunt::Dead;     // the corpse is still there; keep the guid
                Vindicate(ctx);

                if (IsNemesis(ctx) && ctx.state && ctx.run)
                {
                    ctx.state->Set(KEY_DEAD, int32(ctx.run->tier) + NEMESIS_QUIET_TIERS);
                    Say(ctx.player, Label(ctx) + " will not rise again for two tiers.");
                }
            }
            else
            {
                _hunt = Hunt::None;
                _guid.Clear();
                CountEscape(ctx);
            }

            // Re-armed from the resolution rather than from the rise, so the
            // cadence reads as "this long after the last one was settled" and a
            // hunt that runs long does not queue the next one up behind it. It
            // also puts the telegraph decision after the kill that may just
            // have sent the nemesis to sleep.
            ArmNext(ctx);
        }

        void Shade::OnKill(Ctx& ctx, Creature* killed)
        {
            if (!killed || _hunt != Hunt::Alive)
                return;
            if (killed->GetGUID() != _guid)
                return;

            Settle(ctx, /*killed*/ true);
        }

        void Shade::Vindicate(Ctx& ctx)
        {
            _vindicateMs = VINDICATION_MS;

            AddonFor(ctx)->QueueStat(ctx.player, "vindication", int32(VINDICATION_PCT));

            Say(ctx.player, "Vindication: you gain " + std::to_string(VINDICATION_PCT) +
                            "% more experience for five minutes.");
        }

        void Shade::CountEscape(Ctx& ctx)
        {
            // The nemesis rule is the card's rank III. At ranks I and II the
            // Shade is not a named thing that remembers, so leaving it behind
            // costs nothing and killing it buys nothing. The counters are still
            // kept clean, so a run that ranks up later starts its nemesis from
            // zero rather than from whatever happened before the rule applied.
            if (!IsNemesis(ctx) || !ctx.state)
                return;

            int32 const step = ctx.state->Get(KEY_RANK, 0);
            if (step >= NEMESIS_MAX_STEPS)
            {
                Say(ctx.player, Label(ctx) + " is left behind. It is already as strong as it becomes.");
                return;
            }

            ctx.state->Set(KEY_RANK, step + 1);
            Say(ctx.player, Label(ctx) + " is left behind, and will return stronger.");
        }

        bool Shade::NemesisAsleep(Ctx& ctx) const
        {
            if (!ctx.state || !ctx.run)
                return false;
            return int32(ctx.run->tier) < ctx.state->Get(KEY_DEAD, 0);
        }

        int32 Shade::NemesisStep(Ctx& ctx) const
        {
            if (!IsNemesis(ctx) || !ctx.state)
                return 0;
            return std::clamp(ctx.state->Get(KEY_RANK, 0), 0, NEMESIS_MAX_STEPS);
        }

        std::string Shade::Label(Ctx& ctx) const
        {
            if (IsNemesis(ctx) && ctx.run)
                return NemesisName(ctx.run->seed);
            return "The Shade";
        }

        // -------------------------------------------------------------------
        // The tick: Vindication's five minutes, and how a hunt ends
        // -------------------------------------------------------------------

        void Shade::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (_vindicateMs != 0)
            {
                if (_vindicateMs <= diffMs)
                {
                    _vindicateMs = 0;
                    AddonFor(ctx)->QueueStat(ctx.player, "vindication", 0);
                    Say(ctx.player, "Vindication fades.");
                }
                else
                {
                    _vindicateMs -= diffMs;
                }
            }

            if (_hunt == Hunt::None)
                return;

            // OnTick is documented as a 500 ms hook but is driven from
            // OnPlayerUpdate, which is the world tick. Throttling here rather
            // than trusting the caller keeps the object lookup in Poll at two a
            // second whatever cadence integration settles on. This is an
            // accumulator in front of a poll and not a clock: nothing the
            // player is ever shown is timed by it.
            _pollMs += diffMs;
            if (_pollMs < Scheduler::TICK_MS)
                return;
            _pollMs = 0;

            Poll(ctx);
        }

        // -------------------------------------------------------------------
        // The reward
        // -------------------------------------------------------------------

        void Shade::OnXP(Ctx& /*ctx*/, uint32& amount, Unit* /*victim*/)
        {
            if (_vindicateMs == 0 || amount == 0)
                return;

            // Every source, because the card says "experience" and not "kill
            // experience". uint64 on the way through: the multiply is safe for
            // any amount the game can produce, and the clamp costs nothing.
            uint64 const boosted = uint64(amount) * uint64(100u + VINDICATION_PCT) / 100u;
            amount = uint32(std::min<uint64>(boosted, 0xFFFFFFFFull));
        }

        std::string Shade::Describe(AffixInstance const& self) const
        {
            uint8 const rank = RankOf(&self);

            std::string out = "A Shade rises behind you every " +
                              std::to_string(INTERVAL_MS[rank - 1] / 60000u) +
                              " minutes and hunts you until you kill it or leave it behind."
                              " It is slower than you are, and much slower than a mount.";

            if (rank >= MAX_RANK)
                out += " It is one named creature: every time you leave it behind it returns"
                       " stronger, and killing it keeps it down for two tiers.";

            out += " Killing it grants Vindication: " + std::to_string(VINDICATION_PCT) +
                   "% more experience for five minutes.";

            return out;
        }
    }

    GAUNTLET_MECHANIC(1, Shade);
}
