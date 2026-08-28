/*
 * mod-gauntlet - the shared AI every gauntlet summon runs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletSummons.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"

// Targeted rather than `using namespace Gauntlet;`. Two of the module's names
// -- Condition and MECHANIC_NONE -- collide with core globals and cost Phase 0
// two builds; importing only what this file names keeps that class of failure
// impossible however the namespace grows.
using Gauntlet::ENTRY_RESTLESS;
using Gauntlet::SUMMON_CHECK_MS;
using Gauntlet::SUMMON_LEASH_MS;
using Gauntlet::SUMMON_LEASH_YARDS;
using Gauntlet::SUMMON_MAX_LIFE_MS;

namespace
{
    // One AI for all five templates. What differs between them is small enough
    // to read off the entry: the Restless Spirit is scenery and never fights,
    // everything else chases exactly one player and ignores the world.
    //
    // Taunt immunity is not here on purpose. It comes from flags_extra
    // CREATURE_FLAG_EXTRA_NO_TAUNT (0x100, CreatureData.h:54) on the template,
    // which is where the core enforces it.
    struct gauntlet_summon_ai : public ScriptedAI
    {
        explicit gauntlet_summon_ai(Creature* creature) : ScriptedAI(creature) { }

        void InitializeAI() override
        {
            // TempSummon's constructor takes the summoner GUID
            // (TemporarySummon.cpp:27-36) and Creature::Create runs afterwards,
            // so by the time AddToWorld builds this AI (Creature.cpp:319) the
            // owner is already known. Reading it here rather than in
            // IsSummonedBy keeps the AI correct even for a creature that
            // somehow arrives without the summon path.
            if (TempSummon* summon = me->ToTempSummon())
                _owner = summon->GetSummonerGUID();

            _passive = (me->GetEntry() == ENTRY_RESTLESS);

            if (_passive)
            {
                me->SetReactState(REACT_PASSIVE);
                me->SetImmuneToAll(true);
            }
            else
            {
                me->SetReactState(REACT_AGGRESSIVE);
            }

            ScriptedAI::InitializeAI();
        }

        // The one rule that makes the thing owner-bound. Creature::
        // CanCreatureAttack consults it for every candidate target
        // (Creature.cpp:2679), so no threat entry, no assist call and no
        // "attack me" effect can ever point this creature at a bystander.
        // Being attackable by anyone is deliberate and is decision 4.
        bool CanAIAttack(Unit const* target) const override
        {
            return !_passive && target && target->GetGUID() == _owner;
        }

        // A passer-by must not start a fight. CreatureAI's version calls
        // AttackStart on anything aggressive it can see (CreatureAI.cpp), and
        // Map::SummonCreature runs an AIRelocationNotifier over the whole
        // visibility range the moment we spawn (Object.cpp:2346-2347).
        void MoveInLineOfSight(Unit* /*who*/) override { }

        void AttackStart(Unit* target) override
        {
            if (CanAIAttack(target))
                ScriptedAI::AttackStart(target);
        }

        // Never sit in evade. Creature::Update skips UpdateAI entirely while
        // UNIT_STATE_EVADE is set (Creature.cpp:880), which would leave a
        // creature standing in the world with nothing left to remove it -- the
        // exact failure this whole file exists to prevent. Either the owner is
        // still there, in which case go back to them, or they are not, in which
        // case leave.
        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            if (_passive)
                return;

            if (Player* owner = Owner())
            {
                me->ClearUnitState(UNIT_STATE_EVADE);
                AttackStart(owner);
                return;
            }

            Despawn();
        }

        // The stalker is news the moment it falls, not when its corpse decays a
        // minute later, so death and removal are reported separately.
        void JustDied(Unit* /*killer*/) override
        {
            sGauntletSummons->NoteDied(me);
        }

        void UpdateAI(uint32 diff) override
        {
            _lifeMs += diff;

            // The backstop. A summon in a fight it cannot finish freezes its
            // own TempSummon timer (TemporarySummon.cpp:177-190), so without
            // this one clock nothing bounds its life.
            if (_lifeMs >= SUMMON_MAX_LIFE_MS)
            {
                Despawn();
                return;
            }

            if (_checkMs <= diff)
            {
                _checkMs = SUMMON_CHECK_MS;

                Player* owner = Owner();

                // This, and not any hook, is what guarantees nothing is left
                // behind. ObjectAccessor::GetPlayer resolves through the
                // creature's own map (ObjectAccessor.cpp:253), so it answers
                // null for a player who logged out, was deleted, changed map,
                // teleported or is simply no longer in the world -- every way
                // an owner can stop existing, in one test, with no hook to
                // forget to wire.
                if (!owner || !owner->IsInWorld() || !owner->IsAlive())
                {
                    Despawn();
                    return;
                }

                if (me->GetDistance(owner) > SUMMON_LEASH_YARDS)
                {
                    _outOfRangeMs += SUMMON_CHECK_MS;
                    if (_outOfRangeMs >= SUMMON_LEASH_MS)
                    {
                        Despawn();
                        return;
                    }
                }
                else
                {
                    _outOfRangeMs = 0;
                }

                if (!_passive && me->GetVictim() != owner)
                    AttackStart(owner);
            }
            else
            {
                _checkMs -= diff;
            }

            if (_passive)
                return;

            if (me->GetVictim())
                DoMeleeAttackIfReady();
        }

    private:
        Player* Owner() const
        {
            return _owner ? ObjectAccessor::GetPlayer(*me, _owner) : nullptr;
        }

        void Despawn()
        {
            me->CombatStop(true);
            me->DespawnOrUnsummon();
        }

        ObjectGuid _owner;
        uint32 _outOfRangeMs = 0;
        uint32 _lifeMs       = 0;
        uint32 _checkMs      = 0;
        bool   _passive      = false;
    };

    class gauntlet_summon : public CreatureScript
    {
    public:
        gauntlet_summon() : CreatureScript("gauntlet_summon") { }

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new gauntlet_summon_ai(creature);
        }
    };

    // Two jobs that need a hook running over every creature on the realm, both
    // gated on an integer test first so a realm with no Gauntlet player pays
    // almost nothing for them.
    class gauntlet_summon_world : public AllCreatureScript
    {
    public:
        gauntlet_summon_world() : AllCreatureScript("gauntlet_summon_world") { }

        // Level to the owner. Creature::SelectLevel picks a level from the
        // template, offers it here, and then derives health, mana, attack power
        // and weapon damage from whatever it ends up with
        // (Creature.cpp:1495-1556) -- which is why this is the right place and
        // a later SetLevel is not: Creature::UpdateAllStats recomputes max
        // health from the stat modifier SelectLevel already wrote
        // (StatSystem.cpp:1039-1074) and so cannot rescale anything by itself.
        void OnBeforeCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature, uint8& level) override
        {
            if (sGauntletSummons->Idle())
                return;

            uint8 wanted = level;
            if (sGauntletSummons->PendingLevel(creature, wanted))
                level = wanted;
        }

        // The only event that catches every way a summon can leave the world:
        // killed, despawned, unsummoned, or removed with the grid under it.
        // Creature::RemoveFromWorld fires it before anything is torn down
        // (Creature.cpp:336-341).
        void OnCreatureRemoveWorld(Creature* creature) override
        {
            if (sGauntletSummons->Idle())
                return;

            sGauntletSummons->NoteRemoved(creature);
        }
    };
}

// Constructed from Addmod_gauntletScripts(), the one name the core's generated
// module loader calls -- the same seam AddSC_gauntlet_commands() uses. Nothing
// else references this translation unit, and the modules library is a static
// archive ($CORE/modules/CMakeLists.txt:286), so without that call the linker
// is free to drop this file entirely and the templates' ScriptName would
// resolve to no AI at all.
void AddSC_gauntlet_summons()
{
    new gauntlet_summon();
    new gauntlet_summon_world();
}
