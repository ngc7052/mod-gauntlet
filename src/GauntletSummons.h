/*
 * mod-gauntlet - owner-bound summons, and the guarantee they come back out
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_SUMMONS_H
#define MOD_GAUNTLET_SUMMONS_H

#include "Gauntlet.h"
#include "ObjectGuid.h"
#include <cstddef>
#include <unordered_map>
#include <vector>

// Core game types by pointer or reference only, so a mechanic that includes
// this header does not have to drag Player.h in behind it. ObjectGuid is the
// exception and is included, because the per-owner map is keyed on one --
// GauntletAddon.h makes the same trade for the same reason.
class Creature;
class Player;
struct Position;

namespace Gauntlet
{
    // ---------------------------------------------------------------------
    // The reserved creature_template range. data/sql/db-world/base/
    // gauntlet_creatures.sql defines exactly these five and nothing else ever
    // writes into 900000-900999.
    // ---------------------------------------------------------------------
    constexpr uint32 ENTRY_SHADE        = 900001;   // S1
    constexpr uint32 ENTRY_SCAVENGER    = 900002;   // S3 Carrion
    constexpr uint32 ENTRY_AMBUSHER     = 900003;   // S5 Ambush
    constexpr uint32 ENTRY_RESTLESS     = 900004;   // E5 Grudge, visual only
    constexpr uint32 ENTRY_DOPPELGANGER = 900005;   // S2 Echo

    constexpr uint32 ENTRY_FIRST = ENTRY_SHADE;
    constexpr uint32 ENTRY_LAST  = ENTRY_DOPPELGANGER;

    // World Trigger (Not Immune PC), plan appendix A. Invisible model,
    // NOT_SELECTABLE, CREATURE_FLAG_EXTRA_TRIGGER; the core's own scripts use
    // it wherever something has to be cast from a point on the ground. Falling
    // Sky's mark and Death Rattle's circle are both one of these, so the entry
    // is named here rather than in each mechanic -- Scenery() below has to know
    // it.
    constexpr uint32 ENTRY_WORLD_TRIGGER = 21252;

    // "Scenery": something this module put into the world that the player
    // cannot fight. An invisible trigger drawing a circle, and Grudge's
    // Restless Spirit, which is NON_ATTACKABLE, NOT_SELECTABLE and rooted.
    //
    // The distinction exists because of the cap below. Design section 4.2's
    // "at most four affix-spawned creatures in total" is a rule about
    // uninvited *enemies* -- what it is protecting is a player from being
    // swarmed by four affixes at once. Counting a telegraph against it would
    // mean that a run carrying Falling Sky and Death Rattle silently stops
    // drawing circles as soon as two corpses are counting down, which is
    // design section 4.8's rule broken by design section 4.2's, and the
    // telegraph is the half that must never lose.
    constexpr bool IsScenery(uint32 entry)
    {
        return entry == ENTRY_WORLD_TRIGGER || entry == ENTRY_RESTLESS;
    }

    // Scenery gets a cap of its own so that a mechanic arming in a loop is
    // still bounded. Two circles from Death Rattle, two spirits from Grudge and
    // one mark from Falling Sky is the worst an honest run can produce.
    constexpr uint32 SUMMON_CAP_SCENERY = 6;   // TODO(design)

    // Design section 4.2: at most one uninvited creature from the stalker/
    // ambush group alive per player, and at most four affix-spawned creatures
    // in total. The total is also Gauntlet.Summons.MaxAlive in the config; the
    // stalker cap is not configurable because it is a coherence rule rather
    // than a tuning knob -- two stalkers at once is a different game.
    constexpr uint32 SUMMON_CAP_TOTAL   = 4;
    constexpr uint32 SUMMON_CAP_STALKER = 1;

    // S1's card: the Shade gives up when its owner is more than 150 yd away
    // for 15 s. Applied to every summon, because "the owner walked off" is the
    // same situation whatever spawned the thing.
    constexpr float  SUMMON_LEASH_YARDS = 150.0f;
    constexpr uint32 SUMMON_LEASH_MS    = 15000;

    // A backstop, not a rule. Every summon already has a TempSummon timer and
    // an owner check, but TEMPSUMMON_TIMED_OR_DEAD_DESPAWN freezes its timer
    // while the creature is in combat, so a summon locked in a fight it can
    // neither win nor leave would otherwise be immortal. Nothing this module
    // spawns has any business living a quarter of an hour, and no card names a
    // ceiling, so this is a chosen one.
    constexpr uint32 SUMMON_MAX_LIFE_MS = 900000;   // TODO(design)

    // How often the shared AI re-checks its owner. The owner check is the one
    // that actually guarantees nothing is left behind, so it is deliberately
    // frequent; it is two hash lookups.
    constexpr uint32 SUMMON_CHECK_MS = 500;

    // Everything this module puts into the world, and the only thing that
    // takes it out again.
    //
    // A singleton like Addon, with its own per-player map. It is deliberately
    // not part of RunState and does not touch GauntletMgr: a creature has to
    // be cleaned up whether or not the run that spawned it is still loaded.
    class Summons
    {
    public:
        static Summons* instance();

        // Reads Gauntlet.Summons.*. Safe to call repeatedly; called lazily on
        // the first Summon if integration never wires it to OnAfterConfigLoad.
        void LoadConfig();

        // Owner-bound. Levels the creature to the owner, records the GUID
        // against the owner so logout, death and zone change can clean it up,
        // refuses past the caps and returns nullptr when it does -- the caller
        // re-arms its clock rather than retrying or queueing.
        //
        // `mechanic` is an addition to the frozen signature, defaulted so
        // existing callers are unaffected: DespawnFor(owner, mechanic) needs to
        // know which affix owns which creature, and the entry alone cannot say
        // it once Phase 2 starts summoning copies of existing entries. Passing
        // MECHANIC_NONE derives it from the entry where that is possible.
        Creature* Summon(Player* owner, uint32 entry, Position const& at,
                         uint32 despawnMs, bool countsAsStalker,
                         uint16 mechanic = MECHANIC_NONE);

        void DespawnAll(Player* owner);
        void DespawnFor(Player* owner, uint16 mechanic);
        uint32 AliveFor(Player* owner) const;
        bool   IsGauntletSummon(Creature* c) const;
        Player* OwnerOf(Creature* c) const;

        // Logout. Despawns anything still standing and then drops the owner's
        // records. It does not need a Player: each record carries the map it
        // was spawned on, so this still works from a hook that has only a GUID.
        void Forget(ObjectGuid ownerGuid);

        // ------------------------------------------------------------------
        // Additions to the frozen interface. None of them changes a signature
        // above; they exist because something has to do these jobs and this is
        // the only class that knows the answers.
        // ------------------------------------------------------------------

        // True while a stalker-or-ambush creature is alive for this player.
        // What the addon's `SUMMON key 0/1` reports.
        bool HasStalker(Player* owner) const;

        // Which affix spawned this creature, or MECHANIC_NONE if it is not
        // ours. The death-attribution line and DespawnFor both need it.
        uint16 MechanicOf(Creature* c) const;

        // The rank ladders. Every card scales its summon's health and damage
        // by rank (the Shade x1.5/x2/x2.5), and the template can only carry
        // one of the three; the mechanic applies the rest here. Multiplies
        // what the creature already has and refills it, so it composes with
        // the level scaling rather than replacing it.
        void Scale(Creature* c, float healthMult, float damageMult) const;

        // Installed by integration so the addon can be told when a summon
        // appears or disappears without this class knowing the protocol. The
        // owner is passed as a GUID because a summon frequently outlives its
        // owner's session by the few milliseconds it takes to despawn it.
        using ChangeFn = void (*)(ObjectGuid ownerGuid, uint16 mechanic, uint32 entry, bool alive);
        void SetObserver(ChangeFn fn) { _observe = fn; }

        // ------------------------------------------------------------------
        // Called by GauntletSummonAI.cpp only.
        // ------------------------------------------------------------------

        // The level this creature is being summoned at, for the
        // OnBeforeCreatureSelectLevel hook. False when the creature is not one
        // of ours, which is the answer for every other spawn on the realm.
        bool PendingLevel(Creature* c, uint8& level) const;

        // True while this exact creature is inside its own Summon() call, which
        // is the window both AllCreatureScript hooks run in.
        //
        // OnBeforeCreatureSelectLevel is not the only one that needs it any
        // more. Phase 2's Reinforcements summons a copy of *the creature the
        // owner is already fighting*, so the thing that arrives carries a world
        // DB template this module does not own and cannot give a ScriptName to
        // -- and without an AI of ours it would be an ordinary mob that aggroes
        // bystanders and never leashes to an owner. GetCreatureAI answers for
        // it instead (plan section 6: "custom AI for stalkers without touching
        // the world DB's script names"), and this is the test it makes.
        bool IsPendingSummon(Creature* c) const;

        // Bookkeeping: the creature died, and the creature left the world.
        // Both exist because they are minutes apart -- a corpse lingers for the
        // normal decay -- and "the stalker is gone" is news the moment it dies,
        // not when its body finally disappears. The record survives NoteDied so
        // that IsGauntletSummon still answers true while the kill is being
        // rewarded, which is what halves the experience.
        void NoteDied(Creature* c);
        void NoteRemoved(Creature* c);

        // A cheap gate the AllCreatureScript can test before doing anything
        // else, so a realm with no Gauntlet player pays one integer compare.
        bool Idle() const { return _ownerOf.empty() && _pendingOwner.IsEmpty(); }

    private:
        struct Record
        {
            ObjectGuid guid;        // the creature
            uint32     entry   = 0;
            uint16     mechanic = MECHANIC_NONE;
            uint32     mapId   = 0;
            uint32     instanceId = 0;
            bool       stalker = false;
            bool       gone    = false;   // dead; reported, not yet removed
        };

        using Records = std::vector<Record>;

        Creature* Resolve(Record const& r) const;
        void      Drop(Records& list, std::size_t index);
        void      Prune(ObjectGuid ownerGuid);
        void      EnsureConfig();

        std::unordered_map<ObjectGuid, Records>    _byOwner;
        std::unordered_map<ObjectGuid, ObjectGuid> _ownerOf;

        // Set for the duration of one SummonCreature call, so the level hook
        // can recognise the creature it is being asked about before anything
        // has had a chance to record it.
        ObjectGuid _pendingOwner;
        uint8      _pendingLevel = 1;

        ChangeFn _observe = nullptr;

        uint32 _maxAlive     = SUMMON_CAP_TOTAL;
        bool   _configLoaded = false;
    };
}

#define sGauntletSummons Gauntlet::Summons::instance()

#endif // MOD_GAUNTLET_SUMMONS_H
