# Handoff

Everything a new session needs to pick this up. Read §3 before writing any code
and §4 before trusting any test.

---

## 1. What this is

`mod-gauntlet`, an AzerothCore (WotLK 3.3.5a) module: a procedurally generated
hardcore affix challenge. A run offers three "affix" cards per tier, the player
picks one, and the curses accumulate. 69 mechanics, all implemented.

Working directory `/home/nero/projects/mod-gauntlet`, on **`master`**, clean,
level with `origin/master`.

## 2. Standing constraints — these are not negotiable

- **The realm is live and the user plays on it.** `ac-worldserver` is running.
- **Never** `docker system/container/volume/image prune`, and never with
  `-a`/`--all`. Only ever touch the specific compose project. Containers outside
  it are off limits.
- **No client patches.** No DBC edits, no new spell visuals. Existing spell ids
  and display ids only. Where a bonus needs an arbitrary number, the established
  trick is to apply an existing spell and overwrite the aura effect amount —
  `FallingSky::Reward` documents it in full and `BoonSpeed` reuses it.
- **Real players only.** `IsEligible` stays; nothing runs for bots on the live
  realm (`Gauntlet.PlayersOnly = 1`).
- **`addon/GauntletUI/Data.lua` is generated.** Never hand-edit it; regenerate
  with `.gauntlet debug export-addon`.
- **Never put an email address in a commit or a file.**
- Commits go **directly to `master`** and are pushed. The user asked for this
  explicitly; there is no feature branch any more.

## 3. Build, test, deploy

```bash
./tests/compile-check.sh --anchors   # anchors + ladder audit, seconds, no Docker
./tests/compile-check.sh             # full compile + link in the build container
./tests/run-tests.sh                 # 173 unit tests
./sync-to-server.sh                  # rsync the module into the core tree
docker compose -f /mnt/c/Users/3302/azerothcore-wotlk/docker-compose.yml \
  -f /mnt/c/Users/3302/azerothcore-wotlk/docker-compose.override.yml \
  --project-directory /mnt/c/Users/3302/azerothcore-wotlk build ac-worldserver
# ...then `up -d ac-worldserver` to deploy. Restarting kicks the user offline.
```

Gate before every commit: anchors, ladders, compile, link, tests. All green
today — 69 anchors, 79 ladders, 63 objects, 173 tests.

## 4. The testing rig, and what it cannot see

`docs/testing-without-a-client.md` is the full write-up. In short:
**mod-playerbots puts real `Player` objects in the world with no game client**,
console commands run in the world thread so probes are atomic, and an isolated
realm runs on a *copy* of the DB volume with client data mounted read-only.
Containers are named `gt-*`; the live `ac-*` ones are never touched.

Three audit commands, all `Console::Yes` and all taking an optional character
name so they can be driven from the server console:

| Command | What it does |
|---|---|
| `.gauntlet debug leaks [name] [what] [rank]` | attach → detach, report what did not come back |
| `.gauntlet debug soak` | the same, with the card ticked and its own events fired first |
| `.gauntlet debug bench` | every hook driven at every card; ends with the list of cards **no probe reached** |

**Run `.gauntlet debug leaks self` first.** It checks the audit can see the
character at all; a blind audit reports everything clean.

### Where the harness is still blind

Every one of these cost real debugging time this session. They are the reason to
distrust a green result:

- **`BenchQuiet` clears the scheduler's suppression** so cards can be probed. So
  the bench structurally **cannot** see any bug of the form "the run is being
  held back" — it would have passed all ten offer-gated cards forever.
- **The bench captures its baseline *after* attach**, so attach-time effects are
  invisible to its "answered" list. Use `leaks`, which prints them.
- **A harness that skips a step the live code takes will blame the code.** The
  audits did not call `RefreshStats` and duly reported Arcane Frailty halving a
  mage's max health and Faint leaking the restore. An inverse pair on adjacent
  cards is the signature.
- **Weapon passives re-apply asynchronously** after a disarm lifts, so they read
  as "removed and not restored" in a synchronous capture. A second pass is clean.
- **A leak can consume the condition that reveals it.** Berserker's Bargain
  appeared on the first pass and not the second. Run twice, against a bot that
  has been fighting.

