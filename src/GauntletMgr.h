/*
 * mod-gauntlet - per-character run state
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MGR_H
#define MOD_GAUNTLET_MGR_H

#include "Gauntlet.h"
#include "GauntletAggregate.h"
#include "GauntletMechanic.h"
#include "GauntletScheduler.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Gauntlet
{
    // RunState moved to Gauntlet.h with the switchover: plan section 2.6 puts
    // it there, and GauntletMechanic.h's Ctx needs it without dragging
    // Player.h into every mechanic. Its member functions are defined in
    // GauntletMgr.cpp, which is the only place IMechanic is complete.

    class Mgr
    {
    public:
        static Mgr* instance();

        void Load(Player* player);
        void Save(Player* player);
        void Forget(ObjectGuid guid);

        // Everything this module stores about one character, deleted. The core
        // reuses the GUIDs of deleted characters, and a run keyed on the GUID
        // alone would otherwise be inherited by whoever is created next --
        // retired flag, affixes and all. Called from OnPlayerDeleteFromDB so it
        // rides the same transaction the character's own rows are deleted in.
        void PurgeCharacter(uint32 lowGuid, CharacterDatabaseTransaction trans);

        RunState* Get(Player* player);

        // The Scheduler for a loaded run. It is not a member of RunState
        // because GauntletScheduler.h includes Gauntlet.h and a header cannot
        // include something that includes it back; see the note beside
        // RunState::state. Null for a player with no loaded run, which every
        // caller already has to handle because Ctx::clock is documented as
        // possibly null.
        Scheduler* ClockFor(Player* player);

        // The one way a Ctx is built. Every dispatch site goes through it, so a
        // mechanic can never be handed a context with half its members filled
        // in -- which is the failure Phase 0 shipped, where clock and state
        // were null everywhere by construction.
        Ctx MakeCtx(Player* player, RunState* run, AffixInstance* self);

        // ------------------------------------------------------------------
        // The hook fan-outs. GauntletScripts.cpp owns the adapters -- plan
        // section 2.6 leaves it nothing else -- and the loops over the carried
        // set live here, beside the map they walk.
        // ------------------------------------------------------------------

        // One player's world tick: the grace countdown, the actor's memory, the
        // periodic state write, IMechanic::OnTick on the 500 ms cadence, and
        // the scheduler with a filled Suppression.
        void Tick(Player* player, uint32 diffMs);

        void OnEnterCombat(Player* player, Unit* enemy);
        void OnLeaveCombat(Player* player);
        void OnCreatureKill(Player* player, Creature* killed, bool byPet);
        void OnDamageTaken(Player* player, Unit* attacker, uint32 amount);

        // The absolute clamp on a heal, run after ModifyHealReceived has
        // already applied the aggregate and its floor.
        void OnHeal(Player* player, uint32& heal);

        // The cheat-death path, dispatched from UnitScript::DealDamage before
        // the health is applied. Returns the damage the blow should actually
        // do; a mechanic may only lower it, never raise it. Called only when
        // the blow would otherwise be fatal, so the ordinary case costs one
        // integer comparison.
        uint32 OnLethal(Player* player, uint32 damage);

        void OnSpellCast(Player* player, Spell* spell);

        // Group membership changed. Re-runs the stat chain, because nothing
        // else will.
        void OnGroupChanged(Player* player);

        // The repair bill and the three economy vetoes. Allows() answers false
        // the moment any carried mechanic refuses; it does not keep asking,
        // because one refusal is the answer and a second chat line naming a
        // second affix would be noise.
        void OnRepair(Player* player, float& discountMod);
        bool Allows(Player* player, Restricted what);
        void OnMaxHealth(Player* player, float& value);
        void OnGiveXP(Player* player, uint32& amount, Unit* victim);
        void OnLootMoney(Player* player, Loot* loot);

        // The loot window opening, with a real guid on it. Carrion's
        // counter is the only thing that wants the window rather than the
        // purse, and the two hooks fire for the same corpse.
        void OnLootWindow(Player* player, ObjectGuid const& lootGuid, Loot* loot);

        // One item's drop chance, on its way through the loot roll. The core
        // hands this hook a const Player because it has no business changing
        // one; the module needs a non-const pointer only to find the run keyed
        // on its guid, and changes nothing about the player either.
        void OnItemRoll(Player const* player, float& chance);

        // The loot-group size, for the one bargain that pays in items.
        void OnLootGroupAmount(Player const* player, uint32& groupAmount);

        // A blow this character or their pet landed on a creature, before
        // the health comes off. Craven watches for the threshold crossing.
        void OnCreatureDamaged(Player* player, Creature* victim, uint32 damage);

        // Re-opens the grace window. Login sets it in Load, where the run is
        // already in hand; this is the entry point for everything else. A zone
        // change also takes every summon out of the world, which CONTRACT-P1
        // section 2.4 requires and which nothing else covers for a move that
        // stays inside one map.
        void OpenGrace(Player* player);
        void OnZoneChanged(Player* player);

        // Death, whether or not the realm is hardcore: KILLBY goes out, the
        // queue is emptied, every summon is despawned and the state store is
        // written. The hardcore timer is armed separately.
        void OnDied(Player* player);

        // Which affix last acted on this character. Set by the dispatchers
        // themselves -- a scheduler Fire, a summon's blow, a DamageTakenMult
        // that came back above 1.0 -- because a mechanic has no way to say so.
        void NoteActor(Player* player, uint16 mechanic);

        // Design section 4.8's fourth question, answered on death: KILLBY on
        // the addon channel and one chat line for the player without it.
        void ReportKilledBy(Player* player);

        // Design section 4.2's event budget: how many carried affixes are
        // MF_Timed. Called whenever the carried set changes.
        void SyncTimedAffixCount(Player* player);

        // `.gauntlet debug fire <key>`: releases one queued event now, keeping
        // the warning it already sent. Returns false when that mechanic has
        // nothing queued. It is here rather than on Scheduler because it has to
        // build the Ctx and dispatch, which is Mgr's job and not the clock's.
        bool FireNow(Player* player, uint16 mechanic);

        // `.gauntlet debug events on|off`: the same switch as
        // Gauntlet.Events.Enable, for the length of this worldserver session.
        // Turning it off cancels every queued event; a config reload puts the
        // file's value back.
        void SetEventsEnabled(bool enabled);
        bool EventsEnabled() const { return _eventsEnabled; }

        void OfferTier(Player* player, uint32 tier);
        bool Pick(Player* player, uint32 index);
        void EndRun(Player* player, std::string const& cause);

        // Fires IMechanic::OnDetach over the carried set without destroying
        // anything; ~RunState does the freeing when Forget drops the run.
        void DetachAll(Player* player);

        // Recomputes the stats this module contributes to, and the only thing
        // that makes an AggregateKind::MaxHealth contribution real.
        //
        // Player::UpdateMaxHealth is what calls OnPlayerAfterUpdateMaxHealth,
        // and the core only calls it when *the core* thinks a stat moved -- a
        // level, a stamina change, an aura. Picking an affix is none of those,
        // so a BonusMaxHealth boon sat inert from the moment it was taken until
        // something unrelated happened to trigger a recompute. Every affix that
        // changes the pool has to ask for one when it arrives and when it
        // leaves.
        void RefreshStats(Player* player);

        // The product of every active affix's factor, clamped by the config's
        // caps. Replaces Multiplier: the old one summed percentages and
        // floored the result, this multiplies factors and clamps the product.
        // The conditions are evaluated here, against the live player, and
        // handed to the Player-free maths in GauntletAggregate.h.
        float Aggregate(Player* player, AggregateKind kind) const;

        // The same product at a damage or heal site, where the other unit is
        // known. The three IMechanic::*Mult callbacks are folded in *before*
        // plan section 2.5's clamp rather than on top of it: Champions' +25% is
        // one of them, and applied after the clamp it would sail straight past
        // the 2.0x ceiling on damage taken.
        float AggregateAt(Player* player, AggregateKind kind, Unit* other, SpellInfo const* spellInfo);

        // The death sequence (plan section 6, decision 5). Dying arms a timer
        // instead of ending the run outright, so a Phase 3 bargain charge has
        // somewhere to intervene; releasing or letting the timer expire ends
        // it. Phase 0 has no charge to spend, so every armed death ends the
        // run exactly as it did before.
        void BeginPendingDeath(Player* player);
        bool CancelPendingDeath(Player* player);
        void UpdateDeathTimer(Player* player, uint32 diffMs);
        bool AnyPendingDeath() const { return _pendingDeaths != 0; }
        bool IsPendingDeath(Player* player) const;

        // Bots are Player objects too; the challenge is for real players only.
        bool IsEligible(Player* player) const;

        bool Enabled() const { return _enabled; }
        void LoadConfig();

        uint32 Interval() const  { return _interval; }
        uint32 Choices() const   { return _choices; }
        bool   Hardcore() const  { return _hardcore; }

        AggregateCaps const& Caps() const { return _caps; }

        // The offer's player-facing name, built the way Affix::Name() built
        // one: the condition's adjective in front of the mechanic's name,
        // with the boon's adjective in front of that. Public because
        // GauntletScripts.cpp prints carried affixes too.
        std::string NameOf(uint16 mechanic, Condition condition, Boon boon) const;
        std::string DescribeOf(AffixInstance const& instance) const;

    private:
        bool ConditionActive(Player* player, Condition c) const;

        // Fills every slot of AggregateInput::conditionActive for `player`.
        void FillConditions(Player* player, AggregateInput& in) const;

        // The aggregate product with the Mult hooks folded in and nothing
        // clamped. AggregateAt clamps it; OnMaxHealth deliberately does not,
        // because it has a wound to subtract first and section 2.5's floor
        // must be applied once, over the finished number.
        float RawProduct(Player* player, AggregateKind kind, Unit* other, SpellInfo const* spellInfo);

        // The configured caps with every carried affix given its one chance to
        // widen one, per IMechanic::RelaxCaps. Two Phase 3 rows need it and
        // nothing else may: Cursed Hoard's curse is a genuine triple against a
        // 2.0x ceiling, and Lone Wolf halves your health against a 0.6 floor.
        //
        // It is evaluated per query rather than cached because both are
        // state-dependent by design -- the curse lifts after three kills, the
        // health returns the moment you leave the group -- so a cached ceiling
        // would outlive the thing that asked for it. The cost is one virtual
        // call per carried affix on a path that already makes three.
        AggregateCaps EffectiveCaps(Player* player, AggregateKind kind) const;

        // Dispatches one callback to every carried affix that has an
        // implementation. The Ctx is rebuilt per affix because `self` differs.
        template <typename Fn>
        void ForEachMechanic(Player* player, RunState* run, Fn&& fn);

        // Everything per-player that cannot live on RunState. Scheduler is the
        // reason this exists at all -- GauntletScheduler.h includes Gauntlet.h,
        // so RunState cannot hold one -- and the two accumulators keep it
        // company because they are the same kind of thing: session scheduling,
        // not anything a run is made of. Created by Load, erased by Forget, so
        // its lifetime is exactly the run's.
        struct Live
        {
            Scheduler clock;
            uint32    tickMs      = 0;   // up to Scheduler::TICK_MS, in front of OnTick
            uint32    stateSaveMs = 0;   // up to STATE_SAVE_MS, in front of State::SaveTo
        };

        Live* LiveFor(Player* player);

        std::unordered_map<ObjectGuid, RunState> _runs;
        std::unordered_map<ObjectGuid, Live>     _live;
        AggregateCaps _caps;
        bool   _enabled  = true;
        bool   _hardcore = true;
        uint32 _interval = 5;
        uint32 _choices  = 3;
        bool   _announce = true;
        bool   _playersOnly = true;
        uint32 _graceMs  = 60000;

        // The hardcore death window. It reads Gauntlet.Grace.Seconds like
        // _graceMs does, because plan section 6 decision 5 names the same sixty
        // seconds and there is no key of its own -- but it is a separate field
        // on purpose. Phase 1 gives that key its documented consumer, the event
        // grace window, and a later tuning pass that shortens the grace must not
        // silently shorten the window a hardcore character has to be saved in.
        uint32 _deathWindowMs = 60000;   // TODO(design)

        // Gauntlet.Events.*. MinSpacing is configured in seconds and the
        // scheduler takes milliseconds; the conversion is in LoadConfig and
        // nowhere else.
        bool   _eventsEnabled = true;
        uint32 _minSpacingMs  = Scheduler::DEFAULT_MIN_SPACING_MS;
        float  _budgetStep    = Scheduler::DEFAULT_BUDGET_STEP;

        // Gauntlet.Summons.XpRate: what a kill on a creature this module put
        // into the world is worth.
        float  _summonXpRate  = 0.5f;

        // How many loaded runs are counting down. OnPlayerUpdate runs for
        // every player on every tick, so the common case has to be an integer
        // test rather than a hash lookup.
        uint32 _pendingDeaths = 0;
    };
}

#define sGauntlet Gauntlet::Mgr::instance()

#endif
