# Phase 7 report — the module tells the truth about what it is doing

Branch `feature/affix-redesign`. Seven commits, `cb993a9` through this report.

Chosen rather than inherited, like Phase 6. Phase 6's report said the next lever
was not another rank and not more sweep-chasing; what a live hardcore realm with
nothing playtested actually needs is for the numbers a player reads to be the
numbers the module delivers. Every finding below is a place where they were not.

---

## 0. Definition of done

| Asked | Result |
|---|---|
| Audit the paths that can lose a run | **done** — clean, and §5 says so |
| Find where a stated number is not the delivered one | **four found, four fixed** |
| Make the unfixable ones visible instead | **done** — `PACE`, and a status line |
| A way to find this class of fault again | **done** — `.gauntlet debug cards` |
| Unit tests pass | **114 pass** |
| Worldserver starts clean | deployed, and the audit re-run against it |

---

## 1. Four numbers that were not what the player got

### 1.1 The budget was stretching things that are not cadences

`Scheduler::Arm` applied `Budget()` to every interval anyone armed. The budget
exists to stop *pressure* piling up as a run collects timed affixes — design
§4.2's `base × (1 + step × (timed − 1))` — and two kinds of interval are not
pressure being scheduled:

- **A fuse.** Death Rattle's corpse bursts two seconds after the kill, and those
  two seconds are the counterplay. On a run carrying six timed affixes the
  budget made them four and a half, and the minimum spacing could make them
  twelve — so killing three of a pack armed three fuses at zero, twelve and
  twenty-four seconds, by which time the player has walked away and the mechanic
  has silently stopped existing.
- **A telegraph's own arrival.** Ambush and Carrion arm with `inMs == warnMs`, so
  the whole interval *is* the telegraph. `GauntletScheduler.h` has promised since
  Phase 1 that "a five second telegraph stays five seconds however many affixes
  are carried, because it is information rather than pressure" — and it did not,
  because the lead was left unscaled while the fire it belonged to was stretched
  away from it. **The principle was written down and the API could not express
  it.**

`Arm` takes a `Pacing` now. `Fixed` skips the budget *and* the spacing, and does
not reset the spacing clock: a mechanic exempt from a rule must not be able to
enforce it on everything else.

### 1.2 A cadence below the spacing floor is two ranks that only look different

`Gauntlet.Events.MinSpacing` is twelve seconds and a mechanic asking for less
does not get it. Reinforcements asked for 10 s at rank III and — after Phase 6 —
8 s at rank IV. Both were played as twelve, so the offer promised an escalation
the scheduler could not deliver, and had done since Phase 2.

**Weakening the spacing was tried first and reverted whole.** Exempting a fire
from being spaced against the *same mechanic's* previous one reads well — the
rule's own contract says it exists "so unrelated timers cannot fire in the same
tick", and a mechanic's next beat is not unrelated. A fast re-armer then
monopolises the queue, and `Scheduler.AShortLeadIsNotStarvedByTheSpacing` caught
it in seconds: Falling Sky went to 39 warnings and 0 fires, the exact starvation
Phase 3's FIFO tie-break was added to end. **A test written three phases ago
stopped a plausible-sounding change from re-introducing the bug it was written
for**, which is the best argument for that test that could be made.

So the ladder moved instead, and `DEFAULT_MIN_SPACING_MS` now says in the header
that it is a floor on every cadence rather than only on the gap between two.

### 1.3 The cadence a blurb states is a base, and nothing said so

Every timed affix's blurb states the interval its own mechanic asks for, because
that is the only number a mechanic knows. The scheduler multiplies it, so a
player carrying six timed affixes reads "every 20 seconds" and waits forty-five.

No blurb can correct that — the stretch belongs to the whole carried set — so it
is stated once instead of qualified thirty times: a pacing line in
`.gauntlet status`, and a `PACE` frame that puts the multiplier in the addon's
panel footer and a line in the tooltip of the row whose number was just read.

