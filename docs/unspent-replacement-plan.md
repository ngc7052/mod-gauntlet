# Replacing Unspent (C42, id 69)

Requested after a live report: *"Unspent should be redesigned, this is wrong
idea and shouldn't be implemented, instead do something else."*

This is the plan, not the change. Nothing below is implemented.

---

## 1. Why the call is right

**The card.** *You receive a talent point every second level, and each unspent
point makes you weaker.* Shipped ladder: a point every 1½ / 2 / 3 levels.

Four things are wrong with it, and they compound.

**It is a character-sheet tax, which is the thing this redesign exists to
delete.** `docs/affix-design.md`'s whole argument against the four original
scalars — Exposed, Feeble, Withering, Forgetful — is that a number on your sheet
is not a mechanic: there is no button that answers it and no moment where you
play around it. Fewer talent points is the purest possible instance. It is
Feeble with extra steps.

**Half the card does nothing.** "Each unspent point makes you weaker" assumes a
player who banks points. Nobody does. In practice the penalty clause never
fires, so the affix is *only* the talent-rate cut, and the "makes you weaker"
half is flavour text describing a state the game never reaches.

**So it is close to a free boon.** The screenshot that prompted this shows
Unspent III paying **+24% damage** next to a downside the player cannot feel
minute to minute. An affix whose price is invisible and whose payment is a flat
damage number is a strictly good pick, and a strictly good pick is not a choice.

**It silently sabotages other affixes.** Class curses assume the class kit —
Faithless Form assumes Shadowform, Cold Presence assumes presences, Bound Skin
assumes forms. Taking talent points away weakens the *counterplay* to every
other curse in the run, invisibly, which is the worst kind of interaction: it
makes other affixes harder without saying so.

## 2. What the replacement has to be

| Constraint | Why |
|---|---|
| **Classless** (`classMask = 0`) | Unspent is the only classless row in family C. Dropping that leaves nothing generally available in the family, and the table is thin enough already. |
| **A mechanic with counterplay** | A button or a movement that answers it, per the design's own bar for the family. |
| **`MF_RewardShaped`** | See §4. This is the single highest-value change measured this phase. |
| **A wide tier window** | Unspent's 10–40 is narrow; 10–80 is what the measurement below assumes. |
| **No new spell ids, no client patch** | Standing constraint. |
| **Built on hooks the module already dispatches** | Anything else is a phase, not a row. |

## 3. The id

`69` is **retired, not reused**. `src/GauntletRegistry.cpp` states the rule —
"a stored `gauntlet_affix` row from any past run must still resolve to the same
mechanic" — and 21, 22, 72 and 73 are already holes for the same reason. The
replacement takes **74**, and `MECHANIC_UNSPENT = 69` joins `MECHANIC_WITHERING`
and `MECHANIC_FORGETFUL` in `Gauntlet.h` as a spent number.

A migration is needed because a character is carrying id 69 right now. Deleting
the row frees the slot, and it takes the boon with it — which is the point.

```sql
-- data/sql/db-characters/updates/<date>_gauntlet_retire_unspent.sql
DELETE FROM `gauntlet_affix` WHERE `mechanic` = 69;
```

`GeneratorVersion` bumps with the table, `Data.lua` regenerates,
`Protocol.lua`'s `PROTOCOL_VERSION` follows.

## 4. The measurement that decides the shape

`build/sweep --seeds 300` (240,000 offer sets), with id 69 made
`MF_RewardShaped` and its window widened to 10–80 and **nothing else changed**:

| | now | with one classless reward-shaped row |
|---|---|---|
| sets that relaxed any rule | 48.26% | **40.97%** |
| sets with no reward-shaped offer | 36.90% | **28.72%** |
| relaxation that is *only* the reward guarantee | 16.02 pts | **8.72 pts** |
| tier 11 relaxed | 3.37% | **0.00%** |
| tier 21 relaxed | 46.67% | **12.70%** |
| tier 21 with no reward-shaped offer | 36.50% | **5.40%** |

The midgame hole essentially closes, from one row.

The reason is in `docs/phase-5-progress.md`: ten of sixty-nine rows carry
`MF_RewardShaped` and six are gated behind a class or the Bargain family, so a
character has **four** generally available — Carrion, Hubris, Champions,
Frenzy — and two of those expire at tier 50. A fifth, classless, in a window
that runs to 80, is worth more than twenty-one more curses were.

