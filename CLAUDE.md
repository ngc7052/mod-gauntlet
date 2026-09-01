# mod-gauntlet — project instructions

AzerothCore (WotLK 3.3.5a) module: a procedurally generated hardcore affix
challenge. A run offers three affix cards per tier, the player picks one, and
the curses accumulate. 109 mechanics today: twenty-six commons, twelve
uncommons, fifty-five rares, fourteen epics and two legendaries. `docs/rarity-plan.md` takes it to ~160; steps 1-4 have landed and
step 5 -- the remaining cards -- has begun. `docs/commons.md` is the
measurement that decides what step 5 builds next.

**Read `docs/handoff.md` before starting.** It carries the current state, the
recurring bug patterns, and where the test harness is blind. This file is the
rules; that one is the situation.

---

## Hard rules

1. **The realm is live and the user plays on it.** Deploying restarts
   `ac-worldserver` and kicks them offline. Say so before you do it.
2. **Never** run `docker system/container/volume/image prune`, with or without
   `-a`. Only ever touch the specific compose project. Other containers on this
   machine belong to unrelated work and are off limits.
3. **No client patches.** No DBC edits, no new spell visuals, no new spell ids.
   Where a bonus needs an arbitrary number, apply an *existing* spell and
   overwrite the aura effect amount — `FallingSky::Reward` documents the
   technique in full and `src/mechanics/BoonSpeed.cpp` reuses it.
4. **Real players only.** `IsEligible` stays. Nothing runs for bots on the live
   realm.
5. **`addon/GauntletUI/Data.lua` is generated.** Never hand-edit it; regenerate
   with `.gauntlet debug export-addon`.
6. **Never put an email address in a commit message or a file.**
7. **Commit directly to `master` and push.** There is no feature branch.
8. **Every new offer needs a unit test and an end-to-end test.** See "The
   testing rule" — this is not optional and there is no "I will add it after".

## Where offers live

An "offer" is a card the generator can put in front of a player. It is three
things in three places, and all three must exist or it silently does nothing:

| Piece | Where | What it is |
|---|---|---|
| **The row** | `src/GauntletRegistry.cpp` | one line in the table — id, key, name, family, class mask, tier window, rarity, flags, boon, gates, and the one-sentence blurb |
| **The mechanic** | `src/mechanics/<family>/<Name>.cpp` | a class implementing `IMechanic`, registered with `GAUNTLET_MECHANIC(id, Type)` |
| **The anchor** | `AnchorMechanics()` in `src/GauntletScripts.cpp` | `AddSC_gauntlet_mechanic_<Type>();` |

The anchor is not bureaucracy. The module links as a **static archive**, and a
translation unit nothing references is dropped by the linker — registry row
intact, mechanic silently absent. `./tests/compile-check.sh --anchors` audits
this and will fail the build; do not work around it.

Row order matters: ids must ascend. Add new rows at the end of the table.

Tuning values and any arithmetic worth testing go in **`src/GauntletRules.h`**,
not in the mechanic — see the testing rule for why.

## Adding a common

A common is a table row, not a file. Three places, all data:

1. A registry row at the end of the table, `Rarity::Common`.
2. A line in `src/GauntletTrades.h` with the same id: the curse (a denial mask
   or a coefficient), the boon -- which must be the row's -- and its magnitude.
   An **uncommon** trade is the same line with a `Condition`; the condition is
   what makes it one, and `TradesTest` holds rarity and condition to agree.
3. One factory and one `GAUNTLET_MECHANIC_FN` in
   `src/mechanics/common/SimpleTrade.cpp`, and its anchor in
   `AnchorMechanics()`. The macro pastes the name, so it is a plain identifier.

`tests/TradesTest.cpp` holds the three together and holds every line to being
a trade: a boon, a cost, never both on one axis. Then `bench` it -- a denial is
reached by "equipping X refused", a coefficient by "aggregate: ...", and the
strip of a worn item by "on attach: ...". Read `build/sweep --rarity` after.

## Adding an offer

1. **Registry row.** Append to the table in `src/GauntletRegistry.cpp`, ids
   ascending. Field order is `MechanicDef` in `src/GauntletRegistry.h`:

   ```cpp
   { 70, "my_card", "My Card", Family::Tempo, 0, 20, 80, Rarity::Rare,
     MF_Timed, "", Boon::BonusDamage, 0, 0,
     "One sentence, present tense, what the player experiences." },
   ```

   - `classMask` `0` means every class. A classless card is worth **ten times**
     a class card for coverage — see `docs/rarity-plan.md` §1.
   - `rarity` is how much of the run the card changes, not how big its numbers
     are — `docs/rarity-plan.md` §2 has the ladder. The offer builder rolls
     which rarity to draw from per slot, weighted by tier
     (`Rules::RarityWeight`), so a card's rarity decides *when* it shows up.
     Every row is `Rare` today and `Registry.EveryCardIsRareUntilTheEpicPass`
     holds that; the first row that is honestly something else turns that test
     into a list of ids, which is intended — what it forbids is promoting one
     existing card on its own instead of in §7.4's single pass. Check
     `build/sweep --rarity` after adding cards: it prints the delivered mix per
     tier beside the weights.
   - `requiresSpell` is a truthful relevance gate **and** what lets the bench
     drive the card. A card gated on casting something must declare it, or no
     probe can reach it.
   - Declare a `Boon` only if something applies it. A boon with no
     implementation prints a promise on the offer card and does nothing —
     that shipped once and a player found it.

