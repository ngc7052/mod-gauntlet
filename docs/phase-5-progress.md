# Phase 5 — running plan and progress

Live working document, same shape as `docs/phase-4-progress.md`: updated as each
step lands so an interrupted session resumes from it, and so the state is
readable without going through the commits.

Phase 5 is the plan's "pacing tools" phase. Its scope, from
`docs/implementation-plan.md`:

> Event-budget and cap tuning through config; the §4.7 pair tests run
> deliberately (Call to Arms + Craven, Champions + Frenzy, Shade + Deep Wounds);
> `.gauntlet top` prints conducts; README rewritten for the family model;
> determinism note ("offers reproduce, events do not").

Plus the two things four phases left behind: `docs/checklists.md`, which §5.3 of
the plan has asked for since Phase 0 and which matters more than usual because
nothing in this module has been playtested; and the three-way choice
`docs/phase-4-report.md` §7 left the user about the empty tiers 78–80.

## The plan

| # | Step | State |
|---|---|---|
| 1 | The seven dead `Gauntlet.Family.*.Enable` keys | **done** |
| 2 | The empty tail: measure the levers, make the choice a config key | |
| 3 | The pair tests, as unit tests where they can be one | |
| 4 | `docs/checklists.md` | |
| 5 | `.gauntlet top` conducts, and the addon's leaderboard | |
| 6 | README for the family model, and the determinism note | |
| 7 | `docs/phase-5-report.md` | |

## Standing rules for this run

Unchanged from Phase 4, and they are the reason that phase's commits are
revertable one at a time:

- **One commit per step.**
- **`tests/compile-check.sh` before every commit**, and `tests/run-tests.sh`
  whenever anything under `src/` that the harness compiles has moved.
- **Deploy in batches, not per commit.** The realm is live.
- **A key documented in the conf file must have a consumer**, and a consumer
  must have a key. Step 1 exists because that was false in seven places.

## Decisions taken while working

1. **A disabled family is an offer-time filter and nothing else.** The conf
   file has said since Phase 0 that disabling a family "removes it from future
   offers; anything a character already carries keeps working", so that is what
   was built, rather than the more invasive reading where carried affixes go
   dormant. It also means no run is ever silently rewritten by a config edit.
2. **The mask is checked in `Eligible`, not in `BuildPools`.** Relaxation
   exists to fill a slot that would otherwise be empty, and the tempting shape
   is to drop rules at the last rung. A family the realm switched off is not a
   rule the generator may drop, so the check sits where every rung sees it.
