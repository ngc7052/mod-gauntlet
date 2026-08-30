/*
 * mod-gauntlet - drive one card through every hook and see which ones it answers
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_BENCH_H
#define MOD_GAUNTLET_BENCH_H

#include "Gauntlet.h"

#include <string>
#include <vector>

class Player;

namespace Gauntlet
{
    struct RunState;

    // What one card answered when the bench asked it everything.
    //
    // The point of the shape is that it is *derived*, never declared. Nothing
    // here is written per card, and nothing has to be: the bench attaches one
    // affix, drives the whole of Mgr's dispatch surface, and records which
    // probes saw a difference. A card added next year is covered the day its
    // registry row lands, with no new bench code and no new expectations to
    // maintain.
    //
    // That is the whole design constraint. Sixty-nine cards are already more
    // than anyone will hand-write tests for twice, and the plan is more.
    struct ProbeResult
    {
        // Names of the probes that saw this card do something -- "experience",
        // "max health", "kill", "timer". Empty means every probe the bench has
        // ran and none of them moved.
        std::vector<std::string> reached;

        // The card's own Diagnose() after the run, when it has one.
        std::string diagnose;

        // Scheduled events actually released. Zero for a card that never armed.
        uint32 eventsFired = 0;

        // Cooldowns the bench's own cast started. They have to be cleared by
        // the caller *after* the affix is detached, not before: a card that
        // denies the spell buries the real cooldown while it is carried and
        // PermanentCooldown::Allow puts it back on the way out, so clearing it
        // any earlier just means it reappears in the final reading. Dead Weight
        // reported exactly that -- "spell 5384 still on cooldown", on a Feign
        // Death cooldown the bench had started by casting Feign Death.
        std::vector<uint32> castCooldowns;

        bool Reached() const { return !reached.empty(); }
    };

    // What the bench switched off so the cards could be heard, and what has to
    // go back afterwards.
    struct BenchSetup
    {
        std::vector<Offer> offers;
        uint32 graceMs = 0;
    };

    // Silences the scheduler's suppressions for the length of a bench run.
    //
    // Mgr::Tick delivers no event at all while the player is mounted, in
    // flight, in a sanctuary, inside the login grace window, or has an offer on
    // the table (design section 4.2). A playerbot is very often mounted and a
    // fresh run always has an offer pending, so without this the entire timed
    // half of the registry reports "reached by nothing" -- the bench's fault
    // rather than the cards'.
    //
    // Called once around the whole sweep and never per card, because dismounting
    // changes the run speed that Footprint records: doing it between a card's
    // before and after readings makes every card leak a speed change.
    BenchSetup BenchQuiet(Player* player, RunState* run);
    void BenchRestore(Player* player, RunState* run, BenchSetup const& saved);

    // Attach-free: the caller attaches the affix, calls this, then detaches and
    // diffs the footprint. Everything here goes through sGauntlet's own
    // dispatchers rather than IMechanic directly, so the probe exercises the
    // same gating, condition checks and actor-noting the live game does -- and
    // so that a hook added to Mgr is a hook the bench drives, without this file
    // knowing the name of a single mechanic.
    // `requiresSpell` is the card's own registry gate. Casting it is how a
    // cast-driven card is reached without the bench knowing a thing about which
    // card it is: the row already declares the spell, so a new card that
    // declares one is driven the day it lands.
    ProbeResult Probe(Player* player, RunState* run, uint8 slot, uint16 mechanic,
                      uint32 requiresSpell);
}

#endif
