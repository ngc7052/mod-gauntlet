# Rarity, rerolls, and where the next hundred cards come from

A plan for replacing the rank ladder with a rarity ladder, and for getting the
card count from 69 to roughly 160 without writing 90 mechanics by hand.

Everything with a number in it below was measured, not estimated. The
measurements are in §1.

---

## 0. The problem rarity is solving

Ranks are doing two jobs at once, and only one of them is visible.

The visible job is **depth**: rank IV of a card is a stronger version of rank I.
The invisible job is **filling eighty tiers**. `Gauntlet.TierInterval = 1`, so a
tier is a level and a run to 80 sees **80 offers** — but no character can be
offered more than about thirty distinct mechanics. Ranks hide that: 56 mechanics
× 4 ranks is 224 rank-steps to spend 80 picks on.

Remove ranks and both jobs need a new owner. Rarity can take both.

## 1. What is actually there today

Measured from `src/GauntletRegistry.cpp`, per class, against the family caps:

| | Spawn | Enemy | Tempo | Attrition | Rules | Bargain | Class | total |
|---|---|---|---|---|---|---|---|---|
| **family cap** | 3 | 4 | 4 | 3 | 1 | 2 | 3 | 20 |
| warrior | 5 | 7 | 5 | **2** | 3 | **2** | 4 | **28** |
| paladin | 5 | 8 | 5 | 3 | 3 | **2** | 5 | 31 |
| shaman | 5 | 8 | 5 | 3 | 3 | **2** | 6 | 32 |

Mean eligible per character: **29.6** of 69. Carry cap is 16.

Two families are already dead ends. **Bargain has exactly 2 against a cap of 2**
for every class, and **Attrition has 2 against a cap of 3** for warriors, rogues
and death knights. Once those are filled they can never be offered again, today,
with ranks still in.

## 2. The rarity ladder

Rarity is not "the same card, bigger". That is the rank ladder with new colours,
and it would bring back the thing being removed. Rarity is **how much of the run
the card changes**.

| Rarity | What it is | Shape | Count target |
|---|---|---|---|
| **Common** (white) | One small trade | "lose X, gain Y", single axis, no state | ~60 |
| **Uncommon** (green) | A trade with a condition | "lose X while Y, gain Z" | ~30 |
| **Rare** (blue) | A verb — a moment you react to | most of today's 69 | ~40 |
| **Epic** (purple) | Changes how a whole system plays | Killing Floor, Self-found | ~15 |
| **Legendary** (gold) | Run-defining, one per run | build-arounds | ~8 |

The user's own example is exactly a common: *"you cannot wear an axe but get +15
sword expertise"*. Small, concrete, build-flavoured, no state, no timer.

**Rarity is rolled per offer slot and weighted by tier.** That is where the
escalation goes: early tiers are nearly all commons, legendaries only appear
late. The run gets stronger because its cards get rarer, not because the player
spent tiers buying rank II of something they already had.

Suggested weights, to be tuned against the sweep tool:

| Tier band | Common | Uncommon | Rare | Epic | Legendary |
|---|---|---|---|---|---|
| 1–20 | 70% | 25% | 5% | — | — |
| 21–40 | 45% | 35% | 18% | 2% | — |
| 41–60 | 25% | 35% | 30% | 9% | 1% |
| 61–80 | 10% | 25% | 40% | 20% | 5% |

## 3. Where the cards come from without writing ninety mechanics

This is the part that makes the count achievable, and it is already supported by
the code.

`GAUNTLET_MECHANIC_FN(id, fn)` exists in `GauntletMechanic.h` — "for a file that
already has its own factory function". `MechanicRegistrar` takes an id and a
factory, so **one class can back many registry ids**:

```cpp
template <int N> IMechanic* MakeTrade() { return new SimpleTrade(TRADES[N]); }
GAUNTLET_MECHANIC_FN(101, MakeTrade<0>)
GAUNTLET_MECHANIC_FN(102, MakeTrade<1>)
```

So a common is a **table row**, not a file. Sixty commons is one `SimpleTrade`
class and a sixty-row table — a day's work, not a quarter's.

`SimpleTrade` needs three primitives, all verified to exist and all inside the
"no client patches" rule:

- **Deny equipment.** `PlayerScript::OnPlayerCanEquipItem` returns bool
  (`PlayerScript.h:575`). "You cannot wear axes" is a subclass check and a
  return, no DBC involved.
