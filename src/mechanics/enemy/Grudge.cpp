/*
 * mod-gauntlet - E5 Grudge: the dead linger, and standing where they fell saps you
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
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
#include <iterator>

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

        // The card's ladder: 2 -> 3 -> 5% of maximum health per second, and
        // 7% at rank IV. Walking away still stops it at every rank, so the
        // ladder prices standing on a corpse rather than removing the out.
        constexpr uint32 DRAIN_PCT = Rules::GRUDGE_DRAIN_PCT;

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
            // Four seconds, not at once. The corpse is a race now: loot it
            // before the spirit forms and it never does. docs/greed-redesign.md
            // section 3 -- the spirit still punishes standing around, it just
            // stopped punishing playing quickly.
            void OnKill(Ctx& ctx, Creature* killed) override { Mark(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Mark(ctx, killed); }

            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "grudge: " + std::to_string(_pending.size()) + " corpse(s) counting down, "
                                + std::to_string(_spirits.size()) + " spirit(s) standing, "
                                + std::to_string(_beaten) + " beaten to the corpse";
                out += _inSpirit ? "; standing in one now" : "; not in one";
                return out;
            }
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

            void Mark(Ctx& ctx, Creature* killed);
            void RaiseAt(Ctx& ctx, Position const& at);

            // A corpse whose spirit has not formed yet. Kept small: it lives
            // four seconds and a pull leaves a handful.
            struct Pending
            {
                ObjectGuid guid;
                Position   at;
                uint32     lootId = 0;
                uint32     leftMs = 0;
            };

            std::vector<Pending> _pending;
            uint32               _beaten = 0;
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

        // The kill only starts a clock. Everything Raise refuses -- this
        // module's own summons, anything that is not an ordinary foe -- is
        // refused here too, so a corpse that could never have grown a spirit
        // does not pretend it is about to.
        void Grudge::Mark(Ctx& ctx, Creature* killed)
        {
            Player* player = ctx.player;
            if (!player || !killed || !player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                return;
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;
            if (!IsOrdinaryFoe(killed))
                return;

            constexpr std::size_t MAX_PENDING = 8;
            if (_pending.size() >= MAX_PENDING)
                _pending.erase(_pending.begin());

            Pending p;
            p.guid   = killed->GetGUID();
            p.at     = killed->GetPosition();
            p.lootId = killed->GetCreatureTemplate()->lootid;
            p.leftMs = Rules::GRUDGE_RISE_MS;
            _pending.push_back(p);

            if (ctx.addon)
                ctx.addon->SendEvent(ctx.player, MechanicKey(),
                                     Rules::GRUDGE_RISE_MS / 1000u, MechanicName());
        }

        // Beat the clock and the corpse pays for it. The spirit never forms.
        void Grudge::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot)
        {
            Player* player = ctx.player;
            if (!loot || !player)
                return;

            auto const it = std::find_if(_pending.begin(), _pending.end(),
                                         [&](Pending const& p) { return p.guid == lootGuid; });
            if (it == _pending.end())
                return;

            uint32 const lootId = it->lootId;
            _pending.erase(it);
            ++_beaten;

            if (lootId != 0)
                for (uint32 roll = 1; roll < Rules::GRUDGE_LOOT_ROLLS; ++roll)
                    loot->FillLoot(lootId, LootTemplates_Creature, player, true, true);

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r You got there first. Nothing rises, and it gives up more.");
        }

        // The summon itself. Everything that decides *whether* a corpse earns
        // a spirit now happens in Mark, four seconds earlier; what is left here
        // is the part that has to happen at the moment it forms.
        void Grudge::RaiseAt(Ctx& ctx, Position const& at)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
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
        }

        void Grudge::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            // The pending corpses come first, and above the early return
            // below: while a spirit is still forming there are no spirits at
            // all, which is exactly when this has to run.
            for (std::size_t i = _pending.size(); i-- > 0; )
            {
                if (_pending[i].leftMs > diffMs)
                {
                    _pending[i].leftMs -= diffMs;
                    continue;
                }

                Position const at = _pending[i].at;
                _pending.erase(_pending.begin() + static_cast<std::ptrdiff_t>(i));
                RaiseAt(ctx, at);
            }

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

            uint32 const pct = DRAIN_PCT;

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
                            + std::to_string(DRAIN_PCT)
                            + "% of your maximum health as damage every second, and any healing"
                              " you receive is halved. Fight on fresh ground, and decide when"
                              " to loot.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(10, Grudge);
}
