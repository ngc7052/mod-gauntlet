# Phase 0 report — generator, data model, protocol

Written against `docs/implementation-plan.md` §1 Phase 0, on branch
`feature/affix-redesign`, from module commit `c334a9f` and the core at
`/mnt/c/Users/3302/azerothcore-wotlk`.

Nothing is pushed. Every commit is local and named for the step it closes.

## 0. Definition of done, item by item

| Plan §1 Phase 0 requirement | Status |
|---|---|
| A fresh character is offered three affixes from the new generator | **Partly** — verified by driving the generator directly, not in game |
| Picks land in the new columns | **Partly** — schema and code path verified; no pick made in game |
| A pre-migration character logs in with the same affixes it had | **Yes** — 21 rows across two real characters, exact |
| The addon shows offers and carried affixes from GNT messages | **No** — cannot be verified without a client |
| `.gauntlet debug offers 7` prints a plausible tier-7 offer | **No** — the command needs a logged-in player |
| The unit tests pass | **Yes** — 35 tests, and they fail when they should |
| The worldserver starts clean with the new SQL applied | **Yes** — clean boot, zero config warnings |

Everything unverified is unverified for one reason: I cannot drive a WoW client.
§6 says exactly what that leaves open.

---

## 1. What was built

### Step 1 — the golden fixture (`5619298`)

`tests/fixtures/legacy_rolls.json` captures the *current* generator's output —
288 rows, seeds {1, 7, 42, 1337, 65535, 2³¹−1} × tiers 1–16 × roll index 0–2 —
recorded before a line of generator code changed. `tests/tools/dump_legacy_rolls.cpp`
produced it and is committed with the command that runs it.

This is the migration's correctness test. Every existing character's affixes are
regenerated through `LegacyRoll` during the migration, so a divergence of one
rounding step silently rewrites live runs.

### Step 2 — the registry (`646836c`, `e7c1e13`, `8aa2843`)

`src/GauntletRegistry.{h,cpp}`: all 73 `MechanicDef` rows — the design's 71
(S1–S5 = 1–5, E1–E8 = 6–13, T1–T5 = 14–18, A1–A4 = 19–22, R1–R3 = 23–25,
B1–B2 = 26–27, C1–C44 = 28–71) plus Withering = 72 and Forgetful = 73 for
migrated runs. Pure data: no behaviour, no factory pointer. Lookups by id and by
key are indexed, because they sit on the damage path.

Every tier window and name was machine-diffed against `docs/affix-design.md`
(0 mismatches) and all 40 spell ids were verified by name against the core's own
`env/dist/data/dbc/Spell.dbc` (40/40).

Two follow-ups landed on top:

- `e7c1e13` added `requiresTree`, because class-curse relevance is defined as
  trained spells *plus* spec.
- `8aa2843` moved Champions, Carrion and Hubris to `minTier = 1`. See §3.1 —
  this was a real fault, not a tidy-up.

### Step 3 — the generator (`25723b6`)

`src/GauntletGenerator.{h,cpp}` implements plan §2.4 on the existing splitmix
stream with `GeneratorVersion = 2` folded into the seed.
`src/GauntletLegacy.{h,cpp}` carries generator 1 verbatim as `LegacyRoll`, for
migration only.

### Step 4a — the mechanic interface and the four scalars (`49f4be3`)

