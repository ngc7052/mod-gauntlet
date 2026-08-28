/*
 * mod-gauntlet - the per-player event scheduler
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletScheduler.h"

#include <algorithm>

namespace Gauntlet
{
    namespace
    {
        // Budget() multiplies whatever a mechanic asks for, and a mechanic that
        // asks for a nonsense interval should not be able to push an event past
        // the end of the session -- or past the end of the 32-bit millisecond
        // clock. A day is far beyond anything the design uses (the longest
        // stated cadence is Falling Sky's, in minutes) and is comfortably short
        // of the ~49 days uint32 milliseconds can hold.
        constexpr uint32 MAX_INTERVAL_MS = 24u * 60u * 60u * 1000u;

        // Sorted by due time; a warning goes before a fire that shares it, and
        // the mechanic and tag settle the rest so the order is total and the
        // queue reads the same on every run.
        bool Earlier(ScheduledEvent const& a, ScheduledEvent const& b)
        {
            if (a.dueMs != b.dueMs)
                return a.dueMs < b.dueMs;
            if (a.kind != b.kind)
                return static_cast<uint8>(a.kind) < static_cast<uint8>(b.kind);
            if (a.mechanic != b.mechanic)
                return a.mechanic < b.mechanic;
            return a.id < b.id;
        }
    }

    void Scheduler::Sort()
    {
        std::stable_sort(_queue.begin(), _queue.end(), Earlier);
    }

    void Scheduler::Insert(ScheduledEvent const& ev)
    {
        _queue.push_back(ev);
        Sort();
    }

    Scheduler::Pending* Scheduler::Find(uint16 mechanic, uint32 id)
    {
        for (Pending& p : _pending)
            if (p.mechanic == mechanic && p.id == id)
                return &p;
        return nullptr;
    }

    void Scheduler::Drop(uint16 mechanic, uint32 id)
    {
        _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                                      [mechanic, id](Pending const& p)
                                      { return p.mechanic == mechanic && p.id == id; }),
                       _pending.end());
    }

    uint32 Scheduler::Scale(uint32 ms) const
    {
        if (ms == 0)
            return 0;

        double const scaled = static_cast<double>(ms) * static_cast<double>(Budget()) + 0.5;
        if (scaled >= static_cast<double>(MAX_INTERVAL_MS))
            return MAX_INTERVAL_MS;
        return static_cast<uint32>(scaled);
    }

    float Scheduler::Budget() const
    {
        // Design section 4.2: effective interval = base x (1 + step x (timed
        // affixes - 1)). One timed affix, or none, is the unstretched case.
        if (_timedAffixes <= 1)
            return 1.0f;

        float const budget = 1.0f + _budgetStep * static_cast<float>(_timedAffixes - 1);

        // A negative step is a misconfiguration, not an invitation to make
        // events fire faster the more affixes are carried.
        return budget < 1.0f ? 1.0f : budget;
    }

    void Scheduler::SetTimedAffixCount(uint32 n)
    {
        _timedAffixes = n;
    }

    void Scheduler::SetMinSpacingMs(uint32 ms)
    {
        _minSpacingMs = ms;
    }

    void Scheduler::SetBudgetStep(float step)
    {
        _budgetStep = step;
    }

    std::vector<ScheduledEvent> const& Scheduler::Queue() const
    {
        return _queue;
    }

    bool Scheduler::WarnIssued(uint16 mechanic, uint32 id) const
    {
        for (Pending const& p : _pending)
            if (p.mechanic == mechanic && p.id == id)
                return p.warnLeadMs == 0 || p.warnIssued;

        // No record: either the pair has already been released and dropped, or
        // it was never armed. Both mean there is no warning left owing.
        return true;
    }

    void Scheduler::Arm(uint16 mechanic, uint32 id, uint32 inMs, uint32 warnMs)
    {
        // MECHANIC_NONE means "no mechanic" everywhere else in the module and
        // there is nothing to deliver an event to.
        if (mechanic == MECHANIC_NONE)
            return;

        // `id` is the mechanic's tag for one event, so arming it again is a
        // reschedule rather than a second copy. Without this a mechanic that
        // re-arms on a hook that fires twice grows the queue forever.
        _queue.erase(std::remove_if(_queue.begin(), _queue.end(),
                                    [mechanic, id](ScheduledEvent const& ev)
                                    { return ev.mechanic == mechanic && ev.id == id; }),
                     _queue.end());
        Drop(mechanic, id);

        if (_queue.size() + 2 > MAX_QUEUED)
            return;

        uint32 const fireIn = Scale(inMs);
        uint32 const fireAt = _nowMs + fireIn;

        // Budget() stretches the interval; the warning's lead is left alone,
        // because a telegraph is how long the player has to react and that
        // should not change with how many affixes are carried. It is clamped
        // to the interval so a warning can never predate its own arming.
        uint32 const lead = warnMs > fireIn ? fireIn : warnMs;

        Pending pending;
        pending.mechanic   = mechanic;
        pending.id         = id;
        pending.warnLeadMs = lead;
        _pending.push_back(pending);

        ScheduledEvent ev;
        ev.mechanic = mechanic;
        ev.id       = id;

        if (lead > 0)
        {
            ev.dueMs = fireAt - lead;
            ev.kind  = EventKind::Warn;
            Insert(ev);
        }

        ev.dueMs = fireAt;
        ev.kind  = EventKind::Fire;
        Insert(ev);
    }

    void Scheduler::Cancel(uint16 mechanic)
    {
        _queue.erase(std::remove_if(_queue.begin(), _queue.end(),
                                    [mechanic](ScheduledEvent const& ev) { return ev.mechanic == mechanic; }),
                     _queue.end());
        _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                                      [mechanic](Pending const& p) { return p.mechanic == mechanic; }),
                       _pending.end());
    }

    void Scheduler::CancelAll()
    {
        _queue.clear();
        _pending.clear();

        // The clock and the last fire are deliberately left alone. CancelAll is
        // logout and death, and death does not end a run any more -- a player
        // who is raised inside the death window should not get a free burst of
        // events because the spacing forgot what had just happened to them.
    }

    std::vector<ScheduledEvent> Scheduler::Tick(uint32 diff, Suppression const& s)
    {
        std::vector<ScheduledEvent> out;

        // OnPlayerUpdate runs for every player on every world tick, which is
        // roughly every millisecond, so the common path has to be an add and a
        // compare. The leftover carries, which is what makes ten 50 ms ticks
        // do exactly the work of one 500 ms tick.
        _accumulatorMs += diff;
        if (_accumulatorMs < TICK_MS)
            return out;

        while (_accumulatorMs >= TICK_MS)
        {
            _accumulatorMs -= TICK_MS;
            _nowMs         += TICK_MS;
            Step(out, s);
        }

        return out;
    }

    void Scheduler::Step(std::vector<ScheduledEvent>& out, Suppression const& s)
    {
        if (_queue.empty())
            return;

        // Design section 4.2 says "no events" in these states, and it means
        // warnings too: a countdown started while the player is on a griffin is
        // a lie about when the hit lands. Nothing is extracted, so whatever was
        // due comes out unchanged -- the same event, not a new one -- on the
        // first tick after the flag clears.
        if (s.Any())
            return;

        // Warnings are not spaced against each other. A warning is information,
        // not pressure, and two of them in the same half second is a readable
        // screen rather than a pile-up.
        bool warned = false;
        for (size_t i = 0; i < _queue.size(); )
        {
            if (_queue[i].dueMs > _nowMs)
                break;                          // sorted: nothing after this is due
            if (_queue[i].kind != EventKind::Warn)
            {
                ++i;
                continue;
            }

            ScheduledEvent const warn = _queue[i];
            _queue.erase(_queue.begin() + static_cast<ptrdiff_t>(i));
            out.push_back(warn);

            if (Pending* p = Find(warn.mechanic, warn.id))
            {
                p->warnIssued     = true;
                p->warnIssuedAtMs = _nowMs;
                warned            = true;
            }
        }

        // A warning that has only just gone out has to precede its fire by the
        // lead it promised. It normally does; it does not when both halves came
        // due together, which is what happens whenever a suppression outlasts
        // the whole pair.
        if (warned)
        {
            for (ScheduledEvent& ev : _queue)
            {
                if (ev.kind != EventKind::Fire)
                    continue;

                Pending const* p = Find(ev.mechanic, ev.id);
                if (!p || !p->warnIssued || p->warnLeadMs == 0)
                    continue;

                uint32 const notBefore = p->warnIssuedAtMs + p->warnLeadMs;
                if (ev.dueMs < notBefore)
                    ev.dueMs = notBefore;
            }
            Sort();
        }

        for (size_t i = 0; i < _queue.size(); )
        {
            if (_queue[i].dueMs > _nowMs)
                break;
            if (_queue[i].kind != EventKind::Fire)
            {
                ++i;                            // cannot happen: the warns above are gone
                continue;
            }

            // The earliest moment the spacing would let this one out.
            uint32 const earliest = (!_everFired || _nowMs - _lastFireMs >= _minSpacingMs)
                                    ? _nowMs
                                    : _lastFireMs + _minSpacingMs;

            Pending* p       = Find(_queue[i].mechanic, _queue[i].id);
            uint32 const lead = p ? p->warnLeadMs : 0;

            // The telegraph has gone stale while this fire waited -- the
            // countdown reached zero and the hit never came. Re-announce
            // instead of landing silently: a fresh warning, and the fire a full
            // lead behind it. The warning goes out now when the fire is only a
            // lead away, and is queued for later when the spacing pushes the
            // fire further out than that, so the countdown the player sees is
            // always the real one.
            if (p && p->warnIssued && lead > 0 && earliest > p->warnIssuedAtMs + lead + WARN_STALE_SLACK_MS)
            {
                uint32 const fireAt = earliest > _nowMs + lead ? earliest : _nowMs + lead;
                uint32 const warnAt = fireAt - lead;

                _queue[i].dueMs = fireAt;

                ScheduledEvent warn = _queue[i];
                warn.kind  = EventKind::Warn;
                warn.dueMs = warnAt;

                if (warnAt <= _nowMs)
                {
                    out.push_back(warn);
                    p->warnIssuedAtMs = _nowMs;
                    Sort();
                }
                else
                {
                    p->warnIssued = false;
                    Insert(warn);
                }

                i = 0;                          // the queue moved under us
                continue;
            }

            if (earliest > _nowMs)
            {
                // Spacing. This one waits at the exact time it may go, and
                // every fire behind it waits with it: the queue is first come,
                // first served, so a later event cannot jump a delayed one.
                _queue[i].dueMs = earliest;
                Sort();
                break;
            }

            ScheduledEvent const fire = _queue[i];
            _queue.erase(_queue.begin() + static_cast<ptrdiff_t>(i));
            Drop(fire.mechanic, fire.id);
            out.push_back(fire);

            _lastFireMs = _nowMs;
            _everFired  = true;

            // With a spacing set, the loop's own test stops the next fire from
            // also going out now. A spacing of zero deliberately lets them all
            // out at once, which is what the debug switch wants.
        }
    }
}
