# The commons, the uncommons, and the early mix

**A proposal, not a decision.** `docs/rarity-plan.md` is settled and this is
not: it is the measurement behind step 5's next batch and a list of cards to
cut before any of it becomes rows. The measurement is the part worth keeping
if the card list is wrong.

**Steps 1, 2 and 3 of §5 have landed (2026-09-01).** The three reward-shaped cards of
§3.1 are built — Scavenger's Eye (88), Blood for Bread (89), Waste Not (90) —
and the live table now delivers **58% common / 34% uncommon / 7% rare** at
tier 1, against 50/10/40 before them and a 70/25/5 target. The variant sweep
predicted 57/36/7 for three such rows, which is as close as this kind of
forecast gets. Everything below is unchanged apart from that: the nineteen
table rows of §3.2 and §3.3 are still proposals, and the mid-run is still
short of them — tier 21 delivers 40/8/51.

**Step 3 has landed too**, so the whole batch this document proposed is built:
tier 1 delivers **61% common / 25% uncommon / 13% rare** against 70/25/5, with
uncommon exactly on target. The remaining gap is not more commons. Rare runs
over at every tier because epics and legendaries do not exist, so their weight
renormalises into rare — 25% of it at tiers 61–80 — which is the epic pass of
`docs/rarity-plan.md` §7.4, not this document. At tier 1, where both weigh
zero, the eight points of excess rare are the reward-shaped guarantee landing
on a rare whenever the three non-rare ones are carried or ineligible; a fourth
would be worth more than any number of ordinary rows, exactly as §0 measured.

One card of the ten was nearly a lie: two were drafted paying `Boon::BonusRegen`,
which has no generic delivery — `Pays()` does not map it and the two class
cards that name it pay it themselves on a tick — so the row would have printed
"you recover 15% faster" and done nothing.

**Step 2 landed before it.** The nine uncommons of §3.2 are in as ids 91–99, and
tier 1 now delivers **52% common / 38% uncommon / 10% rare**. The uncommon
share overshoots its 25% target and the common share fell from 58% to 52%,
which is exactly the half-batch shape §1's table shows: the two halves compete
for the same three slots, and the ten commons of §3.3 are what settle it. The
weights in `docs/rarity-plan.md` §2 should not be re-cut until they land.

Two things §3.2 got wrong and the build corrected. **Saddle-sore cannot curse
max health**: it is a standing stat and the core only rebuilds it on level and
stamina changes, so a conditional pool would move at the next level-up rather
than when the player mounted — Lone Wolf survives only because
`Mgr::OnGroupChanged` refreshes stats by hand, and there is no mount hook to
copy. It is 25% more damage taken while mounted instead, which is the better
card. And the bench attaches a conditional trade **ungated** (`AuditAttach`),
which is the right call for coverage but has to be said, or a by-day card
benched at night reads as broken gating; the per-card summary now says it.

Written 2026-09-01, after the first three loot trades landed and
`build/sweep --rarity` said tier 1 delivers **50% common / 10% uncommon / 40%
rare** against the plan's 70 / 25 / 5.

---

## 0. The finding, first

The early run is not short of commons. It is short of **reward-shaped cards
that are not rare**.

Every offer set must contain one card flagged `MF_RewardShaped`
(`src/Gauntlet.h:162`, guaranteed at `GauntletGenerator.cpp:810-824`, design
§4.4.5). Eleven rows carry the flag and **every one of them is Rare**. At tier
1, filtered by tier window and class mask, exactly **three** can satisfy the
guarantee for an arbitrary character:

| | Family | Tiers | Rarity |
|---|---|---|---|
| Carrion | Spawn | 1–50 | Rare |
| Champions | Enemy | 1–80 | Rare |
| Hubris | Tempo | 1–50 | Rare |

So one slot in three is reserved, at tier 1, for one of three rare cards. That
is the 40%. No number of commons changes it, and the sweep says so: **fifty-two
hypothetical commons spread over all seven families left tier 1 at 54%.**
Twenty ordinary rows move it to 49%. Three reward-shaped non-rare rows move it
to 60%.

