# Rarity, rerolls, and where the next hundred cards come from

**Decided.** This is not a proposal to weigh; it is what is being built. The
reasoning is kept because the numbers behind it are worth re-reading when the
tuning is wrong, not because the direction is still open.

**Status.** Steps 1 and 2 of §8 landed on 2026-08-31, steps 3 and 4 on
2026-09-01. Rarity is a registry column, rolled per slot and displayed;
`SimpleTrade` and the first ten commons (ids 75–84) are in the table, proven
with the bench; reroll and skip are live, with skipping banking a charge; every
older card is rare; **the rank system is gone** — a card is one value, the one
its blurb states. §7 of `docs/handoff.md` lists what each step put where and
the decisions taken that this document did not spell out, including what the
rank removal did to the late run (§7.1 is now the loudest open number). Step 5
— the remaining cards — has begun: the first loot trades, two commons and the
first uncommon, landed 2026-09-01 with `Boon::BonusLoot`.

- **The rank system is removed.** `MAX_RANK`, the rank ladders in 56 mechanics,
  rank-up offers, and the rank numerals in every card and chat line all go.
- **Rarity replaces it**: common, uncommon, rare, epic, legendary, rolled per
  offer slot and weighted by tier.
- **Reroll and skip are added**, with skipping paying for a reroll.
- **The registry grows from 69 to roughly 160**, of which about 60 are table
  rows rather than new code.
- **Cards do not scale with level.** Researched and rejected; see §5, which is
  the one decision here most worth overturning if play disagrees.

Everything with a number in it below was measured, not estimated. The
measurements are in §1.

---

## 0. Why the ranks are going

Ranks do two jobs at once, and only one of them is visible.

The visible job is **depth**: rank IV of a card is a stronger version of rank I.
The invisible job is **filling eighty tiers**. `Gauntlet.TierInterval = 1`, so a
tier is a level and a run to 80 sees **80 offers** — but no character can be
offered more than about thirty distinct mechanics. Ranks hide that: 56 mechanics
× 4 ranks is 224 rank-steps to spend 80 picks on.

The visible job is the weaker of the two. A rank-up is the least interesting
pick on the table: it costs a tier and hands back a bigger number on a card
already being carried, which is why so much of §5 of the design doc is spent
arguing against cards that are "always-safe picks that made the other two offers
irrelevant". Rank-ups are that shape by construction.

Rarity takes both jobs, and takes the second one better: a run gets stronger
because its cards get *rarer*, not because the player spent eighty tiers buying
the second copy of six of them.

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

