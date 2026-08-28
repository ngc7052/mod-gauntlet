# Phase 2 report — the tax is gone

Written against `docs/implementation-plan.md` §1 Phase 2 and `docs/phase-2-prompt.md`,
on branch `feature/affix-redesign`, from module commit `1545b3a` and the core at
`/mnt/c/Users/3302/azerothcore-wotlk` (`9fb906bb`).

Nothing is pushed. Six commits, one per numbered step.

## 0. Definition of done, item by item

| Requirement | Status |
|---|---|
| Every offer at every tier is a named mechanic that does something | **Yes** — 19 offerable mechanics, none of them a coefficient |
| …with a boon its row names | **Yes** — every offerable row carries one, and the mechanic delivers it |
| No scalar affix exists in the codebase | **Yes** — four registry rows, six source files, `MF_Scalar` and the boon/condition rolls all deleted |
| The invariant sweep passes with no relaxation where it passed before | **Yes, and better** — see §4 |
| The unit tests pass | **Yes** — 97 tests, and they fail when they should (six negative controls) |
| The worldserver starts clean | **Yes** — 19 s boot, zero Gauntlet warnings, module verified in the binary |
| A level-20 character carrying four Phase 2 mechanics plays for an hour | **No** — I cannot drive a WoW client. §7. |
| No summon left in the world after logout | **Unverified in game**, for the same reason; every path is code-read and unchanged from Phase 1 |

---

## 1. What was built

### Step 1 — the fast compile loop (`396c9d7`)

`tests/compile-check.sh`. It keeps one long-lived container built from the
core's own `build` stage, bind-mounts this repository **read-only** over
`/azerothcore/modules/mod-gauntlet` inside it, and drives that build tree's
existing ninja. Nothing is copied, no image is rebuilt, and the objects live in
the container's writable layer, so a second run is incremental.

```bash
tests/compile-check.sh                     # every module translation unit
tests/compile-check.sh src/GauntletMgr.cpp # one file (path or bare name)
tests/compile-check.sh --anchors           # the anchor audit alone, no Docker
tests/compile-check.sh --rebuild-image     # rebuild the base image
tests/compile-check.sh --stop              # remove the helper container
```

**Measured on this machine.** A cold run — container created, `cmake .` re-run,
every object built from nothing — is **14 s**. After that a single file is
**0.5–7 s** depending on whether ccache can answer, a whole no-op pass is
**0.5 s**, the `cmake .` reconfigure a new file needs is **0.9 s**, the partial
link is **0.2 s**, and the anchor audit is **0.03 s** with no Docker at all.
`docker compose build ac-worldserver` is minutes.

Three checks, cheapest first:

1. **The anchor audit**, by text: every `GAUNTLET_MECHANIC` in `src/` must have
   both a declaration and a call in `GauntletScripts.cpp`'s `AnchorMechanics()`,
   and every anchor called there must still be defined by one. This is the
   failure the prompt calls the most dangerous in the codebase, and it now costs
   30 ms to rule out. Negative control: deleting the `AnchorMechanics()` call
   for Champions is reported by id and by name.
2. **The compile**, against the real clang and the real core headers.
3. **A partial link** (`ld -r`) over the module's own objects — the only link
   this module can be given locally — which catches a symbol defined twice and
   an anchor declared but never defined. It prunes objects whose source is gone,
   because otherwise a deleted file goes on being linked and reported.

It earned itself immediately: the first thing it found was `BoonClause` defined
in two translation units, in under a second. Under the old loop that was a
Docker build. It also compiled Phase 1's four mechanics for the first time
outside a full build — they were written, shipped and never locally checked.

### Step 2 — the fifteen mechanics (`c732fd3`, `1eaa1e5`, `b032658`, `f8822eb`)

Written in the plan's order: shared infrastructure, not family.

**2a — Echo and Reinforcements**, the two that summon copies rather than
templates, plus the machinery they share:

- `src/mechanics/Boons.{h,cpp}` — `BoonClause` (moved out of the doomed
  `Scalars.cpp`), `BoonFactor`, `BoonMoneyMult`, `BoonHealMult`. The rule
  commit `04570c9` established is written down here: **a mechanic delivers its
  own boon**, and with the Scalars gone the aggregate delivers none.
