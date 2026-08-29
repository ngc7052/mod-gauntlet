# Phase 4 prompt

Paste the block below into a fresh session.

---

Implement Phase 4 of `docs/implementation-plan.md` for mod-gauntlet, an
AzerothCore (WotLK 3.3.5a) module. The plan is the contract; the design it
implements is `docs/affix-design.md`. Read both in full before touching code,
then read `docs/phase-0-report.md`, `docs/phase-2-report.md` and
`docs/phase-3-report.md` — the last of these has an addendum recording
everything that changed after it was first written, including the tier axis.
Then read the current module end to end (`src/`, `tests/`, `data/sql/`,
`addon/GauntletUI/`, `conf/mod_gauntlet.conf.dist`, `mod-gauntlet.cmake`,
`sync-to-server.sh`).

## The point of this phase

Forty-four class curses, and the reason the design wants them is worth having
in front of you while writing them: a class kit is the one system the player
operates every second, so it is the one place a *character-side* rule earns its
place. Everything else in the table happens to the player. These change how the
character is played.

They are also, arithmetically, the last family the offer builder is waiting for.
A run now sees eighty tiers and the table has twenty-five implemented rows.

### The tail, and what actually causes it

`docs/phase-3-report.md` says the late run goes quiet. Measured over 152,000
live offer sets, tiers 69–80 hand out nothing at all. **The cause is not that
the table is small. It is that almost every row expires in the same place.**

```
rows reaching tier 80   5 of 25 non-class   12 of 44 class
rows reaching tier 70  20 of 25 non-class   40 of 44 class
```

Forty of the forty-four class rows stop at 70. So do Echo, Reinforcements,
Nimble, Cunning and Cursed Hoard. That is not a design decision anyone made: it
is a `maxTier` of 14 — the old axis's default guess — multiplied by five.
Fourteen of the tier windows in `GauntletRegistry.cpp` carry a `TODO(design)`
marker saying the card states no number.

**Revisiting those upper bounds is in scope for this phase**, at the user's
explicit direction. Some are real: Iron Purse ends at 15 because WotLK gold
stops mattering, Self-found at 20 because after that it only deletes gear the
character already owns. Most are not — an affix that is interesting at level 60
is rarely uninteresting at 75, and "the card does not say" became "expires at
70" by accident. Change them deliberately, one at a time, with the reason in the
comment, and re-run the sweep after.

## Decisions already taken — do not re-open; flag if one turns out impossible

1. **Wave A first, in the plan's order.** C1 Red Mist, C2 Berserker's Bargain,
   C4 Deafening Roar, C5 Long Forbearance, C6 Consecrated Ground, C9 Half-Tamed,
   C10 Dead Weight, C11 Wide Dead Zone, C13 Cold Trail, C15 Exposed Back, C17
   Frail Soul, C20 Whispers of the Deep, C21 Rune-starved, C22 Grave Call, C25
   One Totem, C26 Totemic Anchor, C29 Cold Feet, C31 Mana Burn, C33 Fel Pact,
   C37 Bound Skin, C41 Faint. Then wave B. Twenty-one rows is a phase;
   forty-four in one pass is how a phase stops being reviewable.
2. **Three shared helpers before any curse**, exactly as the plan says.
   `PermanentCooldown` — the "cannot use X" primitive, `AddSpellCooldown` with a
   seven-day end time, sent to the client and re-asserted every tick, verified
   to grey the button without a patch. `SelfControl` — confuse, root, stun and
   flee wrappers over `SetControlled` with a timer. `AuraDurationEdit` —
   `OnAuraApply` plus `SetDuration`/`SetMaxDuration` keyed on spell id and
   player. They go in `src/mechanics/class/` beside `Boons.h`, `Nearby.h` and
   `Charges.h`, and each one is written and compiled before the curses that need
   it, for the reason Phase 3 opened with machinery: a mechanic written against
   a helper that does not exist yet is a mechanic nobody can compile.
3. **The resurrect seam is yours and it is still unspent.**
   `OnPlayerResurrect` in `GauntletScripts.cpp` is deliberately empty and
   `Mgr::CancelPendingDeath` is deliberately uncalled. Last Rites is a cheat
   death and never enters the pending-death window; Ankh Pact and Stone of the
   Damned are the two that resurrect, and they spend the charge there.
   `mechanics/Charges.h` already does the per-level accounting.
4. **`RelaxCaps` is not a general escape hatch.** Two mechanics use it, both
   because a card names a number a clamp would otherwise eat, both
   state-dependent and self-lapsing. The test to apply to a class curse is "does
   the blurb promise a number the player will not receive", never "is this affix
   meant to be strong".
5. **Descriptions follow the rule in `mechanics/bargain/LastRites.cpp`.** First
   sentence says what happens to you, in plain words, with the number in digits.
   No opening subordinate clause, no "half again as much" where 50% will do, no
   closing line of flavour. A player read the old Last Rites card and had to ask
   whether it saves you from a killing blow, which is the only thing it does.
6. **Relevance is already built and must be used.** `MechanicDef::requiresSpell`
   with `Player::HasSpell`, and `requiresTree` against `GetTalentTree()` in the
   tabpage-plus-one encoding. A curse aimed at an ability the character has not
   trained is not a curse, it is a blank; `RelevancePercent` prices the
   difference and the offer builder refuses the rest.