The example that started this is exactly a common: *"you cannot wear an axe but
get +15 sword expertise"*. Small, concrete, build-flavoured, no state, no timer,
and readable in one line on an offer card.

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
IMechanic* TradeBareheaded() { return MakeTrade(75); }   // reads TRADES by id
GAUNTLET_MECHANIC_FN(75, TradeBareheaded)
```

(Not `MakeTrade<0>`, as this first said: the macro pastes its second argument
into the registrar's and the anchor's names, so it has to be a plain
identifier, and each one is a line in `AnchorMechanics()` like every other
card's. `src/mechanics/common/SimpleTrade.cpp` is the shape as built.)

So a common is a **table row**, not a file: a registry row and a line in
`src/GauntletTrades.h`. Sixty commons is one `SimpleTrade` class and a
sixty-line table — a day's work, not a quarter's.

`SimpleTrade` needs three primitives, all verified to exist and all inside the
"no client patches" rule:

- **Deny equipment.** `PlayerScript::OnPlayerCanEquipItem` returns bool
  (`PlayerScript.h:589`). "You cannot wear axes" is a subclass check and a
  return, no DBC involved. **Built in step 2** as `IMechanic::CanEquip`, with
  the worn item put into the bags on attach (the core's own
  `AutoUnequipOffhandIfNeed` pattern) and put back on detach.
- **Grant a stat.** `SPELL_AURA_MOD_EXPERTISE = 240` and
  `SPELL_AURA_MOD_RATING = 189` exist (`SpellAuraDefines.h`). The module already
  applies an existing spell and overwrites the effect amount — Falling Sky
  documents the technique in full for `65828`, and `BoonSpeed` now shares it.
  The same trick gives arbitrary amounts on any aura type an existing spell
  happens to carry. **Not yet built:** the first ten commons pay through the
  existing Boon plumbing instead (damage, experience, health, healing, speed),
  and no `TradeCurse` grants a stat. It needs a class-neutral carrier spell
  per aura type, read out of Spell.dbc, before a line can use it.
- **The aggregate.** Anything expressible as a coefficient needs no new code at
  all — `AggregateFactor` already exists and is what most commons will use.
  **Built in step 2** as `TradeCurse::Coefficient`.

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

**Built in step 3**, with the counter in bits 16–23 of the stream seed and the
per-tier count persisted in the run's state store (`RunKeys` in `Gauntlet.h`),
so a relog rebuilds the set the charge bought rather than the one it replaced.
The purse starts at two and a skip banks one (`Rules::REROLL_STARTING_CHARGES`,
`SKIP_EARNS_CHARGES` — §7.5's numbers, still without evidence); the charge
count rides the `RUN` frame rather than `OFFER`, because it is run state and
the panel wants it with no offer on the table.

## 5. Cards do not scale with level

This was the open question, and the answer after looking at it is **no — rarity
carries it.** It is the decision here most worth overturning if play disagrees,
so the reasoning is spelled out rather than asserted.

Level scaling makes a card's own text a lie. "30% of the damage you take becomes
a wound" has to mean one thing, and a card whose number silently changes with
level cannot be read, compared against another offer, or reasoned about. It also
re-creates the rank problem in a place the player cannot see.

Rarity already gives an escalation axis, and it escalates the *interesting* way:
late runs are not the same cards with bigger numbers, they are different cards.

One exception worth keeping: **tier-gated availability**. `minTier`/`maxTier`
already exist and already do this. A legendary that only appears past tier 50 is
scaling by rarity, not by arithmetic.

## 5b. What removing ranks actually touches

Written down because it is the largest single edit in the plan and it is easy to
underestimate:

| Thing | What happens |
|---|---|
| `MAX_RANK` in `Gauntlet.h` | Goes. Every `static_assert(std::size(X) >= MAX_RANK)` goes with it. |
| 79 rank ladders | Each collapses to the single value the card is worth at its rarity. `GauntletRules.h` already holds six of them, which is the pattern for the rest. |
| `MechanicDef::maxRank` | Becomes `rarity`. Same column width, same place in every row. |
| `OfferKind::RankUp` | Goes. `BuildOffers` loses a slot kind and the "rank-ups dominate from tier 11" weighting with it. |
| `AffixInstance::rank` | Becomes `rarity` — the DB column and the wire field are both already the right width, so **no migration**. |
| `RankNumeral` | Becomes a rarity name and colour. |
| The ladder audit | Loses most of its subjects. It stays for the ladders that remain (tier weights, reroll costs) and should not be deleted. |
| `tests/RulesTest.cpp` | Every "gets worse with rank" test becomes "gets worse with rarity" or goes. The shape tests that do not mention rank survive unchanged. |

The audit and the tests are the reason step 4 in §8 comes after the rest: with
rarity already carrying the run, rank removal is a deletion with a green gate
around it rather than a rewrite in the dark.

**Landed 2026-09-01.** The table above was right about everything except the
size of "no migration": the column needed no change, but `Mgr::Load` now takes
rarity from the registry row and ignores the stored value, and a guarded update
normalises old rows for offline readers. The collapse rule was *the value the
blurb states, else rank II*. What the table did not predict is under step 4 in
§8.

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

## 7. Still open

The direction is settled. These are not.

1. **Does rarity gate the carry cap?** Sixteen commons is a very different run
   from sixteen legendaries. A weight per rarity against a budget, rather than a
   flat count of 16, is the obvious answer and is a bigger change than it looks.
2. **Can a common be swapped for a rarer card of the same family later?** This
   is where the "upgrade" feeling goes now that ranks are gone, and it may turn
   out to be all the depth the run needs. Worth prototyping before writing sixty
   commons that might want to be upgrade paths.
3. **One legendary per run, or one per family?** *Per run* is implemented
   (`CAP_LEGENDARY`), because §2 already says so in as many words; per family
   remains the open alternative and is one constant away.
4. ~~**What the existing 69 become.**~~ **Done, 2026-09-01**, as one pass with
   the whole list in front of it. Sixteen promotions: fourteen epics (the four
   classless system-changers — Self-found, Deep Wounds, Killing Floor, Cursed
   Hoard — plus one card per class) and two legendaries (the Shade and Last
   Rites). All three cards this section named read as epics and all three are.
   The list is in `tests/RegistryTest.cpp`, so the seventeenth promotion is a
   deliberate edit. §7 of `docs/handoff.md` has what it measured and the two
   bugs it found.
5. **Does skipping pay a reroll charge**, or something else? Still the one
   number in this plan with no evidence behind it at all.
6. **The rarity weights in §2 are invented.** They are a starting shape for the
   sweep tool to argue with, not a result. The sweep has now argued back:
   `docs/commons.md` (2026-09-01) measures the delivered mix against them and
   finds the early run's rare floor is not a weighting problem at all but the
   reward-shaped guarantee — every one of the eleven rows carrying
   `MF_RewardShaped` is Rare, so one slot in three is reserved for a rare
   whatever §2 asks for. Re-cut the weights **after** that is fixed, not
   before.

## 8. Order of work

1. ~~Rarity as a field, rolled and displayed, with every existing card marked
   rare. Nothing else changes; the ladder still works. This is reversible and
   provable.~~ **Done, 2026-08-31.** `build/sweep --rarity` is the proof: 100%
   Rare at every tier, beside the weights §2 asks for.
2. ~~`SimpleTrade` and the first ten commons. Prove the table-driven path with
   the bench before writing sixty rows.~~ **Done, 2026-08-31.** Seven denials
   (Rules) and three coefficient trades (Attrition), ids 75–84. Measured with
   `build/sweep`: tier 1 is 61% common against the 70% asked for, and the late
   run's empty slots went from 12,418 to zero — but sets with no reward-shaped
   offer rose from 9% to 15%, because a common is neither reward-shaped nor
   rankable and a full set of them has fewer rank-ups to pay the guarantee
   with. That is §7.1's question arriving early; step 4 changes the late run's
   whole payer anyway.
3. ~~Reroll and skip, which are independent of both and immediately
   playable.~~ **Done, 2026-09-01.** `.gauntlet reroll` / `.gauntlet skip`,
   two chooser buttons, `REROLL`/`SKIP` on the wire, and `skip`/`reroll` rows
   in the affix log so the run's story records them.
4. ~~Rank removal, once rarity is carrying enough of the run to make it
   survivable.~~ **Done, 2026-09-01.** 79 ladders collapsed to one value each,
   `RankUp` gone, every offer a card the run does not hold. Measured: with no
   rank-ups to spend tiers on, the 16-card cap fills by tier 17–20 instead of
   ~49, so sixty of eighty tiers are three swaps; the reward-shaped guarantee
   is now paid by a swap of a reward-shaped card (without that, 42% of
   full-set offers had none; with it, 21%). §7.1 — the carry cap by rarity —
   went from an open question to the loudest one, and the loot cards of
   `docs/greed-redesign.md` §7 are the fastest way to refill the late run.
5. The remaining cards, in rarity order, commons first. **Begun 2026-09-01**
   with Magpie, Butterfingers and Night Owl (ids 85–87) and the loot boon they
   pay in. `docs/commons.md` measures where the next rows belong and
   overturns the obvious answer: the third slot of every early offer is a rare
   because of the reward-shaped guarantee, not because of family spread, and
   three reward-shaped non-rare cards are worth more than twenty ordinary
   rows. Its proposed batch — three mechanics, nine uncommons, ten commons —
   measures at 69/23/8 against §2's 70/25/5.

Steps 1–3 were all reversible and all playable on their own, which is why they
came before the deletion. Landing rarity first meant step 4 was a deletion with
a green gate around it rather than a rewrite with nothing holding the run up —
and so it was: the gate caught the guarantee's payer going missing, and
nothing else.
