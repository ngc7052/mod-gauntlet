/*
 * mod-gauntlet - drive one card through every hook and see which ones it answers
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletBench.h"

#include "GauntletAudit.h"
#include "GauntletMechanic.h"
#include "GauntletMgr.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"

#include "Creature.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Gauntlet
{
    namespace
    {
        // How long a tick probe drives the card for, and how many of its own
        // events the bench will release. Same numbers `soak` uses.
        constexpr uint32 BENCH_TICKS = 40;
        constexpr uint32 BENCH_FIRES = 3;

        // The target every hook that needs one is given.
        //
        // One of the module's own entries rather than something from the world
        // database, so the bench cannot fail on a realm whose world data is a
        // different vintage: `data/sql/db-world/base/gauntlet_creatures.sql`
        // ships it.
        //
        // The Ambusher specifically, and not the Restless. The Restless is the
        // module's visual-only creature, and a visual-only creature will not
        // enter combat -- SetInCombatWith is refused for something the combat
        // manager considers no threat. Every combat-gated card in the registry
        // therefore stayed quiet, and Reinforcements' own counters said so in
        // as many words: "out of combat", on a probe that had just called
        // SetInCombatWith both ways. The Ambusher is the one the Spawn family
        // uses to actually attack a player, so it is hostile and attackable.
        constexpr uint32 BENCH_TARGET_ENTRY = ENTRY_AMBUSHER;
        constexpr uint32 BENCH_TARGET_LIFE_MS = 30000;

        bool Moved(float a, float b) { return std::fabs(a - b) > 0.0001f; }

        // What the bench can see about the *target*.
        //
        // Footprint watches the player, and a whole family of cards does not
        // touch the player at all: Champions promotes a creature, Craven makes
        // one run, Nimble hurries one, Grudge answers when one dies. Every one
        // of them read "reached by nothing" until the bench started looking at
        // the other side of the fight.
        struct TargetMark
        {
            uint32 health    = 0;
            uint32 maxHealth = 0;
            uint32 level     = 0;
            uint32 auras     = 0;
            uint32 faction   = 0;
            float  speed     = 0.f;
            bool   alive     = false;
        };

        TargetMark MarkOf(Unit* target)
        {
            TargetMark m;
            if (!target)
                return m;

            m.health    = target->GetHealth();
            m.maxHealth = target->GetMaxHealth();
            m.level     = target->GetLevel();
            m.auras     = static_cast<uint32>(target->GetAppliedAuras().size());
            m.faction   = target->GetFaction();
            m.speed     = target->GetSpeedRate(MOVE_RUN);
            m.alive     = target->IsAlive();
            return m;
        }

        bool Differs(TargetMark const& a, TargetMark const& b)
        {
            return a.health != b.health || a.maxHealth != b.maxHealth || a.level != b.level
                || a.auras != b.auras || a.faction != b.faction || a.alive != b.alive
                || std::fabs(a.speed - b.speed) > 0.0001f;
        }

        // A value hook answered if it changed the number it was handed. This is
        // the bench's whole trick for the by-reference half of IMechanic: no
        // instrumentation, no per-card knowledge, and it cannot be fooled by a
        // card that does its work somewhere the footprint does not look.
        void NoteIf(ProbeResult& out, bool changed, char const* what)
        {
            if (changed)
                out.reached.emplace_back(what);
        }
    }

    BenchSetup BenchQuiet(Player* player, RunState* run)
    {
        BenchSetup saved;
        if (!player || !run)
            return saved;

        saved.offers  = run->pending;
        saved.graceMs = run->graceMs;

        run->pending.clear();
        run->graceMs = 0;

        if (player->IsMounted())
            player->Dismount();

        return saved;
    }

    void BenchRestore(Player* player, RunState* run, BenchSetup const& saved)
    {
        if (!player || !run)
            return;

        // The offers go back. They are the player's actual choice and are not
        // the bench's to discard.
        run->pending  = saved.offers;
        run->graceMs  = saved.graceMs;
    }

    ProbeResult Probe(Player* player, RunState* run, uint8 slot, uint16 mechanic)
    {
        ProbeResult out;
        if (!player || !run)
            return out;

        // ------------------------------------------------------------------
        // The value probes. Every one of these hands a hook a copy and asks
        // whether the card changed it. None of them touch the world, so they
        // run first and in any order.
        // ------------------------------------------------------------------

        {
            uint32 xp = 1000;
            sGauntlet->OnGiveXP(player, xp, nullptr);
            NoteIf(out, xp != 1000, "experience (quest)");
        }

        {
            float maxHealth = 10000.f;
            sGauntlet->OnMaxHealth(player, maxHealth);
            NoteIf(out, Moved(maxHealth, 10000.f), "max health");
        }

        {
            uint32 heal = 1000;
            sGauntlet->OnHeal(player, heal);
            NoteIf(out, heal != 1000, "healing");
        }

        {
            // OnLethal is only ever asked when a blow would kill, so asking it
            // here is asking the cheat-death path directly. It returns the
            // damage that should land; anything lower is a card intervening.
            uint32 const lethal = sGauntlet->OnLethal(player, 1000);
            NoteIf(out, lethal != 1000, "lethal damage");
        }

        {
            float chance = 10.f;
            sGauntlet->OnItemRoll(player, chance);
            NoteIf(out, Moved(chance, 10.f), "item drop chance");
        }

        {
            uint32 group = 2;
            sGauntlet->OnLootGroupAmount(player, group);
            NoteIf(out, group != 2, "loot group size");
        }

        {
            float discount = 1.f;
            sGauntlet->OnRepair(player, discount);
            NoteIf(out, Moved(discount, 1.f), "repair bill");
        }

        {
            uint32 points = 1;
            sGauntlet->OnTalentPoints(player, points);
            NoteIf(out, points != 1, "talent points");
        }

        NoteIf(out, !sGauntlet->Allows(player, Restricted::Trade),      "trade refused");
        NoteIf(out, !sGauntlet->Allows(player, Restricted::Mail),       "mail refused");
        NoteIf(out, !sGauntlet->Allows(player, Restricted::AuctionBid), "auction refused");

        NoteIf(out, sGauntlet->AnyWillBuyDeath(player), "will buy death");

        // The six aggregate products. A card whose entire effect is a
        // coefficient answers here and nowhere else, which is most of the
        // Attrition family.
        for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
        {
            AggregateKind const kind = static_cast<AggregateKind>(k);
            if (Moved(sGauntlet->Aggregate(player, kind), 1.0f))
                out.reached.emplace_back("aggregate: " + AggregateKindName(kind));
        }

        // ------------------------------------------------------------------
        // The world probes. These need a target, and they change things.
        // ------------------------------------------------------------------

        Footprint mark = Capture(player, run);

        // Compares the footprint against the last mark and, if anything moved,
        // records the probe's name and re-marks. Effects rather than
        // invocations: a card that acts is a card that changed something, and
        // this needs to know nothing about which card it is.
        auto noteFootprint = [&](char const* what)
        {
            Footprint const now = Capture(player, run);
            if (!Diff(mark, now).empty())
                out.reached.emplace_back(what);
            mark = now;
        };

        TempSummon* target = player->SummonCreature(BENCH_TARGET_ENTRY, player->GetPosition(),
                                                    TEMPSUMMON_TIMED_DESPAWN, BENCH_TARGET_LIFE_MS);

        TargetMark targetMark = MarkOf(target);

        // Both sides of the fight. A card answers if it moved the player or the
        // thing the player is fighting, and the bench does not need to know
        // which kind of card it is holding to ask.
        auto noteBoth = [&](char const* what)
        {
            Footprint const now = Capture(player, run);
            TargetMark const tm = MarkOf(target);

            if (!Diff(mark, now).empty() || Differs(targetMark, tm))
                out.reached.emplace_back(what);

            mark = now;
            targetMark = tm;
        };

        if (target)
        {
            // Real combat, not just the hook.
            //
            // Calling Mgr::OnEnterCombat is not enough on its own: plenty of
            // cards re-check `player->IsInCombat()` inside the callback -- it is
            // Falter's one rule -- and answer nothing when the world disagrees
            // with the hook that just fired. The first bench run reached twenty
            // of twenty-eight cards with nothing for exactly this reason.
            //
            // Mutual, because IsInCombat reads the player's own combat state and
            // setting only the target's leaves the player out of it.
            player->SetInCombatWith(target);
            target->SetInCombatWith(player);

            sGauntlet->OnEnterCombat(player, target);
            noteBoth("entering combat");

            // Wounded, and wounded *before* everything that follows.
            //
            // This block used to sit at the end, just before leaving combat,
            // and that cost real coverage: Killing Floor heals a share of
            // maximum health on a kill, which is worth exactly nothing on a
            // character already at full, so the card answered on a bot that
            // happened to be hurt and went silent on one that was not. A bench
            // whose result depends on what the character was doing a minute ago
            // is not a bench.
            //
            // Fifteen percent is under every threshold the registry gates on
            // and comfortably clear of zero. Capture does not record current
            // health, so moving it cannot be mistaken for a leak; it is put
            // back once the fight is over.
            uint32 const healthBefore = player->GetHealth();
            uint32 const wounded = std::max<uint32>(1, player->GetMaxHealth() * 15 / 100);

            float healthy[static_cast<std::size_t>(AggregateKind::MAX)];
            for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
                healthy[k] = sGauntlet->AggregateAt(player, static_cast<AggregateKind>(k),
                                                    target, nullptr);

            if (healthBefore > wounded)
                player->SetHealth(wounded);

            // Condition::BelowHalfHealth and its neighbours gate a factor on the
            // health line, so the same product read at 15% is a different number
            // -- and that difference is the card answering.
            for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            {
                AggregateKind const kind = static_cast<AggregateKind>(k);
                if (Moved(sGauntlet->AggregateAt(player, kind, target, nullptr), healthy[k]))
                    out.reached.emplace_back("aggregate while wounded: " + AggregateKindName(kind));
            }

            {
                uint32 heal = 1000;
                sGauntlet->OnHeal(player, heal);
                NoteIf(out, heal != 1000, "healing while wounded");
            }
            {
                uint32 const lethal = sGauntlet->OnLethal(player, wounded * 4);
                NoteIf(out, lethal != wounded * 4, "a killing blow while wounded");
            }
            NoteIf(out, sGauntlet->AnyWillBuyDeath(player), "will buy death while wounded");

            sGauntlet->OnDamageTaken(player, target, wounded / 2);
            noteBoth("taking damage while wounded");


            sGauntlet->OnDamageTaken(player, target, 100);
            noteBoth("taking damage");

            {
                uint32 dmg = 100;
                sGauntlet->OnPetDamage(player, target, dmg);
                NoteIf(out, dmg != 100, "pet damage dealt");
            }
            {
                uint32 dmg = 100;
                sGauntlet->OnPetDamaged(player, target, dmg);
                NoteIf(out, dmg != 100, "pet damage taken");
            }
            {
                uint32 dmg = 100;
                sGauntlet->OnPeriodicTick(player, target, dmg, nullptr);
                NoteIf(out, dmg != 100, "periodic damage");
            }

            // The two multiplier hooks that see the other unit. Asked through
            // AggregateAt rather than the aggregate above, because a card like
            // VersusElites answers only when it can see who it is fighting.
            for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            {
                AggregateKind const kind = static_cast<AggregateKind>(k);
                float const at   = sGauntlet->AggregateAt(player, kind, target, nullptr);
                float const flat = sGauntlet->Aggregate(player, kind);
                if (Moved(at, flat))
                    out.reached.emplace_back("versus a target: " + AggregateKindName(kind));
            }

            {
                uint32 xp = 1000;
                sGauntlet->OnGiveXP(player, xp, target);
                NoteIf(out, xp != 1000, "experience (kill)");
            }

            sGauntlet->OnCreatureDamaged(player, target->ToCreature(), 100);
            noteBoth("damaging a creature");

            // The clock, *while still in combat*.
            //
            // This used to run after OnLeaveCombat, and that one line of
            // ordering was worth eleven cards. Every Spawn and Tempo card arms
            // on entering combat and disarms on leaving it, so ticking them
            // once the fight was over released nothing at all and the whole
            // family reported "reached by nothing".
            for (uint32 i = 0; i < BENCH_TICKS; ++i)
            {
                if (run->dead || !player->IsAlive())
                    break;
                sGauntlet->Tick(player, Scheduler::TICK_MS);
            }
            noteBoth("ticking in combat");

            for (uint32 i = 0; i < BENCH_FIRES; ++i)
            {
                if (run->dead || !player->IsAlive())
                    break;
                if (!sGauntlet->FireNow(player, mechanic))
                    break;
                ++out.eventsFired;
            }
            if (out.eventsFired != 0)
                out.reached.emplace_back("its own timer");
            noteBoth("firing its own event");

            sGauntlet->OnCreatureKill(player, target->ToCreature(), /*byPet*/ false);
            noteBoth("killing");

            sGauntlet->OnCreatureKill(player, target->ToCreature(), /*byPet*/ true);
            noteBoth("a pet killing");

            sGauntlet->OnLeaveCombat(player);
            noteBoth("leaving combat");

            // Put the character back before anything else reads it.
            if (player->IsAlive() && player->GetHealth() < healthBefore)
                player->SetHealth(healthBefore);

            player->CombatStop();
        }

        sGauntlet->OnZoneChanged(player);
        noteFootprint("changing zone");

        sGauntlet->OnGroupChanged(player);
        noteFootprint("group changing");

        // A second pass out of combat, for the cards that act between fights
        // rather than during one.
        for (uint32 i = 0; i < BENCH_TICKS; ++i)
        {
            if (run->dead || !player->IsAlive())
                break;
            sGauntlet->Tick(player, Scheduler::TICK_MS);
        }
        noteFootprint("ticking out of combat");

        for (uint32 i = 0; i < BENCH_FIRES; ++i)
        {
            if (run->dead || !player->IsAlive())
                break;
            if (!sGauntlet->FireNow(player, mechanic))
                break;
            ++out.eventsFired;
            out.reached.emplace_back("its own timer, out of combat");
        }

        // The card's own counters, read while it is still carried.
        if (AffixInstance* live = run->AtSlot(slot))
            if (live->impl)
            {
                Ctx ctx = sGauntlet->MakeCtx(player, run, live);
                out.diagnose = live->impl->Diagnose(ctx);
            }

        // The bench's own litter. The target is despawned rather than left to
        // time out, because the next card is probed immediately and a stale one
        // standing there would be a second creature in every footprint after
        // this one.
        if (target && target->IsInWorld())
            target->DespawnOrUnsummon();

        return out;
    }
}