The registry already recorded a version of this. The note above Killing Floor
(`src/GauntletRegistry.cpp:554`) says the module "had four generally-available
`MF_RewardShaped` rows for eighty tiers, two of which expire at 50, and one
more classless row with the flag measured as worth more than twenty-one more
curses." That was measured before rarity existed. Rarity did not fix it; it
made it visible, because the flag now decides a *rarity* as well as a slot.

## 1. What was measured

`tests/tools/sweep_standalone.cpp` against a copy of `src/` with hypothetical
rows added — the variant recipe in `tests/tools/README-sweep.md`. 400 seeds ×
10 classes × 80 tiers per run; the numbers are common/uncommon/rare **as
delivered**, in percent. 400 seeds rather than the committed sweep's 2000, so
every figure carries about a point of noise — the "today" row reads 51/9/40
where the 2000-seed run reads 50/10/40. Differences of a point mean nothing
here; the differences that matter are twenty.

| Variant | tier 1 | tier 5 | tier 10 | tier 20 | tier 40 | tier 70 |
|---|---|---|---|---|---|---|
| today (82 rows) | 51/9/40 | 43/6/51 | 38/4/58 | 42/3/54 | 36/4/60 | 27/5/68 |
| +12 commons, four families | 54/11/34 | 60/7/33 | 60/3/37 | 74/3/23 | 49/4/48 | 30/5/65 |
| +52 commons, all families | 54/11/34 | 60/7/33 | 60/4/37 | 75/3/23 | 49/4/47 | 30/5/65 |
| +8 uncommons | 38/27/34 | 38/28/34 | 30/24/46 | 37/25/38 | 26/24/49 | 13/30/58 |
| +12 commons, **flagged reward-shaped** | 79/16/5 | 87/7/6 | 80/3/17 | 76/3/21 | 65/4/31 | 28/5/67 |
| **the proposal: 20 rows** | 49/16/34 | 48/20/32 | 34/17/48 | 45/19/37 | 30/21/49 | 13/29/58 |
| **the proposal: 3 reward-shaped only** | 60/33/7 | 57/13/30 | 50/6/44 | 45/7/47 | 42/9/49 | 25/10/65 |
| **the proposal: both** | **69/23/8** | 59/26/16 | 49/24/27 | 47/22/31 | 37/26/37 | 17/29/54 |
| *target* | *70/25/5* | *70/25/5* | *70/25/5* | *70/25/5* | *45/35/18* | *10/25/40* |

Three readings:

1. **The rare floor is the guarantee, and only the guarantee.** Row count does
   nothing to it (52 rows: 34%). Flagging moves it in one step (12 flagged
   rows: 5%).
2. **Uncommons are one card.** Eight hypothetical uncommons take the tier-1
   uncommon share from 9% to 27%, which is the target, and nothing else does.
3. **Rows still matter, later.** The reward-shaped rows alone collapse by tier
   10 (50/6/44) because there is nothing non-rare left to fill with. Both
   halves together give a ladder that holds its shape to tier 70.

**Caveat the numbers deserve.** Every hypothetical row is classless, tier
1–80, one boon, no exclusive key and no gate — maximally offerable. Real rows
carry class masks and narrower windows, so these are an optimistic bound. The
*shape* of the finding survives that; the exact percentages will not.

## 2. What a trade line can express today

`TradeDef` (`src/GauntletTrades.h`) has three curses and, since Night Owl, a
condition. That is the whole vocabulary of a table-row card:

| Primitive | Used so far | Left |
|---|---|---|
| `DenyInventoryType` | head, neck, finger, trinket, cloak, waist | **shoulders, chest, legs, feet, wrists, hands, shield** |
| `DenyWeaponSubclass` | axe, sword | **mace, polearm, staff, dagger, fist, bow, gun, crossbow, wand, thrown** |
| `Coefficient` | DamageTaken, DamageDone, HealTaken, MaxHealth | **Experience, EnemySpeed** |
| `Condition` | AtNight | AtDay, InCombat, OutOfCombat, Below/AboveHalfHealth, WhileSolo/Grouped, In/OutDungeon, WhileMoving/Stationary/Mounted |