## 5. Bug patterns that recurred — check these first

Every one of these was found in play, not by a test, and most bit more than one
card.

1. **`player->GetVictim()` is `m_attacking` — the auto-attack target only.** It
   is null for a caster for an entire fight. It broke Reinforcements (could never
   copy anything) and Wide Dead Zone (point-blank shots landed). Prefer the
   spell's own target, then `getAttackers()`.
2. **`Mgr::Tick` delivers no event at all** while the player is mounted, in
   flight, in a sanctuary, dead, in the login grace window, or **has an offer on
   the table**. That last one froze whole runs. `.gauntlet debug dump` now names
   which of the six is holding the queue — read it before debugging a card.
3. **Ten cards had hard-coded the same offer check by hand.** There is one rule
   now, `OfferHoldsBack()` in `Gauntlet.h`. Do not write `!pending.empty()` in a
   mechanic.
4. **A boon can be declared with no implementation.** `Boon::BonusMoveSpeed` sat
   on three registry rows doing nothing while the offer card promised it. Check
   that something applies a boon before putting it on a row.
5. **Rage is stored at ten times its displayed value.** A refund of
   `info->ManaCost` gives back a tenth.
6. **`OnPlayerSpellCast` fires *before* `TakePower`** (`Spell.cpp:3776` vs
   `:3883`). Credit the cost there and let the core take it.
7. **`AddSpellCooldown` replaces rather than stacks**, so denying a spell
   destroys its real cooldown. `PermanentCooldown` now remembers and restores it.
8. **Silence reads as broken.** Deafening Roar worked and said nothing when a
   shout woke nobody, and was reported as not working.

## 6. What just shipped

26 commits this session, `c88788f..d6fde68`. Highlights:

- The three audit commands and the playerbot rig (`docs/testing-without-a-client.md`).
- `TimedLockout` and the `PermanentCooldown` fix — three curses were handing back
  cooldowns they never took.
- **Six cards redesigned** (`docs/tempo-redesign.md`): Killing Floor, Deep
  Wounds, Falling Sky, Overextended, Hubris, Frenzy. They were taxes, not
  decisions; each now has a moment and a verb, and they chain through kills.
- Gold removed as a boon; five rows pay speed or damage instead.
- `src/GauntletRules.h` + `tests/RulesTest.cpp` — the redesigned cards' ladders
  and arithmetic in a Player-free file so they are testable. The mechanics call
  it, so the tests measure the shipped numbers rather than a copy.

**Nothing in the redesign has been playtested.** Every ladder number is
judgement. The two most likely to be wrong are named at the end of
`docs/tempo-redesign.md`.

## 7. Next: the rarity plan

`docs/rarity-plan.md` is decided, not a proposal. Ranks are being removed and
replaced with a rarity ladder (common → legendary), plus reroll and skip.

The measured problem it solves: a tier is a level, so a run to 80 sees **80
offers**, and no character can be offered more than **29.6 of the 69** mechanics.
Ranks are what hide that gap.

The answer is ~90 new cards to reach ~160, of which **only ~30 are C++ work** —
`GAUNTLET_MECHANIC_FN` already lets one class back many registry ids, so a
"common" is a table row rather than a file.

**Step 1 is: rarity as a field, rolled and displayed, every existing card marked
rare, nothing else changed.** Reversible, provable, and it does not need ranks
gone first. §8 of that doc has the rest of the order and §5b has what rank
removal actually touches.

## 8. Known-open, not assigned

- `docs/checklists.md` — 672 lines of in-game checks, still mostly undone.
- `.gauntlet debug remove all` does not exist, so undoing `give-class` means
  removing ~30 affixes one slot at a time.
- `bench` reports `Spawned nothing: carrion` — a Spawn card that summons nothing
  even with a real kill and a corpse in the probe. Not chased; may be the same
  shape as the Reinforcements bug.
- `CAP_CLASS` is still 3 and still `TODO(design)`, eight phases on.
- Cold Trail's Vanish lockout and Dead Weight's Feign Death lockout deliberately
  survive detach. Design question, not a leak.
- `IsShout` covers four shouts; Challenging Shout and Piercing Howl are not in it.