`src/GauntletMechanic.h` (the full plan §2.2 `IMechanic` surface, `Ctx`, and
`MakeMechanic(uint16)`), `src/GauntletAggregate.{h,cpp}` (the pure maths with
plan §2.5's caps), and Exposed / Feeble / Withering / Forgetful under
`src/mechanics/attrition/` over a shared `ScalarMechanic` base.

### Step 4b — the switchover and the migration (`53bc035`)

`RunState` moved into `Gauntlet.h` as the sole owner of every `IMechanic`.
`Load`/`Save`/`OfferTier`/`Pick`/`EndRun` rewritten onto the new columns and
`BuildOffers`. `Mgr::Multiplier` became `Mgr::Aggregate` reading
`Gauntlet.Caps.*` from config. The death sequence split per decision 5.
`GauntletAffix.cpp` deleted; the legacy vocabulary rehomed into
`GauntletLegacy.{h,cpp}` and a new `src/GauntletNames.cpp`.

The one-shot `LegacyRoll` migration runs on `OnBeforeWorldInitialized`.

### Step 5 — schema and migration SQL (`d5597b4`, `71bf7a4`)

`data/sql/db-characters/base/gauntlet.sql` rewritten to the five-table final
schema; `data/sql/db-characters/updates/2026_08_28_00_gauntlet.sql` migrates an
existing install. `71bf7a4` added `AffixInstance::legacyMag` so the C++ side has
somewhere to load the column into.

### Step 7 — the addon (`549ae76`)

`GauntletUI.lua` split into `Protocol.lua` (GNT transport, version gate,
`PICK`/`SYNC`, an 8-second fallback timer) and `Panel.lua` (the same window,
chooser, settings and minimap button, now reading resolved records). The old
chat scraping is kept intact as the fallback, not as dead code.

### Step 9 / 9a — tests and harness (`914ce57`, `425e1dd`, `c864eea`, `efd36b7`, `d576834`)

`tests/run-tests.sh` builds and runs the module's Player-free code against
googletest; `tests/syntax-check.sh` compiles and **links** every Player-free
translation unit in about a second. `tests/{Registry,Generator,OfferInvariants,
Aggregate}Test.cpp` and the `ACORE_MODULE_TEST_SOURCES` registration in
`mod-gauntlet.cmake`.

### Step 10 — config and README (`19fb546`)

All 19 new keys from plan §5.4 with their defaults, each documented in the
existing file's format, and the ones no code reads yet marked as such. README
rewritten for the family model with a determinism section.

---

## 2. Build and test commands, and their results

### Unit tests — pass

```bash
tests/run-tests.sh          # 35 tests, 7 suites, 50.7 s — PASSED
tests/syntax-check.sh       # 10 Player-free objects compile and link
```

The suite includes the 288-row legacy golden fixture checked field by field, a
1,600,000-set offer-invariant sweep (10,000 seeds × 16 tiers × 10 classes on the
full table) with 23 hard invariants at zero, all 64 subsets of six contributors
per aggregate kind, and the registry's 73 rows. Seven negative controls prove the
tests fail when they should: a perturbed fixture row, a perturbed `LegacyRoll`,
Feeble marked `NotImplemented`, per-contribution clamping, `Always` allowed on a
Scalar, commit `8aa2843` reverted, and the fixture deleted. Each names the exact
row and field, or the exact seed/tier/class/carried set.

### Core build — passes, after two collisions only a real build could find

```bash
./sync-to-server.sh
cd /mnt/c/Users/3302/azerothcore-wotlk
docker compose build ac-worldserver ac-db-import
```

Three attempts. The first two failed to compile `GauntletCommands.cpp`, both for
the same reason: the module's namespace collides with core globals, and outside
its own `namespace Gauntlet` block `using namespace Gauntlet;` makes the name
ambiguous. `Condition` collides with the class in `ConditionMgr.h`;
`MECHANIC_NONE` collides with the spell `Mechanics` enum in `SharedDefines.h`.
Both are fixed by qualification (`8-fix` commits). I then checked every name
`Gauntlet.h` exports against the core's headers — those two are the entire set,
and the file now carries a comment saying so.

Nothing local can catch this class of error: the module's Player-dependent
translation units cannot be compiled without the full core include set, which is
why the build is the first place they are ever compiled.

The third build succeeded. Warm ccache makes it about 90 seconds. Verified the
module really is in the binary rather than merely configured:

```
$ docker run --rm --entrypoint sh acore/ac-wotlk-worldserver:master -c \
    "grep -ao 'Tier {} reached. Choose your affix:' .../worldserver"
Tier {} reached. Choose your affix:      # also gauntlet_affix_log, c44_stone_of_the_damned
```

### SQL, applied through the core's own updater

```bash
docker compose up ac-db-import
>> Applying update "2026_08_28_00_gauntlet.sql" 'CEEA877'...
>> Applying update "gauntlet.sql" '28F7D86'...
>> Applied 2 queries.   [exit 0]
```

Note the order: the dated update runs **before** the base file, exactly as the
SQL worker designed around (`UpdateFetcher::PathCompare` compares filenames
only). Both files are written to be safe in that order and on an empty database.

Before this, `ac-db-import` had to be rebuilt too: the running image contained no
`mod-gauntlet` at all, which is why the module's SQL had never been applied
through the updater on this realm. `ac-worldserver` runs with
`AC_UPDATES_ENABLE_DATABASES=0` and will not apply it either.

### Deployment and the migration on real data

```bash
docker compose up -d ac-worldserver
Gauntlet: migrated 21 legacy affix row(s) to the redesign schema
          and dropped gauntlet_affix.roll and .tier.
WORLD: World Initialized In 0 Minutes 19 Seconds ... ready
```

The gauntlet tables were backed up first. The realm held two real pre-migration
characters, and their affixes had been recorded *before* any code changed:

| | expected | got |
|---|---|---|
| guid 6022 slot 1 | 72 Withering, cond 15, boon 0/0, mag 9 | identical |
| guid 6022 slot 2 | 73 Forgetful, cond 1, boon 4/4, mag 9 | identical |
| guid 6022 slot 3 | 22 Feeble, cond 5, boon 7/5, mag 7 | identical |
| guid 6022 slot 4 | 21 Exposed, cond 8, boon 0/0, mag 10 | identical |
| guid 6021 | 17 rows, mags 2–79, three with boons | all 17 identical |

**All 21 rows across both characters migrated with exact fidelity**, at
`gen_version = 1`. A second restart produced no migration line at all, which is
the idempotency proof. The final `gauntlet_affix` DDL matches a fresh install.

The realm's `mod_gauntlet.conf` predated even `Gauntlet.PlayersOnly`, so the
worldserver logged a warning per missing key. Replaced with the newly built
`.conf.dist` (the five previously-set values were all at their defaults and are
unchanged); the boot is now clean, zero config warnings.

---

## 3. Deviations from the plan, with reasons

### 3.1 Champions, Carrion and Hubris now unlock at tier 1, not tier 2

Design §4.6's tier table gives the tiers 1–2 band "Champions, Hubris, one rule,
Carrion, conditional Exposed" and the job of teaching "that affixes are
content". Reading the band as *by* tier 2 left tier 1 with two families and no
reward-shaped mechanic at all, so the first affix a character is ever offered
could only be a scalar or a rule — and the plan's own offer invariants were
arithmetically unsatisfiable there.

Measured over 320,000 offer sets: tiers 1 and 2 relaxed 100% of the time before
the change and 0% after. Nothing else moved.

### 3.2 `gauntlet_affix.legacy_mag` — a column the plan does not name

Plan §3.1 stores `rank` (1–3) and no magnitude. Generator 1 expressed a curse as
a free percentage in 2–115. Rounding a live player's 115% Exposed onto rank III
would change their run, so the exact number is kept and 0 means "take the
strength from the rank", which is every generator-2 row.

### 3.3 The `Boon` enum was deliberately **not** extended — and this is load-bearing

Roughly 17 design cards name a boon none of the seven `Boon` values expresses:
cooldown reductions, bespoke single-ability buffs, "the second life". Appending
to the enum is a trap: generator 1 rolled its boon as
`RollIn(state, 1, uint32(Boon::MAX) - 1)`, so a new value silently changes every
generator-1 affix for every seed — invalidating the fixture and rewriting live
runs during migration.

Measured: appending one `Boon` changes 83 of the fixture's 288 rows under the
shipped `Roll`, and 0 under `LegacyRoll`. Appending a `Condition` changes 278 and
0. `LegacyRoll` now freezes both ranges as literals, which makes the shared enums
safe to extend in Phase 1. Until they are, family C and B1 show no boon in the
addon, contradicting the design's "family C always comes with a boon" rule.

**This is the largest single thing Phase 0 does not deliver.**

### 3.4 The offer invariants cannot all hold, and the test asserts something stronger instead

With four implemented mechanics in one family, "three offers, distinct families,
no duplicate mechanic" is arithmetically impossible on the live registry, so the
builder degrades in a defined order and records what it did in
`OfferSet::relaxations`. The sweep therefore runs against the full 73-entry
table.

My first instruction was to assert `relaxations == GR_None` for tiers 1–14
exactly. The tests worker measured it and pushed back: tiers 3–14 relax between
0.87% and 6.47%, and my "`GR_FellBackToScalar` at 15–16 is a failure" rule
contradicted itself, because the generator sets that one bit for both "fell back
to a scalar" and "no reward-shaped candidate exists". They were right. What is
asserted instead is exact and holds at every tier: **the relaxation word must
describe the set it came back with** — `GR_RepeatedFamily` iff a family repeats,
`GR_RepeatedMechanic` iff a mechanic repeats, `GR_FellBackToScalar` iff the set
has no reward-shaped offer — plus exact zero at tiers 1, 2 and 8, plus per-tier
ceilings.

Tiers 15–16 still relax 28.8% and 46.1%. That is structural: only 21 of the 73
mechanics have a tier window reaching 15, and a run that far in carries most of
them. Design §4.6 expects rank-ups to dominate there. It is a Phase 5 tuning
item, not a bug.

### 3.5 The chat lines are shape-identical, not byte-identical — my error

I instructed that offer and pick chat lines stay byte-for-byte identical. That
is impossible: the affix *name* was `ConditionName + EffectName` and the
description carried a `[Severity]` prefix that the new schema does not store.
Every `PSendSysMessage` and `StringFormat` **format string** is byte-identical to
`HEAD~1` (extracted and diffed mechanically), so every pattern the addon's chat
fallback matches on still matches. The `[Severity]` prefix is gone and the
addon's `Sev()` falls back to grey.

### 3.5b `RegistryView{includeUnimplemented}` also unhides Withering and Forgetful

The test view ignores `MF_NotImplemented` so the sweep can exercise all families.
Ids 72 and 73 carry that flag not because they lack an implementation — they have
one, so migrated runs keep working — but because they are retired from rolls by
identity. The sweep therefore draws from 73 entries where the design has 71, and
a full-table offer can show "Forgetful".

This weakens no assertion: every hard invariant is checked on whatever came back,
and the *live* view excludes 72 and 73 correctly, which is what a player sees. It
is left as it is rather than fixed, because separating "retired" from
"unimplemented" means a new flag, a generator change and a re-run of the
1.6M-set sweep for no behavioural gain. Phase 2 retires Withering and Forgetful
from the registry entirely, which resolves it.

### 3.6 Structural deviations agreed up front

- `MechanicDef` has no `factory` pointer; `MakeMechanic(uint16)` lives in
  `GauntletMechanic.h` keyed by the same ids (plan §2.1).
- `Ctx`'s `clock` and `state` are null pointers in Phase 0, because `Scheduler`
  and `State` do not exist until Phase 1 (plan §2.2).
- `IPlayerView::GetTalentTree()` and `MechanicDef::requiresTree` encode
  tabpage + 1, with 0 = "no spec yet". `Player::GetMostPointsTalentTree`
  (`Player.cpp:15729`) returns a raw 0-based tabpage *and* returns 0 for a
  character with no talents spent, so a gate written against it is wrong at low
  level and cannot express any class's first tree.
- Aggregation multiplies and clamps the product where the old code summed
  percentages and applied a single 0.05 floor. Stacked scalars are milder as a
  result (two rank-3 Feeble were 0.30, are now 0.4225 clamped to the 0.6 floor);
  single affixes are unchanged. A 0.05 per-contribution floor is kept for
  Experience alone, which plan §2.5 gives no cap, so a migrated 115% Forgetful
  does not reduce experience to zero.
- The migration drops `tier` as well as `roll`, which is what makes a migrated
  table's DDL match a fresh install.
- The migration hook is `OnBeforeWorldInitialized` (`World.cpp:1022`), not
  `OnStartup`: `OnAfterConfigLoad` repeats on `.reload config`, and `OnStartup`
  fires at `Main.cpp:390`, after `StartWorldNetwork` has already opened the
  listening socket.
- A module-local test harness (`tests/run-tests.sh`) exists alongside the plan's
  `ACORE_MODULE_TEST_SOURCES` registration, because the Docker build never passes
  `-DBUILD_TESTING=ON` and this WSL has only g++-9 and no gtest.

### 3.7 A new character inherited a deleted one's run (found in play, fixed)

Reported on the dev realm while this phase was being verified: a freshly created
level-1 paladin showed as *retired* and carried four affixes, and a level-10
priest carried seventeen.

The core hands out the GUIDs of deleted characters again, and the module keys a
run on the GUID alone, so a new character adopts whatever the previous occupant
left behind — retired flag, tier and every affix. This predates the redesign
(the old `Load` read `WHERE guid = ?` just the same, and nothing ever cleaned up
on delete); making test characters simply hits it constantly.

Fixed in `e894d8b`, in two parts. `Mgr::PurgeCharacter` deletes the module's
four tables for a guid and is called from `OnPlayerDeleteFromDB`, which the core
fires *inside* the transaction that removes the character's own rows
(`Player.cpp:4384`), so the two cannot come apart. And `Load` refuses a run that
cannot belong to the character now holding the GUID — a class that does not
match, or fewer levels than tiers — purging it and starting fresh. The second
exists for rows already orphaned before the first shipped.

The level test is deliberately "fewer levels than tiers" rather than
`level < tier × TierInterval`: the stricter form would purge every legitimate
run the day an admin raised the interval. Note the class test alone catches
neither live case, because `gauntlet_run.class` had already been adopted from
the current occupant on an earlier login; the level test is what catches them.

### 3.8 Two hardcore holes closed that the plan did not mention

- `OnPlayerCanResurrect` now also vetoes during the pending-death window.
  Decision 5's 60-second timer would otherwise open a 60-second hole in hardcore.
- Logging out inside the death window ends the run. Otherwise a player could die,
  quit within 60 seconds and return alive — something the old instant `EndRun`
  made impossible.

`OnPlayerResurrect` deliberately does **not** cancel the pending death: decision
5 cancels *with a bargain charge*, and Phase 0 has none. Cancelling
unconditionally would make any future path reaching it silently non-hardcore.

---

## 4. The `TODO(design)` list

49 markers, in four files. They fall into four groups.

**The boon gap — 18 markers, all the same root cause (§3.3).** Cards whose boon
is a cooldown reduction ("Sprint cooldown halved", "Divine Shield cooldown
−1 min", "Frost Nova cooldown −25%"), a bespoke single-ability buff
("Consecration doubled and halved", "Polymorph is instant", "+30% totem
effects", "+3 shield charges"), avoidance ("+5% dodge"), or a second life
(C43 Ankh Pact, C44 Stone of the Damned, B1 Last Rites). Candidate additions
once the enums are safe: `BonusCooldown`, `BonusPetDamage`, `BonusAbility`,
`SecondLife`.

**Tier windows the cards do not state — 4 markers.** Exposed and Feeble are
evergreen 1–16; R1/R2/R3 and C42 take their windows from §4.6 rather than a card.

**Spec and spell gates — 6 markers.** Three `requiresTree` readings (C18
Faithless Form = Shadow, C27 Elemental Overload = Elemental, C38 Nature's Toll =
Feral), and `requiresSpell` choices where a card names several buttons
(C2 → Shield Wall, C3 → Defensive Stance) or none with an id (C14 rogue poisons).
C21 Rune-starved was deliberately **not** spec-gated: every DK spec has six runes
and runic power, and the card's counterplay names only trained abilities.

**Generator tuning — 17 markers.** `RankFloor(tier)`, the boon magnitude table,
the relevance discount, family weights, and the bargain slot's 1-in-6 weight.
None is stated anywhere in the design; each is commented with the reading behind
it.

**Two in the mechanics.** The rank→magnitude ladder for the four scalars, and the
0.05 per-contribution floor kept for Experience.

---

## 5. What Phase 1 should know

1. **Extend `Boon` first.** It is now safe — `LegacyRoll` freezes the legacy boon
   and condition counts as literals — and until it happens, two whole families
   offer no boon at all. Bump `GeneratorVersion` when you do; stored picks are
   unaffected by design.
2. **`Ctx` is waiting for you.** `clock` and `state` are null pointers with the
   right types. Every mechanic already tolerates null. `Scheduler` and `State`
   slot in without touching the interface.
3. **The addon protocol is fully declared, half emitted.** `EVT`, `CTR`, `STAT`,
   `COND`, `SUMMON` and `KILLBY` have real sending functions and parser hooks on
   both sides; Phase 1 only has to call them. Coalescing and the 8/s cap are
   built and untested, because nothing emits into them yet.
4. **The relaxation counters are your tuning signal.** If tiers 15–16 still relax
   40%+ once more mechanics have wide tier windows, the windows are the problem,
   not the builder.
5. **`.gauntlet debug` is GM-only and config-gated**, so it can ship compiled in.
   `fire`, `set` and `events on|off` are deliberately absent — they need the
   scheduler and the state store.
6. **A slot that cannot be filled emits `mechanic == MECHANIC_NONE`** rather than
   an invented offer. Treat 0 as "no offer" everywhere.
7. **The `ac-db-import` image was stale** and contained no `mod-gauntlet` at all,
   which is why the module's SQL had never been applied through the core's
   updater on this realm. `ac-worldserver` runs with
   `AC_UPDATES_ENABLE_DATABASES=0` and will not apply it either. Rebuild and run
   `ac-db-import` whenever module SQL changes.

---

## 6. What does not work, and what is unverified

Stated plainly.

### Not verified, because it needs a WoW client

I cannot drive a game client, so three definition-of-done items are unproven in
the place that matters:

- **A fresh character being offered three affixes in game.** The generator was
  exercised directly instead: at tier 7 the live registry offers a rank-2
  `Restless Exposed` with a Stalwart boon and two rank-2 Feeble variants; at
  tier 1 it offers three rank-1 scalars. On the full table — what Phase 2 will
  see — tier 1 offers Champions, a conditional Exposed and Carrion, which is
  exactly what design §4.6 prescribes for that band.
- **Picks landing in the new columns.** The code path is read and the schema
  matches, but no pick has been made on a live character.
- **The addon showing offers and carried affixes from GNT messages.** Both Lua
  files parse under `luac` and contain no post-3.3.5a API, and the server's wire
  format was hand-checked against the parser field by field. Nothing has been
  seen on screen.

### `.gauntlet debug offers 7` — cannot be run without a client

Every `.gauntlet` command is `Console::No` and needs a player, so it cannot be
driven from the worldserver console. `export-addon` was the exception worth
changing — it reads the registry and writes a file — and is now `Console::Yes`.
The rest genuinely need a character.

### `ACORE_MODULE_TEST_SOURCES` — registration verified, link not

The Docker build never passes `-DBUILD_TESTING=ON`, so the deploy build never
touches `unit_tests`. I therefore tagged the Dockerfile's `build` stage and
re-configured it with `-DBUILD_TESTING=ON` inside a throwaway container. The
registration works:

```
-- mod-gauntlet: registered 5 test source(s) with unit_tests
```

and the resulting compile line for `unit_tests` carries
`-I/azerothcore/modules/mod-gauntlet/src`, so `ACORE_MODULE_TEST_INCLUDES` takes
effect too. `ninja -t targets` lists all five of our test objects under
`src/test/CMakeFiles/unit_tests.dir/__/__/modules/mod-gauntlet/tests/`.

What I could **not** prove is the final link, because `unit_tests` does not
build in this core tree for a reason unrelated to this module:

```
src/test/server/game/Spells/PeriodicAbsorbStealthProcTest.cpp:44:26:
  error: allocating an object of abstract class type 'NiceMock<WorldMock>'
  note: unimplemented pure virtual method 'GetPlayerbotsDBRevision' in 'NiceMock'
  note: unimplemented pure virtual method 'AddQueryHolderCallback' in 'NiceMock'
```

The core's own `IWorld` has gained pure virtuals that the core's own `WorldMock`
does not implement — a pre-existing breakage in this playerbots fork. Until it
is fixed, no module's tests can link here. Ours compile; the static-archive
concern (that `modules` members might not be pulled in) remains untested.

### Known-imperfect, by decision

- Tiers 15–16 relax 28.8% and 46.1% of offer sets, and the reward-shaped
  guarantee fails there in about 5%. Structural, and a Phase 5 tuning item.
- Family C and B1 carry no boon at all, because the `Boon` enum cannot express
  theirs (§3.3). This contradicts the design's "family C always comes with a
  boon" rule and is the phase's largest gap.
- The coalescing queue and the 8-messages-per-second cap are built but untested,
  because nothing emits into them before Phase 1.
- `sync-to-server.sh` is gitignored by the repo's own design, so the fix that
  stops it copying `build/_deps/googletest` and `.omc/` into the Docker build
  context lives only on this machine. Anyone else cloning the repo will need it
  again.
