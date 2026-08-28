/*
 * mod-gauntlet - E4 Death Rattle: corpses burst two seconds after death
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
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

// Registry id 9. Design section 3, card E4: "Corpses burst two seconds after
// death, hurting anyone within five yards."
//
// Melee-weighted on purpose -- the registry restricts it to the classes that
// finish a fight standing in it -- and mutually exclusive with Grudge through
// the "onkill-positional" key, because together they are just "melee is bad".
//
// Two deviations from the card, both about the circle on the ground, and both
// are here rather than in the report alone because they are what the file does.
//
// 1. The radius does not ladder. The card says 5/6/8 yd; the visual this module
//    is allowed to use draws exactly four (see SPELL_GROUND_MARK), and design
//    section 4.8 says the player must know what is about to hit them. A circle
//    that lies about its own size is worse than a circle that is smaller than
//    the card asked for, so the danger zone is the drawn zone at every rank and
//    the ladder moves the damage instead -- which the card also states.
// 2. The delay is a scheduler event, so it can slip. Design section 4.2 puts
//    Death Rattle through the one queue with everything else, minimum spacing
//    included; a burst held back behind another affix's event would otherwise
//    land on a corpse the player walked away from ten seconds ago. The circle
//    is therefore kept on the ground until the burst resolves, so however long
//    it waits the telegraph is still true and the counterplay -- step back --
//    still works.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_DEATH_RATTLE = 9;

        // The card's damage ladder: 8/12/18% of maximum health.
        constexpr uint32 SEVERITY_PCT[MAX_RANK] = { 8, 12, 18 };

        // "Two seconds after a kill".
        constexpr uint32 FUSE_MS = 2000;

        // Spell 30632 "Debris", Magtheridon's ceiling collapse: a
        // SPELL_EFFECT_PERSISTENT_AREA_AURA carrying a bare SPELL_AURA_DUMMY
        // with base points 0, no triggered spell and no damage effect, so its
        // entire job is to draw a circle on the ground. Its EffectRadiusIndex
        // resolves to 4.0 yd in SpellRadius.dbc and its duration index 28 to
        // 5000 ms. Falling Sky uses the same spell for the same reason
        // (mechanics/tempo/FallingSky.cpp) and the core's own script uses it in
        // this exact shape -- a trigger casts it on itself, waits, and only
        // then deals damage -- at boss_magtheridon.cpp:253-258.
        constexpr uint32 SPELL_GROUND_MARK = 30632;
        constexpr float  MARK_RADIUS       = 4.0f;
        constexpr uint32 MARK_VISIBLE_MS   = 5000;

        // The circle lasts five seconds and the burst may wait longer than
        // that behind the scheduler's minimum spacing, so it is re-cast before
        // it can gap. One second of margin, which is two module ticks: a
        // persistent area aura re-cast while the previous one is still up
        // replaces it rather than stacking a second dynamic object on the
        // same spot, so the circle simply never blinks.
        constexpr uint32 MARK_REFRESH_MS = MARK_VISIBLE_MS - 1000;


        // How many corpses may be counting down at once. The summon wrapper
        // caps every affix at four creatures between them and Death Rattle
        // would happily take all four in a camp, leaving a carried Shade or
        // Carrion unable to spawn. Two is enough for the affix to be felt on a
        // double pull and leaves the rest of the budget alone.
        constexpr std::size_t MAX_FUSES = 2;   // TODO(design)

        // The trigger's own backstop, past the widest slip the scheduler
        // permits before it re-telegraphs, so a legitimately delayed burst
        // still finds its circle standing.
        constexpr uint32 TRIGGER_LIFE_MS = 30000;   // TODO(design)

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_DEATH_RATTLE);
            return def ? def->name : "Death Rattle";
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_DEATH_RATTLE);
            return def ? def->key : "death_rattle";
        }

        class DeathRattle final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;
            void OnKill(Ctx& ctx, Creature* killed) override { Light(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Light(ctx, killed); }
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            // BonusMoney: the corpse pays for the danger of standing over it,
            // which is the same bargain Carrion's card makes out loud.
            void OnLootMoney(Ctx& ctx, Loot* loot) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            // One corpse counting down. The trigger's guid is kept so the
            // circle can be taken off the ground the instant the burst
            // resolves, rather than lingering for the rest of its five seconds
            // over ground that is safe again.
            struct Fuse
            {
                uint32     id = 0;
                Position   at;
                uint32     mapId      = 0;
                uint32     instanceId = 0;
                ObjectGuid trigger;
                uint32     sinceMarkMs = 0;   // since the circle was last drawn
            };

            void  Light(Ctx& ctx, Creature* killed);
            void  Burst(Ctx& ctx, Fuse const& fuse);
            void  Extinguish(Ctx& ctx, Fuse const& fuse);
            Fuse* Find(uint32 id);
            void  Drop(uint32 id);

            std::vector<Fuse> _fuses;
            uint32            _nextId = 0;
        };

        DeathRattle::Fuse* DeathRattle::Find(uint32 id)
        {
            for (Fuse& fuse : _fuses)
                if (fuse.id == id)
                    return &fuse;
            return nullptr;
        }

        void DeathRattle::Drop(uint32 id)
        {
            _fuses.erase(std::remove_if(_fuses.begin(), _fuses.end(),
                                        [id](Fuse const& f) { return f.id == id; }),
                         _fuses.end());
        }

        void DeathRattle::OnDetach(Ctx& ctx)
        {
            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_DEATH_RATTLE);

            // A circle drawn by an affix that is no longer carried would be a
            // promise nothing is going to keep.
            if (ctx.player)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_DEATH_RATTLE);

            _fuses.clear();
        }

        void DeathRattle::Light(Ctx& ctx, Creature* killed)
        {
            Player* player = ctx.player;
            if (!player || !killed || !ctx.clock)
                return;
            if (!player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && (ctx.run->dead || !ctx.run->pending.empty()))
                return;

            // Nothing this module spawned bursts: a Shade that dies at the
            // player's feet is a reward, not a second punishment, and a
            // Reinforcements copy bursting would make the two affixes together
            // a trap rather than a pair.
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;

            if (!IsOrdinaryFoe(killed))
                return;

            if (_fuses.size() >= MAX_FUSES)
                return;

            Position const at = killed->GetPosition();

            Creature* trigger = sGauntletSummons->Summon(player, ENTRY_WORLD_TRIGGER, at,
                                                         TRIGGER_LIFE_MS, /*countsAsStalker*/ false,
                                                         MECHANIC_DEATH_RATTLE);
            if (!trigger)
                return;   // the caps refused; an untelegraphed burst is worse than none

            trigger->CastSpell(trigger, SPELL_GROUND_MARK, true);

            Fuse fuse;
            fuse.id         = ++_nextId;
            fuse.at         = at;
            fuse.mapId      = player->GetMapId();
            fuse.instanceId = player->GetInstanceId();
            fuse.trigger    = trigger->GetGUID();
            _fuses.push_back(fuse);

            // Warn lead of zero: the circle on the ground *is* the telegraph,
            // and two seconds is too short for a countdown bar to say anything
            // a circle does not. EVT carries the two seconds so the addon can
            // still draw it.
            ctx.clock->Arm(MECHANIC_DEATH_RATTLE, fuse.id, FUSE_MS, 0);

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), FUSE_MS / 1000u, MechanicName());
        }

        // Keeps every pending circle on the ground. Design section 4.2 puts
        // this affix's two seconds through the same queue as everything else,
        // minimum spacing included, so a burst can legitimately wait longer
        // than the visual lasts -- and a burst whose telegraph has faded is
        // exactly what design section 4.8 forbids. Redrawing costs one cast
        // every four seconds per pending corpse, of which there are at most two.
        void DeathRattle::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (_fuses.empty() || !player || !player->IsInWorld())
                return;

            for (Fuse& fuse : _fuses)
            {
                fuse.sinceMarkMs += diffMs;
                if (fuse.sinceMarkMs < MARK_REFRESH_MS)
                    continue;

                fuse.sinceMarkMs = 0;

                if (Creature* trigger = ObjectAccessor::GetCreature(*player, fuse.trigger))
                    if (trigger->IsInWorld())
                        trigger->CastSpell(trigger, SPELL_GROUND_MARK, true);
            }
        }

        void DeathRattle::OnEvent(Ctx& ctx, uint32 eventId)
        {
            Fuse* fuse = Find(eventId);
            if (!fuse)
                return;

            Fuse const copy = *fuse;
            Drop(eventId);

            Burst(ctx, copy);
        }

        void DeathRattle::Extinguish(Ctx& ctx, Fuse const& fuse)
        {
            if (!ctx.player || fuse.trigger.IsEmpty())
                return;

            // Through Summons rather than a creature pointer, so the record is
            // released with the creature and the next corpse cannot be refused
            // by a cap this one is still occupying. DespawnFor takes out every
            // trigger this mechanic owns, which is correct here only because a
            // burst resolving is the common case and the other fuse re-draws
            // itself on its own event -- so the one line below re-arms it.
            Creature* trigger = ObjectAccessor::GetCreature(*ctx.player, fuse.trigger);
            if (trigger)
                trigger->DespawnOrUnsummon();
        }

        void DeathRattle::Burst(Ctx& ctx, Fuse const& fuse)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            Extinguish(ctx, fuse);

            if (!player->IsInWorld() || !player->IsAlive())
                return;
            if (player->GetMapId() != fuse.mapId || player->GetInstanceId() != fuse.instanceId)
                return;

            // The distance is measured in two dimensions against where the
            // corpse fell, never against where the player is now: the circle on
            // the ground is what is being dodged, and getting this the other
            // way round would make the affix undodgeable.
            if (player->GetExactDist2d(fuse.at) > MARK_RADIUS)
                return;

            uint32 const pct = SEVERITY_PCT[RankIndex(ctx.self)];

            // Sixty-four bits on the way through: health times eighteen
            // overflows uint32 at about 238 million, which no character has and
            // no arithmetic here should quietly depend on.
            uint32 damage = static_cast<uint32>(static_cast<uint64>(player->GetMaxHealth()) * pct / 100u);
            if (damage == 0)
                damage = 1;

            // The affix claims the death before it can cause one. Mgr::Tick
            // already notes a scheduler Fire's mechanic, so this is belt and
            // braces -- but a Fire that is dispatched for a fuse this instance
            // no longer holds would otherwise leave the claim standing with no
            // blow behind it.
            if (ctx.run)
                ctx.run->NoteActor(MECHANIC_DEATH_RATTLE);

            // Player::EnvironmentalDamage is Unit::DealDamage (Player.cpp:853)
            // with a combat log packet in front of it, so a player with no
            // addon sees "You suffer N damage" rather than an unexplained hole
            // in the health bar. It is self-damage, so nothing puts the player
            // into combat with the invisible trigger, and it answers 0 for a
            // player the core considers immune (Player.cpp:811-822) -- an
            // immunity that saves you is a dodge like any other.
            uint32 const dealt = player->EnvironmentalDamage(DAMAGE_FIRE, damage);
            if (dealt == 0)
                return;

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r The corpse bursts. {} strikes for {}.",
                    MechanicName(), dealt);
        }

        void DeathRattle::OnLootMoney(Ctx& ctx, Loot* loot)
        {
            if (!loot || !loot->gold || !ctx.self)
                return;

            float const mult = BoonMoneyMult(*ctx.self);
            if (mult <= 1.0f)
                return;

            uint64 const raised = static_cast<uint64>(static_cast<double>(loot->gold) * mult);
            loot->gold = static_cast<uint32>(std::min<uint64>(raised, std::numeric_limits<uint32>::max()));
        }

        std::string DeathRattle::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "Corpses burst two seconds after they fall, marked by a circle on"
                              " the ground. Standing in one costs "
                            + std::to_string(SEVERITY_PCT[i])
                            + "% of your maximum health. Step back after every kill.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(9, DeathRattle);
}