This is the other half of a live report from Phase 3: *"reinforcements still
says 30 sec on rank 1 and 2 and i'm already minute in a fight?"* The half fixed
then was a real bug. The half left was that thirty seconds was never going to be
thirty seconds.

### 1.4 Eight rank-ups that changed nothing

Half-Tamed's rank I and rank II were identical — same happiness threshold, same
hostile duration, and `Describe()` reads only those two arrays, so the offer card
for rank II was the same *string* as rank I. The card gives two numbers for
three rungs and the middle one had been filled by repeating the first.

That prompted the tool in §2, which found seven more:

| Mechanic | What was wrong |
|---|---|
| Ankh Pact | four ranks, one behaviour, one sentence — three dead rank-ups |
| Stone of the Damned | the same; its card says only "as C43" |
| One Ward | flat two-minute cooldown, so I and II were identical |
| One Totem | rank IV *behaved* differently and the card said "double" for any multiplier above one |

Ankh Pact and Stone of the Damned are one-rank rows now — a mechanic that cannot
escalate should not be offered an escalation. One Ward's shared cooldown carries
its ladder (2/3/5/8 minutes). One Totem's card states the number.

The last one is the interesting one: three of the four were mechanics behaving
identically, and one was a mechanic behaving correctly while its card lied. To a
player they are the same bug — a tier spent on nothing.

---

## 2. `.gauntlet debug cards`

Prints every mechanic's offer text at every rank and shouts when two consecutive
ranks read the same. Boon magnitude is deliberately zero, because the boon
ladders on its own and would make two identical curses read differently.

It exists because **nothing could have caught this off-line**. `Describe()` is a
method on the mechanic, the mechanics need a `Player`, and `tests/run-tests.sh`
compiles neither — so the module has 114 unit tests and not one of them has ever
read a single word of what a player is actually offered. `Console::Yes`, so it
needs no character and nothing staked.

First run, against the live realm:

```
69 mechanic(s), 8 dead rank(s).
```

After the fixes, against the deployed build:

```
69 mechanic(s), 0 dead rank(s).
```

251 rank cards, all distinct. It is now the first item in `docs/checklists.md`.

---

## 3. What the audit found clean

Worth recording, because a clean result is a result:

- **Every self-damage site is floored at one health.** Blood Magic, Cold Feet,
  Spirit Debt, Nature's Toll, Blood Bond, Poisoned Blades — all six compute
  `health > 1 ? min(want, health - 1) : 0`. A hardcore run cannot be ended by
  its own affix.
- **World damage is not floored, and should not be.** Grudge, Falling Sky and
  Death Rattle go through `EnvironmentalDamage` and can kill. That is the right
  line: a price you pay for your own action is floored, a threat with counterplay
  is not.
- **`EndRun` does not clear the carried set**, so a mechanic that kills the
  player mid-callback is not executing inside a destroyed object.
- **`gauntlet_affix.rank` is `TINYINT UNSIGNED`**, so Phase 6's fourth rank
  persists without a migration.

---

## 4. What Phase 8 should know

1. **Still not playtested.** Fourth phase ending on it. But the list is shorter
   by one whole class of check: `.gauntlet debug cards` now covers "does this
   rank-up do anything", which was eight bugs' worth.
2. **The same trick is available for other invariants.** A console command that
   builds throwaway instances and reads what they say is the only way to test
   anything that needs a `Player`. "Does every mechanic's `Diagnose()` mention
   its own rank" would be the next one worth writing.
3. **`Pacing::Fixed` is a decision each new timed mechanic has to make.** The
   default is Paced and that is usually right; anything that is a fuse or a
   telegraph is not.
4. **A rank ladder must not step below `Gauntlet.Events.MinSpacing`.** §1.2.
5. **`CAP_CLASS` is still 3 and still `TODO(design)`.**