2. **The mechanic.** New file under `src/mechanics/<family>/`. Implement only
   the `IMechanic` hooks you need; every one has an empty default. End with
   `GAUNTLET_MECHANIC(70, MyCard);`.

3. **The anchor.** Add `AddSC_gauntlet_mechanic_MyCard();` to
   `AnchorMechanics()`, in the family's group.

4. **Numbers into `GauntletRules.h`** if the card has any, with the pure
   arithmetic as `constexpr` functions the mechanic calls. The mechanic must
   *call* them — a test against a copy of the numbers proves nothing.

5. **Both tests.** See below.

## Supporting an offer

- **Do not write `!ctx.run->pending.empty()`.** Use `OfferHoldsBack(*ctx.run)`
  from `Gauntlet.h`. Ten cards hand-rolled that check and all ten went silent
  for as long as an offer sat unpicked.
- **`player->GetVictim()` is the auto-attack target only** and is null for a
  caster for an entire fight. Prefer the spell's own target, then
  `player->getAttackers()`.
- **A coefficient needs no code.** Override `AggregateFactor` and the aggregate,
  its caps and its condition gating come free.
- **Anything that takes something must give it back in `OnDetach`, and only
  what it took.** `TimedLockout` and `PermanentCooldown` exist for cooldowns;
  clearing a whole group unconditionally steals cooldowns the card never set.
- **Say something when the card acts and finds nothing.** Silence is read as
  broken; that is why Deafening Roar was reported as not working while working.
- **Implement `Diagnose()`.** It is what `.gauntlet debug dump` prints and it is
  the difference between "the trigger never ran" and "it ran and did nothing".

## Running the tests

**Deploying** needs `ac-db-import` built *and run* as well as
`ac-worldserver` -- that container carries its own copy of the module's SQL and
the worldserver's auto-updater is disabled on this realm, so building only the
worldserver ships code with no schema. `docs/handoff.md` §3 has the commands.

```bash
./tests/compile-check.sh --anchors   # anchor + ladder audits; seconds, no Docker
./tests/compile-check.sh             # compile + link in the build container
./tests/run-tests.sh                 # unit tests (gtest)
./tests/run-tests.sh --gtest_filter='Rules.*'
```

All four must be green before a commit. There is no partial pass.

End-to-end, without a game client — `docs/testing-without-a-client.md` has the
setup. mod-playerbots gives real `Player` objects with no client, driven from
the server console on an isolated realm built from a *copy* of the DB volume:

```
.gauntlet debug leaks self          # first, always: can the audit see anything
.gauntlet debug leaks <name> <key>           # attach → detach, what did not come back
.gauntlet debug soak  <name> <key>           # the same, card ticked and fired first
.gauntlet debug bench <name> <key>           # every hook driven; what no probe reached
.gauntlet debug offers <tier> <name>         # what the builder would offer them, rarity included
```

## The testing rule

**Every offer added or changed ships with both.** Neither substitutes for the
other, and this rule exists because each has already let a broken card through.

**1. A unit test**, in `tests/RulesTest.cpp` against `src/GauntletRules.h`.

The mechanics all include `Player.h`, which puts them outside the Player-free
build `run-tests.sh` uses — so anything left inside a mechanic is unreachable by
any test. Put the values and the arithmetic in `GauntletRules.h`, have the
mechanic call them, and test that.

Test **shape, not values**. "Hubris multiplies by 0.75" restates the constant and
fails only when someone edits it on purpose. "The duel is always a shelter and
the rest of the pull is always an exposure" is a claim about the
card being the card, and it fails on a transposed digit — which is the fault
that actually happens. That style caught a real one on its first run: Killing
Floor's two ladders (the ranks were still in) were complements, so breaking off paid exactly what winning
paid and the card's whole decision did not exist.

**2. An end-to-end check** with `bench` on a playerbot, and the assertion must
be the thing the card is *for*.

`bench` once counted "armed a timer" as a Spawn card working, and passed a
Reinforcements that had never put a creature in the world. It now asserts a
Spawn card actually spawns. Hold a new card to the same standard: if it heals,
assert health moved; if it denies, assert the thing is denied.

**Then distrust the green.** §4 of `docs/handoff.md` lists where the harness is
blind — `BenchQuiet` clears the scheduler's suppression, so the bench
structurally cannot see "the run is being held back"; the bench captures its
baseline after attach, so attach-time effects never appear in its verdict; and a
harness that skips a step the live code takes will blame the code.

## Practices

- **Match the file you are in.** This codebase comments the *why* — the reading
  that was rejected, the core line number that settles it, the failure the code
  is shaped around. A comment restating the code is noise; a comment recording
  why it is not the obvious thing is the point.
- **Cite the core.** `Unit.h:903`, `Spell.cpp:3776`. Claims about core behaviour
  get checked against the source at `$AC_CORE`, not assumed.
- **Verify an audit fails before trusting it.** Every guard added here was
  proven to bite by deliberately breaking something first. An audit that has
  never failed is a claim.
- **One commit per logical step**, message explaining the reasoning, not the
  diff. Say what was wrong and why the fix is the right shape.
- **Report failures plainly.** If a fix is unverified, say so. If a test found
  the bug, say which. Do not claim a fix works because it compiles.