- **Grant a stat.** `SPELL_AURA_MOD_EXPERTISE = 240` and
  `SPELL_AURA_MOD_RATING = 189` exist (`SpellAuraDefines.h`). The module already
  applies an existing spell and overwrites the effect amount — Falling Sky
  documents the technique in full for `65828`, and `BoonSpeed` now shares it.
  The same trick gives arbitrary amounts on any aura type an existing spell
  happens to carry.
- **The aggregate.** Anything expressible as a coefficient needs no new code at
  all — `AggregateFactor` already exists and is what most commons will use.

The one honest cost: the client's tooltip shows the DBC number, not ours. That
trade is already made and documented for the speed boon; the icon is the
telegraph, the tooltip is not.

## 4. Reroll and skip

Both are offer-economy, not card content, and they are cheap.

- **Reroll** — rebuild the three offers at the same tier. `BuildOffers` is
  deterministic on `(seed, tier, …)`, so a reroll needs a counter folded into
  the seed: `Mix(seed ^ tier ^ rerollsUsed)`. One field, no new generator logic.
- **Skip** — decline the tier and take nothing.

The interesting part is what pays for a reroll. **Skipping should grant a reroll
charge.** That makes skipping a real choice rather than a trap: you give up a
pick now to get a better one later. A run starts with two or three charges and
earns more by declining.

Both need the addon's chooser to grow two buttons and the `OFFER` frame to carry
the remaining charge count.

## 5. Should cards scale with level?

Recommendation: **no — let rarity carry it.**

Level scaling makes a card's own text a lie. "30% of the damage you take becomes
a wound" has to mean one thing, and a card whose number silently changes with
level cannot be read, compared against another offer, or reasoned about. It also
re-creates the rank problem in a place the player cannot see.

Rarity already gives an escalation axis, and it escalates the *interesting* way:
late runs are not the same cards with bigger numbers, they are different cards.

One exception worth keeping: **tier-gated availability**. `minTier`/`maxTier`
already exist and already do this. A legendary that only appears past tier 50 is
scaling by rarity, not by arithmetic.

## 6. What it costs to build

| Piece | Work | Notes |
|---|---|---|
| Rarity on the offer | small | `Offer` gains a field; `BuildOffers` rolls it from the tier weights |
| Rarity on the affix | **free** | removing ranks frees the `rank` column *and* the `rank` wire field — rarity lives there with no migration |
| The rank removal itself | medium | 56 mechanics carry rank ladders; each collapses to one value. `GauntletRules.h` already holds six of them |
| `SimpleTrade` + table | medium | one class, one table, the three primitives in §3 |
| Reroll / skip | small | a counter in the seed, two buttons, one wire field |
| Addon | medium | rarity colour on the card, reroll and skip buttons, charge count |
| The 90 cards | large but shallow | ~60 are table rows; ~30 are real mechanics |

**Answer to "how many": about 90 new cards to reach ~160, of which only about 30
are C++ work.** That takes per-character eligible from 29.6 to well over 100,
which covers 80 tiers with three real choices at every one of them.

## 7. What still needs deciding

1. **Does rarity gate the carry cap?** Sixteen commons is a very different run
   from sixteen legendaries. A weight per rarity against a budget, rather than a
   flat count, is worth considering and is a bigger change.
2. **Can a common be swapped for a rarer card of the same family later?** That
   is where the "upgrade" feeling goes once ranks are gone, and it may be all
   the depth that is needed.
3. **One legendary per run, or one legendary per family?**
4. **What the existing 69 become.** Most are rares. Which are epics is a
   judgement per card, and several — Self-found, Killing Floor, One Ward — read
   as epics already.
5. **Does skipping really pay a reroll charge**, or something else? This is the
   one number in the plan with no evidence behind it at all.

## 8. Order of work

1. Rarity as a field, rolled and displayed, with every existing card marked rare.
   Nothing else changes; the ladder still works. This is reversible and provable.
2. `SimpleTrade` and the first ten commons. Prove the table-driven path with the
   bench before writing sixty rows.
3. Reroll and skip, which are independent of both and immediately playable.
4. Rank removal, once rarity is carrying enough of the run to make it survivable.
5. The remaining cards, in rarity order, commons first.

Steps 1–3 are worth doing whether or not ranks ever go, which is the argument
for that order.
