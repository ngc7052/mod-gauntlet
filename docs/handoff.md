# Handoff

Everything a new session needs to pick this up. Read §3 before writing any code
and §4 before trusting any test.

---

## 1. What this is

`mod-gauntlet`, an AzerothCore (WotLK 3.3.5a) module: a procedurally generated
hardcore affix challenge. A run offers three "affix" cards per tier, the player
picks one, and the curses accumulate. 85 mechanics, all implemented: 69 rares,
fourteen commons and two uncommons, with reroll and skip live on every tier.
Steps 1-4 of `docs/rarity-plan.md` have landed -- the rank system is gone, a
card is one value and rarity is its only tier -- and step 5, the remaining
cards, has begun with the loot trades.

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
./tests/run-tests.sh                 # 204 unit tests
./sync-to-server.sh                  # rsync the module into the core tree
cd /mnt/c/Users/3302/azerothcore-wotlk
DC="docker compose -f docker-compose.yml -f docker-compose.override.yml --project-directory ."
$DC build ac-worldserver ac-db-import   # BOTH, see below
$DC up -d ac-worldserver                # restarting kicks the user offline
$DC run --rm --no-deps ac-db-import     # applies this module's SQL
```

**Build `ac-db-import` too, and run it, or no SQL update ever reaches the live
database.** Found the hard way on 2026-09-01: `ac-db-import` has no bind mount
for `modules/` -- it carries its own copy of every module's SQL, baked in at
*its* image build -- and the worldserver's own auto-updater is switched off on
this realm by `AC_UPDATES_ENABLE_DATABASES` in the environment, which overrides
the `Updates.EnableDatabases = 7` in worldserver.conf. So the documented deploy
(build worldserver, up worldserver) shipped code and no schema, silently, and
two updates had accumulated unapplied: the reroll/skip log ENUM and the
rank-to-rarity normalisation. The first of those mattered -- `gauntlet_affix_log`
had no `'skip'` or `'reroll'` value, so the first reroll on the live realm
would have failed to log.

Note that changing a *file that was already applied* makes the updater reapply
it ("it changed"), so an edit to `base/gauntlet.sql` -- even to a comment --
re-runs it. That is safe only because every statement in it is guarded; keep it
that way. Verified after the fact on 2026-09-01: the four live runs, eighteen
affixes and twenty-seven log rows were untouched by the reapply.

Gate before every commit: anchors, ladders, compile, link, tests. All green
today — 85 anchors, 4 ladders, 67 objects, 204 tests.

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
| `.gauntlet debug leaks [name] [what]` | attach → detach, report what did not come back; for one card, its own counters both before and after `OnDetach` |
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

### Measured 2026-09-01, while checking step 4

- **The leak audit blames the first denial for a core behaviour.** Unequipping
  *any* item runs `Player::_ApplyItemMods` → `ApplyItemDependentAuras(item,
  false)` → `RemoveItemDependentAurasAndCasts` (Player.cpp:6770, 7267, 12875),
  which removes every self-cast aura whose `EquippedItemClass` requirement is
  unmet -- passives included. A troll with no bow and no thrown weapon carries
  Bow and Throwing Specialization (26290, 20558) only because the learn path
  casts with the full trigger mask; the first card to strip anything loses them,
  and `EquipItem` re-adds only what fits. So `leaks <troll> all` reports
  Bareheaded (id 75, the first denial in id order) leaking two racials, once per
  login, and a re-run is clean. It is what the player does by hand when they
  take a helm off; nothing for a card to fix. The same bookkeeping runs the
  other way on equip (`ApplyItemDependentAuras`, Player.cpp:7267): a death
  knight who had logged in without Two-Handed Weapon Specialization and Dark
  Conviction got both when Bareheaded returned his helm, and the audit read
  "still applied". And an item put back arrives with the core's thirty-second
  equip cooldown on its use effect (`ApplyEquipCooldown`, Player.cpp:12008):
  Fezzik's Pocketwatch read "still on cooldown" after Charmless. **Fixed
  2026-09-01:** the reading leaves every aura with an equipment requirement
  and every item cooldown out, and reads the nineteen equipment slots
  directly instead -- "did the item come back" asked as itself, by guid.
- **`bench` cannot reach four of the five Spawn cards, and where the bot stands
  decides the fifth.** The Shade and Ambush refuse cities and taverns
  (`REST_FLAG_IN_CITY`, Shade.cpp:221, Ambush.cpp:105) and idle bots sit in
  Orgrimmar and Dalaran; on a bot out in Borean Tundra the Shade spawned.
  Reinforcements spawns only for a player something is *attacking*
  (`getAttackers()`, Reinforcements.cpp:155) and the bench's World Trigger
  never fights back. Echo needs 25 kills and the probe makes one. Ambush needs
  20 s of the bot standing still. Carrion needs loot. `Spawned nothing: shade,
  echo, carrion, reinforcements, ambush` is therefore the bench's normal output
  on an idle bot -- reproduced on the step-3 image, so not a step-4 regression
  -- and the bench cannot currently tell a broken Spawn card from a well-guarded
  one. Bench a Spawn card on a bot out in the world, or make the probe fight
  back; §8 has the job.

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

## 7. The rarity plan: steps 1-4 landed, step 5 begun

`docs/rarity-plan.md` is decided, not a proposal. Ranks have been removed and
replaced with a rarity ladder (common → legendary), plus reroll and skip.

The measured problem it solves: a tier is a level, so a run to 80 sees **80
offers**, and no character can be offered more than **29.6 of the 69** mechanics.
Ranks were what hid that gap.

The answer is ~90 new cards to reach ~160, of which **only ~30 are C++ work** —
`GAUNTLET_MECHANIC_FN` already lets one class back many registry ids, so a
"common" is a table row rather than a file.

### What step 1 put in place (2026-08-31)

- `Rarity` in `Gauntlet.h`, `MechanicDef::rarity` beside `maxRank` (which step 4
  removed), every row `Rarity::Rare`.
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

### What step 4 put in place (2026-09-01)

The rank system is gone. What the deletion actually was, and the decisions
§5b of the plan did not spell out:

- `MAX_RANK`, `MechanicDef::maxRank`, `OfferKind::RankUp`, `RankFloor`,
  `RankNumeral`, `.gauntlet debug rank` and the `<rank>` argument on
  give/give-class/leaks/soak/bench all went. `AffixInstance::rank` became
  `rarity`, still written to the `rank` column -- but **`Mgr::Load` ignores
  the stored value** and takes rarity from the registry row, because a card's
  rarity is a fact about its row, not about the run.
  `2026_09_01_01_gauntlet_rank_is_rarity.sql` normalises the stored column for
  anyone reading the table offline; the server never needed it.
- **Every ladder collapsed to one value: the one the registry blurb states,
  else rank II** (index 1). Seventeen took index 0 because the blurb said so
  (Champions' health multiplier, Deep Wounds, Killing Floor's close, Last
  Rites, Cursed Hoard, the class cards' costs and cooldowns, Faint). Two cards
  lost a rank-gated branch outright: One Ward no longer denies Lichborne (that
  was rank III's addition) and No Sanctuary no longer breaks a bubble on
  creature damage (rank III again). The Shade is always a nemesis; Penance no
  longer exempts Renew.
- `GauntletRules.h` lost `Index()` and every `uint8 rank` parameter; the six
  tempo functions take only what the card knows at the moment it fires.
  `RulesTest.cpp` was rewritten to single-value shape claims ("winning pays
  more than walking away", "a wound opens more than a kill closes").
- **Generator:** `Eligible` refuses any mechanic the run carries, for every
  offer kind; `Mgr::Pick` refuses the same at the door. `BoonTable(mechanic,
  boon)` is the category base plus Frenzy's override. On a full set every slot
  is a swap, and **a swap of a reward-shaped card now pays the reward-shaped
  guarantee** -- it had to: rank-ups of carried reward-shaped cards were what
  paid it before, and without a payer the full-set noReward rate was 42%.
  With it, 21%.
- **The structural consequence, measured in `OfferInvariantsTest.cpp`'s
  comments:** every tier is a fresh pick, so the 16-card cap fills by tier
  17-20 where it used to fill around 49. Sixty of eighty tiers are three
  swaps. Tiers 3-4 lost their exact-zero relaxation (now 3.1% / 7.0%: an early
  set used to have rank-ups to fall back on). Every ceiling that moved has its
  reason in the test. **This makes §7.1 of the plan -- the carry cap by
  rarity -- the loudest open number**, and the greed redesign's loot cards,
  reward-shaped by construction, the fastest way to bring noReward back down.
- Addon: `PROTOCOL_VERSION` 16, rank pip and rank-up wire gone, AFFIX/OFFER
  carry rarity in the third field. GeneratorVersion 15 → 16.
- Checked end to end on `gt-world` (rank-free image, `2026_09_01_01` applied by
  hand because the test realm has `Updates.EnableDatabases = 0`): `leaks all`
  and `soak all` on a level-57 warrior -- 38 audited, soak 0 leaked; the one
  `leaks` LEAK (Bareheaded, auras 20558/26290) is the core dropping a troll's
  unfitting weapon-spec racials on *any* unequip, see §4. `offers 1/20/30` on
  him and `offers 60` on a paladin print rarity and kind with no rank anywhere,
  a Swap only at the swap tiers; `cards` is one line per card; `export-addon`
  is byte-identical to the committed Data.lua; `bench all` -- 38 answered, 0
  leaked, and the five Spawn cards "summoned nothing", which the step-3 image
  reproduces on a third bot: not a regression but the harness's reach (§4).
  `give`/`dump` are `Console::No`, so "a carried card is never offered again"
  rests on the 1.6M-set invariant and `Pick`'s refusal, not on a live run.

### What step 5 has put in place so far (2026-09-01)

The first three loot trades and the boon they pay in -- step 2 of the greed
redesign's order, minus Scavenger's Eye, which is a real mechanic and still
open:

- **`Boon::BonusLoot`** ("Lucky", *things drop N% more often*), appended to the
  enum and paid once for every card that names it, in `Mgr::OnItemRoll`, by
  multiplying the chance the core hands `GlobalScript::OnItemRoll` by
  reference (`LootMgr.cpp:311` for a plain entry, `:1268` inside a group; 100
  or more drops). `BoonLootMult` in `Boons.cpp` is the arithmetic, and
  `Boons.cpp` is now in the Player-free test build (`run-tests.sh` walks
  `src/mechanics/` too; every other file there includes `Player.h` and is
  skipped as before).
- **Magpie** (85, Rules, common: no belt, +15% drops), **Butterfingers** (86,
  Attrition, common: -8% damage, +20% drops), **Night Owl** (87, Attrition,
  **the first uncommon**: +10% damage taken by night, +25% drops).
- **A trade line can fix a condition.** `TradeDef::condition` (default
  `Always`); the generator copies it onto the offer, Pick onto the instance,
  and the aggregate's existing gating does the rest -- the uncommon shape
  costs a field, not a mechanism. Rarity and condition are held to agree
  (`Trades.AnUncommonIsATradeWithACondition`; the registry test that pinned
  everything past 74 as Common now says "a trade line, common or uncommon by
  its condition", and will become a list when Scavenger's Eye lands).
  `NameOf` skips the condition adjective for a trade-born condition --
  "Nocturnal Lucky Night Owl" says night twice.
- The bench's existing `item drop chance` probe (10% in through
  `Mgr::OnItemRoll`) reaches the boon; each trade moved it to 11.5 / 12 /
  12.5%.
- Measured (`build/sweep --rarity`): tier 1 delivers 50% common / 10%
  uncommon / 40% rare against 70 / 25 / 5 wanted. The rare share is
  structural, not a weight: commons live in two families (Rules, Attrition),
  three slots must be three families, so the third slot is rare until commons
  exist elsewhere. Uncommon falls to 3% by tier 13 because one uncommon card,
  once carried, is gone. Tier 12's live-view ceiling moved 55 → 60 (three
  rows reshuffle every draw after them).
- Two fixes the step-4 check surfaced: a swap tier no longer offers a Swap to
  a run carrying nothing (`GeneratorSwaps` test), and the leak audit no longer
  reads auras with an equipment requirement at all -- the core removes and
  adds them on any unequip or equip (Player.cpp:12875, :7267), measured in
  both directions on three trolls and a death knight -- nor item cooldowns,
  which the core sets on equip -- and reads the equipment slots directly
  instead, which is the denials' real question
  (`Audit.AnItemThatDidNotComeBackIsReportedBySlot`).
- Checked end to end on `gt-world` at 03:00 local (night): `bench` on a druid
  in the field -- Magpie "equipping Cord of the Patronizing Practitioner
  refused", Butterfingers "damage done x1.00 -> x0.92", Night Owl "damage
  taken x1.00 -> x1.10", and the item-drop probe 10% -> 11.5 / 12.0 / 12.5%;
  `offers 1` on a mage put "Lucky Night Owl" (Uncommon) in slot 1 with no
  "Nocturnal"; `offers 20` for a run carrying nothing gave three plain offers;
  `cards` lists the three; `export-addon` is byte-identical to the regenerated
  Data.lua. Night Owl's curse is reachable only while its condition holds --
  bench it by day and the summary says "no probe reached it", which is the
  harness being honest, not the card being broken.

### What step 5 built next: the three reward-shaped cards (2026-09-01)

`docs/commons.md`'s step 1, and the measurement it was written for paid off:
tier 1 went from **50/10/40** to **58/34/7** against a 70/25/5 target, the
rare floor collapsing because the reward-shaped guarantee can now be paid
without a rare for the first time.

- **Scavenger's Eye** (88, Uncommon, Enemy): five yards of extra notice
  against Keen-nosed's eight, and a fight nothing touches you in rolls its
  corpse twice. The second roll is a second `Loot::FillLoot`, which *appends*
  through `LootTemplate::Process` -> `Loot::AddItem` (`LootMgr.cpp:561`,
  `:481`), called from `OnLoot` because that hook runs on
  `OnPlayerBeforeSendLoot` -- before the window packet, so the extra items are
  visible.
- **Blood for Bread** (89, Common, Rules) and **Waste Not** (90, Common,
  Rules): no food and drink, no potions, and every kill hands back 8% of both
  bars / 5% of health. Both carry `Boon::None` for Killing Floor's reason --
  the payout *is* the upside, and a BoonClause would promise a second one.
- **New plumbing:** `IMechanic::CanUseItem` and `Mgr::CanUseItem`, mirroring
  the equipment veto, on `PlayerScript::OnPlayerCanUseItem`. Verified that a
  right-click reaches it: `HandleUseItemOpcode` -> `Player::CanUseItem(Item*)`
  -> the `ItemTemplate` overload -> the hook (`PlayerStorage.cpp:2341`,
  `:2432`).
- **Keen-nosed's sweep moved to `Nearby.cpp`** as `AlertUnaware`, shared by
  both cards, and its yardage to `GauntletRules.h` so a test can compare the
  two cards at all.
- **Three new bench probes**, because the bench could see none of these three:
  an item-*use* scan beside the equip one, a health/power reading around the
  kill (the footprint holds *max* health, so a restore moved nothing it
  watched), and the loot-window probe `docs/greed-redesign.md` §7.4 asks for
  by name -- which reaches Carrion too. §4's list of what the harness cannot
  see is three items shorter.
- Checked end to end on `gt-world`: Blood for Bread "using <food> refused",
  "a kill restored health", "a kill restored power", counters `refused 1
  meal(s), fed on 5 kill(s), restored 3520 health and 5110 power`; Waste Not
  the same for potions; **Scavenger's Eye `a clean kill's corpse rolled 3 -> 5
  item(s)`**, which is the doubling itself, with counters `2 clean kill(s), 1
  corpse(s) rolled again`. `leaks` and `soak` clean on all three.
- **What the bench still cannot see: the aggro half.** `alerted 0
  creature(s)` -- there is no idle creature at the right distance during the
  probe, and the character is in combat for most of it, which the sweep
  refuses on purpose. Keen-nosed has always had this blind spot and now two
  cards do. Proving it needs a probe that summons a creature *outside* its own
  aggro range and then checks it attacked, out of combat; until then the
  yardage is covered only by the shape test.
- Four shape tests in `RulesTest.cpp`, and `Registry.TheRewardShapedGuarantee-
  CanBePaidWithoutARare` keeps the finding as an assertion. Two registry tests
  became lists exactly as their comments predicted, and
  `Trades.EveryCommonRowHasALine` became a *rule* instead: a common with no
  trade line must carry `MF_RewardShaped`, since that is the only honest
  reason to be one.

### Step 5, the rest -- and what the sweep says it should be

`docs/commons.md` (2026-09-01) is the measurement and the proposed batch. It
overturns the obvious next move. "More commons in more families" was the
guess; the variant sweep says **fifty-two hypothetical commons across all
seven families leave tier 1 at 54%**, because every set reserves a slot for a
card flagged `MF_RewardShaped` and **all eleven rows that carry that flag are
Rare** -- at tier 1, filtered by window and mask, exactly three exist
(Carrion, Champions, Hubris). Three reward-shaped *non-rare* cards move tier 1
further than twenty ordinary rows do, which is the same thing the note above
Killing Floor (`GauntletRegistry.cpp:554`) measured before rarity existed.

So the order is: **the three reward-shaped cards first** (Scavenger's Eye and
Blood for Bread are already designed in `docs/greed-redesign.md`; the third is
proposed there), then nine uncommons, then ten commons -- measured together at
69/23/8 against a 70/25/5 target. Two things that document settles on the way:
a `Family::Class` common would spend `CAP_CLASS`, the run's class-curse
budget, so class-masked trades stay filed `Rules` as Axeless already is; and
Spawn and Bargain cannot be trade lines at all. It also flags a real
disagreement with the greed redesign, which wants Blood for Bread as an epic.

That document is a proposal, not a decision -- the card list is for cutting.

### Queued behind it: the greed redesign

`docs/greed-redesign.md` (2026-09-01) is the plan for the cards that only slow
the run down, and for the loot cards the table never had. The brief it answers:
hardcore is hard by design, but an offer must tempt — make you faster or richer
while it endangers you. It runs the whole table through a three-question test,
names five brakes (Craven, Grudge, Falter, Cunning, Ambush) and one dead letter
(Iron Purse, replaced by **Blood for Bread**: no eating or drinking, kills
restore you), gives each a redesign on an existing seam with the bench probe it
needs, sharpens three boons to accelerants — and adds **seventeen new offers**
(§7): a `Boon::BonusLoot` paid generically in `Mgr::OnItemRoll`, four loot
trades (the first two uncommons), and twelve loot mechanics from Elite Tithe
("elites always drop their blue") through Fresh Kill, The Tenth Corpse, Wanted
(loot that banks a reroll charge), Mimic and Dragon's Hoard to a legendary, The
Vault. Every seam is verified against the core and the world database (§7.1:
`FillLoot` appends, `SummonGameObject` of the world's own level-banded chests
needs no new rows). Steps 1, 2 and 7 of its order (Blood for Bread, the loot
boon and trades, the loot cards) are born rank-free and can land any time; the
redesigns of existing cards waited for the rank removal, which has landed --
nothing blocks any of it now.

Things step 2 left for a later pass, deliberately:
- The **stat-grant primitive** (§3, "grant a stat" — expertise, ratings) is not
  built; the first ten pay through the existing Boon plumbing. It needs a
  class-neutral carrier spell per aura type read out of Spell.dbc.
- `FAMILY_WEIGHT` still gives Rules 1 against Attrition 3, so at tier 1 the
  three coefficient trades are drawn about three times as often as any one of
  the seven denials. Left as measured; the comment on the weights
  still reasons from a Rules family of three, which is no longer true.
- Whether commons stack too freely (+8% damage four times over) is §7.1.

## 8. Known-open, not assigned

- `docs/checklists.md` — 672 lines of in-game checks, still mostly undone.
- `.gauntlet debug remove all` does not exist, so undoing `give-class` means
  removing ~30 affixes one slot at a time.
- The bench cannot reach the *aggro* half of Keen-nosed or Scavenger's Eye
  (§7): it needs a creature summoned outside its own aggro range, with the
  character out of combat, and then an assertion that it attacked.
- `bench` reports `Spawned nothing` for all five Spawn cards on an idle bot,
  for the reasons §4 measures. The job: have the bench's target attack the
  player (`target->Attack(player, true)` puts it in `getAttackers()`), kill
  enough XP-worthy targets to reach Echo's 25, hold the bot still for Ambush's
  20 s, loot a corpse for Carrion, and refuse to bench a Spawn card on a bot in
  a city rather than report it. Until then a Spawn card's spawn is proven only
  by a bot out in the field, and Reinforcements, Echo, Ambush and Carrion not
  at all.
- ~~The leak audit's false positive on unfitting item-dependent passives; the
  Swap offered to a run carrying nothing at a swap tier.~~ Both fixed
  2026-09-01, §7.
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
