/*
 * mod-gauntlet - E2 Craven: enemies flee at low health, and come back with friends
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <vector>
#include <iterator>

// Registry id 7. Design section 3, card E2: "Enemies flee at 25% health, and
// come back with friends."
//
// WoW humanoids already flee at about 15%; what this generalises is the
// *consequence*, and the consequence is what turns it into a decision.
// Execute-range awareness becomes real: snare, root, stun or burst through the
// threshold so nothing flees, and fight away from packs so there is nobody to
// fetch. Both of those are buttons a levelling character already owns, which is
// design section 2.8's first principle.
//
// The threshold crossing is the one thing in this module that cannot be written
// against any other hook: UnitScript::OnDamage runs at Unit.cpp:999, before the
// health is applied, so `GetHealth() - damage` is the health the creature is
// about to have. Every other damage hook has already lost that number.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_CRAVEN = 7;

        // The card's ladder: flee at 20 -> 25 -> 35%, fetches 1 -> 1 -> 2.
        // Rank IV is past the card at 50% and 3: every enemy runs at half
        // health, which turns the affix from a nuisance into a rule about
        // how you open a fight -- burst it down or expect the camp.
        constexpr uint32 FLEE_PCT = 25;
        constexpr uint32 FETCHES = 1;

        // The card's fixed numbers: five seconds of running, and fifteen yards
        // of camp to fetch from when the running stops.
        constexpr uint32 FLEE_MS      = 5000;
        constexpr float  FETCH_YARDS  = 15.0f;

        // How many runners are tracked at once. A record is three fields and a
        // guid and never a pointer, so this bounds a pathological pull rather
        // than rationing honest play.
        constexpr std::size_t MAX_RUNNERS = 6;   // TODO(design)


        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CRAVEN);
            return def ? def->name : "Craven";
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CRAVEN);
            return def ? def->key : "craven";
        }

        class Craven final : public IMechanic
        {
        public:
            void OnDetach(Ctx&) override { _runners.clear(); _bounty.clear(); }

            // The runners go when the fight does; the bounties do not. A
            // corpse is looted after the fight far more often than during it,
            // and the reward for winning the chase must not expire because the
            // player won it.
            void OnLeaveCombat(Ctx&) override { _runners.clear(); }

            void OnKill(Ctx& ctx, Creature* killed) override { Claim(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Claim(ctx, killed); }

            // The bounty's first half. The hook carries the victim, so no
            // guesswork about which kill this experience is for.
            void OnXP(Ctx& /*ctx*/, uint32& amount, Unit* victim) override
            {
                if (!victim || !IsStillRunning(victim->GetGUID()))
                    return;

                amount = Rules::CravenBountyXP(amount);
                ++_paidXp;
            }

            // And its second. The doc proposed flagging the corpse here so
            // OnItemRoll could double the chance, and that cannot work: the
            // item roll happens inside FillLoot, which the core has already
            // finished by the time this hook is called
            // (OnPlayerBeforeSendLoot). Rolling the creature's own table a
            // second time is what "rolls its loot twice" means anyway, and it
            // is the seam Fresh Kill and three other cards already use.
            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "craven: " + std::to_string(_runners.size()) + " runner(s) tracked, "
                                + std::to_string(_paidXp) + " bounty XP paid, "
                                + std::to_string(_paidLoot) + " corpse(s) rolled twice, "
                                + std::to_string(_bounty.size()) + " bounty corpse(s) unopened";
                if (_runners.empty() && _bounty.empty())
                    out += "; nothing has fled since this was attached";
                return out;
            }
            void OnCreatureDamaged(Ctx& ctx, Creature* victim, uint32 damage) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // BonusDamage. The card's counterplay is "burst at 30% so nothing
            // flees", so the boon is exactly the tool the curse asks for and
            // spending it is what makes the curse cheaper.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            // One creature that has already run. `fetchMs` counts down from the
            // moment it started running; when it reaches zero the camp is
            // searched from wherever the runner ended up. `done` is kept rather
            // than the record being dropped so a creature cannot flee twice --
            // the card says "first drops below", and a mob knocked back and
            // forth across the threshold must not run every time.
            struct Runner
            {
                ObjectGuid guid;
                uint32     fetchMs = 0;
                bool       fetched = false;
            };

            Runner* Find(ObjectGuid const& guid);
            void    Flee(Ctx& ctx, Creature* victim);
            void    Fetch(Ctx& ctx, Runner& runner);
            void    Claim(Ctx& ctx, Creature* killed);

            // A runner that is still running: tracked, and has not yet reached
            // its camp. Once it has fetched, the chase is lost and there is no
            // bounty to pay.
            bool IsStillRunning(ObjectGuid const& guid) const
            {
                for (Runner const& r : _runners)
                    if (r.guid == guid)
                        return !r.fetched;
                return false;
            }

            std::vector<Runner>     _runners;
            std::vector<ObjectGuid> _bounty;
            uint32                  _paidXp   = 0;
            uint32                  _paidLoot = 0;
        };

        Craven::Runner* Craven::Find(ObjectGuid const& guid)
        {
            for (Runner& runner : _runners)
                if (runner.guid == guid)
                    return &runner;
            return nullptr;
        }

        void Craven::OnCreatureDamaged(Ctx& ctx, Creature* victim, uint32 damage)
        {
            Player* player = ctx.player;
            if (!player || !victim || !damage)
                return;
            if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                return;

            if (!IsOrdinaryFoe(victim) || !victim->IsAlive())
                return;

            // Nothing this module summoned runs away: a Shade that fled at 25%
            // would take the fight the player chose out from under them, and a
            // Reinforcements copy fetching more copies is a loop.
            if (sGauntletSummons->IsGauntletSummon(victim))
                return;

            if (Find(victim->GetGUID()))
                return;   // it has already had its one flee

            uint32 const max = victim->GetMaxHealth();
            if (max == 0)
                return;

            uint32 const now = victim->GetHealth();
            if (damage >= now)
                return;   // the blow is lethal; a corpse does not flee

            uint32 const after     = now - damage;
            uint32 const threshold = static_cast<uint32>(
                static_cast<uint64>(max) * FLEE_PCT / 100u);

            // "first drops below": the crossing, not the state. A creature
            // already under the line when this affix was picked up mid-fight
            // has not crossed anything.
            if (now <= threshold || after > threshold)
                return;

            Flee(ctx, victim);
        }

        void Craven::Flee(Ctx& ctx, Creature* victim)
        {
            Player* player = ctx.player;

            while (_runners.size() >= MAX_RUNNERS)
                _runners.erase(_runners.begin());

            Runner runner;
            runner.guid    = victim->GetGUID();
            runner.fetchMs = FLEE_MS;
            _runners.push_back(runner);

            // MotionMaster::MoveFleeing(enemy, time) is the core's own fear
            // movement with a timer on it (MotionMaster.h:239), so the creature
            // runs from this player specifically and hands control back by
            // itself when the five seconds are up. Nothing here has to undo it.
            victim->GetMotionMaster()->MoveFleeing(player, FLEE_MS);

            victim->HandleEmoteCommand(EMOTE_ONESHOT_COWER);               // Unit.h:1943

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), FLEE_MS / 1000u, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r {} breaks and runs.",
                    victim->GetNameForLocaleIdx(LOCALE_enUS));
        }

        void Craven::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (_runners.empty())
                return;

            Player* player = ctx.player;
            if (!player || !player->IsInWorld())
            {
                _runners.clear();
                return;
            }

            for (std::size_t i = _runners.size(); i-- > 0; )
            {
                Runner& runner = _runners[i];

                if (runner.fetched)
                {
                    // Kept only so the creature cannot flee a second time, and
                    // only for as long as it is still in the fight. OnLeaveCombat
                    // clears the lot.
                    if (!ObjectAccessor::GetCreature(*player, runner.guid))
                        _runners.erase(_runners.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }

                if (runner.fetchMs > diffMs)
                {
                    runner.fetchMs -= diffMs;
                    continue;
                }

                runner.fetchMs = 0;
                runner.fetched = true;
                Fetch(ctx, runner);
            }
        }

        void Craven::Fetch(Ctx& ctx, Runner& runner)
        {
            Player* player = ctx.player;

            // The search starts where the runner *ended up*, which is the whole
            // reason the flee is five seconds long: it is the distance the
            // player is given to close, and the ground the player chose to
            // fight on is what decides whether there is anybody out there.
            Creature* runaway = ObjectAccessor::GetCreature(*player, runner.guid);
            if (!runaway || !runaway->IsAlive())
                return;

            uint32 const wanted = FETCHES;

            Creature* previous = nullptr;
            uint32    fetched  = 0;

            for (uint32 n = 0; n < wanted; ++n)
            {
                Creature* kin = NearestIdleKin(player, runaway, runaway, FETCH_YARDS, previous);
                if (!kin)
                    break;

                if (CreatureAI* ai = kin->AI())
                    ai->AttackStart(player);

                kin->AddThreat(player, 1.0f);                              // Unit.h:1099

                previous = kin;
                ++fetched;
            }

            // The runner comes back either way -- MoveFleeing's timer has just
            // expired and its own AI resumes -- so the affix has acted whether
            // or not the camp had anybody left in it.
            if (fetched == 0)
                return;

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    fetched == 1 ? "|cffff2020[Gauntlet]|r It comes back, and it is not alone."
                                 : "|cffff2020[Gauntlet]|r It comes back with company.");
        }

        std::string Craven::Describe(AffixInstance const& self) const
        {

            std::string out = "The first time an enemy drops below "
                            + std::to_string(FLEE_PCT)
                            + "% health it runs for five seconds, and then fetches ";
            out += FETCHES == 1 ? "the nearest idle enemy of its own kind"
                                   : "the two nearest idle enemies of its own kind";
            out += " within fifteen yards of wherever it stopped. Burst, snare or stun through"
                   " the threshold, and fight away from packs.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

        // A runner cut down before it reached its camp. The guid is kept so the
        // corpse can pay when it is opened, which is usually after the fight.
        void Craven::Claim(Ctx& /*ctx*/, Creature* killed)
        {
            if (!killed || !IsStillRunning(killed->GetGUID()))
                return;

            constexpr std::size_t MAX_BOUNTY = 8;
            if (_bounty.size() >= MAX_BOUNTY)
                _bounty.erase(_bounty.begin());

            _bounty.push_back(killed->GetGUID());
        }

        void Craven::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot)
        {
            Player* player = ctx.player;
            if (!loot || !player)
                return;

            auto const it = std::find(_bounty.begin(), _bounty.end(), lootGuid);
            if (it == _bounty.end())
                return;

            _bounty.erase(it);

            Creature* corpse = ObjectAccessor::GetCreature(*player, lootGuid);
            uint32 const lootId = corpse ? corpse->GetCreatureTemplate()->lootid : 0;
            if (lootId == 0)
                return;

            for (uint32 roll = 1; roll < Rules::CRAVEN_BOUNTY_ROLLS; ++roll)
                loot->FillLoot(lootId, LootTemplates_Creature, player, true, true);

            ++_paidLoot;

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r It did not make it home. Its pockets are yours twice over.");
        }

    GAUNTLET_MECHANIC(7, Craven);
}