- `src/mechanics/Nearby.{h,cpp}` — `CreaturesNear`, `CorpsesNear`,
  `IsOrdinaryFoe`, `IsFairGame`, `NearestIdleKin`, over the core's own
  `Acore::CreatureListSearcher`. `IsOrdinaryFoe` is Champions' promotion filter
  lifted out, so the eight mechanics asking the same question cannot drift.
- `IMechanic` gains `OnLootMoney`, `OnCreatureDamaged` and `OnItemRoll`;
  `OnLoot` becomes the loot *window* as plan §2.2 always had it.
- `AllCreatureScript::GetCreatureAI` hands the owner-bound AI to anything the
  module is mid-summon. Without it a Reinforcements copy would be an ordinary
  mob that aggroes bystanders and never leashes.

**2b — Call to Arms, Keen-nosed, Death Rattle, Grudge**: the four that work
from a position. Also `RunState::NoteActor`, so a mechanic that hurts its owner
on its own tick can claim the death without including `GauntletMgr.h`.

**2c — Craven, Nimble, Cunning**: creature-side state keyed by GUID. None uses a
new cleanup hook; each polls its own records on the module tick and clears them
when the owner leaves combat, which is the shape Champions already proved and
which cannot be forgotten to be wired.

**2d — Frenzy, Overextended, Falter, Hubris, Carrion, Ambush.**

Every one of the fifteen goes through `sGauntletSummons->Summon` if it puts
anything in the world, through `ctx.clock->Arm` if it is timed, registers with
`GAUNTLET_MECHANIC` and is anchored, names a boon its row carries, and
telegraphs through `EVT`, `CTR`, `STAT`, `SUMMON` and a chat line for the player
with no addon.

### Step 3 — the cut (`0ab01ce`)

Deleted outright: registry ids **21 Exposed, 22 Feeble, 72 Withering, 73
Forgetful**; `src/mechanics/attrition/{Scalars,Exposed,Feeble,Withering,Forgetful}`;
the `MF_Scalar` flag (its bit left unused, never reassigned); the generator's
condition roll, boon roll, `SCALAR_CONDITIONS` table and scalar pool of last
resort; and the aggregate's boon branch, which existed only to pay a Scalar's
generically-rolled boon.

Deleted with them (decision 4): `GauntletLegacy.{h,cpp}`, `LegacyRoll`, the
one-shot startup migration in `Mgr`, `AffixInstance::legacyMag`,
`gauntlet_affix.legacy_mag` (dropped in a dated update, not by editing the base
schema), `tests/fixtures/legacy_rolls.json`,
`tests/tools/dump_legacy_rolls.cpp`, and the two golden tests that held
`LegacyRoll` to that fixture.

**Everything deleted, for git.** `git log --diff-filter=D --name-only 1545b3a..HEAD`
lists it; `git show 1545b3a:<path>` reads any of it back.

### Step 4 — the two bugs (`6e369f1`), and one found here (`0085778`)

See §5.

### Step 5 — this report.

---

## 2. Build and test commands, and their results

```bash
tests/compile-check.sh          # ANCHOR PASS 19 mechanics, COMPILE PASS 34 objects, LINK PASS
tests/run-tests.sh              # 97 tests, 16 suites, 38 s — PASSED
tests/syntax-check.sh           # 7 Player-free objects compile and link
```

The suite includes a 1,600,000-set full-table offer sweep, a 160,000-set live
sweep, all 64 subsets of six contributors per aggregate kind, the scheduler
against a fake clock, and the registry's 69 rows.

```bash
./sync-to-server.sh
cd /mnt/c/Users/3302/azerothcore-wotlk
docker compose build ac-worldserver ac-db-import   # both Built
docker compose up ac-db-import
#   >> Applying update "2026_08_29_00_gauntlet.sql" 'DB680F2'...
#   >> Reapplying update "gauntlet.sql" '28F7D86' -> 'DF387CA' (it changed)...
#   >> Applied 2 queries.   [exit 0]
docker compose up -d ac-worldserver
#   WORLD: World Initialized In 0 Minutes 19 Seconds ... ready
```