Two families are **not** reachable this way and should not be forced:

- **Spawn** needs a creature in the world. That is a mechanic, not a line.
- **Bargain** needs a payoff, and the family is shut until tier 30 anyway.

And one is reachable but should be **avoided**: a `Family::Class` row counts
against `CAP_CLASS` (`GauntletGenerator.cpp:74`, checked at `:278`), the run's
budget of three class curses. A small trade must not spend that budget.
Axeless and Swordless are the precedent and they are right: **class-masked,
filed `Rules`.** So "Class has no common" is not a gap; it is correct.

That leaves Rules, Attrition, Enemy (through `EnemySpeed`) and Tempo (through
the movement conditions) as the families a trade line can honestly wear. Tempo
has no *unconditional* shape — a flat coefficient is not tempo — so every
Tempo trade is an uncommon.

## 3. The cards

### 3.1 The three that matter most (mechanics, not lines)

These carry `MF_RewardShaped`, and the flag is not a decoration: it means *the
card's own mechanic hands you something when you engage with it*, which is
what Champions ("double the reward"), Cursed Hoard ("chests give twice as
much") and Killing Floor ("a kill hands it back") all do. A standing
coefficient with a boon attached is **not** reward-shaped, and flagging one
would be a lie that the guarantee then tells every tier.

| Card | Rarity | Family | The card | Seam |
|---|---|---|---|---|
| **Scavenger's Eye** (88) | Uncommon | Enemy | Enemies notice you from five yards further. A fight in which nothing lays a hand on you rolls its loot twice. | **built** — `AlertUnaware` in `Nearby.cpp`, shared with Keen-nosed; `OnDamageTaken` dirties the fight; a second `Loot::FillLoot` in `OnLoot`, which appends (`LootMgr.cpp:561`) |
| **Blood for Bread** (89) | Common | Rules | You cannot eat or drink. Every kill restores 8% of your health and mana. | **built** — `CanUseItem` on `ITEM_SUBCLASS_FOOD`; `OnKill` |
| **Waste Not** (90) | Common | Rules | You cannot drink a potion. Every kill restores 5% of your health. | **built** — `CanUseItem` on `ItemTemplate::IsPotion`; `OnKill` |

Three things the build settled that the proposal did not:

- **The item-use veto did not exist.** `IMechanic::CanUseItem` and
  `Mgr::CanUseItem` are new, mirroring the equipment veto exactly, wired to
  `PlayerScript::OnPlayerCanUseItem`. Every right-click reaches it:
  `HandleUseItemOpcode` → `Player::CanUseItem(Item*)` → the `ItemTemplate`
  overload, which consults the hook (`PlayerStorage.cpp:2341`, `:2432`).
- **Keen-nosed's sweep is now shared.** `AlertUnaware(owner, bonusYards,
  searchYards)` lives in `Nearby.cpp`, which says in its own comments that it
  exists so a predicate cannot be copied into eight files and drift. Both
  cards call it and Keen-nosed's number moved to `GauntletRules.h`, which is
  what lets a test say "the uncommon notices you from less far than the rare".
- **The bench was blind to all three.** It had no item-use scan, no
  loot-window probe, and its footprint reads *max* health rather than current,
  so a kill that restores health moved nothing it could see. All three probes
  are added; the loot-window one is the one `docs/greed-redesign.md` §7.4 asks
  for by name, and it reaches Carrion too.

**One disagreement to settle.** `docs/greed-redesign.md` §6 names Blood for
Bread an epic candidate. This document says it is worth more as a common,
because a common carrying the flag is the scarcest thing in the table and an
epic carrying it helps nothing before tier 40. If it stays an epic, a third
small reward-shaped common has to be invented in its place.

### 3.2 The uncommons — nine trades with a condition

The shape the rarity plan's §2 gives the tier, and the cheapest real cards in
this document: nine lines in `GauntletTrades.h` and nine registry rows.

| Card | Family | While | The curse | Pays |
|---|---|---|---|---|
| **Sunstruck** | Attrition | AtDay | you take 10% more damage | +25% drops |
| **Skittish** | Tempo | WhileMoving | you take 15% more damage | +8% move speed |
| **Rooted** | Tempo | WhileStationary | you take 15% more damage | +10% damage |
| **Saddle-sore** | Tempo | WhileMounted | you have 25% less health | +10% move speed |
| **Crowd-shy** | Rules | WhileGrouped | you take 15% more damage | +20% experience |
| **Delver** | Rules | InDungeon | you take 15% more damage | +30% drops |
| **Cornered** | Attrition | BelowHalfHealth | healing on you is 25% weaker | +15% damage |
| **Fresh Legs** | Attrition | AboveHalfHealth | you deal 10% less damage | +10% max health |
| **Outlander** | Enemy | InOpenWorld | everything chasing you is 15% faster | +15% experience |

Sunstruck is Night Owl's twin on purpose: together they cover the clock, and a
run that carries both has traded the whole day for drops. Skittish and Rooted
are the same trade pointed opposite ways, which is the closest a table row
gets to a tempo decision.

### 3.3 The commons — ten more trades

| Card | Family | The curse | Pays | Line |
|---|---|---|---|---|
| **Shoulderless** | Rules | no shoulders | +8% damage | deny `INVTYPE_SHOULDERS` |
| **Barefoot** | Rules | no boots | +5% max health | deny `INVTYPE_FEET` |
| **Bare-handed** | Rules | no gloves | +10% healing | deny `INVTYPE_HANDS` |
| **Wristless** | Rules | no bracers | +15% regen | deny `INVTYPE_WRISTS` |
| **Shieldless** | Rules | no shield | +10% damage | deny `INVTYPE_SHIELD`, masked to the classes that can hold one |
| **Maceless** | Rules | no maces | +10% damage | deny `MACE`\|`MACE2`, masked |
| **Daggerless** | Rules | no daggers | +10% damage | deny `DAGGER`, masked |
| **Staffless** | Rules | no staves | +15% regen | deny `STAFF`, masked |
| **Hunted** | Enemy | everything chasing you is 10% faster | +8% damage | coefficient on `EnemySpeed` |
| **Slow Learner** | Attrition | you gain 15% less experience | +25% drops | coefficient on `Experience` |

Slow Learner is the one card here that fights the greed redesign's brief —
less experience is the anti-accelerant — and it is proposed anyway because
"level slower, gear better" is a real choice and the only honest use of the
`Experience` coefficient. Cut it if the brief wins.

Not proposed, deliberately: **no chest and no legs**. A denial has to be a
decision, and those two are a stat loss with no play behind them.

## 4. What lands in the tests

Not incidental — this is most of the work of a batch:

- `TABLE_SIZE`, `OFFERABLE` and `all.back().id` in `tests/RegistryTest.cpp`.
- The family bands and per-family counts in the same file; the new rows are
  Rules, Attrition, Enemy and Tempo, and the id ranges split again.
- `Registry.TheOriginalCardsAreRareAndEverythingAfterIsATradeLine` and
  `Trades.AnUncommonIsATradeWithACondition` both **become lists** the moment
  Scavenger's Eye lands: it is past id 74, it is an uncommon, and it is a
  mechanic with no trade line. Both tests say in their comments that this is
  expected; that is the day.
- `OfferInvariants.LiveRegistryView`'s per-tier ceilings, re-measured with the
  reason each moved. The noReward census should **fall** for the first time
  since the ranks went — three new reward-shaped cards is exactly what it has
  been short of.
- `build/sweep --rarity` re-run, and §2's weights re-cut against it if they
  still disagree. That answers the rarity plan's §7.6, "the weights are
  invented".

## 4b. The reward-shaped low end (2026-09-01)

§0 measured a shortage of non-rare reward-shaped cards and §3.1 fixed it with
three. Three more loot cards (`docs/greed-redesign.md` §7.3) then took the
worst of what was left: tier 21's epic overshoot from 14% to 5%, and the
"no reward-shaped offer" rate from 5.1% to 0.36%.

**One number did not move, and the reason is exact.** Tier 1 delivers 13% rare
against 5%. Two of those three loot cards are themselves Rare, so they joined
the pool tier 1 draws from rather than diluting it. Slot B's guarantee draws
from the reward-shaped cards *of a family none of the other two slots used*,
and at tier 1 that set is:

| Family | Reward-shaped, tier 1, classless |
|---|---|
| Rules | Blood for Bread (C), Waste Not (C), Fresh Kill (R) |
| Enemy | Scavenger's Eye (U), Trophy Hunter (U), Champions (R) |
| Spawn | Carrion (R) |
| Tempo | Hubris (R) |
| Attrition | Blood Price (R) |

Three families can only answer with a rare. Whenever slot A and slot C use
Rules and Enemy — which the commons make likely, since that is where they
live — slot B has no non-rare answer at all.

So the cards worth building next are **reward-shaped commons in Spawn, Tempo
and Attrition**, and they are worth more there than anywhere else in the table.
Two are proposed, both classless and open at tier 1:

| Card | Family | The card | Seam |
|---|---|---|---|
| **Scavenge** | Attrition | You take 10% more damage. Every corpse you loot restores 4% of your health. | a `DamageTaken` coefficient through `AggregateFactor`; `OnLoot` heals |
| **Gravedigger** | Spawn | Every 8th corpse you loot gets up again. Kill it and it drops what it was holding back. | `GauntletSummons` and `ENTRY_RISEN`; the core's own death path run by hand in `OnKill` |

**Both are built.** Gravedigger was held back one commit while the seam it
needs was checked, and it turned out to be the core's own death path rather
than anything new: `Unit::Kill` fills a dying creature's loot and then sets
`UNIT_DYNFLAG_LOOTABLE` only when that loot is not already looted
(Unit.cpp:14210-14220). `ENTRY_RISEN` carries lootid 0, so the core fills
nothing — and the card makes the same four calls in the same order, which the
bench confirmed by watching the thing it raised drop two items.

With both in, **tier 1 delivers 73/23/5 against 70/25/5** — rare exactly on
target, from the 13% three loot cards could not shift. The two families that
could only answer slot B with a rare now answer with a common, which is the
whole of what §0's measurement asked for.

Scavenge is deliberately the opposite of Blood Price: one makes looting cost
health and the other makes it restore health, and a run offered both is being
asked which kind of looter it is. That is the shape §2 of
`docs/rarity-plan.md` wants from a common — one small trade, one axis — with
the payout on the engagement rather than in a boon.

Tempo is left without one on purpose. Every reward-shaped tempo card that
suggests itself is Frenzy with smaller numbers, and a common that is a rare
with the volume down is the rank ladder again.

## 5. Order of work

1. ~~**The three reward-shaped cards.**~~ **Done, 2026-09-01.** Tier 1 went
   from 50/10/40 to 58/34/7. Blood for Bread landed as a Common, so the
   disagreement in §3.1 was settled that way; if play says otherwise it is one
   field to change.
2. ~~**The nine uncommons.**~~ **Done, 2026-09-01.** Uncommon went 34% → 38%
   at tier 1, past its 25% target, and common fell 58% → 52%. Half a batch
   looks like an overshoot; it is the other half missing.
3. ~~**The ten commons.**~~ **Done, 2026-09-01.** Common 52% → 61% at tier 1,
   uncommon settling to exactly 25%.
4. **Re-measure, then re-cut the weights.** Measured; **do not re-cut yet.**
   Rare is over target at every tier for a reason the weights cannot fix — the
   epic and legendary columns have no cards to draw from, so `RollRarity`
   renormalises their share into rare. Re-cut after §7.4's epic pass, or the
   weights will be tuned around an absence.

Step 3 is a day of rows. Step 1 was the one worth doing carefully and doing
first, and the measurement held: without it, twenty rows bought a 49% common
share at tier 1 and the run still opened with a rare in every set.
