# Phase 10 report — the check that needed a character

Branch `feature/affix-redesign`, from `c88788f`.

Phase 9's closing note said the machine-checkable surface was thinning and the
next phase should weight itself toward content or toward the in-game checklist.
This took the second half of that, and found there was a third option hiding
inside it: a large slice of `docs/checklists.md` is not really about a screen.

---

## 0. The gap this closes

Sixty-nine mechanics implement `OnAttach` and `OnDetach`. Nothing checked that
the second undoes the first.

It is not an oversight anyone could have fixed with a test. Both hooks take a
`Player`, `tests/run-tests.sh` builds a Player-free set on purpose, and there is
no world for a unit test to attach an affix to. So the only tool that had ever
been pointed at this class of fault was reading — which is what Phase 9 did, for
four subsystems, one at a time, and whose own report closes with **"verify a new
audit fails before trusting it."**

The gap is narrow and the consequences are not. The four `PermanentCooldown`
users hold a real cooldown on a real spell; a swap that holds and never releases
leaves a client button greyed out for seven days. A summon whose affix went away
is the orphan-stalker failure the code's own comments call the worst thing this
module can produce. Neither shows up in a compile, a link, a ladder audit, or
150 unit tests.

## 1. `.gauntlet debug leaks`

Attach every affix at its top rank, detach it, and say what did not come back.

```
.gauntlet debug leaks self            check the audit can see anything
.gauntlet debug leaks                 every mechanic, at its top rank
.gauntlet debug leaks class           one family
.gauntlet debug leaks shade 2         one mechanic, at a rank you name
```

The reading — a `Footprint` — is deliberately wider than any one mechanic needs:
applied auras, running cooldowns, max health, max power, free talent points,
shapeshift form, run and swim speed, owned summons, queued scheduler entries,
carried affix count, and all six aggregate products. An audit that only looks
where a bug is expected finds only the bugs that were expected.

Three verdicts, and the middle one is the one that matters:

| Verdict | Meaning |
|---|---|
| `LEAK` | The character is not the way it was found; the lines say how. |
| clean | It changed something at attach and put all of it back. |
| inert | Nothing measurable changed at attach. **Not a pass.** |

`inert` exists because without it the command lies. A hook-driven mechanic and a
class curse for the wrong class both change nothing on attach, so both would
report "clean" — a pass earned by having nothing to look at. Separating them is
the difference between "sixty-nine clean" and the truth.

### What it cleans up, and what it deliberately does not

Queued events and summons are cleaned after the verdict is printed. Both outlive
the command: an event fires later with nothing carried to blame it on, and a
summon just stands there. Both would be the audit's own mess.

Auras and cooldowns are left exactly as found. Stripping them would stop a second
run telling a leak from the first run's cleanup, and they are on a game master's
own character, where a relog clears them.

The order is the point and is stated in the source: cleaning before the verdict
would erase the evidence and report every mechanic clean.

## 2. The split, and the twenty-one tests

Same shape as `GauntletWire.cpp` in Phase 8, for the same reason. `Diff` is pure
and lives in `src/GauntletAudit.cpp`, which includes no core header and therefore
lands in the Player-free test build automatically. `Capture` is the read of nine
getters that needs a world, and lives in `src/GauntletAuditLive.cpp`.

`tests/AuditTest.cpp` moves each field and checks `Diff` reports it — and checks
the two properties that decide whether the command is usable at all:

- **A footprint compared with itself reports nothing.** Sixty-nine mechanics run
  through this; one spurious line each is sixty-nine lines of chat saying nothing.
- **An aura stacked twice and removed once is a leak.** Both readings hold the
  same *set* of ids, so a set difference sees nothing. `Extra` is a multiset walk
  for exactly this, and the test is what keeps it one.

## 3. `leaks self`, and the failure this phase could have shipped

`Capture` cannot be unit-tested, and its failure mode is silent and total: wire
it to the wrong getter and it returns an empty footprint, an empty footprint
compares equal to another empty footprint, and the command cheerfully reports all
sixty-nine mechanics clean. A green result from a blind audit is worse than no
audit, because it gets believed.

`.gauntlet debug leaks self` checks the live half can see the character before
any verdict is worth reading: that the aura and cooldown lists are as long as the
character's own and are sorted the way `Diff`'s multiset walk requires, that max
health is real and non-zero, that the carried count is the run's — and that
arming one scheduler entry is visible and cancelling it goes back. That last one
is the only thing it changes, and it changes it back; if arming is invisible, the
audit is blind to every Timed mechanic in the module.

**This is the phase's own answer to Phase 9's rule.** The pure half is verified by
21 tests that fail when `Diff` stops reporting. The live half is verified by a
command that fails when `Capture` stops seeing.

## 4. Also

`AggregateKindName` moved from a file-local static in `GauntletCommands.cpp` into
`GauntletNames.cpp`, where a comment had been asking for it since the switchover.
The audit was the second caller that would otherwise have copied the six labels,
and two copies of a label the player reads is how the two drift.

## 5. State

```
ANCHOR  PASS  69 registered mechanic(s), every one anchored
LADDER  PASS  78 rank ladder(s), every one monotonic
COMPILE PASS  60 object(s)          (58 before: +GauntletAudit, +GauntletAuditLive)
LINK    PASS  60 objects, no duplicate definitions
        PASSED  150 tests           (129 before: +21)
```

The worldserver image is rebuilt and carries the command; the realm itself is
down and was left that way.

## 6. What Phase 11 should know

1. **The audit has never been run against the module.** It is written, compiled,
   deployed and unit-tested, and its first real run is still ahead. The first
   thing to do with it is `leaks self`, then `leaks`, and the result is a finding
   either way: a leak is a bug, and sixty-nine clean is the first machine
   statement anyone has been able to make about `OnDetach`.
2. **A high `inert` count is a result, not a pass.** If most of the registry
   reports inert, the audit is measuring the wrong dimensions and the footprint
   needs widening — that is the honest reading, not "the module is fine".
3. **This closes one row of `docs/checklists.md`, not the file.** It answers
   "does detaching put everything back" for every mechanic at once. It cannot
   answer whether the effect was *correct* while it was on, which is the rest of
   the 672 lines and still needs a screen.
4. **`CAP_CLASS` is still 3 and still `TODO(design)`.** Seven phases.
