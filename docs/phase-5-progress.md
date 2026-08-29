# Phase 5 — running plan and progress

**Status: complete.** See `docs/phase-5-report.md`.

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
| 2 | The empty tail: measure the levers | **done** — the answer is not a config key; see below |
| 3 | The pair tests | **folded into step 4** — see below |
| 4 | `docs/checklists.md` | **done** |
| 5 | `.gauntlet top` conducts, and the addon's leaderboard | **done** |
| 6 | README for the family model, and the determinism note | **done** |
| 7 | `docs/phase-5-report.md` | **done** |

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
3. **The sweep gets a second home, as a tool.** `tests/OfferInvariantsTest.cpp`
   runs the same simulation and must keep every knob at its shipped value; the
   question Phase 4 left needs the opposite. `tests/tools/sweep_standalone.cpp`
   takes them from the command line and runs 240,000 sets in a second.
4. **`GR_NoCandidate` was two failures on one bit, and they are not the same
   failure.** Split into `GR_NoCandidate` and `GR_NoRewardShaped`. See the
   measurement below: the second is the largest single relaxation in the module
   and it had been invisible for five phases.

## The measurement, and what it says about the phase-4 report's three options

All figures from `build/sweep --seeds 300` (240,000 sets), live table.

| variant | relaxed | empty slots | no reward-shaped | tier 80 relaxed |
|---|---|---|---|---|
| **shipped** (carry 16, rank 3, `CAP_CLASS` 3) | 48.86% | 130,277 | 37.03% | 100% |
| `MAX_CARRIED` 20 | 56.40% | 245,180 | 51.58% | 100% |
| `MAX_CARRIED` 24 or 30 | 56.36% | 245,407 | 51.56% | 100% |
| `MAX_RANK` 4 (and every row's `maxRank` with it) | **27.70%** | 46,231 | **18.10%** | 91.97% |
| every window opened to tier 80 | 39.34% | **17,338** | 29.14% | **62.73%** |
| `CAP_CLASS` 4 | 46.75% | 103,289 | 35.01% | 99.93% |
| `CAP_CLASS` 5 | 46.20% | 90,059 | 34.59% | 99.73% |

**Option 2 of `docs/phase-4-report.md` §7 -- raise `MAX_CARRIED` -- is refuted.**
It makes every number worse, and 24 and 30 measure identically to 20 because a
run never reaches twenty affixes: the eligible pool is exhausted at about 19.4.
The reason is that the carry cap was *helping*. A full set is what turns slot C
into a guaranteed Swap and puts the whole set on the Swap fallback; a set that
never fills never gets either, so its slots stay New, find nothing, and come
back empty.

**The relaxation number was three different problems wearing one word.** With
`GR_NoRewardShaped` split out, over the test's own 160,000 live sets:

```
repeated family 19,660   repeated mechanic 0   no candidate 35,576   no reward-shaped 59,022
```

and by region of the run:

- **Tiers 13-50: the reward-shaped guarantee.** At tier 21 the set relaxes
  46.67% of the time and 36.50 points of that is sets with three real offers, no
  empty slot and no repeated family, marked only because none of the three was
  reward-shaped.
- **Tiers 51-70: repeated family.** 43.93% at tier 61. A genuinely thin pool.
- **Tiers 66-80: empty slots.** The tail Phase 4 found.

**The reward-shaped guarantee cannot be met and it is not a pool problem.** Ten
of sixty-nine rows carry `MF_RewardShaped`, and six of those are gated behind a
class or the Bargain family, so a character has exactly **four** generally
available: Carrion (1-50), Hubris (1-50), Champions (1-80), Frenzy (8-80). Once
all four are carried the guarantee is unsatisfiable, which happens in the
twenties -- and past tier 50 only two of the four are still in window. This is
the same shape as Phase 4's finding about the tier curve: the design's rule was
written for sixteen tiers and does not stretch to eighty.

The tag itself is not the thing to loosen. The design's test is "bargains in
structure -- they pay out for engagement", and applying it honestly to the rows
Phases 3 and 4 added does not produce many more: Deep Wounds and Falling Sky
have counterplay but pay nothing, Blood Magic is a tax with a boon, the Rules
rows are restrictions. Diluting `MF_RewardShaped` until the guarantee is
trivially met would make the guarantee mean nothing, which is worse than it
failing honestly.

## Why step 2 did not end in a config key

The phase's plan said "make the choice a config key", and two of the three
choices turned out not to need one.

**Option 3, stopping the tier axis before 80, is already the shipped
behaviour.** `Mgr::OfferTier` checks whether any of the three offers names a
mechanic and, if none does, advances the tier silently -- no chat lines, no
chooser. So a run past the point where the table is exhausted is already quiet,
and a `Gauntlet.MaxTier` key would only have made the same silence explicit. A
key that changes nothing is the fault this phase opened by fixing seven of them.

**Option 2 is refuted**, so there is nothing to expose.

**Option 1, `MAX_RANK` above 3, cannot be a config key at all.** Every
mechanic's rank table is `constexpr T X[MAX_RANK] = { a, b, c }`, so the
ceiling is a compile-time constant by construction -- and raising it without
writing 69 files of fourth values makes every one of them zero-fill silently.
It is a phase of work with a balance pass in it, and the numbers to justify it
are in the table above.

`CAP_CLASS` is the one number in that list that is already marked
`TODO(design)` and that the design has an opinion about: the cards give each
class four class curses and the generator allows three. Measured, four is a net
improvement (relaxed 48.86% to 46.75%, empty slots 130,277 to 103,289) with a
small cost in the thirties, because a run that takes a fourth class curse
earlier runs its class pool down sooner. It is left at three: the phase-4 report
put the choice as "three leaves the fourth a live offer; four would let a run be
entirely defined by its class, which may be the better game", and that is a
question about what the game should feel like rather than one the sweep can
answer.

## Why step 3 is part of step 4

Plan §5.4 names three pairs and a role-tax exclusivity to run "on a level-40
character". Only the exclusivity half can be asserted off-game, and it already
is: Shade/Echo and Cunning/Falter carry the `stalker` and `roletax` exclusive
keys, and `OfferInvariants`' `I_EXCLUSIVE_KEY` checks every carried pair over
1.6 million sets. The other three are behavioural -- do Call to Arms and Craven
compound into an unkillable chain, does Champions plus Frenzy outrun the damage
cap -- and answering them needs the mechanics' implementations, which cannot
be built without the core. They are checklist entries, so they are in
`docs/checklists.md` with the rest.