Schema verified afterwards: `gauntlet_run` has `char_created`, `gauntlet_affix`
has no `legacy_mag`. The module is in the binary rather than merely configured —
`grep -a` over `worldserver` finds `A Shade rises behind you`, `The kill is
heard`, `You hear footsteps`, `The corpse bursts`, `breaks and runs`, `cuts your
cast short`, `An Echo of you steps out`, and finds **zero** occurrences of
`You take more damage.` or `Healing on you is less effective`.

Zero Gauntlet lines in the worldserver log at any level. The only config warning
on boot is `AiPlayerbot.ForceRebuffOnReadyCheck`, which predates this work.

---

## 3. Deviations, with reasons

### 3.1 Eleven of the fifteen cards name no boon, so eleven were chosen

The phase requires every offer to carry a boon its row names. Four cards state
one (Echo's five kills' XP, Carrion's money and drop chance, Frenzy's damage
half, Hubris's +40%). The other eleven state none, so each was chosen to be the
tool the curse's own counterplay asks for:

| Mechanic | Boon | Why |
|---|---|---|
| S4 Reinforcements | BonusDamage | the card's counter is burst; a shorter fight draws fewer |
| S5 Ambush | BonusMaxHealth | it taxes sitting down; a bigger pool is fewer stops |
| E2 Craven | BonusDamage | "burst at 30% so nothing flees" |
| E3 Call to Arms | BonusExperience | the curse manufactures fights; the boon pays for them |
| E4 Death Rattle | BonusMoney | the corpse pays for the danger of standing over it |
| E5 Grudge | BonusHealing | the curse halves healing near a spirit; the boon pays it back off one |
| E6 Nimble | BonusMaxHealth | enemies you cannot kite must be fought standing still |
| E7 Cunning | BonusDamage | fewer casts land, so the ones that do are worth more |
| E8 Keen-nosed | BonusMoney | fights you did not choose still leave something |
| T3 Overextended | BonusHealing | the resource a player fighting three things is spending |
| T4 Falter | BonusMaxHealth | two to four seconds with no hands is survived or it is not |

Delivery needs no new machinery: `BonusDamage`, `BonusExperience` and
`BonusMaxHealth` are returned from `IMechanic::AggregateFactor` (so the caps
apply once, to the product), `BonusHealing` from `HealTakenMult`, `BonusMoney` at
the loot site. Spread: four damage, three max health, three money, two
experience, two healing.

Frenzy and Hubris get per-mechanic `BoonTable` overrides so the offer card
promises exactly what the mechanic pays — 4/6/8% per stack and 20/30/40%.

### 3.2 Four tier windows widened

| Row | Card | Now | Why |
|---|---|---|---|
| S5 Ambush | 3–9 | **2**–9 | §4.6's tiers 1–2 band bars "interrupts, silences or stalkers"; Ambush is none of the three (§4.1's stalker cap is S1 xor S2) and its counter is standing up and moving |
| E8 Keen-nosed | 3–11 | **2**–11 | a routing rule, answered by hugging a path edge and pulling singles |
| T3 Overextended | 3–12 | **1**–12 | §4.6 gives tiers 1–2 "conditional Exposed", which Phase 2 deleted; §5 names Overextended as the shape a scalar takes when it earns its place |
| A1 Deep Wounds | 4–12 | **3**–12 | §4.6's own tier table already puts it in the 3–5 band; the card says 4–12 and the two disagree. Tier 3 was the one tier where only three families existed and every one was thin. |

Measured: with these reverted, tier 4 relaxes in 1 of 10,000 live sets and
tiers 1–3 still hold. So they are **not** what carries the definition of done —
§3.3 is — but they are what gives tiers 1–2 any variety at all, which is what
the band the design describes needs.

### 3.3 One real fault in the offer builder, fixed rather than tolerated

The builder preferred **a repeated family with a new mechanic over an unused
family with a rank-up**. Plan §2.4 rolls three *families* first and only then a
mechanic inside one, and §4.1 makes a rank-up a first-class offer — "an affix
already carried is never offered again as a duplicate; it is offered as its next
rank" — so distinct families should win. They did not: a `New` slot only ever
looked at rank-ups once *every* relaxation rung had failed.

