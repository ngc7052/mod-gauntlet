# Handoff

Everything a new session needs to pick this up. Read §3 before writing any code
and §4 before trusting any test.

---

## 1. What this is

`mod-gauntlet`, an AzerothCore (WotLK 3.3.5a) module: a procedurally generated
hardcore affix challenge. A run offers three "affix" cards per tier, the player
picks one, and the curses accumulate. 79 mechanics, all implemented: 69 rares
and the first ten commons, with reroll and skip live on every tier. Steps 1-3
of `docs/rarity-plan.md` have landed and step 4 -- the rank removal -- is next.

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
./tests/run-tests.sh                 # 200 unit tests
./sync-to-server.sh                  # rsync the module into the core tree
docker compose -f /mnt/c/Users/3302/azerothcore-wotlk/docker-compose.yml \
  -f /mnt/c/Users/3302/azerothcore-wotlk/docker-compose.override.yml \
  --project-directory /mnt/c/Users/3302/azerothcore-wotlk build ac-worldserver
# ...then `up -d ac-worldserver` to deploy. Restarting kicks the user offline.
```

Gate before every commit: anchors, ladders, compile, link, tests. All green
today — 79 anchors, 83 ladders, 64 objects, 200 tests.

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
| `.gauntlet debug leaks [name] [what] [rank]` | attach → detach, report what did not come back; for one card, its own counters both before and after `OnDetach` |
| `.gauntlet debug soak` | the same, with the card ticked and its own events fired first |
| `.gauntlet debug bench` | every hook driven at every card, plus what attaching alone changed and whether any item in the world is refused; ends with the list of cards **no probe reached** |
| `.gauntlet debug offers <tier> [name]` | what the builder would put in front of that character at that tier, rarity and kind per line; the only way to watch the roll without a client. Name *after* the tier. |

**Run `.gauntlet debug leaks self` first.** It checks the audit can see the
character at all; a blind audit reports everything clean.

### Where the harness is still blind

Every one of these cost real debugging time this session. They are the reason to
distrust a green result:

- **`BenchQuiet` clears the scheduler's suppression** so cards can be probed. So
  the bench structurally **cannot** see any bug of the form "the run is being
  held back" — it would have passed all ten offer-gated cards forever.
- **`Probe`'s own baseline is read *after* attach.** The bench command now reads
  the footprint before and after `AuditAttach` as well and reports the
  difference as "on attach: ...", so a standing aura or a stripped helm counts
  as reached — but only through that command; anything else calling `Probe`
  directly is still blind to attach-time effects.
- **A harness that skips a step the live code takes will blame the code.** The
  audits did not call `RefreshStats` and duly reported Arcane Frailty halving a
  mage's max health and Faint leaking the restore. An inverse pair on adjacent
  cards is the signature.
- **Weapon passives re-apply asynchronously** after a disarm lifts, so they read
  as "removed and not restored" in a synchronous capture. A second pass is clean.
  The same happens on *any* equipment change: a common that puts a helm into
  the bags and back on leaves the racial weapon specialisations (human 20597/
  20864, troll 20558/26290) reading as removed until the next world update.
- **Re-equipping an item restarts its equip cooldown** (the core's 30-second
  anti-swap rule, `Player::ApplyEquipCooldown`), so a trinket a denial put back
  on reads as "spell N still on cooldown". That is the proof the return
  happened, not a leak.
- **Armour cannot be equipped in combat** (`EQUIP_ERR_NOT_IN_COMBAT`), so a
  denial's return is refused on a fighting bot. `leaks` does not stop combat;
  `bench` does. Queue `.combatstop <name>` in the same append as the audit --
  console commands appended together run in one world update, so the bot's AI
  cannot re-engage between them.
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

## 7. The rarity plan: steps 1-3 landed, step 4 next

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

### What step 2 put in place (2026-08-31)

- `src/GauntletTrades.h`: the commons' table, Player-free. One `TradeDef` line
  per common — a denial mask (inventory type or weapon subclass) or a signed
  coefficient on one `AggregateKind`, plus the boon and its magnitude. The
  generator's `BoonTable` reads the magnitude from it, so the card promises
  what the mechanic pays. `inline constexpr`, and it has to be: as a plain
  `constexpr` every translation unit got its own copy and the implicitly
  inline `FindTrade` returned pointers into someone else's.
- `src/mechanics/common/SimpleTrade.cpp`: one class behind every common. Ten
  named factories and ten `GAUNTLET_MECHANIC_FN`s — the plan's `MakeTrade<N>`
  snippet cannot compile, the macro pastes the name — and ten anchors.
- **A new hook**: `IMechanic::CanEquip(Ctx&, ItemTemplate const*)`,
  `Mgr::CanEquip`, and `OnPlayerCanEquipItem` in `GauntletScripts.cpp`. The
  core asks it before anything else in `Player::CanEquipItem`
  (`PlayerStorage.cpp:1912`) and shows a refusal as the generic "You can't do
  that right now", so the mechanic prints the reason, once per item per two
  seconds. `not_loading == false` (the login loader) is never vetoed.
- A denial **strips what is worn** on attach — bags first, mailbox when full,
  the core's own `AutoUnequipOffhandIfNeed` pattern — remembers the guids in
  the state store (`<key>.took0/1`) and re-equips on detach through the core's
  own `CanEquipItem`, with its own veto stood down for the moment.
- The bench gained the equipment probe (every item template offered to the
  carried set; first refusal wins) and the on-attach reading (§4).
- The ten: Bareheaded, Cloakless, Ringless, Charmless, Bare-necked (armour
  slots, classless), Axeless and Swordless (class-masked to the classes that
  can hold one; the relevance discount halves their boon), Glass, Frail, Thin
  Blood (coefficients). Families by lever: denials are Rules, coefficients
  Attrition — spread on purpose so a set can hold more than one common.
- Measured (`build/sweep --seeds 300`): tier 1 is 61% common (target 70), the
  late run's empty slots went from 12,418 to **zero**, and sets with no
  reward-shaped offer rose 9% → 15% — commons are neither reward-shaped (the
  design's definition is "tagged as family B") nor rankable, so a full set of
  them has fewer rank-ups to pay the guarantee with. The invariant test's
  ceiling moved 12 → 18 with that reasoning in its journey list.
- GeneratorVersion 13 → 14.
- Checked end to end on `gt-world`: `offers 1` on a warrior and a mage gives
  two commons and a rare per set; `bench` reaches every one of the ten (the
  denials by "equipping X refused" -- Shadowblade Helmet, Troggbane, Marrowgar's
  Scratching Choker, Runed Band of the Kirin Tor -- the coefficients by
  "aggregate: ..."); `leaks` on Bareheaded, Ringless and Charmless shows the
  item stripped at attach and back on at detach, with only the two artefacts
  above left in the diff. The first pass found the in-combat refusal (put back
  0, silently); the card now says so and keeps the guid for a later detach.
  **Not seen on a screen:** the chat lines a real player gets, and the addon
  drawing a Common (white) card.

### What step 3 put in place (2026-09-01)

- **Reroll** rebuilds the pending tier's offers with a counter folded into the
  stream seed (bits 16–23; zero folds to nothing, so unrerolled sets are
  byte-identical to version 14's). **Skip** declines the tier outright — the
  tier advances as a pick advances it — and banks a charge.
- The purse and the per-tier reroll count live in the run's **state store**
  under `RunKeys` (`run.reroll_charges`, `run.rerolls`, `run.reroll_tier`), not
  in new `gauntlet_run` columns. Two decisions worth knowing:
  1. The purse has **no initialiser anywhere**: `Mgr::RerollCharges` reads it
     with `Rules::REROLL_STARTING_CHARGES` as the `Get` fallback, so a key
     never written *is* the starting purse — and every run from before rerolls
     existed gets its charges retroactively, the user's included.
  2. The per-tier count is persisted (`run.rerolls` + `run.reroll_tier`)
     because pending offers are rebuilt from the seed at login: without it a
     relog would show the pre-reroll set and the charge would have bought
     nothing. A stale pair from an older tier is ignored by the tier compare.
- Numbers: start 2, skip banks 1, saturate at 250 (`GauntletRules.h`,
  `TODO(design)` — §7.5 of the plan says these have no evidence at all).
- Surfaces: `.gauntlet reroll` / `.gauntlet skip` (players), `REROLL`/`SKIP`
  wire verbs, two chooser buttons (reroll shows the count and greys at zero),
  the purse on the **RUN** frame's fifth field, `debug reroll [name]` /
  `debug skip [name]` for the console, charges in `dump`, and the offer chat
  hint line. The chat-fallback chooser closes on the "Tier N declined" line.
- The affix log's ENUM gains `skip` and `reroll` (rows with mechanic 0), via
  `data/sql/db-characters/updates/2026_09_01_00_gauntlet_reroll_log.sql` —
  guarded like every update, and **applied by hand to `gt-db`** since the test
  realm runs with `Updates.EnableDatabases = 0`. The base file and the
  repeated definition in `2026_08_28_00_gauntlet.sql` moved with it, kept
  byte-identical.
- GeneratorVersion 14 → 15. The bump moved tier 30's live-view relaxation
  ceiling (8 → 10): the version is in the stream, a bump reshuffles which
  seeds land badly, and tier 30 sits on the bargain family's opening edge.

### Step 4

Rank removal — §5b of the plan is the inventory of what it touches, and it is
the largest single edit in the plan. With rarity carrying the run and reroll/
skip live, it is a deletion with a green gate around it. Then step 5, the
remaining cards, commons first.

### Queued behind it: the greed redesign

`docs/greed-redesign.md` (2026-09-01) is the plan for the cards that only slow
the run down. The brief it answers: hardcore is hard by design, but an offer
must tempt — make you faster or richer while it endangers you. It runs the
whole table through a three-question test, names five brakes (Craven, Grudge,
Falter, Cunning, Ambush) and one dead letter (Iron Purse, replaced by **Blood
for Bread**: no eating or drinking, kills restore you), gives each a redesign on
an existing seam with the bench probe it needs, and sharpens three boons to
accelerants. Its step 1 (Blood for Bread) has no ladder and can land any time;
steps 2–5 want the rank removal done first so each card is touched once. Its
rarity column feeds the plan's §7.4 epic pass.

Things step 2 left for a later pass, deliberately:
- The **stat-grant primitive** (§3, "grant a stat" — expertise, ratings) is not
  built; the first ten pay through the existing Boon plumbing. It needs a
  class-neutral carrier spell per aura type read out of Spell.dbc.
- `FAMILY_WEIGHT` still gives Rules 1 against Attrition 3, so at tier 1 the
  three coefficient trades are drawn about three times as often as any one of
  the seven denials. Left as measured; the comment on the weights still
  describes Rules as "three one-rank entries", which is no longer true.
- Whether commons stack too freely (+8% damage four times over) is §7.1.

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
- A denial's strip-and-return runs on every session detach too: logging out
  puts the helm back on, logging in takes it off again. Harmless churn, and
  the contract ("detach gives back what attach took") is what `leaks` audits;
  a mechanic cannot tell a swap from a logout.
- The two weapon denials are relevance-gated only by class. A shaman who has
  never held an axe pays nothing for Axeless and gets half the boon; a
  `requiresSpell` on the weapon skill would be the truthful gate.
