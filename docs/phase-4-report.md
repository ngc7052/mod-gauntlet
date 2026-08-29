# Phase 4 report — the class family

Branch `feature/affix-redesign`. Ten commits, `b960fb9` through the report.

---

## 0. Definition of done, item by item

| Asked | Result |
|---|---|
| Wave A's 21 curses offerable, implemented, anchored | **yes** — 48 mechanics registered, all anchored |
| The two class bargains | **yes**, and they spend the seam Phase 0 opened |
| Three helpers exist and are used rather than reimplemented | **yes** — all three, by nine curses between them |
| `PermanentCooldown` verifiably greys a client button | **reads correct; not seen on a screen** (§5) |
| Each curse passes its checklist on its class | **not done** — see §5 |
| Tier windows gone through deliberately, sweep re-measured | **yes** (§3.1, §4) |
| Sweep passes with no relaxation where it passed before | **yes**, and improves everywhere above tier 40 |
| Unit tests pass | **105 pass** |
| Worldserver starts clean | deployed at the end of the session |

---

## 1. What was built

**Step 1, the three primitives** (`b960fb9`). `PermanentCooldown`, `SelfControl`,
`AuraDurationEdit`, in `src/mechanics/class/`. Plus `OnAuraApply` wired, which
had been declared on `IMechanic` since Phase 0 and dispatched from nowhere.

**Step 2, wave A** — 21 curses across ten classes, one commit per class:
warrior `9b6b16c`, paladin `ca3ee88`, hunter `0341882`, rogue `aeb2d56`,
priest `8bfa5c5`, death knight `5d7bf68`, shaman `c87b51c`, and
mage/warlock/druid/Faint `29fd547`.

**Step 3, the class bargains** (`0fda74b`). Ankh Pact and Stone of the Damned.

**Step 4, the tier windows** (`6d6be99`). And the thing behind them; see §3.1.

**Step 5, conducts.** Already built. Phase 0 wrote the loop in `Mgr::EndRun`
against a Class family that was entirely `MF_NotImplemented`, so it held an
empty string for four phases — and was written anyway so the column would not
be left behind when the family landed. It was not. Only the comment changed.

### New dispatch points

Four, all of them because a card asked for something the module could not
express:

| Point | For | Note |
|---|---|---|
| `OnPetDamage` | `Boon::BonusPetDamage` | In the enum since Phase 0 with nothing able to pay it |
| `OnShapeshift` | Bound Skin | Declared since Phase 0, dispatched from nowhere |
| `WillBuyDeath` / `OnResurrect` | the two class bargains | The seam Phase 0 left open |
| `OnAuraApply` | Long Forbearance, Frail Soul | Declared since Phase 0, dispatched from nowhere |

---

## 2. Build and test commands

```
$ ./tests/compile-check.sh
ANCHOR  PASS  48 registered mechanic(s), every one anchored in AnchorMechanics()
COMPILE PASS  56 object(s)
LINK    PASS  56 objects, no duplicate definitions
```

Warning-free. 41 objects at the start of the phase, 56 now.

```
$ ./tests/run-tests.sh
[  PASSED  ] 105 tests.
```

`Data.lua` regenerated through the standalone exporter after every registry
change. One SQL migration, `2026_08_29_02_gauntlet_risen.sql`, applied with a
rebuilt `ac-db-import`.

---

## 3. Deviations and findings

### 3.1 The tail was a Phase 0 bug, not a window problem

**The largest finding of the phase, and it corrects something I wrote myself.**

`docs/phase-4-prompt.md` says the rows expiring at tier 70 do so because a
`maxTier` of 14 was the old axis's `TODO(design)` default. That is wrong. The
cards state their windows — "Tiers 3–14" for Red Mist and most of family C — so
14 × 5 = 70 is the design speaking. What is actually wrong is that the design's
tier curve was written for sixteen tiers and does not stretch: "not the last
eighth of the run" was two thin tiers on the old axis and is ten empty levels
on this one.

So thirty-three rows that closed at 70 now close at 80. **That measured as no
improvement whatsoever**, which is what sent me looking further.

Every class row carried the exclusive key `"classcurse"`, and exclusivity in
this generator means no two carried affixes may share a key. **So a run could
carry exactly one class curse, ever** — the other forty-four were unreachable
the moment the first was taken, whatever their windows said. Forty-four rows
that the whole of Phase 4 exists to add, gated behind a limit of one.

The design does not ask for that. Its rule is "never pay twice" and its examples
are pairs: Divine Shield, shapeshifting, kiting. Those three keep their keys, the
blanket one is gone, and how many a run may carry is now a cap — `CAP_CLASS = 3`
against the design's four per class, leaving the fourth a live offer rather than
the only thing left to take.