7. **No client patches.** No DBC edits, no new spell visuals, existing spell ids
   and display ids only. A reused spell shows its own DBC tooltip and will lie
   about any amount overridden — Falling Sky's dodge buff already pays that, and
   it is why Last Rites' Mark is state and hooks rather than an aura.

## Scope, in this order

### 1. The three helpers, and nothing else

`src/mechanics/class/{PermanentCooldown,SelfControl,AuraDurationEdit}.{h,cpp}`.
Each compiled with `tests/compile-check.sh` and each with the core call sites
quoted as file:line. `PermanentCooldown` is the one to verify hardest: the plan
says it has been shown to grey the client's button without a patch, and if that
turns out not to hold, every "cannot use X" curse in the design needs a
different shape and this phase needs to know on day one rather than in week two.

### 2. Wave A, four classes at a time

`src/mechanics/class/{Warrior,Paladin,Hunter,Rogue,Priest,DeathKnight,Shaman,
Mage,Warlock,Druid}.cpp`, plus `class/Common.cpp` for Faint and Unspent.

Every one of them, as in Phase 2 and 3: through `sGauntletSummons->Summon` if it
puts anything in the world; through `ctx.clock->Arm` if it is timed and never
owning a clock; `GAUNTLET_MECHANIC(id, Type)` **and** an anchor in
`AnchorMechanics()`; a boon its registry row names and the mechanic delivers;
and telegraphed, because design §4.8's fourth question is still the bar — when
the player dies, do they know which affix did it.

### 3. `class/Bargains.cpp` — Ankh Pact and Stone of the Damned

The two that spend the resurrect seam. Decision 3.

### 4. The tier windows

Only after the curses exist, so the sweep measures the real table. Go through
the fourteen `TODO(design)` windows and every row whose `maxTier` is 70, decide
each one on its own merits, and record the reasoning. Then re-run both sweeps
and report them against `docs/phase-3-report.md`'s addendum as the baseline.

The bar to beat: **tiers 69–80 currently offer nothing.** If they still offer
nothing after forty-four curses and a window pass, say so plainly and say why,
because the next thing to try is structural — more ranks, or a tier axis that
stops before 80.

### 5. The leaderboard

Plan §1 Phase 5 wants `.gauntlet top` printing conducts and the class curses
recorded as conducts rather than as affixes (Phase 0 decision 7). The recording
half belongs here, with the curses; the printing half can wait.

### 6. Report

`docs/phase-4-report.md`: what was built per step, the build and test commands
and their results, every deviation with its reason, the `TODO(design)` list,
both sweeps, every tier window changed and why, and what Phase 5 should know.
Be plain about anything that does not work — `docs/phase-2-report.md` §7 and
`docs/phase-3-report.md` §6 are the standard.

## Environment and guardrails

- The core is `/mnt/c/Users/3302/azerothcore-wotlk`; `sync-to-server.sh` mirrors
  the module into its `modules/`.
- **The realm is live and the user plays on it.** Build with
  `docker compose build ac-worldserver`, deploy with
  `docker compose up -d ac-worldserver`. Never run any `docker … prune`, never
  touch a container outside that compose project.
- **`ac-db-import` runs the SQL baked into its image, not the mounted source.**
  A migration that is edited and re-applied without `docker compose build
  ac-db-import` applies the old file and reports the old hash. This cost two
  failed applications in Phase 3.
- **A rebuild does not deploy a config key.** `docker-compose.yml:47`
  bind-mounts `./env/dist/etc` over the image's own, so
  `conf/mod_gauntlet.conf.dist` has to be copied to
  `<core>/env/dist/etc/modules/` by hand. Diff the live `.conf` first;
  `Gauntlet.Debug.Enable = 1` is a hand edit on this realm.
- **Do not write a config key you have not wired.** `Gauntlet.Family.*.Enable`
  has been in the conf since Phase 0 and nothing reads it. Wire those or delete
  them; do not add a fourth.
- Verify every core API by reading its header in that tree and quote file:line.
  `Condition` and `MECHANIC_NONE` collide with core globals outside
  `namespace Gauntlet`.
- Work on `feature/affix-redesign`, commit per numbered step, do not push. The
  repo commits as `nero <26593322+ngc7052@users.noreply.github.com>`; do not
  change the identity and never put an email address in a commit or file.
- Real players only; `IsEligible` stays and nothing runs for bots.
- The user's account is `gmlevel 3`, and several mechanics refuse while
  `IsGameMaster()` is true. Check `.gm off` before concluding anything is broken.
- `.gauntlet debug hurt <percent>` deals a share of your own maximum health
  through the real damage pipeline. It is how a cheat-death path is tested
  without staking a run on the answer.
- Ids 21, 22, 72 and 73 are spent forever.

## Definition of done

Wave A's twenty-one curses and the two class bargains are offerable, implemented
and anchored; the three helpers exist and are used rather than reimplemented;
`PermanentCooldown` verifiably greys a client button; each curse passes its
checklist on its own class at the tier it unlocks; the tier windows have been
gone through deliberately and the sweep re-measured; the invariant sweep passes
with no relaxation where it passed before; the unit tests pass; and the
worldserver starts clean.
