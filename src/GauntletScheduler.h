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

    // Whether an interval is a cadence the run's event budget may stretch, or
    // the mechanic's own short timing that it may not.
    //
    // The distinction was implicit and therefore wrong. The budget exists to
    // stop *pressure* piling up as a run collects timed affixes -- design
    // section 4.2's "effective interval = base x (1 + step x (timed - 1))" --
    // and Arm() applied it to every interval anyone armed, including two kinds
    // that are not pressure being scheduled at all:
    //
    //   - **A fuse.** Death Rattle's corpse bursts two seconds after the kill,
    //     and those two seconds are the counterplay: the card's answer is to
    //     step back. Scaled by a run carrying six timed affixes it became four
    //     and a half, and spaced against other fires it could become twelve --
    //     by which time the player has walked away and the mechanic has
    //     silently stopped existing. Killing three of a pack armed three fuses
    //     twelve seconds apart.
    //   - **A telegraph's own arrival.** Ambush and Carrion arm with
    //     inMs == warnMs, so the whole interval *is* the telegraph. The header
    //     has always said "a five second telegraph stays five seconds however
    //     many affixes are carried, because it is information rather than
    //     pressure" -- and it did not, because the lead was left alone while
    //     the fire it belonged to was stretched away from it.
    //
    // Fixed skips both the budget and the minimum spacing, because both are
    // rules about pacing a run's events against each other and neither applies
    // to a two-second consequence of something the player did two seconds ago.
    enum class Pacing : uint8 { Paced, Fixed };

    struct ScheduledEvent
    {
        uint32    dueMs    = 0;
        uint16    mechanic = MECHANIC_NONE;
        uint32    id       = 0;      // the mechanic's own tag for this event
        EventKind kind     = EventKind::Warn;
        Pacing    pacing   = Pacing::Paced;

        // Arming order, assigned once and never changed -- a re-telegraph keeps
        // it. Two events due at the same instant are ordered by this rather
        // than by mechanic id, so the one that has been waiting longer goes
        // first and nothing can be lapped forever. See Earlier().
        uint32    seq      = 0;
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
        // A floor on how close together any two of a player's *paced* events may
        // land, and therefore a floor on every cadence a mechanic can actually
        // deliver: a mechanic that asks for 8 s gets 12, whatever its card says.
        // A rank ladder that steps below this number is two ranks that differ on
        // the offer card and not on screen -- Reinforcements had one, see the
        // note on REPEAT_MS there.
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

        // Arms a warning at `inMs - warnMs` and the fire at `inMs`, the fire
        // scaled by Budget() unless `pacing` is Fixed. warnMs == 0 arms only
        // the fire.
        //
        // Budget() stretches the interval, never the warning's lead: a five
        // second telegraph stays five seconds however many affixes are carried,
        // because it is information rather than pressure. Arming (mechanic, id)
        // again replaces whatever that pair already had queued -- `id` is the
        // mechanic's tag for one event, so a second Arm is a reschedule.
        //
        // Pacing::Fixed is for an interval that is the mechanic's own timing
        // rather than its cadence -- a fuse, or a telegraph whose whole length
        // is the warning. See the note on Pacing.
        void Arm(uint16 mechanic, uint32 id, uint32 inMs, uint32 warnMs,
                 Pacing pacing = Pacing::Paced);
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
        //
        // Re-telegraphing is unbounded on purpose: a Fire held for a very long
        // time re-announces as often as it has to, and each announcement is
        // true when it is made. What stops that becoming a mechanic that only
        // ever warns is the ordering rule on ScheduledEvent::seq -- without it,
        // a re-telegraphed Fire moved to the back of a queue sorted by due
        // time and, when it tied with a fresher event, lost the tie-break to
        // the lower mechanic id every single time.
        //
        // Measured before that fix, with four timed affixes and the shipped
        // 12 s spacing: Falling Sky III issued 23 warnings and fired 0 times in
        // five minutes of unbroken combat. Its 3 s lead tolerates 5 s of delay
        // against a 12 s wait, so it re-telegraphed on every cycle, and id 14
        // sat behind 2, 4 and 5 on every tie. The player saw the circle land
        // twenty-three times and never once saw the sky fall.
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

    public:
        // What the run's timed affixes are doing to every paced cadence, and
        // the floor under two consecutive fires. Read by `.gauntlet status` and
        // by the addon, because a mechanic that says "every 20 seconds" and
        // delivers every forty-five is indistinguishable from a broken one.
        uint32 TimedAffixes() const { return _timedAffixes; }
        uint32 MinSpacingMs() const { return _minSpacingMs; }

    private:

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
        uint32 _seq           = 0;   // monotonic arming counter; see ScheduledEvent::seq
    };
}

#endif // MOD_GAUNTLET_SCHEDULER_H
