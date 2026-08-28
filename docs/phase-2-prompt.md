# Phase 2 prompt

Paste the block below into a fresh session.

---

Implement Phase 2 of `docs/implementation-plan.md` for mod-gauntlet, an
AzerothCore (WotLK 3.3.5a) module. The plan is the contract; the design it
implements is `docs/affix-design.md`. Read both in full before touching code,
then read `docs/phase-0-report.md` and `docs/phase-1-report.md` — they record
the decisions these phases inherit and several traps that already cost a build
each. Then read the current module end to end (`src/`, `tests/`, `data/sql/`,
`addon/GauntletUI/`, `conf/mod_gauntlet.conf.dist`, `mod-gauntlet.cmake`,
`sync-to-server.sh`).

## The point of this phase

Kill the tax. Today a player is still offered things like *Fleetfooted Wandering
Exposed* — a flat coefficient, a condition bolted on, and a generic boon rolled
from a table. That is the shape the whole redesign exists to remove, and it is
still most of what the live registry can offer, because five mechanics are
implemented out of seventy-three.

By the end of this phase every offer is a **named mechanic that does
something**, with a boon its own registry row names. The scalar affixes are gone
from the codebase, not merely switched off.

## Decisions already taken — do not re-open; flag if one turns out impossible

1. **There is no backward compatibility to preserve.** The dev realm's gauntlet
   tables were wiped deliberately (`gauntlet_run`, `gauntlet_affix`,
   `gauntlet_affix_log`, `gauntlet_state`, `gauntlet_leaderboard` are all empty;
   the characters themselves are untouched). Nothing in the database needs
   carrying forward. A backup of the pre-wipe data exists outside the repo and
   is not a constraint on your design.
2. **The scalars are deleted, not retired.** `Exposed` (21), `Feeble` (22),
   `Withering` (72) and `Forgetful` (73) go: their registry rows, their
   implementations under `src/mechanics/attrition/`, `Scalars.{h,cpp}`, and the
   `MF_Scalar` concept wherever it survives only to serve them. This overrides
   design §5, which kept a threshold `Exposed`; the user's position is that a
   coefficient with a condition on it is still a tax. If you think one of them
   deserves to come back later it comes back as a real mechanic, not as a
   multiplier.
3. **Generic boons stop being rolled.** `RollBoon` draws a Scalar's boon from
   `[1, LastRolledBoon]`. With no Scalars that path is dead: delete it, and let
   every boon come from `MechanicDef::boon`, named by the row and delivered by
   the mechanic.
4. **The legacy generator apparatus goes with them.** `LegacyRoll`,
   `GauntletLegacy.{h,cpp}`, the `legacy_mag` column, the one-shot startup
   migration in `Mgr`, `tests/fixtures/legacy_rolls.json` and its golden test all
   exist to protect pre-redesign characters. There are none. Remove them, and
   drop `legacy_mag` in a dated update file rather than editing the base schema
   in place. Say in the report what you deleted, so it can be found in git if it
   is ever wanted.
5. **The cut is gated on breadth, not on compatibility.** The offer builder must
   fill three distinct-family slots at every tier 1–16. Four mechanics across
   four families cannot do that below tier 4 — Champions is the only one whose
   window reaches tier 1. So implement first, delete last, and if the invariant
   sweep cannot pass after the deletion, the answer is more mechanics, never a
   weaker test.

## Scope, in this order

### 1. Build the fast compile loop first

This is the highest-leverage hour in the project and it comes before any
mechanic work. Eleven of twenty-three translation units cannot be compiled on
this machine — anything including `Player.h` needs boost, mysql headers and a
generated `revision_data.h` that only the Docker build has. Every Phase 1 worker
therefore wrote hundreds of lines that nothing could check, and three separate
build failures (`Condition` colliding with a core global, `MECHANIC_NONE`
colliding, `QueryResult.h` missing) were each discovered a whole Docker build
apart.

It is already proven possible:

```bash
docker build --target build -f apps/docker/Dockerfile -t ac-gauntlet-build .
docker run --rm ac-gauntlet-build sh -c "cd /azerothcore/build && ninja \
  modules/CMakeFiles/modules.dir/mod-gauntlet/src/GauntletMgr.cpp.o"
```

Wrap it as `tests/compile-check.sh` so anyone can compile one module translation
unit, or all of them, in seconds against the real clang and the real core
headers. Mount the module source so a rebuild needs no new image. Report what it
costs per file and per full pass.

Everything after this step uses it. A worker that has not compiled its file has
not finished.

### 2. The Phase 2 mechanics (plan §1 Phase 2)

`src/mechanics/spawn/{Echo,Carrion,Reinforcements,Ambush}.cpp`,
`enemy/{Craven,CallToArms,DeathRattle,Grudge,Nimble,Cunning,KeenNosed}.cpp`,
`tempo/{Frenzy,Overextended,Falter,Hubris}.cpp`.

