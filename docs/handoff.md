# Handoff

Everything a new session needs to pick this up. Read §3 before writing any code
and §4 before trusting any test.

---

## 1. What this is

`mod-gauntlet`, an AzerothCore (WotLK 3.3.5a) module: a procedurally generated
hardcore affix challenge. A run offers three "affix" cards per tier, the player
picks one, and the curses accumulate. 69 mechanics, all implemented, all
marked rare: step 1 of `docs/rarity-plan.md` has landed and step 2 is next.

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
./tests/run-tests.sh                 # 189 unit tests
./sync-to-server.sh                  # rsync the module into the core tree
docker compose -f /mnt/c/Users/3302/azerothcore-wotlk/docker-compose.yml \
  -f /mnt/c/Users/3302/azerothcore-wotlk/docker-compose.override.yml \
  --project-directory /mnt/c/Users/3302/azerothcore-wotlk build ac-worldserver
# ...then `up -d ac-worldserver` to deploy. Restarting kicks the user offline.
```

Gate before every commit: anchors, ladders, compile, link, tests. All green
today — 69 anchors, 83 ladders, 63 objects, 189 tests.

## 4. The testing rig, and what it cannot see

`docs/testing-without-a-client.md` is the full write-up. In short:
**mod-playerbots puts real `Player` objects in the world with no game client**,
console commands run in the world thread so probes are atomic, and an isolated
realm runs on a *copy* of the DB volume with client data mounted read-only.
Containers are named `gt-*`; the live `ac-*` ones are never touched.

Three audit commands and the offer preview, all `Console::Yes` and all taking a
character name so they can be driven from the server console:

| Command | What it does |
|---|---|
| `.gauntlet debug leaks [name] [what] [rank]` | attach → detach, report what did not come back |
| `.gauntlet debug soak` | the same, with the card ticked and its own events fired first |
| `.gauntlet debug bench` | every hook driven at every card; ends with the list of cards **no probe reached** |
| `.gauntlet debug offers <tier> [name]` | what the builder would put in front of that character at that tier, rarity and kind per line; the only way to watch the roll without a client. Name *after* the tier. |

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

## 7. The rarity plan: step 1 landed, step 2 next

`docs/rarity-plan.md` is decided, not a proposal. Ranks are being removed and
replaced with a rarity ladder (common → legendary), plus reroll and skip.

The measured problem it solves: a tier is a level, so a run to 80 sees **80
offers**, and no character can be offered more than **29.6 of the 69** mechanics.
Ranks are what hide that gap.

The answer is ~90 new cards to reach ~160, of which **only ~30 are C++ work** —
`GAUNTLET_MECHANIC_FN` already lets one class back many registry ids, so a
"common" is a table row rather than a file.

### What step 1 put in place (2026-08-31)

- `Rarity` in `Gauntlet.h`, `MechanicDef::rarity` beside `maxRank` (which it
  will replace), every row `Rarity::Rare`.
  `Registry.EveryCardIsRareUntilTheEpicPass` holds that and is *meant* to be
  rewritten into a list of ids the day the first non-rare row lands.
- The tier weights of §2 in `GauntletRules.h` as `Rules::RarityWeight`, one
  column per rarity so the ladder audit checks "commons fall, rarer rises";
  the uncommon column humps on purpose and carries `LADDER-SENTINEL`.
- `BuildOffers` rolls a rarity per slot **before** the family, over the
  rarities that actually have an eligible card, weights renormalised. Three
  decisions were taken here that the plan did not spell out:
  1. **Renormalise over what is available** rather than roll-then-fall-back to
     a neighbour. It is the pattern `Draw` already used for families, and it
     makes the all-Rare table draw exactly as before.
  2. **Every available rarity weighing zero goes uniform**, not empty: the
     weights shape the mix, they do not veto a card the registry made eligible
     at that tier.
  3. **Rarity outranks family** in roll order, because rarity is now the axis
     the run escalates along; distinct families are still a rule.
- `Offer::rarity` is the *card's* rarity (never the rolled target), asserted
  as a hard invariant in the 1.5M-set sweep. `RollRarity` is public and tested
  directly, because with every card Rare the sweep cannot show the weights
  doing anything.
- `build/sweep --rarity` prints the delivered mix per tier beside the weights
  — 100% Rare everywhere today. That column is the tuning instrument §7.6 asks
  for; watch it as commons land.
- Display: rarity colour on the offer and "You bear" chat lines, a trailing
  field on the OFFER frame, `rarities` and per-row `rarity` in Data.lua, the
  chooser border and badge, a strip and tooltip line in the carried list, and
  `offers`/`dump`/`cards` print it. GeneratorVersion 12 → 13.
- Checked end to end on `gt-world` (which now runs the rarity build):
  `leaks self` passes, `export-addon` on the server produces a Data.lua
  byte-identical to the repo's, and `offers <tier> <bot>` for a warrior, a
  mage and a druid at tiers 5, 45 and 75 reports every card Rare with no
  relaxation. **What no harness has seen:** the addon panel itself. Panel.lua
  parses (`luac -p`) and the wire field is last, but the border, the badge and
  the strip have not been looked at on a screen.
- The `acore/ac-wotlk-worldserver:master` image was rebuilt with this code for
  the test realm and is **not deployed**: the live `ac-worldserver` is still on
  the previous image until someone runs `up -d`, which kicks the user offline.

### Step 2

`SimpleTrade` and the first ten commons — prove the table-driven path with the
bench before writing sixty rows. §3 of the plan has the three primitives, all
verified to exist. Expect to: add `Rarity::Common` rows via
`GAUNTLET_MECHANIC_FN`, rewrite `EveryCardIsRareUntilTheEpicPass` into a list,
and read `--rarity` at tiers 1–20 to see the common share move off zero. §8
has the rest of the order and §5b what rank removal actually touches.

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
- The server-wide announce ("X reached tier N and took Y") does not name the
  rarity. Left out on purpose while every card is rare; worth revisiting when
  an epic is worth announcing.
