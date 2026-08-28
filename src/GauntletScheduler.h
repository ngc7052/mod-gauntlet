/*
 * mod-gauntlet - the per-player event scheduler
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_SCHEDULER_H
#define MOD_GAUNTLET_SCHEDULER_H

#include "Gauntlet.h"

#include <vector>

// Design section 4.2's "one event at a time", made a class. Every timed affix
// in the module -- the Shade's clock, Falling Sky's cadence, Ambush,
// Reinforcements, Falter, Carrion, Death Rattle -- arms its events here rather
// than keeping a timer of its own, so three affixes cannot all land in the same
// second and leave the player unable to tell what killed them.
//
// Like the registry, the generator and the aggregate maths, this header is free
// of Player.h. Tick() takes a Suppression struct and a diff and touches no
// Player, no config and no wall clock; Mgr fills Suppression from the live
// player and carries the config values in through the setters. That is what
// makes plan section 5.1's "scheduler with a fake clock" test possible at all.

namespace Gauntlet
{
    // Why an event may not fire right now. Filled by Mgr from the live player,
    // so the queue logic itself stays testable without a game world.
    struct Suppression
    {
        bool mounted      = false;
        bool inFlight     = false;
        bool inSanctuary  = false;
        bool dead         = false;
        bool inGrace      = false;   // login / zone-in window
        bool offerPending = false;

        bool Any() const
        {
            return mounted || inFlight || inSanctuary || dead || inGrace || offerPending;
        }
    };

    enum class EventKind : uint8 { Warn, Fire };

    struct ScheduledEvent
    {
        uint32    dueMs    = 0;
        uint16    mechanic = MECHANIC_NONE;
        uint32    id       = 0;      // the mechanic's own tag for this event
        EventKind kind     = EventKind::Warn;
    };

    // One per player. Owns the clock; mechanics never own one.
    class Scheduler
    {
    public:
        // The cadence the whole module runs on. Tick() accumulates whatever
        // diff the core hands it and only does work on a boundary, so the
        // queue advances at a fixed rate however jittery World::Update is.
        static constexpr uint32 TICK_MS = 500;

        // Plan section 2.3 and the config's Phase 1 defaults. Set from
        // Gauntlet.Events.MinSpacing and .BudgetStep by Mgr::LoadConfig.
        static constexpr uint32 DEFAULT_MIN_SPACING_MS = 12000;
        static constexpr float  DEFAULT_BUDGET_STEP    = 0.25f;

        // How much longer than its own lead a telegraph may be stale before the
        // event re-announces itself instead of landing silently. See the
        // slipped-fire rule on Tick().
        static constexpr uint32 WARN_STALE_SLACK_MS = 2000;   // TODO(design)

        // A queue this size is already pathological -- four timed affixes with
        // a warning each is eight entries -- so the cap exists to bound a
        // mechanic that arms in a loop, not to ration honest use.
        static constexpr size_t MAX_QUEUED = 64;

        // Arms a warning at `inMs - warnMs` and the fire at `inMs`, both scaled
        // by Budget(). warnMs == 0 arms only the fire.
        //
        // Budget() stretches the interval, never the warning's lead: a five
        // second telegraph stays five seconds however many affixes are carried,
        // because it is information rather than pressure. Arming (mechanic, id)
        // again replaces whatever that pair already had queued -- `id` is the
        // mechanic's tag for one event, so a second Arm is a reschedule.
        void Arm(uint16 mechanic, uint32 id, uint32 inMs, uint32 warnMs);
        void Cancel(uint16 mechanic);
        void CancelAll();

        // Advances by `diff` and returns what is due, honouring suppression and
        // the minimum spacing. A Fire held back by spacing waits; it is never
        // dropped. Warn events are not spaced against each other.
        //
        // Suppression holds everything, warnings included: design section 4.2
        // says "no events" while mounted, in flight, in a sanctuary, dead, in
        // the grace window or with an offer on the table, and a countdown
        // started while the player is on a griffin is a lie about when the hit
        // lands. Nothing is dropped -- the same event comes out once the flag
        // clears.
        //
        // The slipped-fire rule. A Fire whose warning has already gone out and
        // which then slips -- by spacing, or by a long suppression -- would
        // otherwise land after a countdown that reached zero and stayed there.
        // So when the earliest time it could fire is more than its own lead
        // plus WARN_STALE_SLACK_MS after the warning was sent, the pair is
        // re-telegraphed: a fresh Warn goes out and the Fire moves to a full
        // lead behind it. The player always gets a warning that is still true.
        std::vector<ScheduledEvent> Tick(uint32 diff, Suppression const& s);

        // 1 + step * (timedAffixes - 1); intervals a mechanic asks for are
        // multiplied by this, so three timed affixes each fire less often.
        float Budget() const;
        void  SetTimedAffixCount(uint32 n);

        void SetMinSpacingMs(uint32 ms);
        void SetBudgetStep(float step);

        std::vector<ScheduledEvent> const& Queue() const;   // .gauntlet debug dump

        // Whether the warning for (mechanic, id) has already gone out. False
        // for a pair that has none and for one this scheduler has never heard
        // of. `.gauntlet debug fire` is the only caller: releasing a fire early
        // must not swallow a telegraph the player has not been given yet, and
        // the plan's wording for that command is "skip the clock, keep the
        // warning".
        bool WarnIssued(uint16 mechanic, uint32 id) const;

        // The scheduler's own clock, in milliseconds since this instance was
        // created. Addition to the frozen interface: ScheduledEvent::dueMs is
        // an absolute time on this clock, so `.gauntlet debug dump` cannot turn
        // a queue entry into "in 7 s" without it.
        uint32 NowMs() const { return _nowMs; }

    private:
        // What Arm() knows about an event that ScheduledEvent has no room for:
        // the warning's lead, and whether it has already been sent. One record
        // per (mechanic, id) pair, alive from Arm until the Fire is released or
        // cancelled.
        struct Pending
        {
            uint16 mechanic       = MECHANIC_NONE;
            uint32 id             = 0;
            uint32 warnLeadMs     = 0;
            bool   warnIssued     = false;
            uint32 warnIssuedAtMs = 0;
        };

        // One 500 ms boundary's worth of work.
        void Step(std::vector<ScheduledEvent>& out, Suppression const& s);

        void     Insert(ScheduledEvent const& ev);
        void     Sort();
        Pending* Find(uint16 mechanic, uint32 id);
        void     Drop(uint16 mechanic, uint32 id);

        // `ms` stretched by Budget(), rounded to the nearest millisecond.
        uint32 Scale(uint32 ms) const;

        std::vector<ScheduledEvent> _queue;     // sorted; small enough for linear work
        std::vector<Pending>        _pending;

        uint32 _accumulatorMs = 0;
        uint32 _nowMs         = 0;
        uint32 _lastFireMs    = 0;
        bool   _everFired     = false;

        uint32 _minSpacingMs  = DEFAULT_MIN_SPACING_MS;
        float  _budgetStep    = DEFAULT_BUDGET_STEP;
        uint32 _timedAffixes  = 0;
    };
}

#endif // MOD_GAUNTLET_SCHEDULER_H
