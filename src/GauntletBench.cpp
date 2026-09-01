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
#include "LootMgr.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
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

        // A corpse with a real loot table. Every creature this module owns
        // carries lootid 0, so a card that rolls a table again has nothing to
        // roll on the bench's own target; Wiry Swoop (2969) is a level-5 world
        // creature whose table has two entries at Chance 100, which makes the
        // first fill deterministic and a second roll visible as the item count
        // doubling rather than as a coin flip. If the world database does not
        // have it, the probe skips rather than pretends.
        constexpr uint32 BENCH_LOOT_ENTRY = 2969;

        // Something the world calls special. Thuros Lightfingers (61) is a
        // level-11 silver dragon -- CreatureTemplate::rank 4,
        // CREATURE_ELITE_RARE -- and killing one is a seam of its own: Trophy
        // Hunter (112) pays only for a rare, and the greed redesign's elite
        // cards will want the same probe. Nothing else the bench summons is
        // anything but rank 0, so without this those cards read as doing
        // nothing while working.
        constexpr uint32 BENCH_RARE_ENTRY = 61;

        bool Moved(float a, float b) { return std::fabs(a - b) > 0.0001f; }

        std::vector<uint32> CooldownIds(Player* player)
        {
            std::vector<uint32> ids;
            for (auto const& cd : player->GetSpellCooldownMap())
                ids.push_back(cd.first);
            std::sort(ids.begin(), ids.end());
            return ids;
        }

        std::vector<uint32> AuraIds(Player* player)
        {
            std::vector<uint32> ids;
            for (auto const& applied : player->GetAppliedAuras())
                ids.push_back(applied.first);
            std::sort(ids.begin(), ids.end());
            return ids;
        }

        // Every id `b` has more copies of than `a` does. Same multiset walk
        // GauntletAudit uses, and for the same reason: a second stack of an
        // aura the player already had is a change.
        std::vector<uint32> Added(std::vector<uint32> const& a, std::vector<uint32> const& b)
        {
            std::vector<uint32> out;
            std::size_t i = 0, j = 0;
            while (j < b.size())
            {
                if (i >= a.size() || b[j] < a[i]) { out.push_back(b[j]); ++j; }
                else if (a[i] < b[j])             { ++i; }
                else                              { ++i; ++j; }
            }
            return out;
        }

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

    ProbeResult Probe(Player* player, RunState* run, uint8 slot, uint16 mechanic,
                      uint32 requiresSpell)
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

        // The equipment veto. Nothing per card, as with everything else here:
        // every item template the world knows is offered to the carried set,
        // and the first one refused is the answer. A card that denies a slot
        // or a weapon class is reached by this and by nothing else, because a
        // denial changes no number until someone tries to put the thing on --
        // and asserting the refusal is asserting the thing the card is for.
        //
        // Forty thousand templates and one virtual call each, once per card;
        // the whole scan is milliseconds and the container is the core's own.
        // The item-*use* veto, which is a different hook and a different set of
        // cards: Blood for Bread (89) refuses food, Waste Not (90) refuses
        // potions, and neither changes a number until someone reaches for one.
        // Without this scan both read as cards that do nothing, which is the
        // exact failure this file was rewritten to stop -- a probe that cannot
        // see the thing the card is for.
        if (ItemTemplateContainer const* store = sObjectMgr->GetItemTemplateStore())
            for (auto const& [entry, proto] : *store)
                if (!sGauntlet->CanUseItem(player, &proto))
                {
                    out.reached.emplace_back("using " + proto.Name1 + " refused");
                    break;
                }

        if (ItemTemplateContainer const* store = sObjectMgr->GetItemTemplateStore())
            for (auto const& [entry, proto] : *store)
                if (!sGauntlet->CanEquip(player, &proto))
                {
                    out.reached.emplace_back("equipping " + proto.Name1 + " refused");
                    break;
                }

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
            out.maxSummons = std::max(out.maxSummons, now.summons);
            if (!Diff(mark, now).empty())
                out.reached.emplace_back(what);
            mark = now;
        };

        TempSummon* target = player->SummonCreature(BENCH_TARGET_ENTRY, player->GetPosition(),
                                                    TEMPSUMMON_TIMED_DESPAWN, BENCH_TARGET_LIFE_MS);

        // The bench's own cast, and what it displaced, so both can be undone.
        std::vector<uint32> castAdded, castRemoved;

        TargetMark targetMark = MarkOf(target);

        // Both sides of the fight. A card answers if it moved the player or the
        // thing the player is fighting, and the bench does not need to know
        // which kind of card it is holding to ask.
        auto noteBoth = [&](char const* what)
        {
            Footprint const now = Capture(player, run);
            TargetMark const tm = MarkOf(target);

            out.maxSummons = std::max(out.maxSummons, now.summons);

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

            // And a real attack link in both directions, which is a different
            // thing from combat state.
            //
            // GetVictim() is m_attacking and getAttackers() is m_attackers, and
            // only Unit::Attack populates either. SetInCombatWith alone leaves
            // both empty, so a card that asks "what am I fighting" saw nothing
            // -- which is exactly the hole Reinforcements fell through in play
            // and the bench reproduced without noticing.
            player->Attack(target, true);
            target->Attack(player, true);

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

            // Real damage, through the core's own pipeline, rather than only
            // calling the hook.
            //
            // Craven watches for a creature crossing a health threshold and a
            // synthetic OnCreatureDamaged never moves the creature's health, so
            // the threshold was never crossed and the card could not answer.
            // The same is true of anything that reads the victim's state rather
            // than the number it was handed.
            sGauntlet->OnCreatureDamaged(player, target->ToCreature(), 100);
            noteBoth("damaging a creature");

            if (target->IsAlive())
            {
                uint32 const bite = std::max<uint32>(1, target->GetMaxHealth() / 3);
                Unit::DealDamage(player, target, bite);
                noteBoth("really damaging a creature");
            }

            // The card's own spell gate, cast for real at the target. The
            // registry row declares it, so this reaches a cast-driven card
            // without the bench knowing which card it is holding.
            if (requiresSpell != 0 && player->HasSpell(requiresSpell))
            {
                // Exactly what the cast changes, so exactly that can be undone.
                //
                // Undoing only the aura of the spell itself is not enough: a
                // cast applies more than its own name. Casting Vanish stealths
                // the rogue, casting a paladin's bubble leaves Forbearance, and
                // casting a stance displaces the one before it -- and every one
                // of those came back as a leak against the card, which had done
                // none of it. Iron Discipline, Long Forbearance and Cold Trail
                // were all the bench blaming a card for the bench's own cast.
                //
                // Reading immediately either side of the cast is what makes the
                // delta *the cast's* rather than the card's, so undoing it
                // cannot quietly erase a leak the card is responsible for.
                std::vector<uint32> pre    = AuraIds(player);
                std::vector<uint32> preCds = CooldownIds(player);

                player->CastSpell(target, requiresSpell, true);

                std::vector<uint32> post    = AuraIds(player);
                std::vector<uint32> postCds = CooldownIds(player);

                noteBoth("casting the spell it names");

                castAdded   = Added(pre, post);
                castRemoved = Added(post, pre);

                // A cast starts the spell's own cooldown, and that is the
                // bench's doing too. Dead Weight came back as leaking a Feign
                // Death cooldown that the bench had started by casting Feign
                // Death at it.
                out.castCooldowns = Added(preCds, postCds);
            }

            // Full power, for the cards gated on a resource rather than a
            // spell -- Red Mist waits for a hundred rage. Generic: it is
            // whatever this class's primary power happens to be.
            {
                Powers const type = player->getPowerType();
                uint32 const parked = player->GetPower(type);
                player->SetPower(type, player->GetMaxPower(type));
                noteBoth("at full power");

                for (uint32 i = 0; i < 4; ++i)
                    sGauntlet->Tick(player, Scheduler::TICK_MS);
                noteBoth("ticking at full power");

                player->SetPower(type, parked);
            }

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

            // A card that pays on a kill pays into the health and power bars,
            // and neither is in the footprint -- Capture reads *max* health,
            // because a current-health line would report every fight as a
            // change. So they are read directly, here, around the kill.
            //
            // The character is standing at 15% of their health by this point
            // (the wound above), which is what makes the reading possible at
            // all: at full health a restore is a no-op and the probe would
            // report Blood for Bread doing nothing while it worked.
            uint32 const healthAtKill = player->GetHealth();
            uint32 const powerAtKill  = player->GetPower(player->getPowerType());

            sGauntlet->OnCreatureKill(player, target->ToCreature(), /*byPet*/ false);
            noteBoth("killing");

            sGauntlet->OnCreatureKill(player, target->ToCreature(), /*byPet*/ true);
            noteBoth("a pet killing");

            if (player->GetHealth() > healthAtKill)
                out.reached.emplace_back("a kill restored health");
            if (player->GetPower(player->getPowerType()) > powerAtKill)
                out.reached.emplace_back("a kill restored power");

            // And then kill it for real. A corpse is a different thing from a
            // hook call: Carrion wants something to scavenge, Echo wants a
            // creature to copy, Grave Call wants a body, and Killing Floor's
            // heal is paid on the death rather than on the callback.
            if (target->IsAlive())
            {
                Unit::Kill(player, target);
                noteBoth("really killing it");

                for (uint32 i = 0; i < 8; ++i)
                    sGauntlet->Tick(player, Scheduler::TICK_MS);
                noteBoth("ticking over the corpse");

                for (uint32 i = 0; i < BENCH_FIRES; ++i)
                {
                    if (run->dead || !player->IsAlive())
                        break;
                    if (!sGauntlet->FireNow(player, mechanic))
                        break;
                    ++out.eventsFired;
                }
                noteBoth("firing over the corpse");

                // The loot window, which docs/greed-redesign.md section 7.4
                // asks for by name. OnLootWindow is a different hook from the
                // item roll and nothing else in this file opens a corpse, so
                // a card that acts when loot is opened -- Scavenger's Eye's
                // second roll, Carrion's counter -- is reached here and
                // nowhere else.
                //
                // What it can and cannot prove is worth being plain about.
                // The bench's target is the module's own Ambusher and carries
                // no loot template, so an empty window is the expected result
                // and "items added" will not fire for a card whose extra roll
                // is of the creature's own table. What it does prove is that
                // the card's hook ran over a real corpse it had just watched
                // die, with whatever the card tracked about that fight intact;
                // the count of corpses it acted on is in its own counters,
                // which the summary prints beneath this list.
                if (Creature* corpse = target->ToCreature())
                {
                    std::size_t const itemsBefore = corpse->loot.items.size();
                    sGauntlet->OnLootWindow(player, corpse->GetGUID(), &corpse->loot);
                    if (corpse->loot.items.size() != itemsBefore)
                        out.reached.emplace_back("opening the corpse: loot was added");
                    noteBoth("opening the corpse");
                }

                // A clean kill, on something worth looting.
                //
                // Both halves are needed and neither exists above. The fight
                // staged by this probe is one the player was hit in -- the
                // wound to 15% that the health-gated conditions need makes
                // sure of it -- so a card that pays for a fight nothing
                // touched you in reads as doing nothing. And the target is one
                // of the module's own summons, which carry no loot table, so
                // there is nothing for a second roll to double.
                //
                // Scavenger's Eye (88) needs both at once. This stages them:
                // a fresh out-of-combat edge (Mgr::OnEnterCombat is that edge
                // by construction, GauntletMgr.cpp:1585), a kill nothing
                // interrupts, and a corpse whose table was filled once before
                // the card is asked. What the card does to the item count is
                // then the whole answer.
                if (TempSummon* quarry = player->SummonCreature(BENCH_LOOT_ENTRY, player->GetPosition(),
                                                                TEMPSUMMON_TIMED_DESPAWN, BENCH_TARGET_LIFE_MS))
                {
                    if (Creature* body = quarry->ToCreature())
                    {
                        uint32 const lootId = body->GetCreatureTemplate()->lootid;

                        sGauntlet->OnEnterCombat(player, quarry);
                        if (body->IsAlive())
                            Unit::Kill(player, body);
                        sGauntlet->OnCreatureKill(player, body, /*byPet*/ false);

                        if (lootId != 0)
                        {
                            body->loot.clear();
                            body->loot.FillLoot(lootId, LootTemplates_Creature, player, true, true);

                            std::size_t const before = body->loot.items.size();
                            sGauntlet->OnLootWindow(player, body->GetGUID(), &body->loot);
                            std::size_t const after = body->loot.items.size();

                            if (after > before)
                                out.reached.emplace_back(
                                    "a clean kill's corpse rolled " + std::to_string(before)
                                    + " -> " + std::to_string(after) + " item(s)");
                        }
                    }

                    quarry->DespawnOrUnsummon();
                }

                // And a kill of something rare. Separate from the quarry above
                // because the two questions are different -- that one is "was
                // this corpse worth looting", this one is "was this creature
                // worth killing" -- and because a card may answer either
                // without answering the other.
                if (TempSummon* trophy = player->SummonCreature(BENCH_RARE_ENTRY, player->GetPosition(),
                                                               TEMPSUMMON_TIMED_DESPAWN, BENCH_TARGET_LIFE_MS))
                {
                    if (Creature* rare = trophy->ToCreature())
                    {
                        // The tick is what notices one is nearby; a card that
                        // reads the world on its own clock has to be given the
                        // clock before it can be asked about the kill.
                        for (uint32 i = 0; i < 4; ++i)
                            sGauntlet->Tick(player, Scheduler::TICK_MS);
                        noteBoth("something rare standing nearby");

                        if (rare->IsAlive())
                            Unit::Kill(player, rare);
                        sGauntlet->OnCreatureKill(player, rare, /*byPet*/ false);
                        noteBoth("killing something rare");
                    }

                    trophy->DespawnOrUnsummon();
                }
            }

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

        // Undo the bench's own cast before anything reads the character again.
        // The aura it applied comes off, and anything it displaced goes back --
        // re-cast rather than re-added, because these were on the player a
        // moment ago and putting them back is restoring state rather than
        // inventing it.
        for (uint32 id : castAdded)
            player->RemoveAura(id);

        for (uint32 id : castRemoved)
            if (!player->HasAura(id) && player->HasSpell(id))
                player->CastSpell(player, id, true);

        // The bench's own litter. The target is despawned rather than left to
        // time out, because the next card is probed immediately and a stale one
        // standing there would be a second creature in every footprint after
        // this one.
        if (target && target->IsInWorld())
            target->DespawnOrUnsummon();

        return out;
    }
}