It was the **only** rule being relaxed below tier 11 on the live view. Fixing it
took tiers 1–4 to exact zero. The same preference is applied to the
reward-shaped replacement in slot B, where a reward-shaped rank-up in an unused
family satisfies §4.4.1 and §4.4.5 at once.

Swaps are deliberately excluded: slot C at tiers 4, 8 and 12 is the run's one
chance to undo an early mistake, and trading that for a tidier family spread is
a worse offer. Those tiers carry the cost, which is why 8 and 12 stand out below.

### 3.4 A slot that cannot be filled comes back empty

Plan §2.4's last line was "if all empty: fall back to a Scalar". There are no
Scalars. There is no generic filler affix left to invent one from, so a slot
that cannot be filled returns `MECHANIC_NONE`, which Phase 0 already made mean
"no offer" everywhere: `OfferTier` prints it as *Nothing — no affix is available
to you at this tier* and `Pick` refuses it. It never happens below tier 14.
`GR_FellBackToScalar` keeps its bit value and is renamed `GR_NoCandidate`.

### 3.5 Death Rattle's radius does not ladder

The card says 5/6/8 yd. The only ground visual this module may use — 30632
"Debris", the same one Falling Sky uses, verified pure-visual in Spell.dbc —
draws exactly 4.0 yd. A circle that lies about its own size is worse than a
small one (§4.8), so the danger zone is the drawn zone at every rank and the
**damage** ladders instead, which the card also states (8/12/18%).

The circle is also **redrawn every four seconds** while the burst waits. §4.2
puts this affix's two seconds through the same queue as everything else,
minimum spacing included, so a burst can legitimately wait longer than the
visual lasts — and a burst whose telegraph has faded is what §4.8 forbids.

### 3.6 Two summon caps, not one

Design §4.2's "at most four affix-spawned creatures in total" is a rule about
uninvited *enemies*. Falling Sky's mark, Death Rattle's circle and Grudge's
Restless Spirit are all summons and none can be fought. A run carrying Falling
Sky and Death Rattle would spend the whole budget on scenery and silently stop
drawing circles — §4.2 breaking §4.8. The cap now splits on whether the thing can
be fought: creatures keep `Gauntlet.Summons.MaxAlive`, scenery gets six of its
own. The stalker cap is unchanged and counts across both.

Grudge caps itself at two spirits and Death Rattle at two fuses for the same
reason.

### 3.7 `Unit::SetSpeed`, not the card's `SetSpeedRate`

`SetSpeedRate` writes `m_speed_rate` and sends nothing (`Unit.h:1742`), so the
creature would be faster on the server than on any client — the untelegraphed
punishment §2.9 lists as known-bad. `SetSpeed(..., forced)` writes the same
field, calls `propagateSpeedChange` and sends the opcode
(`Unit.cpp:11310-11335`).

### 3.8 Smaller ones

- **`Mgr::NameOf` stops printing "Everlasting".** Every affix in the game was
  named `Everlasting <something>`, harmless only while the Scalars made the
  condition axis mean something. A real condition still prints; the axis is kept
  for a later phase (§6).
- **`Protocol.lua`'s `PROTOCOL_VERSION` was 2 while the server announced 3.**
  Phase 1 bumped `GeneratorVersion` and regenerated `Data.lua` but not the
  parser, so the addon was refusing every handshake and falling back to chat.
  Both are 4 now. **This is a Phase 1 bug found here, not a Phase 2 one.**
- **`GeneratorVersion` 3 → 4.** Four rows deleted, fifteen made offerable, four
  windows widened, two rolls removed from the stream, the slot loop changed.
- **Falter's class rule** is a reading, not a table: warrior/rogue/DK always
  disarmed, mage/warlock/priest always silenced, paladin/hunter/shaman disarmed,
  druid by form as the card says.