### 3.2 A boon that no hook can pay is not promised

Two cards ask for something the core has no seam for.

**Rune-starved**: "runic power decays 50% slower". That decay is inside the
core's own rune tick with no hook on it. The boon is not delivered *and the
blurb does not mention one*. Saying nothing is better than promising a number
nothing pays, which is the fault this whole redesign exists to remove.

**Faint**: "+15% mana regeneration while casting". There is no hook on the
five-second rule, so it is paid as a small per-second top-up while in combat —
the same thing from the player's side, and the blurb says "while fighting"
rather than "while casting", because that is what it does.

### 3.3 Three registry boons changed to match their cards

Deafening Roar and Long Forbearance were `BonusRegen`, which would have had the
offer promise a regeneration percentage the mechanic never pays; both are
`BonusAbility` now, which is what a bespoke upside to a named ability is for.
Berserker's Bargain and Long Forbearance also needed `BoonTable` overrides,
because their cards state one flat number where the category would have laddered
three.

### 3.4 `Boon::BonusAvoidance` is a full avoid, not dodge

There is no server-side way to add flat dodge without applying an aura, and an
aura needs a spell id whose DBC tooltip would then describe something else — the
cost Falling Sky's speed buff already pays. So Exposed Back's boon does what a
dodge does, which is that the blow deals nothing, and the wording says "avoid"
rather than "dodge" because the combat log will read as a zero.

### 3.5 A "cheaper ability" boon is a refund

The cost is taken in `Spell::TakePower`, which runs before
`OnPlayerSpellCast`, so by the time this module hears about a cast the mana is
gone. The bar lands where a discount would have left it and the player cannot
tell the difference.

### 3.6 One new creature, and no client patch

Entry 900006 `Risen`, for Grave Call. Display 570 is Slavering Ghoul's, in use
by creature 1791 in the world today, so it is present in
`CreatureDisplayInfo.dbc` on any client that can see Duskwood.

### 3.7 Smaller ones

- **One Totem culls on the tick, not at the cast.** The new totem is not in its
  slot until the spell effect has run, so a cull at `OnPlayerSpellCast` removes
  the three already standing and then lets the fourth land — right by accident,
  and wrong whenever the cast fails.
- **Asking and paying are separate for the bargains.** `WillBuyDeath` runs from
  the resurrection veto, before the core has committed to anything; a charge
  spent there would be spent on a resurrection that never happened.
- **Stone of the Damned records the killer on every blow**, not on the killing
  one: by then the attacker may have wandered off or despawned. It is not
  persisted, and a run with nothing remembered gets that second life free.
- **Bound Skin's boon is applied in `OnMaxHealth`**, because it is gated on
  being shapeshifted and `AggregateFactor` is Player-free.
- **Blink's and Blood Magic's self-damage share `RunState::selfDamage`**, so
  neither makes a Deep Wound nor spends a Last Rites charge.

---

## 4. The sweep

160,000 live sets, 2,000 seeds × 10 classes × 80 tiers.

```
tier          1    11    21    31    41    51    61    71
before     0.00  4.65 51.35  4.45 62.05 91.95 98.90   100
after      0.00  4.65 51.35  4.45 30.30 56.20 70.40  99.50
```

| | before | after |
|---|---|---|
| empty offer slots | 179,972 | **96,225** |
| sets with no reward-shaped offer | 44.6% | **36.9%** |

Everything above tier 40 improved and nothing below it moved, which is what
should happen: the class family is gated behind a class and unlocks late.

**Tiers 76–80 are still empty, and that is now an honest pool limit rather than
a gate.** A run there carries sixteen of roughly twenty-two rows its class can
be offered, and the rest are carried too. Closing it needs wave B's
twenty-three curses, a higher `MAX_RANK`, or a tier axis that stops before 80 —
and that is a design decision, not a bug to fix.

---

## 5. What does not work, and what is unverified

**Nothing has been seen on a screen.** Everything below reads correct, compiles,
links and passes its tests, and none of that is a playtest.

**In priority order for a first session:**

1. **`PermanentCooldown` greying the button.** The plan says it has been shown
   to work; I verified the parameter that does it (`needSendToClient`,
   `Player.h:1825`) but not the effect. If it does not grey, five curses in wave
   A need a different shape. Test with `.gauntlet debug give 40 3` on a rogue —
   Vanish should go grey and stay grey through Preparation.
2. **The two bargains.** `.gauntlet debug hurt 100` to die, then Reincarnation
   or a Soulstone. The run should survive, and `.gauntlet status` should show
   every boon at zero (Ankh Pact) or something standing next to you (Stone).
