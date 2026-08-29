/*
 * mod-gauntlet - S4 Reinforcements: a long fight draws another enemy
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"
#include "Unit.h"

#include <string>
#include <iterator>

// Registry id 4. Design section 3, card S4: "Fights longer than 30 seconds draw
// another enemy every 15 seconds."
//
// The card's own note is the reason this mechanic is cheap: summoning a copy of
// *the thing you are already fighting* keeps it zone-appropriate and legible
// ("another Defias Thug arrives") with no bestiary work. The consequence is
// that the creature that arrives carries a world DB template this module does
// not own, so it cannot be given a ScriptName -- GauntletSummonAI.cpp's
// AllCreatureScript::GetCreatureAI is what makes the copy owner-bound anyway,
// and its halved experience comes from Gauntlet.Summons.XpRate, which applies
// to anything the summon wrapper recorded.
//
// The counterplay is the clock: leaving combat resets it, so disengage-and-
// reset is always available, and burst is what stops the second copy arriving.
// That is what the BonusDamage boon pays for.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_REINFORCEMENTS = 4;

        // The first value of the per-arming tag. It is NOT the id armed with:
        // _eventId is bumped on every Arm and on every Stop, and the scheduler
        // is armed with the current value, so a Fire released for one fight can
        // never be acted on in the next.
        //
        // Arming with this constant instead was a bug that made the affix fire
        // exactly once per session. The tag was bumped and then ignored, so the
        // scheduler always called back with 1 while _eventId had moved on --
        // 3 by the second fight, 5 by the third -- and OnWarn and OnEvent both
        // dropped every callback on the `eventId != _eventId` guard. The first
        // fight worked, because one bump from 0 lands on 1 by luck.
        constexpr uint32 EVENT_FIRST = 1;

        // The card's ladder: 45/15 s -> 30/15 s -> 20/10 s, cap 2 -> 3 -> 4.
        // Rank IV is past the card at 15/8 s and a cap of 6: a fight that
        // runs a minute is now six enemies deep, which makes disengaging a
        // decision rather than an option you never take. The cap matters
        // more than the cadence here -- Gauntlet.Summons.MaxAlive still
        // bounds what can be standing at once across every spawn mechanic.
        constexpr uint32 FIRST_MS[]  = { 45000, 30000, 20000, 15000 };
        static_assert(std::size(FIRST_MS) >= MAX_RANK, "FIRST_MS is short a rank");
        constexpr uint32 REPEAT_MS[] = { 15000, 15000, 10000, 8000 };
        static_assert(std::size(REPEAT_MS) >= MAX_RANK, "REPEAT_MS is short a rank");
        constexpr uint32 CAP[]       = { 2, 3, 4, 6 };
        static_assert(std::size(CAP) >= MAX_RANK, "CAP is short a rank");

        // "spawns 20 yd away and attacks the owner".
        constexpr float SPAWN_YARDS = 20.0f;

        // Not on the card. A copy lives as long as the fight that drew it plus
        // a little; the shared AI despawns it the moment its owner is gone, and
        // OnLeaveCombat below takes the whole set out, so this is only the
        // backstop for a fight that somehow never ends.
        constexpr uint32 LIFETIME_MS = 300000;   // TODO(design)

        // Five seconds of warning, which is the smallest number that is still a
        // decision: it is long enough to break off, to spend an interrupt, or
        // to move to where the arrival will not add to a pack. The card asks
        // for the arrival to be legible and does not say how far ahead.
        constexpr uint32 WARN_MS = 5000;   // TODO(design)

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_REINFORCEMENTS);
            return def ? def->key : "reinforcements";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // What the fight is against, and whether it may be copied. The card
        // excludes an elite, a boss, a summon, a vehicle and a quest-flagged
        // creature; IsOrdinaryFoe is exactly that list and is shared with the
        // seven other Phase 2 mechanics that ask the same question.
        Creature* CopyableVictim(Player* player)
        {
            Unit* victim = player->GetVictim();               // Unit.h:1042
            Creature* creature = victim ? victim->ToCreature() : nullptr;
            if (!creature || !IsOrdinaryFoe(creature))
                return nullptr;

            // Never a copy of a copy, and never a copy of a Shade: this module
            // must not be able to feed itself.
            if (sGauntletSummons->IsGauntletSummon(creature))
                return nullptr;

            return creature;
        }

        class Reinforcements final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Sync(ctx); }
            void OnDetach(Ctx& ctx) override { Stop(ctx); }

            // The clock is a state, not a stopwatch: the tick reads whether the
            // player is fighting and arms or disarms to match, which is correct
            // whether OnTick arrives every 500 ms or every world tick.
            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnEnterCombat(Ctx& ctx, Unit* /*enemy*/, bool /*wasOutOfCombat*/) override { Sync(ctx); }
            void OnLeaveCombat(Ctx& ctx) override { Stop(ctx); }

            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            // BonusDamage: the card's counterplay is "burst, pull small, use CC
            // to shorten fights", and a fight that ends sooner draws fewer
            // arrivals. The boon is therefore the tool the curse asks for, and
            // it is self-limiting -- spending it is what makes the curse
            // cheaper, which is the kiss/curse shape design section 2.8 wants.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

            // Everything this mechanic holds that a player cannot see, because
            // it was reported as not working twice and reading the code
            // distinguished none of the ways that could be true. `.gauntlet
            // debug dump` now answers it in one line.
            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "reinforcements: ";
                out += _armed ? "armed" : "not armed";
                out += ", " + std::to_string(_spawned) + " spawned this fight, tag "
                     + std::to_string(_eventId);

                if (ctx.player)
                {
                    out += ctx.player->IsInCombat() ? ", in combat" : ", out of combat";

                    Unit* victim = ctx.player->GetVictim();
                    if (!victim)
                        out += ", NO VICTIM (nothing to copy)";
                    else if (!CopyableVictim(ctx.player))
                        out += ", victim not copyable (elite, boss, or one of ours)";
                    else
                        out += ", victim copyable";
                }

                return out;
            }

        private:
            void Sync(Ctx& ctx);
            void Arm(Ctx& ctx, uint32 inMs);
            void Stop(Ctx& ctx);
            void Arrive(Ctx& ctx);

            uint32 _eventId = EVENT_FIRST - 1;   // Arm() pre-increments
            uint32 _spawned = 0;      // this fight only; the cap is per fight
            bool   _armed   = false;
        };

        void Reinforcements::Sync(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;

            bool const fighting = player->IsInWorld() && player->IsAlive() && player->IsInCombat();
            if (fighting == _armed)
                return;

            if (!fighting)
            {
                Stop(ctx);
                return;
            }

            // A new fight: the cap is per fight, so the count starts again.
            // This is the card's release valve -- "leaving combat resets the
            // clock, so disengage-and-reset is always available".
            _spawned = 0;
            _armed   = true;
            Arm(ctx, FIRST_MS[RankIndex(ctx.self)]);
        }

        void Reinforcements::Arm(Ctx& ctx, uint32 inMs)
        {
            ++_eventId;
            ctx.clock->Arm(MECHANIC_REINFORCEMENTS, _eventId, inMs, WARN_MS);
        }

        void Reinforcements::Stop(Ctx& ctx)
        {
            _armed = false;

            // Bumping the tag is what makes a Fire already handed out for this
            // fight unrecognisable: one Tick can release a Warn and its Fire
            // together, and a cancel from inside the Warn cannot take the Fire
            // out of a batch that has already been returned.
            ++_eventId;

            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_REINFORCEMENTS);

            // Copies are the fight, so they leave with it. Without this a
            // player who broke off would be followed by the very creatures the
            // disengage was supposed to escape, which is the card's counterplay
            // taken away.
            if (ctx.player && _spawned != 0)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_REINFORCEMENTS);

            _spawned = 0;
        }

        void Reinforcements::OnWarn(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (!player || eventId != _eventId || !_armed)
                return;

            if (!CopyableVictim(player))
                return;   // nothing to copy; the Fire will find the same and re-arm

            AddonFor(ctx)->SendEvent(player, MechanicKey(), WARN_MS / 1000u, "Reinforcements");
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff2020[Gauntlet]|r You hear more of them coming.");
        }

        void Reinforcements::OnEvent(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock || eventId != _eventId)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
            {
                Stop(ctx);
                return;
            }

            uint32 const cap = CAP[RankIndex(ctx.self)];
            if (_spawned >= cap)
                return;   // the fight has had its share; no re-arm

            Arrive(ctx);

            if (_spawned < cap)
                Arm(ctx, REPEAT_MS[RankIndex(ctx.self)]);
        }

        void Reinforcements::Arrive(Ctx& ctx)
        {
            Player* player = ctx.player;

            Creature* victim = CopyableVictim(player);
            if (!victim)
            {
                // Fighting an elite, a boss or something this module summoned.
                // The card skips those, and skipping is not spending: re-arm and
                // look again, because the fight may turn into a copyable one.
                Arm(ctx, REPEAT_MS[RankIndex(ctx.self)]);
                return;
            }

            uint32 const entry = victim->GetEntry();

            // Twenty yards, at a quarter turn from the player's facing, so the
            // arrival is beside the fight rather than on top of it or behind
            // the player's back -- this affix is a pacing rule, not an ambush.
            Position const at = player->GetFirstCollisionPosition(SPAWN_YARDS, float(M_PI) / 2.0f);

            Creature* copy = sGauntletSummons->Summon(player, entry, at, LIFETIME_MS,
                                                      /*countsAsStalker*/ false,
                                                      MECHANIC_REINFORCEMENTS);
            if (!copy)
            {
                // The four-summon cap refused, which it may legitimately do
                // while other affixes have creatures out. Try again on the next
                // interval rather than dropping the fight's whole ladder.
                Arm(ctx, REPEAT_MS[RankIndex(ctx.self)]);
                return;
            }

            ++_spawned;

            copy->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);           // Unit.h:1943

            Addon* addon = AddonFor(ctx);
            addon->SendEvent(player, MechanicKey(), 0, "Reinforcements");
            addon->QueueCounter(player, MechanicKey(), _spawned, CAP[RankIndex(ctx.self)]);

            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff2020[Gauntlet]|r {} arrives.", copy->GetNameForLocaleIdx(LOCALE_enUS));
        }

        std::string Reinforcements::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "A fight that lasts longer than " + std::to_string(FIRST_MS[i] / 1000u)
                            + " seconds draws another of whatever you are fighting, and another every "
                            + std::to_string(REPEAT_MS[i] / 1000u) + " seconds after that, up to "
                            + std::to_string(CAP[i])
                            + ". Leaving combat resets the clock and sends them away.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(4, Reinforcements);
}