Order by shared infrastructure, not by family, exactly as the plan says: Echo
and Reinforcements first (summons of copied entries, clone spells); then Call to
Arms, Keen-nosed, Death Rattle, Grudge (grid search and corpse positions); then
Craven, Nimble, Cunning (creature-side state keyed by GUID with evade and
exit-combat cleanup); then Frenzy, Overextended, Hubris, Falter.

Every one of them:

- goes through `sGauntletSummons->Summon` if it puts anything in the world, and
  never calls `SummonCreature` directly;
- goes through `ctx.clock->Arm` if it is timed, and never owns a clock;
- registers with `GAUNTLET_MECHANIC(id, Type)` **and** has its anchor added to
  `AnchorMechanics()` in `GauntletScripts.cpp` — without the anchor the file is
  dropped from `libmodules.a` and `MakeMechanic` returns nullptr with no error
  anywhere, which is the most dangerous failure mode in this codebase;
- names a boon in its registry row that the mechanic itself delivers. If the
  `Boon` enum cannot express it, extend the enum — that is now safe and the
  reasoning is in `docs/phase-0-report.md` §3.3;
- telegraphs. Design §4.8's fourth question is the bar: when the player dies, do
  they know which affix did it. `EVT`, `CTR`, `STAT`, `SUMMON` and `KILLBY` are
  all wired and emitting.

Aim for enough of them that tiers 1–3 have three families available. If the
design's tier windows fight that, widen a window and say which and why — the
same reading that moved Champions, Carrion and Hubris to tier 1 in Phase 0.

### 3. Delete the scalars and the legacy apparatus

Only once step 2 gives the pool enough breadth. Remove the four mechanics, their
registry rows, the generic boon roll, and everything listed in decision 4. Then
update the tests that encode which mechanics are offerable — `RegistryTest` and
`OfferInvariantsTest` both assert this set. Change the assertions deliberately
and negative-control both, the way Phase 1 did.

After the cut the invariant sweep must pass at every tier with no relaxation
where it passed before. `MECHANIC_WITHERING` and `MECHANIC_FORGETFUL` in
`Gauntlet.h`, and ids 21 and 22, are never reused for anything else.

### 4. Two known bugs, both handed over

- **`.gauntlet debug`-built characters get purged on login.** The GUID-reuse fix
  in `GauntletMgr::Load` discards any run with fewer levels than tiers, which is
  exactly the shape a debug-granted test character has. Real GUID reuse and
  deliberate testing are indistinguishable by that test. Keep the protection —
  the core does hand out deleted characters' GUIDs — but stop it eating test
  characters. A `debug_touched` marker set by any `.gauntlet debug` mutation and
  skipped by the heuristic is the obvious shape; choose your own and justify it.
- **Deep Wounds' visible effect is unconfirmed.** The state store proved the
  mechanic runs and the dispatch chain reads correct end to end, but the user
  reported it did not work and the failing link was never identified. Reproduce
  it before changing anything: `.gauntlet status` prints the aggregate products,
  `.gauntlet debug dump` prints the wound and whether `impl` is live. One strong
  candidate was never ruled out: a wound accumulated at a high level survives a
  GM level-down and then sits permanently at the low-level cap.

### 5. Report

`docs/phase-2-report.md`: what was built per step, the build and test commands
and their results, every deviation with its reason, the `TODO(design)` list,
what you deleted, and what Phase 3 should know. Be plain about anything that
does not work.

## Environment and guardrails

- The core is `/mnt/c/Users/3302/azerothcore-wotlk`; `sync-to-server.sh` mirrors
  the module into its `modules/` because Docker will not follow symlinks. Read
  that tree's `AGENTS.md` and its compose files.
- **The realm is live and the user plays on it.** Build with
  `docker compose build ac-worldserver ac-db-import`, apply SQL with
  `docker compose up ac-db-import`, deploy with
  `docker compose up -d ac-worldserver`. Never run any `docker … prune`, never
  touch a container outside that compose project.
- Verify every core API by reading its header in that tree and quote file:line.
  Two names collide with core globals outside `namespace Gauntlet`: `Condition`
  and `MECHANIC_NONE`.
- Work on `feature/affix-redesign`, commit per numbered step, do not push. The
  repo commits as `nero <26593322+ngc7052@users.noreply.github.com>`; do not
  change the identity and never put an email address in a commit or file.
- No client patches: no DBC edits, no new spell visuals, existing spell ids and
  display ids only. If a needed visual does not exist, say so rather than
  inventing one. Note that a reused spell shows its own DBC tooltip, which will
  lie about any amount you override — Falling Sky's dodge buff already does.
- Real players only; `IsEligible` stays and nothing runs for bots.
- `addon/GauntletUI/Data.lua` is generated. Regenerate it whenever the registry
  or the `Boon` enum changes — `tests/tools/README-export.md` has both ways —
  and never hand-edit it.

## Definition of done

Every offer at every tier is a named mechanic that does something, with a boon
its row names; no scalar affix exists in the codebase; the invariant sweep
passes with no relaxation at the tiers where it passed before; a level-20
character carrying four Phase 2 mechanics plays for an hour with every event
telegraphed and attributed; no summon is left in the world after logout; the
unit tests pass; and the worldserver starts clean.
