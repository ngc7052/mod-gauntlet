/*
 * mod-gauntlet - the dispatch surface every mechanic implements
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANIC_H
#define MOD_GAUNTLET_MECHANIC_H

#include "Gauntlet.h"
#include <string>

// Core game types. Every one of them appears here only as a pointer or a
// reference, so this header -- and therefore every mechanic that does not
// actually touch the world -- stays free of Player.h and compiles into the
// unit test build. The class keys are the core's own, checked against the
// definitions: Loot is a struct (LootMgr.h:312) and the rest are classes
// (Player.h:1085, Unit.h:664, Creature.h:46, SpellInfo.h:339, Spell.h:297,
// SpellAuras.h:86 and :36, ObjectGuid.h:122). ObjectGuid is taken by const
// reference rather than by value, as plan section 2.2 has it, because a
// function definition may not take an incomplete type by value.
class Player;
class Unit;
class Creature;
class SpellInfo;
class Spell;
class Aura;
class AuraApplication;
class ObjectGuid;
struct Loot;

namespace Gauntlet
{
    // Defined in GauntletMgr.h until step 4b moves it into Gauntlet.h.
    struct RunState;

    // Phase 1 brings these. They are named now so Ctx has its final shape and
    // the mechanics written against it do not need re-touching later; Phase 0
    // never dereferences a Scheduler or a State because it never sets one.
    class Scheduler;
    class Addon;
    class State;

    // Passed to every callback.
    //
    // Deviation from plan section 2.2, which declares run/self/clock/addon/state
    // as references: Scheduler, Addon and State do not exist in Phase 0, and a
    // reference cannot be left unbound. They are pointers here, per CONTRACT
    // section 7.4, and `clock` and `state` are always null until Phase 1. Every
    // mechanic must therefore check them before use.
    struct Ctx
    {
        Player*        player = nullptr;
        RunState*      run    = nullptr;
        AffixInstance* self   = nullptr;
        Scheduler*     clock  = nullptr;   // Phase 1; null in Phase 0
        Addon*         addon  = nullptr;   // set from Phase 0's GauntletAddon
        State*         state  = nullptr;   // Phase 1; null in Phase 0
    };

    // One affix's behaviour. Every callback has an empty default body so a
    // mechanic overrides only the two or three hooks it actually cares about;
    // GauntletScripts.cpp calls each one over the carried set and does nothing
    // else. Describe() is the exception and is pure virtual: it is what
    // `.gauntlet status`, the offer chat line and the addon's fallback print,
    // and an affix that cannot say what it does has no business existing.
    class IMechanic
    {
    public:
        virtual ~IMechanic() = default;

        virtual void  OnAttach(Ctx&) {}                       // pick or login
        virtual void  OnDetach(Ctx&) {}                       // swap, logout, death
        virtual void  OnTick(Ctx&, uint32 /*diffMs*/) {}      // 500 ms, Timed mechanics
        // The two halves of every timed event. Scheduler::Arm queues a Warn at
        // `inMs - warnMs` and a Fire at `inMs`; the framework calls OnWarn for
        // the first and OnEvent for the second, with the same eventId. A
        // mechanic that telegraphs implements both. Design section 4.8: if a
        // player cannot tell which affix acted, the addon needs a line -- and
        // the warning is that line.
        virtual void  OnWarn(Ctx&, uint32 /*eventId*/) {}     // scheduler telegraph
        virtual void  OnEvent(Ctx&, uint32 /*eventId*/) {}    // scheduler callback
        virtual void  OnKill(Ctx&, Creature*) {}
        virtual void  OnPetKill(Ctx&, Creature*) {}
        virtual void  OnEnterCombat(Ctx&, Unit* /*enemy*/, bool /*wasOutOfCombat*/) {}
        virtual void  OnLeaveCombat(Ctx&) {}

        // The three multiplier callbacks see the other Unit, so a mechanic that
        // needs the attacker (VersusElites, Overextended) answers here. A
        // mechanic whose whole effect is a coefficient answers AggregateFactor
        // below instead, and gets caps and condition gating for free.
        virtual float DamageTakenMult(Ctx&, Unit* /*attacker*/, SpellInfo const*) { return 1.f; }
        virtual float DamageDoneMult (Ctx&, Unit* /*victim*/,   SpellInfo const*) { return 1.f; }
        virtual float HealTakenMult  (Ctx&, Unit* /*healer*/,   SpellInfo const*) { return 1.f; }

        virtual void  OnDamageTaken(Ctx&, Unit* /*attacker*/, uint32 /*amount*/) {}  // observer, post-mult
        virtual uint32 OnLethal(Ctx&, uint32 damage) { return damage; }              // UnitScript::DealDamage
        virtual void  OnSpellCast(Ctx&, Spell*) {}
        virtual void  OnAuraApplied(Ctx&, Unit* /*target*/, Aura*) {}
        virtual void  OnAuraRemoved(Ctx&, Unit* /*target*/, AuraApplication*) {}
        virtual void  OnShapeshift(Ctx&, uint8 /*form*/) {}
        virtual void  OnLoot(Ctx&, ObjectGuid const& /*lootGuid*/, Loot*) {}
        virtual void  OnMaxHealth(Ctx&, float& /*value*/) {}
        virtual void  OnXP(Ctx&, uint32& /*amount*/, Unit* /*victim*/) {}
        virtual void  OnTalentPoints(Ctx&, uint32& /*points*/) {}

        virtual bool  IsRelevant(Player*) const { return true; }   // beyond the classMask

        // The Player-free half of the multiplier callbacks, and an addition to
        // plan section 2.2. Aggregate() runs over the carried set with no Player
        // and no Unit -- that is the whole point of AggregateInput -- so it
        // cannot call the three Mult hooks above. A mechanic that is nothing but
        // a coefficient on one AggregateKind reports it here; the caller has
        // already checked the instance's condition, and clamps the product.
        virtual float AggregateFactor(AffixInstance const& /*self*/, AggregateKind /*kind*/) const { return 1.f; }

        virtual std::string Describe(AffixInstance const& self) const = 0;
    };

    // The registry table is pure data and carries no factory pointer, so the
    // map from registry id to implementation lives here (CONTRACT section 7.1).
    //
    // The caller owns the returned pointer and must delete it: in the running
    // module that owner is the RunState holding the AffixInstance whose `impl`
    // it becomes, which creates it on attach and destroys it on detach.
    //
    // Returns nullptr for every id Phase 0 does not implement, which is a
    // normal answer rather than an error -- a run migrated from a newer
    // registry, or a family switched off in config, legitimately carries one.
    // Nothing may crash on it; Aggregate() ignores such an instance entirely.
    // Registry id to implementation. Returns nullptr for an id this build has
    // no code for, which is a normal state -- a run migrated from a newer
    // registry, or a mechanic whose phase has not landed yet -- and every
    // caller must tolerate it.
    IMechanic* MakeMechanic(uint16 id);

    using MechanicFactoryFn = IMechanic* (*)();

    // A mechanic registers itself from its own translation unit, so adding one
    // never means editing a file someone else is editing. Declare one at file
    // scope in the .cpp:
    //
    //     GAUNTLET_MECHANIC(6, Champions);
    //
    // where Champions is default-constructible and derives from IMechanic.
    struct MechanicRegistrar
    {
        MechanicRegistrar(uint16 id, MechanicFactoryFn fn);
    };

