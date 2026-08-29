# Phase 6 — running plan and progress

**The fourth rank.**

The implementation plan stops at Phase 5, so this phase is chosen rather than
inherited. It is `MAX_RANK = 4`, and two independent things point at it.

**The measurement.** `docs/phase-4-report.md` §7 and `docs/phase-5-report.md` §3
both put a higher rank ceiling as the strongest lever available and both left it
to the user because it is a phase of work. Re-measured on the current table,
after Killing Floor landed, over 240,000 offer sets:

| | rank 3 (now) | rank 4 |
|---|---|---|
| sets that relaxed any rule | 37.57% | **14.90%** |
| empty offer slots | 97,381 | **8,296** |
| sets with no reward-shaped offer | 25.91% | **8.55%** |
| tier 21 relaxed | 11.87% | **0.53%** |
| tier 41 relaxed | 25.77% | **5.30%** |
| tier 71 relaxed | 94.67% | **34.37%** |
| tier 80 relaxed | 99.87% | **64.13%** |

Tiers 1–41 come out essentially clean, with no empty slots at all below 51.

**The live report.** *"Still after tier 42 there is only replacements."* That is
the same finding from the other end: a full set with everything at rank III has
nothing to deepen, so every slot degrades to a trade. Three swaps is not three
choices, it is one question asked three times. A fourth rank is what gives the
late run something to grow into.

## The danger, and the order that removes it

Every mechanic's rank table is `constexpr T X[MAX_RANK] = { a, b, c }`. Raising
the constant **does not fail to compile** — it zero-fills, so rank IV of
everything silently becomes 0%, 0 seconds, 0 yards. Eighty-two tables would go
wrong at once and nothing would say so.

So the flip goes last, not first:

| # | Step | State |
|---|---|---|
| 1 | Every table becomes `X[] = {…}` with `static_assert(std::size(X) >= MAX_RANK)` | **done** |
| 2 | Fourth values, family by family, each commit green | **done** |
| 3 | `MAX_RANK` 3 → 4, registry `maxRank`, addon pip, version bump | **done** |
| 4 | Re-measure, and the report | |

Step 1 changes no behaviour: the tables still hold three values and `MAX_RANK` is
still 3. What it buys is that step 3 becomes a **compile error** for every table
that was missed, rather than a silent zero. `>=` and not `==` on purpose — too
few entries is the dangerous direction, too many is harmless, and it lets step 2
land family by family while `MAX_RANK` is still 3.

## Standing rules

- **One commit per file or family**, so a bad ladder is one revert.
- **`tests/compile-check.sh` before every commit.**
- **A fourth value is a judgement, not an extrapolation.** The whole point of
  this redesign is that no number is generically rolled; a rank IV produced by
  continuing an arithmetic series is exactly the thing it deletes. Each one is
  chosen against the card and the reason goes in the file when it is not
  obvious.
- **Where rank III already ends the ladder** — Dead Weight's "never", Cold
  Trail's "never", Cold Feet's "denied" — rank IV cannot be "more of that".
  Those need a second axis or they stay at `maxRank = 3`.

## Decisions taken while working

1. **`RankFloor` is not rescaled, so rank IV is rank-up-only.** The floor is
   `1 + (tier - 5) / 30`, which reaches III at tier 65 and would reach IV at 95
   — past the end of the axis. That is left alone deliberately: a new affix
   picked late should not arrive at the ceiling, and IV being reachable only by
   ranking something up makes it a reward for commitment rather than a freebie.


## Eight rows that keep `maxRank = 3`

A fourth rank has to *do* something. Where rank III already ends a mechanic's
ladder, offering a IV would promise an escalation that does not exist — the
exact fault this redesign was written to remove — so these eight stop at three.
Their tables still carry a fourth entry, because the assert requires one and the
assert is what made this phase safe; each says in the file that it is
unreachable, and `Eligible` refuses a rank-up past `def.maxRank`.

| Row | Why |
|---|---|
| Nimble | 40% is exactly `Gauntlet.Caps.EnemySpeed`. A IV would be clamped to the same number, and raising the cap breaks §2.8's "never remove the universal escape" |
| Dead Weight | Feign Death is denied outright at III. Nothing is past never |
| Cold Trail | the same, for Vanish |
| Cold Feet | the same, for Blink |
| Mana Burn | III already burns a point of mana per point of damage |
| Slow Hands | regeneration is zero at II and III spends the second axis (no combo points). There is no third |
| No Sanctuary | three discrete stages, and III is the last |
| One Ward | the same |

**Hubris and Arcane Frailty nearly joined them and did not**, for opposite
reasons. Hubris's curse is absolute at III, so a fourth number would have moved
only its boon — a rank-up with a bigger reward and no extra cost. It got a
second axis instead: at rank IV an enemy at *exactly* your level counts as below
it. Arcane Frailty's health floor is `Gauntlet.Caps.MaxHealth` at 0.6, which is
precisely where its rank III sits — but it already relaxes that floor to its own
number, so x0.50 is deliverable where Nimble's 50% speed was not.

## The measurement

`build/sweep --seeds 300`, 240,000 sets, live table.

| | before | after |
|---|---|---|
| sets that relaxed any rule | 37.57% | **15.14%** |
| empty offer slots | 97,381 | **12,147** |
| sets with no reward-shaped offer | 25.91% | **8.56%** |
| tier 21 relaxed | 11.87% | **0.43%** |
| tier 41 relaxed | 25.77% | **4.80%** |
| tier 71 relaxed | 94.67% | **36.43%** |
| tier 80 relaxed | 99.87% | **65.87%** |

Tiers 1–31 relax essentially nothing and produce no empty slot at all; the first
empty slot in any run now appears at tier 41.

And on the invariant sweep's own 160,000 live sets:

```
                      before Phase 6      after
no reward-shaped         59,022          14,019
no candidate             35,576           4,512
repeated family          19,660          13,309
empty slots              86,916           7,988
```

The whole-run no-reward rate over the full table is 8.68%, down from the 44.6%
Phase 2 measured, and `RelaxationRatesAreWhereTheyWereMeasured` now bounds it at
12% instead of 55% — a ceiling forty-six points above the measurement is not a
regression test.
