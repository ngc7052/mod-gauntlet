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
COMPILE PASS  61 object(s)          (58 before: +GauntletAudit, +GauntletAuditLive, +TimedLockout)
LINK    PASS  61 objects, no duplicate definitions
        PASSED  150 tests           (129 before: +21)
```

The worldserver image is rebuilt and carries the command. The live realm was
down throughout and was left that way; all testing ran on an isolated copy.

## 6. It has now been run — and it found three bugs

The section that stood here said the audit had never been run and its first real
run was still ahead. It has been run. `docs/testing-without-a-client.md` is how:
mod-playerbots puts real `Player` objects in the world with no game client, the
server console can address them by name, and console commands execute in the
world thread so the three captures are atomic. An isolated realm on a copy of
the database, and no part of the live one touched.

**Its own probe failed first.** `Scheduler::Arm` refuses `MECHANIC_NONE` and
returns without queueing anything, so `leaks self` was measuring an `Arm` that
never happened — and said so, on the first run, before the audit was ever
pointed at the module. That is the phase's rule working on the phase's own code.

**The clean/inert split was meaningless.** `Capture` records the carried-affix
count, which necessarily moves while an affix is attached, so every mechanic
looked like it had done something. The first full run read *69 clean, 0 inert*
on a warrior most of those curses are not for. Normalised out, the same run
reads 6 clean, 63 inert — which is the truth.

**Then three real leaks**, all the same fault:

| Curse | Left behind |
|---|---|
| Iron Discipline IV | warrior stance cooldowns 71, 2457, 2458 cleared |
| Berserker's Bargain IV | Shield Wall (871) cleared |
| Cold Presence IV | presences 48263, 48265, 48266 cleared |

Five class curses lock a group of abilities against each other and all five
released by clearing the whole group in `OnDetach` — which also clears whatever
cooldown the spell was on for its own reasons. `TimedLockout` replaces the five
copies and clears only locks it placed that are still running.
`PermanentCooldown::Allow` had it too, and now releases only what `IsDenied`
recognises as ours.

Berserker's Bargain is the one to remember: **it appeared on the first pass and
not the second**, because the audit itself consumed the real Shield Wall cooldown
that made it visible. A single clean run proves less than it looks like.

And worth stating plainly: **Phase 9 read all four `PermanentCooldown` users by
hand and called them clean.** They are symmetric — `Deny` pairs with `Allow` on
every one. Reading could not have found this, because what makes it a bug is a
cooldown that was already there. That is the argument for the audit, made
against the phase that did the reading.

Final state: 20 audits, two passes over one bot of each of the ten classes,
**0 leaked**.

## 7. What Phase 11 should know

1. **`inert` is the headline, not `leaked`.** 63 of 69 report inert on a typical
   character, because the audit attaches and detaches and never makes the player
   cast, kill, or take a hit. What it proved is that `OnDetach` is now honest.
   What it did not touch is every curse whose behaviour lives on a hook.
2. **The rig is the asset, more than the audit is.** There is now a way to run
   this module against live `Player` objects with no client and no human. Driving
   the hooks from it is the obvious next build, and it is most of what
   `docs/checklists.md` is still holding.
3. **Run it twice, against a bot that has been fighting.** See Berserker's
   Bargain above.
4. **Two lockouts deliberately survive detach**: Cold Trail's Vanish and Dead
   Weight's Feign Death. Those are minutes-long prices for an ability already
   spent, and whether losing the affix should refund them is a design question
   that has not been answered.
5. **`CAP_CLASS` is still 3 and still `TODO(design)`.** Seven phases.