// Each macro also defines an empty global anchor, and this is not decoration.
// The module's objects are archived into libmodules.a and linked plainly
// (modules/CMakeLists.txt:286, src/server/apps/CMakeLists.txt:137), and a
// static-archive member is pulled in only to resolve an undefined symbol. A
// translation unit that nothing references is therefore dropped, its
// .init_array never runs, and the registrar below never executes -- so
// MakeMechanic returns nullptr for a mechanic whose code is right there in the
// tree. Because nullptr is a legitimate answer, the affix would simply be
// offered and do nothing, with no error anywhere.
//
// The anchor is defined in whatever namespace the macro is invoked in, which
// is `namespace Gauntlet` for every mechanic in the tree, so that is where
// GauntletScripts.cpp declares them.
//
// GauntletScripts.cpp calls every anchor from AnchorMechanics(). That is one
// line per mechanic in a shared file, which is exactly what self-registration
// was meant to avoid, but the alternative is forcing whole-archive on the
// `modules` target, and that is a change to the core rather than to this
// module. Measured both ways: plain archive registers 0 of 4, --whole-archive
// registers 4 of 4.
#define GAUNTLET_MECHANIC(id, Type)                                                \
    namespace                                                                      \
    {                                                                              \
        ::Gauntlet::IMechanic* Make##Type() { return new Type(); }                  \
        ::Gauntlet::MechanicRegistrar const g_register##Type{ (id), &Make##Type };  \
    }                                                                              \
    void AddSC_gauntlet_mechanic_##Type() {}

// The same, for a file that already has its own factory function.
#define GAUNTLET_MECHANIC_FN(id, fn)                                               \
    namespace                                                                      \
    {                                                                              \
        ::Gauntlet::MechanicRegistrar const g_register_##fn{ (id), &fn };           \
    }                                                                              \
    void AddSC_gauntlet_mechanic_##fn() {}
}

#endif // MOD_GAUNTLET_MECHANIC_H
