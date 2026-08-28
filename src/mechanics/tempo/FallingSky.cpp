/*
 * mod-gauntlet - T1 Falling Sky: the sky marks your spot, then it strikes
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"
#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"
#include "../attrition/Scalars.h"

#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "Position.h"

#include <string>

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
        constexpr uint32 CADENCE_MS[MAX_RANK]   = { 25000, 20000, 15000 };
        constexpr uint32 SEVERITY_PCT[MAX_RANK] = { 25, 35, 50 };

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

        // World Trigger (Not Immune PC), named by plan appendix A. This
        // realm's creature_template carries it with the invisible model 169,
        // unit_flags 0x2000000 UNIT_FLAG_NOT_SELECTABLE and flags_extra 0x82 =
        // CREATURE_FLAG_EXTRA_TRIGGER | CREATURE_FLAG_EXTRA_CIVILIAN
        // (CreatureData.h:53 and :47), so it cannot be seen, selected,
        // attacked, or provoked into the fight that would freeze its despawn
        // timer.
        constexpr uint32 ENTRY_WORLD_TRIGGER = 21252;

        // The trigger's own backstop, and the one invented number in this
        // file. The strike despawns it explicitly, so this only matters when
        // the strike never arrives; it is set past the widest slip the
        // scheduler permits before it re-telegraphs (a lead plus
        // Scheduler::WARN_STALE_SLACK_MS) so that a legitimately delayed Fire
        // still finds its mark standing.
        constexpr uint32 TRIGGER_LIFE_MS = 8000;   // TODO(design)

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
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnEnterCombat(Ctx& ctx, Unit* /*enemy*/, bool /*wasOutOfCombat*/) override { Sync(ctx); }
            void OnLeaveCombat(Ctx& ctx) override { Disarm(ctx); }

            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Sync(Ctx& ctx);
            void Arm(Ctx& ctx);
            void Disarm(Ctx& ctx);
            void ClearMark(Ctx& ctx, bool announce);
            void Strike(Ctx& ctx, Player* player);

            uint8 RankIndexOf(Ctx const& ctx) const { return RankIndex(ctx.self ? ctx.self->rank : 1); }

            // Where the sky is going to land, fixed at Warn time. Everything
            // here is transient on purpose and none of it is written to
            // ctx.state: the plan names "a Falling Sky clock" as state that
            // resets on login by design, and a mark three seconds from landing
            // means nothing to a character who logs in somewhere else. There
            // is no counter behind this affix to carry across a session.
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
        };

        // The cadence runs while the player is fighting and at no other time.
        // Everything else that can stop an event is the scheduler's business.
        void FallingSky::Sync(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;

            bool const fighting = player->IsInWorld() && player->IsAlive() && player->IsInCombat();
            if (fighting == _armed)
                return;

            if (fighting)
                Arm(ctx);
            else
                Disarm(ctx);
        }

        void FallingSky::Arm(Ctx& ctx)
        {
            ++_eventId;
            _armed = true;
            ctx.clock->Arm(FALLING_SKY, _eventId, CADENCE_MS[RankIndexOf(ctx)], WARN_MS);
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
            bool const hit = marked
                          && ageMs >= WARN_MS
                          && ageMs <= MARK_VISIBLE_MS
                          && player->GetMapId() == _markMap
                          && player->GetInstanceId() == _markInstance
                          && player->GetExactDist2d(_mark) <= MARK_RADIUS;

            if (hit)
                Strike(ctx, player);

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

        std::string FallingSky::Describe(AffixInstance const& self) const
        {
            uint8 const rank = RankIndex(self.rank);

            std::string out = "In combat, every " + std::to_string(CADENCE_MS[rank] / 1000)
                            + " seconds the sky marks your spot; " + std::to_string(WARN_MS / 1000)
                            + " seconds later it strikes for " + std::to_string(SEVERITY_PCT[rank])
                            + "% of your maximum health. Move out of the mark.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(FALLING_SKY, FallingSky);
}
