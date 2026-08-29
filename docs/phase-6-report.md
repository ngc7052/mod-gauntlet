# Phase 6 report — the fourth rank

Branch `feature/affix-redesign`. Nine commits, `8de9d87` through this report.

The implementation plan stops at Phase 5, so this phase was chosen rather than
inherited. It did two things: it replaced Unspent with Killing Floor, and it
gave the table a fourth rank.

---

## 0. Definition of done

| Asked | Result |
|---|---|
| Unspent redesigned (user request) | **retired, and replaced by Killing Floor** |
| `MAX_RANK` 3 → 4 | **done**, with nothing zero-filled |
| Every rank IV chosen rather than extrapolated | **done** — and eight rows correctly refused one |
| Docs follow the numbers | **done** — 55 ladders in `checklists.md`, plus the README |
| Unit tests pass | **108 pass** |
| Worldserver starts clean | deployed |

---

## 1. What was built

| # | Step | Commit |
|---|---|---|
| — | Unspent retired, Killing Floor (74) added | `8de9d87` |
| 1 | Every rank table asserted against `MAX_RANK` | `500e1ee` |
| 2a–2e | Fourth values, family by family | `c8fa723` … `929f7c8` |
| 3 | `MAX_RANK` 3 → 4, registry, addon, versions | `c891e70` |
| 4 | Docs | `a09e0c7` |

---

## 2. The finding that shaped the whole phase

**Raising `MAX_RANK` does not fail to compile.**

Every mechanic's rank table was `constexpr T X[MAX_RANK] = { a, b, c }`. C++
zero-fills a short initializer, so changing the constant would have made rank IV
of everything 0% severity, 0 seconds of stun, 0 yards of radius — eighty-two
tables at once, silently, with every test still passing and the game quietly
offering rank-ups that made affixes *weaker*.

That is why step 1 exists and why it went first. Every table became
`X[] = { a, b, c }` with `static_assert(std::size(X) >= MAX_RANK)` beside it,
which changed no behaviour while `MAX_RANK` was still 3 and turned the flip into
a compile error for anything missed. `>=` rather than `==` deliberately: too few
entries is the dangerous direction, too many is harmless, and the inequality is
what let the fourth values land family by family with every commit in between
green.

When the flip came, **nothing fired** — which is the outcome the design was for,
not evidence it was unnecessary.

---

## 3. Eight rows that refused a fourth rank

A fourth rank has to *do* something. Where rank III already ends a ladder,
offering a IV promises an escalation that does not exist, which is the exact
fault this redesign was written to remove.

| Row | Why |
|---|---|
| **Nimble** | 40% is exactly `Gauntlet.Caps.EnemySpeed`. A IV would be clamped to the same number, and raising the cap breaks design §2.8's "never remove the universal escape" |
| **Dead Weight** | Feign Death is denied outright at III. Nothing is past never |
| **Cold Trail** | the same, for Vanish |
| **Cold Feet** | the same, for Blink |
| **Mana Burn** | III already burns a point of mana per point of damage |
| **Slow Hands** | regeneration is zero at II, and III spends the second axis (no combo points while moving). There is no third |
| **No Sanctuary** | three discrete stages, and III is the last |
| **One Ward** | the same |

Their tables still carry a fourth entry, because the assert requires one; each
says in the file that it is unreachable, and `Eligible` refuses a rank-up past
`def.maxRank`.

**Two more nearly joined them, for opposite reasons.**

*Hubris* is the interesting one. Its curse is absolute at rank III — an enemy
below your level gives nothing, and nothing is the floor — so a fourth number
could only have moved `ABOVE_BONUS_PCT`, which is the **boon**. A rank-up with a
bigger reward and no extra cost is not a choice. It got a second axis instead:
at rank IV an enemy at *exactly* your level counts as below it, so only
something higher pays experience at all. The card's own advice — "level in the
zone one step ahead" — becomes the rule.

*Arcane Frailty* looked like Nimble: its rank III health multiplier is 0.60 and
`Gauntlet.Caps.MaxHealth` is 0.6. The difference is that it already implements
`RelaxCaps`, lowering the floor to its own number, so ×0.50 is deliverable where
Nimble's fourth 50% was not. The mechanic that had already solved the problem
once could have a fourth rank; the one that had not, could not.

