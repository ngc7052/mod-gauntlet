/*
 * mod-gauntlet - S5 Ambush: resting in the wild attracts an ambush
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
#include "Map.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"

#include <cmath>
#include <string>
#include <iterator>

// Registry id 5. Design section 3, card S5: "Resting in the wild attracts an
// ambush."
//
// The card is unusually explicit about what the affix is for: "the warning is
// the counterplay -- stand up and move three steps". So the four seconds between
// the footsteps and the Ambusher are not decoration, they are the mechanic, and
// every path below that could land one without them refuses instead.
//
// It is deliberately restricted to the open world. Punishing between-pull rest
// in a dungeon group is a role burden -- design section 2.9's second known-bad
// pattern -- so Map::IsDungeon is a hard refusal rather than a modifier.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_AMBUSH = 5;

        constexpr uint32 EVENT_AMBUSH = 1;

        // The card's ladder: 30 -> 20 -> 12 seconds of stillness. Rank IV is
        // past the card and continues the compression to 8, which is short
        // enough that eating a meal is no longer safe -- the point at which
        // the affix stops being about resting and starts being about where.
        constexpr uint32 STILL_MS[] = { 30000, 20000, 12000, 8000 };
        static_assert(std::size(STILL_MS) >= MAX_RANK, "STILL_MS is short a rank");

        // The card's fixed numbers: a four-second warning, twelve yards, and a
        // rest clock that resets to sixty seconds afterwards.
        constexpr uint32 WARN_MS      = 4000;
        constexpr float  SPAWN_YARDS  = 12.0f;
        constexpr uint32 COOLDOWN_MS  = 60000;

        // Not on the card, and the same two minutes every other summon gets.
        constexpr uint32 LIFETIME_MS = 120000;   // TODO(design)

        // "You hear footsteps" is the card's own wording, and it means a sound.
        // A chat line alone is not one: a player watching their character, or
        // reading a full combat log, misses it entirely -- and the four seconds
        // between the warning and the Ambusher are the whole of this affix's
        // counterplay, so a telegraph that can be missed is the affix broken.
        //
        // 8663 is the sound the core plays for C'Thun's out-of-combat whispers
        // (boss_cthun.cpp:108), and it is played there the same way it is
        // played here: PlayDirectSound(id, player) with the player as target,
        // which sends SMSG_PLAY_SOUND to that one session and to nobody else
        // (boss_cthun.cpp:449-452). It is a client-side sound in the 3.3.5 data
        // already, so no patch is involved.
        //
        // TODO(design): the design names no sound, and this one is chosen for
        // being low, wordless, already in the client and already used by the
        // core for exactly this job -- a cue meant for one player who is about
        // to be in trouble.
        constexpr uint32 SOUND_FOOTSTEPS = 8663;

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_AMBUSH);
            return def ? def->key : "ambush";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // Everything the card requires before the stillness clock may run:
        // "out of combat, not moving, not in a rest area and in the open
        // world". The scheduler's own suppression covers mounted, in flight, in
        // a sanctuary, dead, in the grace window and with an offer on the
        // table, but the clock below is not a scheduler event -- it is what
        // decides whether to arm one -- so the settled states are checked here
        // as well.
        bool Resting(Player* player)
        {
            if (!player->IsInWorld() || !player->IsAlive())
                return false;
            if (player->IsInCombat())
                return false;
            if (player->isMoving())                                     // Unit.h:1712
                return false;
            if (player->IsMounted() || player->IsInFlight())            // Unit.h:1887, :1709
                return false;
            if (player->HasRestFlag(REST_FLAG_IN_TAVERN) ||             // Player.h:1221
                player->HasRestFlag(REST_FLAG_IN_CITY))
                return false;
            if (player->IsGameMaster())
                return false;

            Map* map = player->GetMap();
            if (!map)
                return false;

            // "in the open world", and the card means it literally: never in a
            // dungeon, never in a raid, never in a battleground.
            if (map->IsDungeon() || map->IsBattlegroundOrArena())       // Map.h:302, :307
                return false;

            return true;
        }

        class Ambush final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Reset(ctx); }
            void OnDetach(Ctx& ctx) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;
            void OnEnterCombat(Ctx& ctx, Unit* /*enemy*/, bool /*wasOutOfCombat*/) override { Reset(ctx); }

            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            // BonusMaxHealth. The curse punishes sitting down, so the boon is
            // the thing that makes sitting down less necessary: a bigger pool
            // is fewer stops, which is the same verb the curse is taxing.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Reset(Ctx& ctx);
            void Spring(Ctx& ctx);

            uint32 _stillMs   = 0;   // how long the player has been settled
            uint32 _cooldown  = 0;   // the card's sixty seconds after one lands
            bool   _armed     = false;
        };

        void Ambush::Reset(Ctx& ctx)
        {
            _stillMs = 0;

            if (!_armed)
                return;

            _armed = false;
            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_AMBUSH);

            // The countdown was on screen and is not going to finish, so it is
            // taken off. This is the affix keeping its promise: standing up
            // really does call it off.
            AddonFor(ctx)->SendEvent(ctx.player, MechanicKey(), 0, "Ambush");
        }

        void Ambush::OnDetach(Ctx& ctx)
        {
            Reset(ctx);

            if (ctx.player)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_AMBUSH);
        }

        void Ambush::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;

            if (_cooldown != 0)
                _cooldown = _cooldown > diffMs ? _cooldown - diffMs : 0;

            if (ctx.run && (ctx.run->dead || !ctx.run->pending.empty()))
            {
                Reset(ctx);
                return;
            }

            if (!Resting(player))
            {
                // Moving three steps is the counterplay, so it has to work at
                // every point up to and including the last half-second before
                // the Ambusher lands.
                Reset(ctx);
                return;
            }

            if (_armed || _cooldown != 0)
                return;

            _stillMs += diffMs;
            if (_stillMs < STILL_MS[RankIndex(ctx.self)])
                return;

            _armed = true;
            // Fixed: inMs == warnMs, so the whole interval is the telegraph, and
            // the header has always promised a telegraph does not change
            // length with how many affixes are carried.
            ctx.clock->Arm(MECHANIC_AMBUSH, EVENT_AMBUSH, WARN_MS, WARN_MS,
                           Pacing::Fixed);
        }

        void Ambush::OnWarn(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (eventId != EVENT_AMBUSH || !player || !_armed)
                return;

            AddonFor(ctx)->SendEvent(player, MechanicKey(), WARN_MS / 1000u, "Ambush");

            // Three telegraphs, because each of them reaches a player the
            // others do not: the addon's countdown bar for a player running it,
            // the chat line for one who is not, and the sound for one who is
            // looking at the world rather than at either.
            player->PlayDirectSound(SOUND_FOOTSTEPS, player);              // Object.h:573

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r You hear footsteps behind you.");
        }

        void Ambush::OnEvent(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (eventId != EVENT_AMBUSH || !player)
                return;

            _armed = false;

            // Checked again at the moment it would land, and not only when it
            // was armed: the four seconds between the two are exactly the
            // window the player is meant to spend standing up, and an Ambusher
            // that arrives anyway would make the warning a lie.
            if (!Resting(player))
                return;

            if (ctx.run && ctx.run->dead)
                return;

            Spring(ctx);
        }

        void Ambush::Spring(Ctx& ctx)
        {
            Player* player = ctx.player;

            // Twelve yards, behind and to one side. Not directly behind, which
            // is the Shade's angle: this one is meant to be seen the moment the
            // player stands up and turns, because it is an ordinary mob and
            // fighting it at full health is supposed to be free.
            Position const at = player->GetFirstCollisionPosition(SPAWN_YARDS, float(M_PI) * 0.75f);

            Creature* ambusher = sGauntletSummons->Summon(player, ENTRY_AMBUSHER, at, LIFETIME_MS,
                                                          /*countsAsStalker*/ true, MECHANIC_AMBUSH);

            // The rest clock restarts either way. If the caps refused, the
            // player still stood still for their twenty seconds and the affix
            // still owes them nothing; queueing an ambush behind another
            // affix's creature would land it with no warning at all.
            _stillMs  = 0;
            _cooldown = COOLDOWN_MS;

            if (!ambusher)
                return;

            ambusher->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);       // Unit.h:1943

            Addon* addon = AddonFor(ctx);
            addon->SendEvent(player, MechanicKey(), 0, "Ambush");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Something was waiting for you to sit down.");
        }

        std::string Ambush::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "Standing still in the open world for "
                            + std::to_string(STILL_MS[i] / 1000u)
                            + " seconds brings footsteps, and four seconds later an Ambusher."
                              " Moving cancels it, right up to the last moment. Nothing happens"
                              " in inns, in cities or in dungeons.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(5, Ambush);
}
