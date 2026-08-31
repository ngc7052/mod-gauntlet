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
struct ItemTemplate;

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

    // What a Rules affix may refuse. Named actions rather than a bitmask
    // because the mechanic answers one question at a time and the caller
    // always knows which one it is asking.
    enum class Restricted : uint8 { Trade, Mail, AuctionBid };

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

        // A heal, after the aggregate has multiplied it and after the cap, so
        // a mechanic can put an absolute limit on where the player ends up.
        //
        // HealTakenMult cannot express one. It returns a ratio and never sees
        // the heal, so "you cannot be healed above half" comes out as either
        // "all of it lands" or "none of it does" -- and at 49% health the first
        // of those takes the player to full, straight past the line the affix
        // draws. Last Rites' Mark is the whole reason this exists.
        //
        // Post-cap on purpose: plan section 2.5's heal floor exists to stop
        // curses stacking into "you cannot heal at all", and an absolute
        // ceiling is not a curse stacking. Lower `heal`; never raise it.
        virtual void  OnHeal(Ctx&, uint32& /*heal*/) {}

        // Damage the player's pet, guardian or totem is about to deal, by
        // reference. Lower it or raise it.
        //
        // The aggregate cannot express this: AggregateKind::DamageDone is the
        // *player's* damage, and a boon that reads "your pet hits harder" would
        // otherwise have nowhere to land. Boon::BonusPetDamage has existed
        // since Phase 0 with nothing able to pay it.
        //
        // Dispatched from the same two Modify hooks the aggregate uses, when
        // the attacker resolves to this player through
        // GetCharmerOrOwnerPlayerOrPlayerItself. It is deliberately outside the
        // aggregate's clamp: the caps bound what the world does to the player
        // and what the player does to the world, and a hunter's pet is neither.
        virtual void  OnPetDamage(Ctx&, Unit* /*victim*/, uint32& /*damage*/) {}

        // The mirror: damage the player's pet is about to *take*, by reference.
        //
        // Blood Bond is the card that needs it -- a share of what the pet takes
        // is dealt to the hunter as well -- and it is a separate callback from
        // OnPetDamage rather than a flag on it, because a mechanic almost never
        // wants both and conflating them would make every implementation start
        // with the same branch.
        virtual void  OnPetDamaged(Ctx&, Unit* /*attacker*/, uint32& /*damage*/) {}

        // One tick of a periodic damage aura this player applied, by reference.
        // Poisoned Blades reads the rogue's own poisons off it.
        virtual void  OnPeriodicTick(Ctx&, Unit* /*victim*/, uint32& /*damage*/,
                                     SpellInfo const*) {}
        virtual uint32 OnLethal(Ctx&, uint32 damage) { return damage; }              // UnitScript::DealDamage

        // The two halves of a bargain that buys back a death, and the seam
        // Phase 0 left open with a comment naming Phase 3 and Phase 4.
        //
        // WillBuyDeath answers "if this player is resurrected right now, will I
        // pay for it" and must not change anything: Mgr asks it from the
        // resurrection *veto*, which the core consults before it has committed
        // to anything, and a mechanic that spent its charge there would spend
        // it on a resurrection that never happened.
        //
        // OnResurrect is where the price is actually paid, from the hook the
        // core fires once the player is back on their feet. The mechanic that
        // pays is responsible for calling Mgr::CancelPendingDeath -- which is
        // what makes the run survive -- and for saying so to the player.
        virtual bool  WillBuyDeath(Ctx&) const { return false; }
        virtual void  OnResurrect(Ctx&) {}
        virtual void  OnSpellCast(Ctx&, Spell*) {}
        virtual void  OnAuraApplied(Ctx&, Unit* /*target*/, Aura*) {}
        virtual void  OnAuraRemoved(Ctx&, Unit* /*target*/, AuraApplication*) {}
        virtual void  OnShapeshift(Ctx&, uint8 /*form*/) {}

        // The loot window opening, from PlayerScript::OnPlayerBeforeSendLoot
        // (Player.cpp:8369). `lootGuid` is real here and may be a creature, a
        // game object or a corpse, so a mechanic that counts corpses branches
        // on lootGuid.IsCreature() -- which is what Carrion does.
        virtual void  OnLoot(Ctx&, ObjectGuid const& /*lootGuid*/, Loot*) {}

        // Money about to be added, from PlayerScript::OnPlayerBeforeLootMoney
        // (PlayerScript.h:290), which carries no guid at all -- the source has
        // to be read off Loot::sourceWorldObjectGUID, which Creature::
        // AddToWorld stamps on (Creature.cpp:331).
        //
        // A separate callback and not the one above, because the two fire for
        // the same corpse and a mechanic that multiplies a purse in both would
        // double it twice. Champions' guaranteed extra coin roll and every
        // BonusMoney boon are paid here and nowhere else.
        virtual void  OnLootMoney(Ctx&, Loot*) {}

        // One item's drop chance, from GlobalScript::OnItemRoll
        // (LootMgr.cpp:315 and :1276), which passes the chance by reference and
        // is the only hook in the core that can move a drop rate for one
        // player. Carrion's card promises "+25% item drop chance on creature
        // loot" and this is where that is paid; Phase 3's Cursed Hoard and
        // Self-Found want the same seam.
        virtual void  OnItemRoll(Ctx&, float& /*chance*/) {}

        // How many items the core is about to roll out of one loot group.
        // GlobalScript::OnAfterCalculateLootGroupAmount is where a chest's
        // contents are actually decided, which is where "chests hold twice the
        // loot" has to be written.
        virtual void  OnLootGroupAmount(Ctx&, uint32& /*groupAmount*/) {}

        // A blow this character (or their pet) landed on a creature, from
        // UnitScript::OnDamage (Unit.cpp:999), which runs *before* the health
        // is applied -- so `victim->GetHealth() - damage` is the health the
        // creature is about to have. Craven's whole mechanic is that
        // subtraction crossing a threshold, and there is no other hook in the
        // core that can see it happen.
        //
        // An observer: the damage is not passed by reference, because nothing
        // in this phase changes it and a mechanic that wants to belongs in the
        // three Mult callbacks above where the caps apply.
        virtual void  OnCreatureDamaged(Ctx&, Creature* /*victim*/, uint32 /*damage*/) {}
        virtual void  OnMaxHealth(Ctx&, float& /*value*/) {}
        virtual void  OnXP(Ctx&, uint32& /*amount*/, Unit* /*victim*/) {}
        virtual void  OnTalentPoints(Ctx&, uint32& /*points*/) {}

        // The repair bill's discount modifier, straight off
        // OnPlayerBeforeDurabilityRepair. Multiply to make it dearer.
        virtual void  OnRepair(Ctx&, float& /*discountMod*/) {}

        // The economy vetoes, as one callback rather than three near-identical
        // ones. Self-found is the only mechanic that refuses anything, and it
        // refuses three things with the same sentence and a different noun; a
        // virtual apiece would have been three copies of one function.
        //
        // Returning false stops the action. The mechanic that refuses owes the
        // player a line saying which affix did it and why -- a veto with no
        // explanation is indistinguishable from a bug, and the player has no
        // other way to find out, because the core's own refusal for these is
        // silent or generic.
        virtual bool  Allows(Ctx&, Restricted) { return true; }

        // The equipment veto: may this be equipped while the card is carried?
        // Asked from PlayerScript::OnPlayerCanEquipItem, which the core
        // consults first thing in Player::CanEquipItem (PlayerStorage.cpp:1912)
        // and turns a false into EQUIP_ERR_CANT_DO_RIGHT_NOW -- the client's
        // generic "You can't do that right now", which names nothing. So the
        // mechanic that refuses owes the line, as with Allows() above.
        //
        // The template rather than the Item, because the bench has no Item to
        // offer: it walks the world's templates and asks each one, which is
        // how a denial is reached with nothing written per card.
        virtual bool  CanEquip(Ctx&, ItemTemplate const*) { return true; }

        virtual bool  IsRelevant(Player*) const { return true; }   // beyond the classMask

        // The Player-free half of the multiplier callbacks, and an addition to
        // plan section 2.2. Aggregate() runs over the carried set with no Player
        // and no Unit -- that is the whole point of AggregateInput -- so it
        // cannot call the three Mult hooks above. A mechanic that is nothing but
        // a coefficient on one AggregateKind reports it here; the caller has
        // already checked the instance's condition, and clamps the product.
        virtual float AggregateFactor(AffixInstance const& /*self*/, AggregateKind /*kind*/) const { return 1.f; }

        // The one way a mechanic may move a cap, and the reason it exists is
        // that two Phase 3 rows promise a number the clamp would quietly eat.
        // Cursed Hoard's curse is a triple, and Gauntlet.Caps.DamageTaken is
        // 2.0; Lone Wolf halves your health while you are in a group, and
        // Gauntlet.Caps.MaxHealth floors at 0.6. Delivering -40% behind a blurb
        // that says half is the same lie as an unfelt scalar, which is what
        // this whole redesign exists to remove.
        //
        // What this is NOT: a bypass. The relaxation moves the ceiling and the
        // product is still clamped exactly once, so a bargain curse and three
        // ordinary affixes reach the new bound together rather than each being
        // paid out on top of it. Nothing is ever applied after the clamp.
        //
        // Called once per carried affix per query, after the same condition
        // gate the factors get, with `caps` starting at the configured values.
        // A mechanic may only widen: narrowing here would let one affix trim
        // another's contribution, which is precisely what clamping the product
        // rather than the contribution was chosen to avoid. It must also be
        // *state-dependent* -- Cursed Hoard relaxes only while the curse is up,
        // Lone Wolf only while the player is grouped -- so the ceiling returns
        // on its own the moment the mechanic stops needing it.
        //
        // AggregateTest.OnlyThePhaseThreeBargainsMoveACap is what keeps this
        // from becoming the place everything goes to escape its budget.
        virtual void RelaxCaps(AffixInstance const& /*self*/, AggregateKind /*kind*/,
                               AggregateCaps& /*caps*/) const {}

        virtual std::string Describe(AffixInstance const& self) const = 0;

        // One line of internals for `.gauntlet debug dump`, and empty by
        // default because most mechanics have nothing worth saying.
        //
        // It exists because of Deep Wounds. The mechanic was reported not to
        // work, the dispatch chain read correct end to end, and the failing
        // link was never identified -- which is a diagnosis problem, not a
        // wound problem: an affix that runs entirely out of sight can only be
        // debugged by reading it. A mechanic that keeps state a player cannot
        // see should say here what it is holding and which of its callbacks
        // have actually been reached, so "it does not work" becomes one
        // command instead of a build.
        virtual std::string Diagnose(Ctx&) const { return {}; }
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