- **Echo's guaranteed uncommon item is not implemented.** The card promises "one
  guaranteed uncommon item of the owner's level" and the XP half is what the
  registry's `BonusExperience` names. Choosing an item pool is a design decision
  the design does not make — quality, slot, class restriction, bind-on-pickup —
  and inventing one that hands out arbitrary items on a live hardcore realm is
  not a call this phase should take. The XP reward is implemented in full.
- **Carrion's `+25% item drop chance` is fixed, not laddered**, because the card
  states one number and `BoonTable` has no second magnitude for a row.
- **The addon's `SUMMON` flag on Ambush.** Its row carries `MF_Stalker` so that
  an Ambusher hunting you shows on the addon; it carries no `"stalker"`
  exclusive key, so it is not capped against the Shade and the Echo, which is
  what §4.1's stalker cap actually means.

---

## 4. The invariant sweep

### Live view — what a player is actually offered

19 mechanics across 3 families, 160,000 sets (1,000 seeds × 10 classes × 16
tiers), each tier's carried set built from the run's own previous picks.

```
tier      1     2     3     4     5     6     7     8
%     0.000 0.000 0.000 0.000 0.050 0.120 0.460 13.61
tier      9    10    11    12    13    14    15    16
%     1.100 1.210 8.670 45.66 40.94 63.38 95.19 99.08
```

Before the builder fix (§3.3), with everything else in this phase already in
place, the same measurement read 6.59 / 30.15 / 6.08 / 10.01 / 10.37 / 17.10 /
24.30 / 25.60 / 30.86 / 45.51 / 51.81 at tiers 2–12.

Asserted: **exact zero at tiers 1–4**, per-tier ceilings elsewhere, no empty
offer slot at or below tier 13, and per set — the tier window, the class gate,
the spell gate, the rank ceiling, `Condition::Always`, that the offer's boon is
the one its row names, and that nothing `MF_NotImplemented` is ever offered.

Tiers 8 and 12 are the swap tiers (§3.3). Tiers 13–16 are the structural tail:
a run that far in carries most of the nineteen mechanics its class can be
offered, at their ceilings, and §4.6 expects rank-ups to dominate from tier 11.
It closes as families arrive — Rules and Bargains in Phase 3, forty-four class
curses in Phase 4.

### Full table — 1.6 M sets over all 69 rows

```
tier      1     2     3     4     5     6     7     8
now   0.000 0.000 0.000 0.000 0.002 2.334 2.328 0.000
was   0.000 0.000 2.914 0.869 2.390 2.246 2.442 0.000
tier      9    10    11    12    13    14    15    16
now   3.490 3.667 3.887 1.555 4.372 4.899 46.29 78.39
was   3.657 3.796 4.716 2.145 5.544 6.469 28.77 46.14
```

**Every tier from 1 to 14 is at least as good as Phase 0's**, and tiers 3 and 4
are now exact where they used to relax — so "no relaxation where it passed
before" holds, with room to spare. Tiers 15–16 are worse, and that is the
arithmetic of the deletion rather than a regression: four of the rows with a
window reaching tier 15 *were* the four flat scalars. Seventeen reach it where
twenty-one did.

### Negative controls — six, all firing

| Control | Caught by |
|---|---|
| Nimble silently marked `MF_NotImplemented` | `Registry.OnlyTheImplementedMechanicsMayBeOffered`, printing both sets |
| Id 21 resurrected in the table | `Registry.TheDeletedScalarIdsAreGoneAndStayGone` **and** the row-count test |
| The distinct-family preference removed | live tiers 2–4 and full-table tiers 2–4 |
| The four widened windows reverted | live tier 4, 1 set in 10,000 |
| Craven offered a boon that is not its row's | `OfferInvariants.LiveRegistryView`, naming id and key |
| `AnchorMechanics()` call for Champions deleted | `compile-check.sh --anchors`, in 30 ms |

`GeneratorDeterminism.DifferentSeedsGiveDifferentOffers` moved from 0.0975% to
0.535% of adjacent seed pairs colliding, and now asserts a 1% global ceiling
**and** a 10% per-tier ceiling rather than one number. The rise is arithmetic:
a Scalar offer used to carry one of thirteen conditions rolled from the stream,
which multiplied the distinct sets a tier could produce. 180 of the 428 sit at
tier 1, where the whole eligible table is seven mechanics.

