# Phase 5 report — pacing tools

Branch `feature/affix-redesign`. Eight commits, `8f060ae` through this report.

---

## 0. Definition of done, item by item

| Asked (plan, Phase 5) | Result |
|---|---|
| Event-budget and cap tuning through config | **done** — and seven keys that read nothing now read something |
| The §5.4 pair tests run deliberately | **written, not run** — they need a client; `docs/checklists.md` §9 |
| `.gauntlet top` prints conducts | **done**, and the addon leaderboard the plan pointed them at exists |
| README rewritten for the family model | **done** |
| Determinism note | **done**, and corrected: the realm's config is one of the inputs |
| `docs/checklists.md` (outstanding since Phase 0) | **done** — 601 lines |
| The tier 78–80 choice left by Phase 4 §7 | **measured**; one option refuted, one already shipped, one costed |
| Unit tests pass | **108 pass** |
| Worldserver starts clean | deployed, one unrelated startup warning |

---

## 1. What was built

| # | Step | Commit |
|---|---|---|
| 1 | The seven `Gauntlet.Family.*.Enable` keys get a consumer | `8f060ae` |
| 2a | The sweep, as a tool with the knobs on the command line | `152d49c` |
| 2b | `GR_NoRewardShaped` split from `GR_NoCandidate` | `9b501f8` |
| 5 | The addon leaderboard tab, and conducts in chat | `3daf05b` |
| 3–4 | `docs/checklists.md` | `a710c22` |
| 6 | README | `1e61fc5` |
| — | Two live bug reports | `e337221` |
| — | The Unspent replacement plan | `284cedc` |

---

## 2. The three findings

### 2.1 Seven config keys read nothing

`Gauntlet.Family.<Spawn|Enemy|Tempo|Attrition|Rules|Bargain|Class>.Enable` have
been in `conf/mod_gauntlet.conf.dist` since Phase 0, documented down to the
sentence about what happens to affixes a character already carries, and read by
no code at all. A realm could set all seven to `0` and be offered the whole
table.

That is the same fault `Gauntlet.MaxAffixes` had when Phase 3 found it, and it
is worth naming as a pattern: **this codebase writes conf documentation ahead of
the consumer and then forgets the consumer.** The rule going in is that a key
documented in the conf file must have a consumer and a consumer must have a key,
and `GeneratorFamilyMask.*` in `tests/GeneratorTest.cpp` is the first test that
checks half of it.

The behaviour is exactly what the conf file already promised — offers only,
carried affixes keep acting — so no run is rewritten by a config edit. The check
sits in `Eligible` rather than `BuildPools` so it holds at every relaxation rung:
a relaxation exists to fill a slot that would otherwise be empty, and a family
the realm has turned off is not a rule the generator may drop.

### 2.2 The largest relaxation in the module was invisible

`GR_NoCandidate` carried two meanings on one bit: a slot that could not be
filled at all, and the "one reward-shaped offer per tier" guarantee finding
nothing to pay itself with. Phase 0 wrote them together on the reasoning that
they are the same failure seen from two ends.

They are not, and the conflation is why 37% "no reward-shaped offer" read as a
pool problem for four phases. Split, over the invariant sweep's own 160,000 live
sets:

```
repeated family 19,660   repeated mechanic 0   no candidate 35,576   no reward-shaped 59,022
```

**The reward-shaped guarantee is the biggest single relaxation in the module,
and it is not about the size of the table.** Ten of sixty-nine rows carry
`MF_RewardShaped` and six are gated behind a class or the Bargain family, so a
character has four generally available — Carrion, Hubris, Champions, Frenzy —
and two of those expire at tier 50. Once all four are carried the guarantee
cannot be met, which happens in the twenties.

It is the same shape as Phase 4's finding about the tier curve: **a rule written
for sixteen tiers that does not stretch to eighty.**

Splitting the bit also restored an invariant. "The builder stopped trying to
satisfy the guarantee" has been unassertable since the tier axis changed,
because an empty slot late in a run set the same bit; it is a biconditional
again in both sweeps.

### 2.3 The relaxation number is three problems wearing one word

With the bits separated, the run has three distinct failures in three distinct
places, and they want three different fixes:

| Region | What is actually happening | At its worst |
|---|---|---|
| tiers 13–50 | the reward-shaped guarantee | tier 21: 46.67% relaxed, **36.50 points of it this alone** |
| tiers 51–70 | repeated family — a genuinely thin pool | tier 61: 43.93% |
| tiers 66–80 | empty slots — nothing left at all | tier 80: 8,578 slots |

Four phases of "the table is too small" were mostly a description of the middle
row.

---

## 3. The choice Phase 4 left, answered with measurement

`docs/phase-4-report.md` §7 offered three ways to close the empty tail and left
the choice to the user. Two of them can now be closed out.

All figures `build/sweep --seeds 300`, 240,000 sets, live table.