3. **Half-Tamed and Fel Pact turning a pet hostile.** `RemovePet` then a summon
   of the same entry — the copy should attack and the real pet should be
   callable afterwards.
4. **Grave Call's Risen**, which is the one new creature template.
5. **One Totem's cull**, and whether the tick-based timing looks instant.
6. **The spell ids.** Most could not be confirmed from this machine, because
   3.3.5 spells live in DBC rather than SQL. A wrong id means that one curse
   silently stops reacting to that one spell — not a crash. Every curse's
   `Diagnose()` reports enough to tell.

**Known-imperfect, by decision:** Rune-starved's undelivered boon (§3.2); Faint's
approximated one; the tiers 76–80 tail (§4); `docs/checklists.md` still unwritten.

---

## 6. What Phase 5 should know

1. **Wave B is twenty-three curses and the same shape.** The three primitives
   cover almost all of them; `class/Common.cpp` is where Unspent belongs.
2. **`CAP_CLASS = 3` is a `TODO(design)` number.** The design gives each class
   four. Three leaves the fourth a live offer; four would let a run be entirely
   defined by its class, which may be the better game.
3. **The exclusive-key mechanism is powerful and was misused once.** A key on
   every row of a family is a limit of one, not a family cap. Check any new key
   against that.
4. **Four dispatch points added this phase are generic**, not class-specific:
   `OnPetDamage`, `OnShapeshift`, `WillBuyDeath`/`OnResurrect`, `OnAuraApply`.
5. **`.gauntlet debug hurt <percent>` is how a death path gets tested** without
   staking a run on the answer.
6. **The 36.9% of sets with no reward-shaped offer is the number to beat.** It
   is the clearest single statement of how thin the table still is for eighty
   tiers.


---

## 7. Addendum — wave B, and the family is finished

Wave A was the design's build-priority A. Wave B is the other twenty-one, and
they landed in the same session: `af76b48`, `2745c5a`, `e5865cf`, `a048be8`,
`ff39afa`.

**Sixty-nine of sixty-nine rows now have an implementation.** Four phases after
the registry was written as pure data with nothing behind any of it, no row
carries `MF_NotImplemented` and every one can be offered.

### What wave B needed that wave A did not

Three more dispatch points, all for the same reason as wave A's four — a card
asked for something the module could not express:

| Point | For |
|---|---|
| `OnPetDamaged` | Blood Bond — the mirror of wave A's `OnPetDamage` |
| `OnPeriodicTick` | Poisoned Blades, Affliction of the Self, Blood Bond's boon |
| `OnTalentPoints` | Unspent — declared since Phase 0, dispatched from nowhere |

That is five Phase 0 seams closed across the phase: `OnAuraApply`,
`OnShapeshift`, `OnResurrect`, `OnTalentPoints`, and `Boon::BonusPetDamage`.

### Three implementations that are honestly narrower than their card

- **Penance of Silence** applies a stun, not a silence. There is no `UNIT_STATE`
  for silence — it is an aura mechanic — and applying one needs a spell id whose
  tooltip would then describe something else. A stun of the same length is a
  heavier price than the card asks for, and the file says so.
- **Shard Economy** implements only the second half of its card. Shards from
  under-level enemies are taken back, since the core has already created the
  item by the time any kill hook runs; the summon-and-Healthstone cost is not
  implemented, because it would double-charge a warlock also carrying Fel Pact.
- **Cold Presence** does not deliver its "+25% presence effects", which lives
  inside each presence's own aura with no seam to reach. Like Rune-starved, the
  blurb does not promise it.

### The measurement, and what twenty-one more curses did not fix

```
tier              41     51     61     71     80
wave A end     30.30  56.20  70.40  99.50    100
wave B end     27.50  53.05  70.95  99.55    100

empty offer slots   96,225 -> 86,916
```

**Tiers 78–80 are unchanged at 100%, and that is the finding.** Twenty-one more
curses moved the top of the run by nothing, which is much stronger evidence than
§4 had that the tail is structural rather than a content shortage. A run at tier
78 carries `MAX_CARRIED` = 16 of the roughly twenty-five rows its class can be
offered, everything carried is at rank III, and the handful uncarried are not in
window. Adding rows to the table cannot fix that; only one of three things can:

1. **Raise `MAX_RANK` above 3.** Every mechanic's rank table grows, which is 69
   files, but it turns the late run into deepening rather than collecting.
2. **Raise `MAX_CARRIED`.** Cheapest, and it makes the aggregate caps bite
   harder — the reason the cap exists at all.
3. **Stop the tier axis before 80.** A tier per level to 70, then nothing, is an
   honest statement that the run's building phase is over.

That is a design decision and is left for the user rather than taken here.