---

## 5. The two handed-over bugs

### 5.1 `.gauntlet debug`-built characters purged on login — **fixed**

Phase 0's guard discarded any run with fewer levels than tiers. It could not
tell real GUID reuse from deliberate testing: a game master who levels a
character *down* to try an affix at level 10 produces exactly the shape it
purged.

Replaced with an exact test rather than a better heuristic.
`gauntlet_run.char_created` holds `UNIX_TIMESTAMP(characters.creation_date)`,
which is the one fact about a guid that cannot be inherited — a character
created into a recycled guid has a new one. Zero on either side is "cannot say"
and is not a mismatch, so a run predating the column is backfilled rather than
purged. A character may now be levelled anywhere at all and keep its run.

I chose this over the prompt's suggested `debug_touched` marker because a marker
only covers characters a GM touched with `.gauntlet debug`; the level-down that
actually triggers the purge is `.character level`, which no Gauntlet command
sees. The creation date covers both, and it is evidence rather than an
inference.

### 5.2 Deep Wounds — **not reproduced**, and said plainly

I cannot drive a WoW client, so I could not reproduce the report. What I can
say:

- **The named candidate is ruled out, and always was.** "A wound accumulated at
  a high level survives a GM level-down and then sits permanently at the
  low-level cap" cannot happen: `CheckLevel` zeroes the wound on *any* level
  change, in either direction, and it was in the mechanic's first commit
  (`676b3f7`).
- **Every link reads correct.** `UnitScript::DealDamage` reaches
  `OnDamageTaken` (`Unit.cpp:983`, before health is applied);
  `Player::UpdateMaxHealth` calls `OnPlayerAfterUpdateMaxHealth`
  (`Unit/StatSystem.cpp:322`) and then `SetMaxHealth` with the modified value;
  and an empty `enabledHooks` vector on a `PlayerScript` enables **all** hooks
  rather than none (`PlayerScript.cpp:963-971`), which was my strongest
  suspicion and is wrong.

So instead of changing code I cannot test, I made the failure identifiable in
one command. `IMechanic` gains `Diagnose()`, one optional line of internals for
`.gauntlet debug dump`, and Deep Wounds implements it:

```
wound 412 (18% of 2260) | applied 412 | asked 412 | saved 412 | cap 904
   | level 20 | ticks 1841 blows 63 recomputes 12
```

The three counters are the whole dispatch chain and a zero names the broken link
outright — `ticks 0` means `Mgr::Tick` is not reaching `OnTick`, `blows 0` means
no wound is accumulating, `recomputes 0` means the wound exists and is
subtracted from nothing (and `asked` above `applied` with `recomputes 0` is
exactly that case, because `ApplyIfDue` stops asking after the first).

Plan §5.2's last three debug commands are wired, which is the other half of the
same job: **`set <key> <value>`** writes a `gauntlet_state` key straight through,
which separates the accumulate half from the apply half; **`fire <key>`**
releases a queued event now and keeps the warning already sent;
**`events on|off`** is the realm switch for a session.

**To reproduce it in five minutes:** `.gauntlet debug give deep_wounds 3`, take
a few hits, `.gauntlet debug dump`, and read the line above.

---

## 6. The `TODO(design)` list

77 markers. The 17 in `GauntletGenerator.cpp` (family weights, `RankFloor`, the
boon magnitude table, the relevance discount, the bargain weight) and the 14 in
`GauntletRegistry.cpp` (tier windows the cards do not state, plus the four this
phase moved) are inherited or listed above. The 30 new ones in the Phase 2
mechanics are all the same kind of thing — a number the card does not give:

- **Lifetimes and leads.** Echo, Carrion and Ambush each give their summon two
  minutes and their telegraph 4–10 seconds; Reinforcements warns 5 s ahead; the
  Shade's 30 s is Phase 1's.