| variant | relaxed | empty slots | no reward-shaped | tier 80 relaxed |
|---|---|---|---|---|
| **shipped at the start of the phase** | 48.86% | 130,277 | 37.03% | 100% |
| `MAX_CARRIED` 20 | 56.40% | 245,180 | 51.58% | 100% |
| `MAX_CARRIED` 24 or 30 | 56.36% | 245,407 | 51.56% | 100% |
| `MAX_RANK` 4 (+ every row's `maxRank`) | **27.70%** | 46,231 | **18.10%** | 91.97% |
| every window opened to tier 80 | 39.34% | **17,338** | 29.14% | **62.73%** |
| `CAP_CLASS` 4 | 46.75% | 103,289 | 35.01% | 99.93% |
| `CAP_CLASS` 5 | 46.20% | 90,059 | 34.59% | 99.73% |

**Option 2 — raise `MAX_CARRIED` — is refuted.** It makes every number worse,
and 24 and 30 measure identically to 20 because a run never reaches twenty
affixes: the eligible pool is exhausted at about 19.4. The carry cap was
*helping*: a full set is what turns slot C into a guaranteed Swap and puts every
slot on the Swap fallback, and a set that never fills gets neither, so its slots
stay New, find nothing, and come back empty.

**Option 3 — stop the tier axis before 80 — is already the shipped behaviour.**
`Mgr::OfferTier` advances the tier silently when no offer names a mechanic: no
chat, no chooser. A `Gauntlet.MaxTier` key would only have made the same silence
explicit, and a key that changes nothing is the fault §2.1 exists to fix.

**Option 1 — `MAX_RANK` above 3 — is the strongest lever and cannot be a config
key.** Every mechanic's rank table is `constexpr T X[MAX_RANK] = { a, b, c }`, so
the ceiling is a compile-time constant by construction, and raising it without
writing sixty-nine files of fourth values makes every one of them **zero-fill
silently**. It is a phase of work with a balance pass in it. The numbers to
justify it are above and it is left for the user, as it was.

Two levers Phase 4 did not consider were also measured and dropped:

- **Ungating the "fall back to a swap" rung from `full`.** Zero change at the
  shipped cap, because a Swap still needs an uncarried, in-window, eligible
  mechanic and there is not one.
- **Opening every tier window to 80.** The best empty-slot number of anything
  measured, and wrong: the cards state their windows, and offering Carrion fresh
  at tier 70 is offering a level-1 problem to a level-70 character.

`CAP_CLASS` stays at 3. Four is a net improvement with a small cost in the
thirties, but the phase-4 report put the choice as "three leaves the fourth a
live offer; four would let a run be entirely defined by its class, which may be
the better game", and that is a question about how the game should feel.

---

## 4. Two live bug reports, mid-phase

**"After tier 42 there is only replacements."** Most of the screenshot is the
carry cap working as designed. One part was not: `Eligible` tested
`tier < minTier || tier > maxTier` for every offer kind, so **an affix taken near
the end of its window was frozen at whatever rank it happened to get,
permanently.** A window says when a mechanic may be *introduced*; whether
something already carried may deepen is a different question, and one test was
answering both. Rank-ups now ignore `maxTier` — empty slots 130,277 → 122,100,
nothing worse anywhere. `GeneratorVersion` 6 → 7.

**Mana Burn.** No bug found by reading, and none findable by playing either:
`Diagnose()` reported the configured percentage and nothing else, so "it never
ran" and "it ran and you did not notice" looked identical. It now counts blows
seen, mana burned and both skip reasons. One real fix while there — the power
test was `getPowerType() != POWER_MANA`, which asks "is mana the bar you are
spending right now" and is false for a druid in any form; it asks `GetMaxPower`
now. And a first-time chat line, for Blood Magic's reason.

---

## 5. What is still not true

**Nothing has been playtested.** That was Phase 4's headline and it is still
Phase 5's. `docs/checklists.md` is now the list, in priority order, and its first
item is unchanged: **`PermanentCooldown` greying a client button**, which five
wave-A curses depend on.

The pair tests are written and not run — they need a client and a level-40
character.

---

## 6. What Phase 6 should know

1. **The Unspent replacement is planned and unbuilt.**
   `docs/unspent-replacement-plan.md`, recommendation Killing Floor.
2. **One classless reward-shaped row is worth more than wave B was.** Measured:
   relaxed 48.26% → 40.97%, no-reward 36.90% → 28.72%, tier 21 relaxed 46.67% →
   **12.70%**. Twenty-one curses in wave B moved the top of the run by nothing.
   The shortage was never the size of the table.
3. **`MAX_RANK` 4 is the biggest single improvement available** and the most
   expensive. §3 has the numbers.
4. **The sweep is a tool now.** `tests/tools/README-sweep.md`. Any argument
   about the shape of the run should come with a number from it.
5. **`OnTalentPoints` will be orphaned** when Unspent goes. Leave the dispatch
   point: it is generic, and the next curse to want it should find it there.
6. **A conf key must have a consumer.** Two phases have now found keys that had
   none.
