/*
 * mod-gauntlet - E5 Grudge: the dead linger, and standing where they fell saps you
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
#include "ObjectAccessor.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"

#include <algorithm>
#include <string>
#include <vector>

// Registry id 10. Design section 3, card E5: "The dead linger. Standing where an
// enemy died saps you."
//
// The card calls it "Sanguine for one player", and the decision it creates is
// about *when to loot*: loot now and eat the ticks, or wait twenty-five seconds.
// That decision only exists because the spirit is visible, which is why the
// Restless Spirit is a real creature standing on the corpse rather than an
// invisible radius -- and why it is worth a summon slot.
//
// Mutually exclusive with Death Rattle through the registry's
// "onkill-positional" key (design section 4.1): both are on-kill positional
// rules and together they are just "melee is bad".

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_GRUDGE = 10;

        // The card's ladder: 2 -> 3 -> 5% of maximum health per second.
        constexpr uint32 DRAIN_PCT[MAX_RANK] = { 2, 3, 5 };

        // The card's other numbers, which do not ladder: 25 s, 4 yd, and half
        // the healing while inside one.
        constexpr uint32 SPIRIT_LIFE_MS  = 25000;
        constexpr float  SPIRIT_RADIUS   = 4.0f;
        constexpr float  HEAL_MULT       = 0.5f;

        // How many spirits may stand at once. The summon wrapper caps every
        // affix at four creatures between them, and a camp cleared in twenty
        // seconds would have Grudge take all four -- leaving a carried Shade or
        // Echo unable to spawn at all. Two is enough for the "fight on fresh
        // ground" decision to bite and leaves the rest of the budget alone.
        constexpr std::size_t MAX_SPIRITS = 2;   // TODO(design)

        // The drain's own cadence. The card says "per second" and means it, so
        // this is a second and not the module's 500 ms tick: half a percent of
        // a level-10 pool rounds to zero twice as often as a whole one does.
        constexpr uint32 DRAIN_INTERVAL_MS = 1000;

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_GRUDGE);
            return def ? def->name : "Grudge";
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_GRUDGE);
            return def ? def->key : "grudge";
        }

        class Grudge final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override;
            void OnKill(Ctx& ctx, Creature* killed) override { Raise(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Raise(ctx, killed); }
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // The curse's own healing cut, and the boon, in one place. The card
            // says "receive 50% less healing" while inside a spirit; the
            // BonusHealing boon is what the player gets back everywhere else,
            // which makes the affix a reason to fight on fresh ground rather
            // than a flat healing tax -- the exact distinction design section 5
            // draws when it replaces Withering.
            float HealTakenMult(Ctx& ctx, Unit* /*healer*/, SpellInfo const*) override
            {
                float mult = _inSpirit ? HEAL_MULT : 1.0f;
                if (ctx.self)
                    mult *= BoonHealMult(*ctx.self);
                return mult;
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            struct Spirit
            {
                ObjectGuid guid;
                Position   at;
                uint32     mapId      = 0;
                uint32     instanceId = 0;
            };

            void Raise(Ctx& ctx, Creature* killed);
            void Drain(Ctx& ctx, Player* player);
            void Publish(Ctx& ctx, bool inside);

            std::vector<Spirit> _spirits;
            uint32 _drainMs  = 0;
            bool   _inSpirit = false;
        };

        void Grudge::OnDetach(Ctx& ctx)
        {
            if (ctx.player)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_GRUDGE);

            _spirits.clear();

            if (_inSpirit)
                Publish(ctx, false);
            _inSpirit = false;
        }

        void Grudge::Raise(Ctx& ctx, Creature* killed)
        {
            Player* player = ctx.player;
            if (!player || !killed || !player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && (ctx.run->dead || !ctx.run->pending.empty()))
                return;

            // Nothing this module spawned leaves a spirit: a Shade killed on
            // ground the player chose must not then deny them that ground.
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;

            if (!IsOrdinaryFoe(killed))
                return;

            // Prune what has already gone before asking for room, so a camp
            // cleared slowly is never refused by a spirit that faded a minute
            // ago.
            _spirits.erase(std::remove_if(_spirits.begin(), _spirits.end(),
                                          [player](Spirit const& s)
                                          {
                                              Creature* c = ObjectAccessor::GetCreature(*player, s.guid);
                                              return !c || !c->IsInWorld();
                                          }),
                           _spirits.end());

            if (_spirits.size() >= MAX_SPIRITS)
                return;

            Position const at = killed->GetPosition();

            Creature* spirit = sGauntletSummons->Summon(player, ENTRY_RESTLESS, at, SPIRIT_LIFE_MS,
                                                        /*countsAsStalker*/ false, MECHANIC_GRUDGE);
            if (!spirit)
                return;

            Spirit record;
            record.guid       = spirit->GetGUID();
            record.at         = at;
            record.mapId      = player->GetMapId();
            record.instanceId = player->GetInstanceId();
            _spirits.push_back(record);

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), SPIRIT_LIFE_MS / 1000u, MechanicName());
        }

        void Grudge::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            if (_spirits.empty())
            {
                if (_inSpirit)
                {
                    _inSpirit = false;
                    Publish(ctx, false);
                }
                return;
            }

            _drainMs += diffMs;
            if (_drainMs < DRAIN_INTERVAL_MS)
                return;
            _drainMs = 0;

            Drain(ctx, player);
        }

        void Grudge::Drain(Ctx& ctx, Player* player)
        {
            // A spirit whose creature is gone is a spirit that has faded: the
            // template's own 25 s TempSummon timer is what ends it, and this is
            // where the record catches up. Resolving through the player's map
            // also answers null for a spirit left behind by a zone change,
            // which is exactly the escape the card's "fight on fresh ground"
            // counterplay names.
            bool inside = false;

            for (std::size_t i = _spirits.size(); i-- > 0; )
            {
                Spirit const& s = _spirits[i];

                Creature* c = player->IsInWorld() ? ObjectAccessor::GetCreature(*player, s.guid) : nullptr;
                if (!c || !c->IsInWorld())
                {
                    _spirits.erase(_spirits.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }

                if (player->GetMapId() != s.mapId || player->GetInstanceId() != s.instanceId)
                    continue;

                if (player->GetExactDist2d(s.at) <= SPIRIT_RADIUS)
                    inside = true;
            }

            if (inside != _inSpirit)
            {
                _inSpirit = inside;
                Publish(ctx, inside);
            }

            if (!inside || !player->IsAlive() || !player->IsInWorld())
                return;

            if (ctx.run && ctx.run->dead)
                return;

            uint32 const pct = DRAIN_PCT[RankIndex(ctx.self)];

            uint32 damage = static_cast<uint32>(static_cast<uint64>(player->GetMaxHealth()) * pct / 100u);
            if (damage == 0)
                damage = 1;

            // This affix hurts its owner on its own tick rather than through
            // the scheduler, so nothing else is going to claim the death for
            // it. Design section 4.8's fourth question is answered here.
            if (ctx.run)
                ctx.run->NoteActor(MECHANIC_GRUDGE);

            player->EnvironmentalDamage(DAMAGE_FIRE, damage);
        }

        void Grudge::Publish(Ctx& ctx, bool inside)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            // A readout rather than a chat line per second. The health going
            // down is visible; what is not visible is *why*, and the addon's
            // STAT is what says so continuously. The one chat line on entering
            // is for a player without it.
            if (ctx.addon)
                ctx.addon->QueueStat(player, MechanicKey(), inside ? 1 : 0);

            if (inside && player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r You are standing where something died. It is taking it out of you.");
        }

        std::string Grudge::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            // Two words changed in Phase 3, both because a player read them and
            // could not tell what the affix did.
            //
            // The registry blurb said "saps you", which is the design card's
            // own wording and is wrong twice over: it names no effect, and Sap
            // is a rogue ability, so it reads as crowd control.
            //
            // This sentence said "and heal for half", which was meant to say
            // that healing received is halved and can just as easily be read
            // as the drain healing you -- the opposite of what it does.
            std::string out = "Everything you kill leaves a Restless Spirit standing on its corpse"
                              " for twenty-five seconds. Within four yards of one you take "
                            + std::to_string(DRAIN_PCT[i])
                            + "% of your maximum health as damage every second, and any healing"
                              " you receive is halved. Fight on fresh ground, and decide when"
                              " to loot.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(10, Grudge);
}