So whatever replaces Unspent should be reward-shaped, and that constrains the
design: by the design's own test it has to be a **bargain in structure — it pays
out for engagement**, the way Frenzy, Champions, Echo, Carrion and Hubris do.

## 5. Two candidates

Both are classless, reward-shaped, and built on hooks this module already
dispatches. Both need a family decision: family C is "a curse written for one
class specifically", which Unspent never was, so the replacement is a chance to
put the row where it belongs.

### A · Overdraw — family Tempo

> *Abilities used within three seconds of one another stack Overdraw. Each
> stack is +4% damage dealt and +4% damage taken. Stop, and it falls away a
> stack at a time.*

- **Ranks.** Per-stack 3% / 4% / 5%, cap 5 / 7 / 10 stacks.
- **Counterplay.** Pace the rotation, or commit and end the fight before the
  stacks end you. Both are real decisions and neither is a button press.
- **Payout.** The stack *is* the payout — the same clean shape the design
  praises in Frenzy and Berserker's Bargain, where the upside and the price are
  two halves of one sentence rather than a bolted-on boon.
- **Hooks.** `OnSpellCast` to add a stack, `AggregateFactor(DamageTaken)` for
  the price, `DamageDoneMult` for the payout, `OnTick` for decay, `QueueStat`
  for the HUD counter. Every one already in use.
- **Risk.** Frenzy-adjacent in feel. They are genuinely different axes — Frenzy
  counts kills, this counts casts — and the aggregate caps clamp the pair, but a
  player carrying both will read them as one idea twice.

### B · Killing Floor — family Attrition

> *You cannot be healed while something you have wounded is still alive. Every
> kill releases a burst of healing instead.*

- **Ranks.** Kill-burst 10% / 8% / 6% of maximum health; at rank III the block
  lingers ten seconds after the last wounded enemy dies.
- **Counterplay.** Finish what you start. Do not leave a runner alive. Break
  combat properly before you try to top up.
- **Payout.** The roguelike loop — Hades, Dead Cells — where healing comes from
  killing rather than from resting, which is exactly "pays out for engagement".
- **Hooks.** `IMechanic::OnHeal` for the block (Phase 3 added it; Last Rites
  already uses it for an absolute ceiling), `OnKill` for the burst. Both in use.
- **Risk.** Food and drink go through `SPELL_AURA_MOD_REGEN`, not
  `ModifyHealReceived`, so eating is not blocked. That is survivable — you would
  not be eating with a wounded enemy alive anyway — but it must be said in the
  blurb rather than discovered.
- **Note.** The stronger version of this — *health does not regenerate out of
  combat at all* — **cannot be built.** `Player::RegenerateHealth()`
  (`$CORE/src/server/game/Entities/Player/Player.cpp:2026`) has no script hook,
  and the only way to fake it is to re-set health every tick, which would
  visibly stutter the bar. Same class of problem as Rune-starved's undelivered
  boon.

## 6. Recommendation

**B, Killing Floor.** Overdraw is the safer build and the duller row: it is a
second stacking damage mechanic in a table that has Frenzy, and its whole
existence is numbers going up and down. Killing Floor changes what the player
does — it turns every runner into a problem and every kill into a heal — which
is what family D is for and what "a mechanic, not a multiplier" means.

It is also the better fit for a hardcore module. Healing is the resource a
one-life run is actually managing, and nothing in the table currently touches
it except Deep Wounds' ceiling and Last Rites' mark.

## 7. Order of work, once the shape is chosen

1. Retire 69: `MECHANIC_UNSPENT` reservation in `Gauntlet.h`, row deleted from
   the registry, the hole recorded in the registry's own comment, `OFFERABLE`
   in `tests/RegistryTest.cpp` updated, `src/mechanics/class/Common.cpp`'s
   Unspent class and its anchor deleted, `OnTalentPoints` left in place — it is
   a generic dispatch point now and the next curse to want it should find it.
2. The SQL migration, and a rebuilt `ac-db-import` (that container runs the SQL
   baked into its image).
3. The new row at 74, its mechanic file, its anchor, its `OFFERABLE` entry.
4. `GeneratorVersion` bump, `Data.lua` regenerated, `Protocol.lua` version.
5. Re-run the sweep and confirm the numbers in §4 against the real row.
6. A checklist entry in `docs/checklists.md` §6 or §5.