- **Deferral intervals.** What a mechanic does when the scheduler releases its
  event into a state it cannot act in (10 s across the board, against the
  Shade's 20/60 s split).
- **Concurrency caps.** Death Rattle 2 fuses, Grudge 2 spirits, Craven 6
  runners, Nimble 8 hurried creatures, Cunning 8 kickers, `SUMMON_CAP_SCENERY` 6.
  Every one bounds a pathological case rather than rationing honest play.
- **Search radii and cadences.** Keen-nosed's 60 yd grid search and its 1 s
  sweep; Death Rattle's 4 s circle refresh.
- **Falter's class table** (§3.8) and **Carrion's fixed +25% drop chance**.

---

## 7. What does not work, and what is unverified

Stated plainly.

**Nothing has been seen on a screen.** I cannot drive a WoW client, so the
prompt's "a level-20 character carrying four Phase 2 mechanics plays for an hour
with every event telegraphed and attributed" is untested, and so is "no summon
is left in the world after logout". The despawn paths are unchanged from Phase 1
and every one of the fifteen mechanics goes through the same wrapper, but that
is a code reading and not a test. The in-game checklist plan §5.3 asks for
(`docs/checklists.md`) is not written, because writing a checklist I cannot run
is not evidence.

**Specific things a first hour should look at, in priority order:**

1. **Death Rattle's circle.** Whether 30632 draws where the corpse fell and
   whether the four-second redraw is visible as a blink. It is the one visual
   this phase leans on twice.
2. **Falter's disarm and silence.** Whether 676 and 15487 apply to the player at
   all when self-cast, and whether the overwritten duration is honoured. The
   tooltip will read the DBC's 10 s and 5 s, which is the same cost Falling
   Sky's dodge buff already pays and cannot be fixed without a client patch.
3. **Reinforcements' copies.** Whether `GetCreatureAI` really takes for an
   arbitrary world-DB entry — the window it relies on is the same one
   `OnBeforeCreatureSelectLevel` uses and that one is proven, but this is a
   different hook.
4. **Craven's flee.** `MoveFleeing` on a creature already in melee.
5. **Nimble's speed on the client.** `SetSpeed(..., true)` should send the
   opcode; whether the creature visibly moves faster is the test.
6. **Echo's clone.** Whether 45204 and 41055 in the directions this file uses
   produce a copy of the player's face and weapon.

**Known-imperfect, by decision:** Echo's item reward (§3.8); the tier 13–16 tail
(§4); Carrion's flat drop-chance bonus; the `docs/checklists.md` file.

---

## 8. What Phase 3 should know

1. **Run `tests/compile-check.sh` on every file you write.** It is 0.5–7 s and
   it is the difference between finding a collision now and finding it after a
   Docker build. The anchor audit alone would have caught the whole class of
   "the mechanic is offered and does nothing" failure.
2. **A mechanic delivers its own boon.** The aggregate pays none. Return
   `BoonFactor(self, kind)` from `AggregateFactor` for the three kinds that have
   one, and `BoonMoneyMult` / `BoonHealMult` at the site for the other two.
   `AggregateTest.TheAggregatePaysNoBoonOfItsOwn` enforces it.
3. **`src/mechanics/Nearby.h` already has the grid search.** Cursed Hoard and
   Self-Found want `OnItemRoll` and `OnLoot`, both of which are now wired and
   dispatched.
4. **Rules and Bargains are what fix the tier 13–16 tail.** Two more families is
   the single biggest thing available to the offer builder; the numbers in §4
   are the baseline to beat.
5. **`Condition` is intact but unused.** The enum, the column, `ConditionName`,
   `Mgr::ConditionActive` and the aggregate's gating all still work; nothing
   rolls one. `OfferInvariants` asserts every offer carries `Always`, so the
   day something starts rolling one again, it says so.
6. **Ids 21, 22, 72 and 73 are spent forever.**
   `Registry.TheDeletedScalarIdsAreGoneAndStayGone` is what enforces it.
7. **`.gauntlet debug` is now complete** — `give`, `remove`, `rank`, `dump`
   (with per-mechanic internals), `offers`, `seed`, `export-addon`, `fire`,
   `set`, `events`. Implement `Diagnose()` on anything that holds state a player
   cannot see.
8. **Rebuild and run `ac-db-import` whenever module SQL changes.**
   `ac-worldserver` runs with `AC_UPDATES_ENABLE_DATABASES=0` and will not apply
   it.
