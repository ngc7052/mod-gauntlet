/*
 * mod-gauntlet - T1 Falling Sky: the sky marks your spot, then it strikes
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"
#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"

#include <string>
#include <iterator>

// Registry id 14, key "falling_sky", design section 4 T1: "In combat, every
// 20 seconds the sky marks your spot; three seconds later it strikes."
//
// The whole mechanic is the scheduler's two-step and nothing else. The mark is
// the Warn and the strike is the Fire, so there is no clock here: Arm() asks
// for one pair, the framework calls OnWarn and then OnEvent, and OnEvent asks
// for the next pair. What is left over is a position, a distance and a
// percentage of maximum health.
//
// One rule belongs to this file and the rest do not, which is worth being
// explicit about. "Never out of combat" is this mechanic's; mounted, in
// flight, in a sanctuary, dead, inside the login grace window and with an
// offer on the table are the scheduler's, and Scheduler::Step has already
// applied every one of them before OnWarn or OnEvent is ever reached.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 FALLING_SKY = 14;

        // Both ladders are stated on the card -- cadence 25/20/15 s, severity
        // 25/35/50% of maximum health -- so neither is a chosen number.
        // How long you may stand still, in combat, before the sky notices.
        //
        // This used to be a cadence: every N seconds, wherever you were and
        // whatever you were doing, the sky marked you and you stepped sideways.
        // A metronome is not a decision -- you are not choosing anything, you
        // are waiting for a beep -- and it was reported from play as one of six
        // cards that felt like taxes.
        //
        // The sky now marks the ground you have refused to leave, so the card
        // has a verb and the verb is movement. Stand and cast and it finds you;
        // keep moving and it never does. See docs/tempo-redesign.md.
        //
        // The lowest rank is deliberately generous: eight seconds is longer
        // than any cast in the game, so rank I asks a caster to weave rather
        // than to stop casting.
        constexpr uint32 STILL_MS[]     = { 8000, 6000, 4500, 3000 };
        static_assert(std::size(STILL_MS) >= MAX_RANK, "STILL_MS is short a rank");

        // How far counts as having moved. Wide enough that turning on the spot
        // or a step of melee shuffle is not movement, short enough that leaving
        // the mark is unambiguous.
        constexpr float MOVED_YARDS = 5.0f;
        // Rank IV is past the card at 12 s and 65%. The three-second warning
        // does not move at any rank, so the ladder prices standing still and
        // never shortens the answer to it.
        constexpr uint32 SEVERITY_PCT[] = { 25, 35, 50, 65 };
        static_assert(std::size(SEVERITY_PCT) >= MAX_RANK, "SEVERITY_PCT is short a rank");

        // The card's three seconds. Handed to Scheduler::Arm as the warning's
        // lead rather than counted here; Budget() stretches the cadence and
        // deliberately never the lead (GauntletScheduler.cpp), so a player
        // carrying four timed affixes still gets the full three seconds to
        // move.
        constexpr uint32 WARN_MS = 3000;

        // "within 4 yd of the mark". Not a chosen number either: it is the
        // radius of the circle the player can actually see. Spell 30632's
        // EffectRadiusIndex resolves to 4.0 yd in SpellRadius.dbc, so the
        // danger zone and the graphic are the same thing by construction
        // rather than by two constants that can drift apart.
        constexpr float MARK_RADIUS = 4.0f;

        // The visual, and the reason this affix needs no client patch. Spell
        // 30632 "Debris" is Magtheridon's ceiling collapse: Spell.dbc gives it
        // Effect[0] = 27 SPELL_EFFECT_PERSISTENT_AREA_AURA with a bare
        // SPELL_AURA_DUMMY, base points 0, no triggered spell and no damage
        // effect, which is to say its entire job is to draw a circle on the
        // ground for five seconds. AzerothCore's own script uses it in exactly
        // the shape this affix wants -- a trigger casts it on itself, waits,
        // and only then deals the damage -- at
        // src/server/scripts/Outland/HellfireCitadel/MagtheridonsLair/
        // boss_magtheridon.cpp:253-258.
        constexpr uint32 SPELL_GROUND_MARK = 30632;

        // How long that circle lasts, read off the same DBC row (duration
        // index 28, 5000 ms). The strike is refused once the mark has faded:
        // the scheduler can hold a Fire back behind the minimum spacing, and a
        // hit whose telegraph is no longer on the ground is not a hit anyone
        // can dodge. Design section 4.8 is the reason this is a rule and not a
        // tolerance.
        constexpr uint32 MARK_VISIBLE_MS = 5000;


        // The trigger's own backstop, and the one invented number in this
        // file. The strike despawns it explicitly, so this only matters when
        // the strike never arrives; it is set past the widest slip the
        // scheduler permits before it re-telegraphs (a lead plus
        // Scheduler::WARN_STALE_SLACK_MS) so that a legitimately delayed Fire
        // still finds its mark standing.
        constexpr uint32 TRIGGER_LIFE_MS = 8000;   // TODO(design)

        // The boon. Registry row 14 names Boon::BonusMoveSpeed, and the card
        // says why: Diablo 4's repair of Lightning Storm paired the hazard with
        // a movement-speed bonus for dodging it, and "a small speed reward for
        // a clean dodge is worth testing". Since 04570c9 the aggregate pays
        // only a Scalar's boon, so this one is this file's to deliver, and it
        // is delivered as an aura rather than as an AggregateKind on purpose:
        // the aggregate is a continuous multiplier evaluated per query, and
        // what the card describes is a timed reward for an action. The aura
        // also brings the buff icon, which is the only telegraph the reward
        // has -- a speed change with nothing on the frames is not something a
        // player can learn from.
        //
        // Spell 65828 "Surge of Speed", read field by field out of
        // env/dist/data/dbc/Spell.dbc:
        //   Effect[0]           = 6  SPELL_EFFECT_APPLY_AURA (SharedDefines.h:772)
        //   EffectApplyAuraName = 31 SPELL_AURA_MOD_INCREASE_SPEED
        //                            (SpellAuraDefines.h:94)
        //   EffectBasePoints[0] = 69, EffectDieSides[0] = 1  -> +70% run speed
        //   DurationIndex       = 8  -> 15000 ms in SpellDuration.dbc
        //   EffectImplicitTargetA[0] = 1 TARGET_UNIT_CASTER
        //   Attributes..AttributesEx7 all 0 -- so not SPELL_ATTR0_PASSIVE
        //                            (0x40) and not SPELL_ATTR0_DO_NOT_DISPLAY
        //                            (0x80), which is what puts it on the buff
        //                            frame, and not SPELL_ATTR5_DO_NOT_DISPLAY_
        //                            DURATION (0x400), which is what gives it a
        //                            visible countdown (SharedDefines.h:377,
        //                            378, 565; SpellAuras.cpp:202,218-221)
        //   SpellFamilyName = 0, Mechanic = 0, EffectMechanic[0] = 0,
        //   EffectTriggerSpell[0] = 0, ProcFlags = 0, AuraInterruptFlags = 0,
        //   Dispel = 0 DISPEL_NONE, ManaCost = 0, SpellLevel = 0
        //                         -- class-neutral, no daze, no snare, no
        //                            second effect, nothing to dispel and no
        //                            level gate.
        //   SpellIconID = 516 -> Interface\Icons\Ability_Rogue_Sprint.
        //
        // The core uses it for exactly this shape of thing: a player who plays
        // the Twin Val'kyr essence mechanic correctly is given it as a reward,
        // at src/server/scripts/Northrend/CrusadersColiseum/TrialOfTheCrusader/
        // boss_twin_valkyr.cpp:801.
        constexpr uint32 SPELL_SURGE_OF_SPEED = 65828;

        // 70% for fifteen seconds is a boss-fight reward, not this one, so the
        // amount and the duration are both overwritten on the aura after it is
        // applied. That is a deliberate deviation from "use the spell as it is"
        // and it is what the alternative -- a server-side spell_dbc row, or a
        // client patch -- is not: nothing outside this player's aura is
        // touched. AuraEffect::ChangeAmount re-runs the effect handler
        // (SpellAuraEffects.cpp:713-753), and the handler for aura 31 is
        // HandleAuraModIncreaseSpeed, whose whole body is
        // UpdateSpeed(MOVE_RUN, true) (SpellAuraEffects.cpp:3815-3828), so the
        // new amount is live the moment it is set.
        //
        // The one visible cost of overwriting it. The buff's tooltip is built
        // by the client from its own copy of Spell.dbc -- 65828's aura
        // description is "Increases move speed by $s1%." -- so hovering the
        // icon reads 70%, not the 5/10/15% the server is applying. The
        // countdown on the icon is right, because that number comes over the
        // wire; the tooltip cannot be, because nothing but a client patch or a
        // new spell id could make it so, and both are out of bounds. The
        // alternative was three separate spells whose DBC amounts happen to be
        // 5, 10 and 15 -- 22586, 22588 and 22590 -- but all three are item
        // enchant auras with SpellIconID 1, no duration, and nothing in the
        // core using them, which trades an honest tooltip for a blank icon and
        // no telegraph at all. The icon is the telegraph; the tooltip is not.
        //
        // TODO(design): the card says the reward is "worth testing", not what
        // it is worth. Five seconds, fixed at every rank. Rank scales the
        // percentage instead, which is how the curse scales -- 25/35/50% of
        // maximum health -- and it is the generator that already scales it:
        // BoonTable gives BonusMoveSpeed a base of 5 multiplied by rank
        // (GauntletGenerator.cpp:104-126), so the offer card promises 5/10/15%
        // and this pays exactly that. Five seconds is the smallest number that
        // is still worth something: at +15% of the 7.0 yd/s a player runs at
        // (Unit.cpp:80-83) it buys back 5.25 yards, which is a little more
        // than the 4 yard mark that was just dodged. It is also at most a
        // third of the shortest cadence (15 s at rank III), so even a player
        // who never once eats the strike spends most of every cycle without
        // it, which is what keeps it a reward and not a state.
        constexpr uint32 DODGE_SPEED_MS = 5000;   // TODO(design)

        // Only reached when the instance carries the boon with no magnitude,
        // which the generator cannot produce for this row but a hand-built Ctx
        // or a later table could. It mirrors BoonTable's 5-per-rank so the two
        // cannot disagree about what rank II is worth, and it exists at all for
        // the same reason ScalarMagnitude has a fallback: an affix whose card
        // promises an upside must never silently deliver nothing.
        constexpr uint8 FALLBACK_SPEED_PCT[] = { 5, 10, 15, 20 };   // TODO(design)
        static_assert(std::size(FALLBACK_SPEED_PCT) >= MAX_RANK, "FALLBACK_SPEED_PCT is short a rank");

        uint8 RankIndex(uint8 rank)
        {
            if (rank < 1)
                return 0;
            return static_cast<uint8>((rank > MAX_RANK ? MAX_RANK : rank) - 1);
        }

        // Both come from the registry rather than from literals here, so the
        // key the addon coalesces on and the name the chat line prints cannot
        // drift from the module's own table. The fallbacks exist because
        // FindMechanic answers null for a table that does not carry the id,
        // which is a normal answer everywhere else in the module.
        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(FALLING_SKY);
            return def ? def->key : "falling_sky";
        }

        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(FALLING_SKY);
            return def ? def->name : "Falling Sky";
        }

        class FallingSky final : public IMechanic
        {
        public:
            // Picking the affix mid-fight, or logging in already in one, is
            // the same situation as the fight starting: Sync decides.
            void OnAttach(Ctx& ctx) override { Sync(ctx); }
            void OnDetach(Ctx& ctx) override { Disarm(ctx); }

            // The cadence is a state, not a stopwatch, so the tick reads the
            // state rather than counting the diff. That also makes this
            // correct whether integration calls OnTick every 500 ms or every
            // world tick.
            void OnTick(Ctx& ctx, uint32 diffMs) override { Sync(ctx); Watch(ctx, diffMs); }

            void OnEnterCombat(Ctx& ctx, Unit* /*enemy*/, bool /*wasOutOfCombat*/) override { Sync(ctx); }
            void OnLeaveCombat(Ctx& ctx) override { Disarm(ctx); }

            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Sync(Ctx& ctx);
            void Watch(Ctx& ctx, uint32 diffMs);
            void Arm(Ctx& ctx);
            void Disarm(Ctx& ctx);
            void ClearMark(Ctx& ctx, bool announce);
            void Strike(Ctx& ctx, Player* player);
            void Reward(Ctx& ctx, Player* player);

            uint8 RankIndexOf(Ctx const& ctx) const { return RankIndex(ctx.self ? ctx.self->rank : 1); }

            // Where the sky is going to land, fixed at Warn time. Everything
            // here is transient on purpose and none of it is written to
            // ctx.state: the plan names "a Falling Sky clock" as state that
            // resets on login by design, and a mark three seconds from landing
            // means nothing to a character who logs in somewhere else. There
            // is no counter behind this affix to carry across a session.
            // Where the stillness count started, and how long it has run.
            Position _stillFrom;
            uint32   _stillMs      = 0;

            Position _mark;
            uint32   _markMap      = 0;
            uint32   _markInstance = 0;
            uint32   _markAtMs     = 0;

            // The scheduler's tag for the pair currently queued. It only ever
            // goes up, so a callback that survives a Cancel is recognisable
            // and ignorable rather than acted on twice.
            uint32   _eventId = 0;

            bool     _armed  = false;
            bool     _marked = false;

            // The reward is taught once and then left to its buff icon. A line
            // on every clean dodge would be four lines a minute at rank III,
            // which is how a chat log stops being read at all; a line on the
            // first one is how a player without the addon finds out that the
            // dodge pays. It is per-instance and therefore per-attach, so the
            // lesson comes back on the next login, which is cheap and is
            // roughly when a player would want reminding.
            bool     _taughtDodge = false;   // TODO(design)
        };

        // The cadence runs while the player is fighting and at no other time.
        // Everything else that can stop an event is the scheduler's business.
        // Leaving combat is still what stops it; entering combat no longer
        // starts it, because standing still is what starts it now.
        void FallingSky::Sync(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;

            bool const fighting = player->IsInWorld() && player->IsAlive() && player->IsInCombat();
            if (!fighting && _armed)
                Disarm(ctx);

            if (!fighting)
                _stillMs = 0;
        }

        // The whole card. Watch the ground under the player and arm when they
        // have refused to leave it.
        void FallingSky::Watch(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;
            if (ctx.run && ctx.run->dead)
                return;
            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
                return;

            // Moving is measured against where the count started, not against
            // the last tick: a player edging sideways a yard at a time is
            // standing still, and measuring tick to tick would call it running.
            if (_stillFrom.GetExactDist2d(player) > MOVED_YARDS || _stillMs == 0)
            {
                _stillFrom = player->GetPosition();
                _stillMs   = 0;

                // Moved out from under a mark that has not landed: the sky
                // loses interest. This is the counterplay, and it has to be
                // able to cancel a warning or the warning is decoration.
                if (_armed)
                    Disarm(ctx);
            }

            _stillMs += diffMs;

            if (_armed || _stillMs < STILL_MS[RankIndexOf(ctx)])
                return;

            Arm(ctx);
        }

        void FallingSky::Arm(Ctx& ctx)
        {
            ++_eventId;
            _armed = true;

            // Pacing::Fixed: the warning is the whole event here -- three
            // seconds to step off ground you have already been standing on --
            // and stretching it with the event budget would make the telegraph
            // mean different things on different runs.
            ctx.clock->Arm(FALLING_SKY, _eventId, WARN_MS, WARN_MS, Pacing::Fixed);
        }

        void FallingSky::Disarm(Ctx& ctx)
        {
            _armed = false;

            // Bumping the tag is what makes the claim on _eventId true. One
            // Tick can release a Warn and its Fire together when the world
            // has been frozen for longer than the lead, and the framework
            // dispatches the whole batch after Tick has returned -- so a
            // cancel from inside the Warn cannot take the Fire out of a list
            // that has already been handed out. It can only make it
            // unrecognisable, which is this.
            ++_eventId;

            if (ctx.clock)
                ctx.clock->Cancel(FALLING_SKY);

            ClearMark(ctx, true);
        }

        // Takes the circle off the ground and, when asked, the countdown off
        // the addon. Despawning goes through Summons rather than through a
        // creature pointer this class holds, because the record is what
        // survives the pointer: a trigger despawned here is dropped from the
        // owner's list immediately, so the next mark cannot be refused by a
        // summon cap the previous one is still occupying.
        void FallingSky::ClearMark(Ctx& ctx, bool announce)
        {
            bool const had = _marked;
            _marked = false;

            if (!ctx.player)
                return;

            sGauntletSummons->DespawnFor(ctx.player, FALLING_SKY);

            if (had && announce && ctx.addon)
                ctx.addon->SendEvent(ctx.player, MechanicKey(), 0, MechanicName());
        }

        void FallingSky::OnWarn(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock || eventId != _eventId)
                return;

            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
            {
                Disarm(ctx);
                return;
            }

            // A re-telegraph is a fresh mark, not a second one: the scheduler
            // re-issues the Warn for a Fire that has slipped past its own
            // countdown, and the circle has to move to where the player is
            // now or the promise the countdown makes is false.
            ClearMark(ctx, false);

            Position const at = player->GetPosition();

            Creature* trigger = sGauntletSummons->Summon(player, ENTRY_WORLD_TRIGGER, at,
                                                         TRIGGER_LIFE_MS, false, FALLING_SKY);
            if (!trigger)
            {
                // The summon caps refused us, which they may legitimately do
                // when other affixes already have creatures in the world.
                // Leaving _marked false is the whole handling: the Fire still
                // arrives, finds no mark, and does nothing. An unannounced
                // strike is worse than a missed one.
                return;
            }

            trigger->CastSpell(trigger, SPELL_GROUND_MARK, true);

            _mark         = at;
            _markMap      = player->GetMapId();
            _markInstance = player->GetInstanceId();
            _markAtMs     = ctx.clock->NowMs();
            _marked       = true;

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), WARN_MS / 1000, MechanicName());
        }

        void FallingSky::OnEvent(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock || eventId != _eventId)
                return;

            bool const   marked = _marked;
            uint32 const ageMs  = marked ? ctx.clock->NowMs() - _markAtMs : 0;

            ClearMark(ctx, false);

            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
            {
                // TODO(design): the card says the sky never falls out of
                // combat, and this reads that as covering the moment of the
                // fall as much as the moment of the mark -- so killing the
                // last enemy inside the three seconds is a way out. The other
                // reading, that only the cadence is gated and a mark once
                // placed always lands, is equally defensible and one line
                // away. The cadence stops with the fight either way; Sync
                // starts it again with the next one.
                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, MechanicName());
                Disarm(ctx);
                return;
            }

            // The distance is measured against where the mark was put, never
            // against where the player is standing now, and in two dimensions
            // because the circle on the ground is what the player is dodging.
            // Getting this the other way round would make the affix
            // undodgeable, which is the one thing it must not be.
            //
            // The lower bound is the same rule as the upper one seen from the
            // other side. Scheduler::NowMs advances in 500 ms steps and one
            // Tick runs as many steps as the diff it was handed, so a world
            // that froze for longer than the lead can release the Warn and
            // the Fire into the same batch; the mark would then be nought
            // milliseconds old and the player would have had no telegraph at
            // all. A strike is only allowed while its circle has been on the
            // ground for the full three seconds and has not yet faded.
            //
            // Everything except the distance: the strike really did resolve,
            // on a mark that was really on the ground, in the place it was put.
            // Split out from the hit test because the boon is owed to the
            // player who was standing outside a strike that resolved, and to
            // nobody else. A refused mark, a mark the scheduler held past its
            // own telegraph, a mark released in the same batch as its warning,
            // a hearth or a portal out between the two -- none of those is a
            // dodge, and each of them leaves `resolved` false so no reward is
            // paid for merely not having been in danger.
            bool const resolved = marked
                               && ageMs >= WARN_MS
                               && ageMs <= MARK_VISIBLE_MS
                               && player->GetMapId() == _markMap
                               && player->GetInstanceId() == _markInstance;

            bool const hit = resolved && player->GetExactDist2d(_mark) <= MARK_RADIUS;

            if (hit)
                Strike(ctx, player);
            else if (resolved)
                Reward(ctx, player);

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, MechanicName());

            // Strike can have been lethal, and the death path detaches and
            // cancels underneath us; re-arming after it would put an event on
            // a run that has ended.
            if (player->IsInWorld() && player->IsAlive() && player->IsInCombat())
                Arm(ctx);
            else
                Disarm(ctx);
        }

        void FallingSky::Strike(Ctx& ctx, Player* player)
        {
            uint32 const pct = SEVERITY_PCT[RankIndexOf(ctx)];

            // The multiplication is done in 64 bits because health times 50
            // overflows uint32 at about 86 million, which no character has and
            // no arithmetic here should quietly depend on. The quotient is at
            // most half the pool, so it always fits back into uint32. The
            // floor of one exists so that the affix is never silently nothing.
            uint32 damage = static_cast<uint32>(static_cast<uint64>(player->GetMaxHealth()) * pct / 100u);
            if (damage == 0)
                damage = 1;

            // Player::EnvironmentalDamage is Unit::DealDamage (Player.cpp:853)
            // with a combat log packet in front of it, which is what makes the
            // hit legible to a player with no addon: they see "You suffer N
            // Fire damage" rather than an unexplained hole in their health
            // bar. It is self-damage, so nothing about it puts the player into
            // combat with an invisible trigger or hands that trigger threat,
            // and it goes through the core's own death sequence untouched.
            // It answers 0 for a player the core considers immune -- a GM, or
            // one under Divine Shield or Ice Block (Player.cpp:811-822) -- and
            // an immunity that saves you is a dodge like any other.
            uint32 const dealt = player->EnvironmentalDamage(DAMAGE_FIRE, damage);
            if (dealt == 0)
                return;

            // Design section 4.8's fourth question, answered for the player
            // who is not running the addon. The name comes from the registry,
            // so it is the same string the offer and the panel used.
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff2020[Gauntlet]|r The sky falls on you. {} strikes for {}.",
                MechanicName(), dealt);
        }

        // The clean dodge, paid. Called only from OnEvent and only on a strike
        // that resolved and missed, so every rule about when the boon may not
        // fire is the caller's `resolved` and not a second set of checks here.
        void FallingSky::Reward(Ctx& ctx, Player* player)
        {
            if (!ctx.self || ctx.self->boon != Boon::BonusMoveSpeed)
                return;

            uint32 const pct = ctx.self->boonMag != 0
                             ? static_cast<uint32>(ctx.self->boonMag)
                             : static_cast<uint32>(FALLBACK_SPEED_PCT[RankIndexOf(ctx)]);
            if (pct == 0)
                return;

            // Unit::AddAura(uint32, Unit*) (Unit.h:1351, Unit.cpp:15150-15187)
            // applies the aura with no cast, no global cooldown and no line of
            // sight, and answers null rather than throwing for a spell the
            // world does not know, a dead target or a target immune to it. A
            // player who is somehow immune to a self-cast run-speed buff simply
            // does not get it; there is nothing here worth failing over.
            Aura* aura = player->AddAura(SPELL_SURGE_OF_SPEED, player);
            if (!aura)
                return;

            // All three writes happen after AddAura and not before, because a
            // second application of an aura that is still up goes down the
            // refresh path: ModStackAmount -> SetStackAmount reinstates the
            // DBC amount and RefreshTimers reinstates the maximum duration
            // (SpellAuras.cpp:942-1004), so anything set first would be undone.
            //
            // Both duration fields are set because the client is told the pair:
            // AuraApplication::BuildUpdatePacket sends GetMaxDuration() and
            // then GetDuration() (SpellAuras.cpp:218-221), and it is the first
            // of them the buff frame draws its sweep against. SetDuration also
            // re-flags the application for a client update (SpellAuras.cpp:
            // 815-825), so the shortened timer reaches the player whether the
            // aura was created or refreshed.
            aura->SetMaxDuration(static_cast<int32>(DODGE_SPEED_MS));
            aura->SetDuration(static_cast<int32>(DODGE_SPEED_MS));

            if (AuraEffect* eff = aura->GetEffect(EFFECT_0))
                eff->ChangeAmount(static_cast<int32>(pct));

            // Once per attach. The buff icon is the telegraph from then on.
            if (!_taughtDodge)
            {
                _taughtDodge = true;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r You are clear when it lands. {} leaves you {}% faster for {} seconds.",
                    MechanicName(), pct, DODGE_SPEED_MS / 1000);
            }
        }

        std::string FallingSky::Describe(AffixInstance const& self) const
        {
            uint8 const rank = RankIndex(self.rank);

            std::string out = "Stand still in combat for " + std::to_string(STILL_MS[rank] / 1000)
                            + " seconds and the sky marks the ground under you; "
                            + std::to_string(WARN_MS / 1000)
                            + " seconds later it strikes for " + std::to_string(SEVERITY_PCT[rank])
                            + "% of your maximum health. Keep moving and it never finds you.";

            // Not BoonClause. The shared clause reads " In exchange, you move
            // 10% faster.", which is true of a Scalar's rolled boon and false
            // of this one: the speed is owed for one dodge and lasts five
            // seconds. A card that describes a permanent buff would teach the
            // wrong loop, which is the one thing the boon exists to teach.
            // Anything other than BonusMoveSpeed on this row would be a table
            // change rather than a roll, so it falls through to the shared
            // wording rather than being silently dropped.
            if (self.boon == Boon::BonusMoveSpeed)
            {
                uint32 const pct = self.boonMag != 0
                                 ? static_cast<uint32>(self.boonMag)
                                 : static_cast<uint32>(FALLBACK_SPEED_PCT[rank]);
                if (pct != 0)
                    out += " Move clear in time and you are " + std::to_string(pct)
                         + "% faster for " + std::to_string(DODGE_SPEED_MS / 1000) + " seconds.";
            }
            else
            {
                out += BoonClause(self.boon, self.boonMag);
            }

            return out;
        }
    }

    GAUNTLET_MECHANIC(FALLING_SKY, FallingSky);
}