Three more escalate on a different axis than the obvious one, because theirs was
spent: the Shade's nemesis, Half-Tamed's happiness threshold, and Grave Call's
risen health all stop at III while the mechanic's other number keeps going.

---

## 4. One bug this phase would have introduced, caught by reading

The Shade's named, escalating nemesis was gated on
`RankOf(self) >= MAX_RANK`. That read as "the last rank" and meant three while
there were three. A fourth rank would have moved the nemesis to IV and **quietly
taken it away from every character already carrying a Shade III** — a mechanic
removed by a constant moving.

No test would have caught it: the gate is correct C++, the value is in range,
and nothing asserts which rank the nemesis starts at. It is caught by grepping
`MAX_RANK` for uses that mean "the top" rather than "the bound", which is a
habit worth keeping the next time a constant like this moves.

---

## 5. The measurement

`build/sweep --seeds 300`, 240,000 offer sets, live table.

| | Phase 5 end | + Killing Floor | + rank IV |
|---|---|---|---|
| sets that relaxed any rule | 48.26% | 37.57% | **15.14%** |
| empty offer slots | 122,100 | 97,381 | **12,147** |
| sets with no reward-shaped offer | 36.90% | 25.91% | **8.56%** |
| tier 21 relaxed | 46.67% | 11.87% | **0.43%** |
| tier 41 relaxed | 27.67% | 25.77% | **4.80%** |
| tier 71 relaxed | 98.63% | 94.67% | **36.43%** |
| tier 80 relaxed | 100% | 99.87% | **65.87%** |

Tiers 1–31 relax essentially nothing and produce no empty slot at all. The first
empty slot in a run now appears at tier 41.

On the invariant sweep's own 160,000 live sets:

```
                   before Phase 6      after
no reward-shaped       59,022         14,019
no candidate           35,576          4,512
repeated family        19,660         13,309
empty slots            86,916          7,988
```

`RelaxationRatesAreWhereTheyWereMeasured` bounded the whole-run no-reward rate at
55% against a measurement of 44.6%. It is 8.68% now, so the bound is 12% — a
ceiling forty-six points above the measurement is not a regression test.

### The shape of the last three phases, in one number

The rate of sets with no reward-shaped offer, and what moved it:

```
44.6%  Phase 2, the world-side families complete
36.9%  Phase 4, after forty-four class curses
25.9%  Phase 6, after ONE classless reward-shaped row
 8.6%  Phase 6, after the fourth rank
```

Forty-four curses bought 7.7 points. One row bought 11. The fourth rank bought
17.3. **The shortage was never the size of the table**, and three phases spent
looking for more rows was the wrong search — a conclusion only reachable because
Phase 5 split `GR_NoRewardShaped` off `GR_NoCandidate` and made the two failures
tellable apart.

---

## 6. Rank IV is rank-up-only

`RankFloor` is `1 + (tier - 5) / 30`, which reaches III at tier 65 and would
reach IV at 95 — past the end of the axis. It was left alone deliberately, so a
newly picked affix never arrives at the ceiling and the fourth rank is only ever
reached by ranking something up.

That is what makes it an answer to the live report this phase started from
— *"after tier 42 there is only replacements"* — rather than another thing to
collect. A full set now always has something to deepen.

---

## 7. What Phase 7 should know

1. **Nothing has been playtested. Still.** Three phases have now ended with this
   sentence. `docs/checklists.md` is the list and its first item has not moved:
   `PermanentCooldown` greying a client button, which five curses depend on.
2. **The rank IV values are unplayed judgements.** Each was chosen against its
   card, none has been felt. The ones most likely to be wrong are the ones that
   moved a *rule* rather than a number: Hubris at IV, Craven at 50%, Overextended
   at 40% (three extra attackers is the whole damage-taken cap).
3. **`MAX_RANK` 5 is not the next lever.** The curve is flattening — tiers 1–31
   are already at zero — and what is left is tiers 66–80, where the pool is
   genuinely exhausted rather than gated. More reward-shaped rows are worth more
   than a fifth rank, and §5's table is why.
4. **`CAP_CLASS` is still 3 and still `TODO(design)`.** The design gives each
   class four.
5. **Grep `MAX_RANK` for "the top" before moving it again.** §4.
