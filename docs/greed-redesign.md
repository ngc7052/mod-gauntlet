# Every card must tempt: the greed redesign

**Direction decided; every number is judgement.** The brief, in the words it
was given: *hardcore is hard by design, but offers should give fun, and make you
faster while putting you at risk* — and, on reading the first draft, *not enough
new offers; some should actually offer loot.* This document turns that into a
test, runs the whole table through it, writes the redesigns for the cards that
fail (§3), and adds **seventeen new offers** (§7), twelve of them about loot —
the greed axis the table barely touches: of seventy-nine cards, exactly one,
Cursed Hoard, has anything to say about what drops.

It is the third pass of its kind. `docs/tempo-redesign.md` turned six taxes into
decisions ("a moment and a verb"). `docs/rarity-plan.md` replaced the rank ladder
with rarity so a run escalates by *what* it carries, not by bigger numbers on the
same thing. This one asks the question neither of those asked: **does anyone
want this card?**

---

## 0. The test

Hardcore is allowed to kill you. It is not allowed to bore you. The difference
between the two is the whole of this document, so it is spelled out as three
questions asked of every card:

1. **Is there a moment?** Something happens at a time, and the player does
   something about it. (The tempo redesign's rule; kept.)
2. **Does the run get faster or richer somewhere because of this card?** Not
   "does the boon say +5%" — does carrying it make you *want* to do something
   you would otherwise not, because it pays? Champions' eighth fight pays double.
   Cursed Hoard's chest is worth opening. That is the shape.
3. **Is the risk chosen?** Can the player lean *in* — take more of the card's
   danger on purpose for more of its pay — or does the danger merely happen to
   them?

A card that passes all three **tempts**. A card that fails the second is the
one this document exists for, and it has a name: a **brake**. A brake is a card
whose only effect on the run is that the run takes longer. Craven turns every
kill into a chase. Grudge makes you step off every corpse and wait. Falter stops
you playing for three seconds on a timer. Not one of them ever pays.

Two clarifications the test needs, because they are where the argument usually
goes wrong:

- **Hard is not slow.** A cooldown tax on a panic button (Cold Trail, Dead
  Weight, Cold Feet) raises the *stakes* of a mistake and costs no time. Those
  are stakes cards; they pass. The design doc's §2.9 already names the failing
  shape: "Chores — costs time, not skill." A brake is a chore with a monster in
  it.
- **A boon is not a temptation.** Every card in the table pays a boon and most
  of them still fail question 2, because "+8% damage" on the card is not a thing
  you *do*. The temptation has to be inside the mechanic — the eighth fight, the
  chest, the runner you can catch — and the boon is what makes the card worth
  carrying between those moments.

### The boon rule that falls out of it

Boons come in two kinds. **Accelerants** make the run faster: damage,
experience, movement speed. **Cushions** make it safer: maximum health, healing
received. Under this brief the default is the accelerant, and for a reason
sharper than theme: a card that adds danger and pays a cushion is negotiating
with itself. Killing Floor's two ladders were once exact complements and
`RulesTest.KillingFloorMakesWinningWorthMoreThanWalkingAway` caught the card
having no decision in it; a danger curse paid back in health is the same fault
with more steps.

Not a ban. A card whose *curse* is tempo rather than danger — Falter's stumble
gets you hit; Ambush interrupts a rest — can honestly pay a cushion, because the
cushion does not cancel the thing the card does. But "which kind of boon" is
now a question each row answers on purpose, and §3's table answers it.

### What this does not touch

- **The commons.** Ten "lose X, gain Y" trades, deliberately small
  (`docs/rarity-plan.md` §2). Their job is texture and reach, not moments, and
  the test is not applied to them.
- **The stakes cards.** Named in §5. They are hard; that is the premise.
- **The six the tempo redesign already fixed.** They pass.

---

## 1. Measured: the table against the test

Thirty-six rows every class can be offered (ids 1–27 less the deleted 21 and 22,
74, and the ten commons), and forty-three class curses.

### World families

| Card | Moment | Pays inside | Lean in | Verdict |
|---|---|---|---|---|
| The Shade | yes | Vindication: +25% XP for five minutes on the kill | fight it or outrun it | **tempts** |
| Echo | yes — you author the 25th kill | the echo itself | choose which kill | **tempts** |
| Carrion | yes | no — the scavengers are only a cost | loot in cleared ground | fine; payout thin |
| Reinforcements | yes — the 30 s clock | no, but the clock *is* pace | kill faster | fine |
| Ambush | yes | no | none — the answer is "walk away" | **brake on recovery** |
| Champions | yes | double reward | the eighth fight | **tempts** — the exemplar |
| Craven | the flee | no — a runner is only a chase | none | **brake** |
| Call to Arms | the kin arrive | no | fight where the kin are | fine; payout thin |
| Death Rattle | yes — the burst | no | none | fine (a dance) |
| Grudge | the spirit rises | no | none — step off and wait | **brake** |
| Nimble | no | no | none | ambient; boon is a cushion |
| Cunning | the kick | no | fake-cast | **brake** for casters |
| Keen-nosed | no | no | none | ambient; boon is an accelerant |
| Falling Sky | yes | speed for a clean dodge | keep moving | **tempts** |
| Frenzy | yes | the chain | chain-pull | **tempts** |
| Overextended | no | no | face them | fine (positional) |
| Falter | yes — on a schedule | no | none | **brake** |
| Hubris | yes — the opener | the shelter | pick the duel | **tempts** |
| Deep Wounds | yes | kills close wounds | keep killing | **tempts** |
| Blood Magic | no | no — health is spent, nothing bought | none | ambient; payout thin |
| Killing Floor | yes | the bank on a kill | push on | **tempts** |
| Self-found | identity | — | — | fine (a conduct) |
| Lone Wolf | identity | +XP alone | stay solo | **tempts** |
| Iron Purse | no | no | none | **dead letter** |
| Last Rites | yes | the second life | — | **tempts** |
| Cursed Hoard | yes | double loot | open the chest | **tempts** — the best card in the table |

Five brakes, one dead letter, four thin payouts. Nine of the twenty-six rares
tempt outright — and seven of those nine are cards a redesign already touched
or a bargain. The table's *original* world cards, taken as written from the
design doc, mostly fail question 2. That is not surprising: the design doc's
§2.8 principles are about answerability and telegraphing, and its one greed
principle ("Task with a reward", number 6) was the one least applied.

### Class curses, in three heaps

The forty-three are triaged rather than tabled, because most of them share a
shape and the fix is a pattern, not a card.

- **Already tempt (7):** Berserker's Bargain, Arcane Frailty, Shared Blood,
  Deafening Roar, Consecrated Ground, Ankh Pact, Stone of the Damned. Every one
  pays *inside* — more damage below 35%, a stronger demon, free shouts, a
  second life.
- **Stakes cards (about 30):** cooldown taxes on panic buttons, resource-cap
  and resource-floor verbs, positional and commitment rules. Cold Trail, Dead
  Weight, Cold Feet, Iron Discipline, Long Forbearance, Rune-starved, Faint,
  Red Mist, Wide Dead Zone, Exposed Back, Commitment, Commitment of Roots,
  Totemic Anchor, One Totem, Elemental Overload, Fickle Sheep, Fel Pact, Grave
  Call, Half-Tamed, Two Faces, Nature's Toll, Bound Skin, One Ward, Cold
  Presence, Faithless Form, Frail Soul, Penance of Silence, Spirit Debt,
  Whispers of the Deep, Slow Hands, Shard Economy. They make mistakes cost
  more and cost no time. They stay as they are.
- **Brakes and dead letters (5):** **No Sanctuary** does nothing in nearly
  every run — the cheese it forbids is rare, and a card that never acts reads
  as broken (the Deafening Roar lesson). **Blood Bond**, **Poisoned Blades**,
  **Affliction of the Self** and **Mana Burn** are ambient self-damage taxes: a
  share of something you do comes back at you, forever, with no moment and no
  way to lean in. §3 gives them a pattern; the numbers are a per-class pass.

---

## 2. The rules the redesigns follow

The tempo redesign's two rules stand — a moment and a verb; cards should chain —
and four join them:

3. **Faster or richer, never merely slower.** If a card's whole effect is that
   the run takes longer, it is redesigned or retired. Time is the one weapon
   the module is not allowed.
4. **Boons accelerate by default.** Damage, experience, movement speed. A
   cushion is a choice made on purpose, for a curse that costs tempo rather
   than health.
5. **Lean-in over ambient.** Each card should have a way to volunteer for more
   of its risk for more of its pay. Dead Cells' cursed chest is the model the
   design doc already cites: "the player authors the danger."
6. **One number, not a ladder.** The rank ladder is being removed
   (`docs/rarity-plan.md` §5b). Every value below is a single value at the
   card's rarity. No `constexpr X[] = { a, b, c, d }` is written for any of
   these, and the tempo redesign's ladders collapse with the rest in step 4.

---

## 3. The redesigns

| Card | Was | Becomes | The verb | The greed |
|---|---|---|---|---|
| **Craven** | Enemies flee at 25% and come back with friends | Runners flee at 25%; a runner **cut down before it reaches its camp** pays double XP and rolls its loot twice; one that gets there fetches a friend | execute, snare, burst | every kill can be worth two |
| **Grudge** | A spirit rises on every corpse and drains you | The spirit rises **four seconds after** the kill; **loot the corpse first** and it never forms, and the corpse rolls once more | loot fast | the extra roll |
| **Falter** | Every 45 s your hands fail for 3 s | The same — and the **first blow or cast after they return is a Reprisal**, +50% | plan the stumble, set up the return | a burst window every 45 s |
| **Cunning** | Melee enemies kick your cast every 12 s each | The same — and a cast that **completes with a kicker in melee range** hits for +40% | fake-cast, root, then commit | casting in their face is the high roll |
| **Ambush** | Resting in the wild draws an ambush; walk away | The same — and **killing the Ambusher finishes your rest**: health and mana to full, and the rest clock is spent | stand and fight, or stand and move | a fight instead of a drink |
| **Iron Purse** | Repairs cost double | **Retired.** Replaced by **Blood for Bread**: you cannot eat or drink; every kill restores 8% of your health and mana | keep pulling | downtime deleted |
| **Nimble** | Enemies +40% speed; +max health | Unchanged curse; the boon becomes **+movement speed** | commit to the fight | they are faster; so are you |
| **Call to Arms** | Kills alert the nearest kin; +XP | Unchanged — and **kin that answer the call pay +25% XP** each | fight where the kin are | the pack comes to you, and pays |
| **Blood Magic** | Spells cost 3% health; +damage | Unchanged — and **below 35% health spells cost no health and hit for +25%** | spend down, then cast | Berserker's Bargain for casters |
| **No Sanctuary** | Hearthstone denied under Divine Shield | **Retired.** | — | — |

### How they chain

- **Kills pay in four more places.** Craven's caught runner, Grudge's fast
  loot, Call to Arms' answering kin, Blood for Bread's restore. With the tempo
  redesign's three (Killing Floor's bank, Deep Wounds' closing, Frenzy's chain)
  the run has one verb and seven reasons for it: *keep winning fights*.
- **Blood for Bread plus Killing Floor is deliberately brutal**, as Killing
  Floor plus Deep Wounds already is: nothing heals until you kill, and nothing
  heals *between* fights at all. The offer builder prices it; the design does
  not soften it.
- **Falter's Reprisal is Frenzy's friend and Cunning's cousin.** A Reprisal
  lands hardest on a full Frenzy chain; Cunning's landed cast is the same idea
  for a caster — the card takes your buttons and then pays you for the button
  you get back.
- **Ambush now argues with Blood for Bread.** One says a fight *is* your rest;
  the other says you cannot rest at all. Carried together they say the same
  thing from two sides, which is the interlock the tempo redesign asked for.

### Card by card

Every number is `TODO(design)` until played. Each entry names the seam it lands
on, because a proposal that needs a client patch is not a proposal here.

#### Craven — the bounty on the runner

*Flee at 25%. A runner cut down before it reaches its camp pays double
experience and rolls its loot twice. A runner that reaches its camp comes back
with one friend.*

- **Why.** The chase is the brake. Today the card makes every kill slower and
  then punishes you for the slowness. The redesign keeps the flee and makes the
  chase a race you can win: execute-range awareness, snares, roots and burst
  are the buttons the card always asked for; now they are worth pressing.
- **Seam.** `Craven.cpp` already tracks its runners by guid with the flee
  timer. A tracked runner killed before the timer ends is the bounty:
  `OnXP(ctx, amount, victim)` doubles when `victim` is a tracked runner
  (the hook already carries the victim), and `OnLoot` flags the corpse so
  `OnItemRoll` doubles the chance once. Both hooks exist and are dispatched.
- **Numbers.** Flee 25%, double XP, one extra roll, one fetch. The design doc's
  ladder had fetches rising to three; with the bounty in, one is enough — the
  card no longer needs to punish to matter.
- **Boon.** Stays +damage (an accelerant, and the card's tool).
- **Rarity.** Rare. It is a verb with a prize, not a system change.

#### Grudge — loot fast

*Everything you kill leaves a Restless Spirit on its corpse — four seconds after
it dies. Loot the corpse before the spirit rises and it never forms, and the
corpse yields one more roll. Stand in a spirit and it drains 5% of your health a
second.*

- **Why.** "Step off and wait twenty-five seconds" is a chore. The redesign
  turns the same spirit into a clock the player can beat, and looting — the
  thing a levelling character does after every kill anyway — into a speed
  verb. The spirit still punishes standing around; it just no longer punishes
  playing quickly.
- **Seam.** `Grudge.cpp` raises the spirit in `OnKill`; raising it through the
  scheduler four seconds later instead is the same code with a delay, and the
  Scheduler is already in the file. `OnLoot(ctx, lootGuid, loot)` fires when
  the corpse is opened with the corpse's guid — cancel the pending raise for
  that guid, set the extra roll for `OnItemRoll`. The 25-second life, the
  4-yard radius and the half-healing inside stay.
- **Numbers.** 4 s window, 5%/s drain, one extra roll.
- **Boon.** +healing received (a cushion) becomes **+movement speed**: get to
  the corpse first. Rule 4.
- **Rarity.** Rare.

#### Falter — the stumble and the Reprisal

*Every 45 seconds in combat your hands fail you for three seconds — disarmed if
you fight with weapons, silenced if you cast; you are warned two seconds ahead.
The first blow or cast after they return is a Reprisal, and hits for half again.*

- **Why.** A scheduled three seconds of not playing is the purest brake in the
  table; the planning it asks for ("do not be mid-burst") is real but pays
  nothing. The Reprisal makes the schedule a rhythm worth hitting: the stumble
  is when you reposition and line up the big one.
- **Seam.** The warn/fire pair already exists on the scheduler. When the
  disarm or silence *ends*, open a five-second Reprisal window
  (`_reprisalUntilMs`); `DamageDoneMult(ctx, victim, spellInfo)` returns 1.5
  while it is open and the observer `OnCreatureDamaged` closes it on the first
  landed hit. The addon's `EVT` already draws the countdown; the window wants a
  `STAT` so the HUD shows "Reprisal" while it is up.
- **Numbers.** 45 s cadence, 3 s failure, +50%, 5 s to use it.
- **Boon.** Stays +max health: the curse costs tempo and gets you hit, so a
  cushion does not cancel it.
- **Rarity.** Rare.

#### Cunning — the cast that lands

*Enemies in melee range kick the spell you are casting, once every 12 seconds
each. A cast that completes with a kicker in melee range hits for 40% more.*

- **Why.** Interrupts feel like lag, and a card that only interrupts is a
  brake with a special hatred for casters. The fix keeps the whole puzzle
  (fake-cast, root, cast at range) and adds the reason to *stop* solving it:
  once the kicker has spent its kick, the next cast in its face is the high
  roll.
- **Seam.** `Cunning.cpp` tracks kickers by guid with the per-attacker
  cooldown. `OnSpellCast` fires as the cast completes (`Spell.cpp:3776`); if a
  tracked kicker is in melee range at that moment, mark the spell, and
  `DamageDoneMult` pays it for that `spellInfo` and clears the mark.
- **Numbers.** 12 s per kicker, +40%.
- **Boon.** Stays +damage.
- **Rarity.** Rare. Still class-gated to casters; the design doc's
  "free heat" argument holds.

#### Ambush — the ambusher carries your supper

*Resting in the wild draws an ambush: four seconds of footsteps, then an
Ambusher. Kill it and it finishes your rest — health and mana to full — and the
rest clock is spent.*

- **Why.** Today the only answer is "stand up and walk three steps", which is a
  chore that interrupts the one slow activity the game has. The redesign gives
  the rest a second answer: fight for it. A fight that replaces a thirty-second
  drink is the run getting faster, and choosing to sit through the footsteps
  is the lean-in.
- **Seam.** `Ambush.cpp` owns its summon through `GauntletSummons`, and
  `OnKill` plus `sGauntletSummons->IsGauntletSummon(creature)` (Nimble uses
  it) identifies the Ambusher. Full health and power are `SetHealth` and
  `SetPower`; a visible existing self-heal for the moment is the same
  "existing spell, overwritten amount" technique `FallingSky::Reward` documents.
- **Numbers.** Footsteps 4 s, restore 100%. The stillness window stays at the
  card's 20 s (its one value since the ranks went).
- **Boon.** Stays +max health — the curse is about rest, not danger.
- **Rarity.** Rare.

#### Iron Purse — retired; Blood for Bread takes the seat

*You cannot eat or drink. Every kill restores 8% of your health and mana.*

- **Why retire.** `IronPurse.cpp` says it itself: "the weakest row in the
  table … If a later phase wants the slot back, this is the row to spend." Its
  one argument for existing was structural — the cheapest row that widened tier
  1 — and the rarity plan's step 2 put seven commons into the Rules family at
  tier 1. The argument is gone, and repair costs are the design doc's own
  example under "Chores".
- **Why this replacement.** It is the most literal reading of the brief in the
  table: the run is *faster* (no drinking between pulls) and *riskier* (your
  only recovery is another fight). It is the design doc's §2.10 rule applied
  exactly — "subtraction alone is ignored; a removed verb needs a granted one"
  — and it is what separates it from §2.9's "cannot use potions": the potions,
  bandages and panic buttons all stay. Only the sitting-down goes.
- **Seam.** The use veto is the equipment veto's twin:
  `PlayerScript::OnPlayerCanUseItem(Player*, ItemTemplate const*,
  InventoryResult&)`, gated on `ITEM_CLASS_CONSUMABLE` and the food/drink
  subclass, dispatched the way `Mgr::CanEquip` is. The restore is `OnKill`
  with `SetHealth`/`ModifyPower`, which is what Killing Floor's payout already
  does. New id 85; Iron Purse's id 25 is spent forever, and a stored row for
  it resolves to nothing, which `Mgr::Load` already tolerates.
- **Numbers.** 8% of maximum health and of primary power per kill.
- **Boon.** None. The restore is the boon, written into the card, as Killing
  Floor's is.
- **Rarity.** **Epic.** It changes how the whole run is paced, which is the
  plan's definition of the tier.

#### The three sharpenings

- **Nimble**: the curse is honest ambient pressure and stays; the boon moves
  from max health to **movement speed**. "They are faster; so are you" is the
  card the blurb was already describing.
- **Call to Arms**: kin the kill alerts pay **+25% XP** each. `CallToArms.cpp`
  is what sends them, so it knows their guids; `OnXP` with the victim does the
  rest. Fighting next to the camp becomes the lean-in instead of the mistake.
- **Blood Magic**: below **35% health, spells cost no health and hit for
  +25%**. Berserker's Bargain's shape, for the classes that pay in mana. The
  curse above the line is unchanged; the line is where the card starts to pay.

#### The class pass: ambient self-damage becomes a lean-in

Blood Bond, Poisoned Blades, Affliction of the Self and Mana Burn all have the
shape "a share of X comes back at you". The pattern that fixes all four: **the
share that comes back also charges something.** Poison that ticks on you
stacks poison damage; a demon's shared wounds stack its damage; curses that
afflict you stack their duration on the enemy; damage that burns your mana
refunds it on the kill. Numbers are a per-class pass with each class's kit
open, not this document's. No Sanctuary is retired outright: a card that never
acts is indistinguishable from a broken one, and it has nothing to become.

---

## 4. What the harness cannot see, and what it needs

The bench asserts "the thing the card is for" (CLAUDE.md, the testing rule).
Four of these cards are for something the bench cannot currently produce:

| Card | The assertion | What the bench lacks |
|---|---|---|
| Craven | a caught runner paid double XP | the bench damages its target by a third and then kills it; a runner needs to be under 25% first. One more `DealDamage` before the kill probe. |
| Falter, Cunning | the Reprisal / the landed cast paid | the "versus a target" multiplier read happens *before* the bench fires the card's events. A second read after "firing its own event". |
| Ambush | killing the Ambusher restored health | the bench kills its own target, never the card's summon. A probe that kills the newest summon the card owns. |
| Blood for Bread | food is refused; a kill restored | the use veto has no probe; the equipment probe's shape — offer every consumable template — is the answer. The restore is already visible: the bench wounds to 15% before the kill. |

And the shape tests, in `GauntletRules.h` where the arithmetic goes:

- *A caught runner is always worth more than a fled one* (Craven).
- *The Reprisal pays only after a failure, and only once* (Falter).
- *A landed cast pays only with a kicker in range* (Cunning).
- *Blood for Bread's restore per kill is below a drink's worth, or eating was
  never a cost* — the one number in this document most likely to be wrong.

---

## 5. What deliberately stays hard

The stakes cards of §1 — the cooldown, resource and positional class curses,
Reinforcements' clock, Death Rattle's burst, Overextended's facing, Keen-nosed's
routing — are not softened by anything here. They cost no time; they cost
mistakes. That is hardcore, and it is the premise this document was written
under, not a problem it solves. The test in §0 is a filter for boredom, not for
difficulty, and a redesign that made a card easier would have failed it.

---

## 6. Order of work

1. **Blood for Bread, and Iron Purse retired.** Smallest, most literal, no
   ladder to collapse (Rules rows are single-rank already). It can land before
   the rank removal.
2. **The loot boon and the four loot trades** (§7.2). Plumbing first, then
   rows: the trades are table lines, and `Boon::BonusLoot` is what every later
   loot card's boon column names. **Landed 2026-09-01** — the boon, Magpie,
   Butterfingers and Night Owl; Scavenger's Eye is a mechanic, not a line, and
   is still open.
3. **Craven and Grudge** — the two brakes felt on every single kill.
4. **Falter and Cunning** — the role taxes.
5. **Ambush.**
6. **The three sharpenings** (Nimble, Call to Arms, Blood Magic).
7. **The loot cards** (§7.3), in the order given there: Elite Tithe and Fresh
   Kill first, because they are the two the brief named.
8. **The class pass**, one class at a time, with the kit open.

Steps 3–6 wanted to land **after** the rarity plan's step 4 (rank removal),
which landed on 2026-09-01: every one of those cards is one value now, and
nothing here waits on anything else. Steps 1, 2 and 7 never had the
dependency — new rows are born rank-free — and step 2 is where this started. The rarity column above and in §7 feeds the plan's
§7.4 pass directly: the cards with a greed loop inside them are the epic
candidates, and Blood for Bread, The Tenth Corpse, Dragon's Hoard and Reliquary
arrive as ones.

---

## 7. New offers: the loot cards

Loot is the one greed that WoW already knows how to pay. Every kill in the game
is followed by the same small hope, and the module has never once touched it.
These cards do, and they are written against what the core actually lets a
module do to loot without a client patch — checked, not assumed.

### 7.1 The seams

| What a card wants | How the core lets it | Where |
|---|---|---|
| **Force a drop** | `GlobalScript::OnItemRoll` hands the module every candidate item's chance by reference, one item per roll; setting it to 100 is a guarantee. Carrion already uses it. The module's adapter drops the `LootStoreItem` on the way to `IMechanic::OnItemRoll`; the cards below need the item id (for its quality), so the adapter grows a parameter. | `GlobalScript.h:68`, `GauntletScripts.cpp:665` |
| **Roll a table twice** | `Loot::FillLoot` calls `LootTemplate::Process`, which *appends* through `Loot::AddItem`. Calling it a second time on a corpse's `Loot` with the creature's own `lootid` is exactly "it drops twice". The corpse's `Loot*` is handed to the module by `OnPlayerBeforeSendLoot` before the window opens. | `LootMgr.h:377`, `LootMgr.cpp` (`tab->Process`), `PlayerScript.h:293` |
| **Empty a corpse** | `Loot::clear()`, or clearing `Loot::items` alone so `quest_items` survive — a card may take your greens, never your quest. | `LootMgr.h:343` |
| **Spawn a real chest** | `WorldObject::SummonGameObject(entry, x, y, z, ang, rot…, respawnTime)`. The world database already holds level-banded treasure chests with hundreds of loot rows each and existing display ids: Battered Chest 2849 (loot 2280, 712 rows), Solid Chest 2850, Large Iron Bound Chest 74447, Solid Chest 153451 (the sixties), Fel Iron Chest 181798, Bound Adamantite Chest 184936, Ancient Drakkari Chest 190552, Dark Runed Chest 190663. A chest for the player's level is a lookup, and **no new database rows are needed at all.** | `Object.h:641`; `acore_world.gameobject_template`, type 3 |
| **Tell an elite from a trash mob** | `Creature::isElite()`, `isWorldBoss()`, `IsDungeonBoss()`, and `CreatureTemplate::rank` for the silver-dragon rares `isElite()` excludes. 7,457 templates are elite or rare-elite. Champions already reads all three. | `Creature.h:115-132` |
| **Hand over an item** | `Player::AddItem(itemId, count)`; the potion ladder is existing items: Minor 118 → Lesser 858 → Healing 929 → Greater 1710 → Superior 3928 → Major 13446 → Super 22829 → Runic 33447, and the mana potions 2455 → 33448 beside them. | `Player.h:1429` |
| **Refuse a purchase or a use** | `OnPlayerCanUseItem` (the equipment veto's twin) and `OnPlayerBeforeBuyItemFromVendor`, which hands the item id by reference. | `PlayerScript.h:593`, `:466` |
| **Charge per item looted** | `OnPlayerStoreNewItem`. | `PlayerScript.h:439` |
| **Pay a reroll charge** | `RunKeys::RerollCharges` in the state store, as Skip does. Loot that pays in the offer economy's own currency is the link between step 3 and this document. | `Gauntlet.h`, `Mgr::Skip` |

Two honest limits. **Quality cannot be invented**: a card can make an elite drop
the blue in its table, it cannot put a blue in a table that has none; a chest's
contents are the zone's, which is greens and greys at level and the occasional
blue. And **loot is shared**: a second roll on a corpse in a group is the
group's, which is generous and acceptable, but a card that *empties* a corpse
must apply only to a corpse the player killed alone — one player's curse may
never be the group's (design §2.9, "role-exclusive burden").

### 7.2 A boon that pays in loot

The generic boons are damage, healing, speed, experience, health. **None of
them is loot**, and loot is the accelerant WoW's own players chase hardest.

`Boon::BonusLoot` — appended to the enum, as the enum's rule requires; named
*Lucky*; clause "In exchange, things drop N% more often"; paid once, generically,
in `Mgr::OnItemRoll` by multiplying every candidate's chance by the carried
magnitudes. One delivery for every card that names it, the way `BoonSpeed::Sync`
pays movement speed. Four trades name it on arrival; every loot card below can.

| Card | Rarity | Family | The trade | Seam |
|---|---|---|---|---|
| **Magpie** | Common | Rules | You cannot wear a belt. Things drop 15% more often. | a `TradeDef` line: deny `INVTYPE_WAIST`, `Boon::BonusLoot` |
| **Butterfingers** | Common | Attrition | You deal 8% less damage. Things drop 20% more often. | a `TradeDef` line: coefficient on DamageDone |
| **Night Owl** | Uncommon | Attrition | By night you take 10% more damage, and things drop 25% more often. | `Condition::AtNight` — the condition axis has been kept idle since Phase 2 exactly for the uncommon tier's "a trade with a condition" |
| **Scavenger's Eye** | Uncommon | Enemy | Enemies notice you from five yards further. A fight in which you are never hit rolls its loot twice. | Keen-nosed's radius seam; `OnDamageTaken` dirties the fight, `OnLoot` rolls again when it is clean |

Two commons, two uncommons — the first uncommons in the table, and the shape
the rarity plan's §2 gave that tier.

### 7.3 The loot cards

| Card | Rarity | Family | Was never | Becomes | The greed | The risk |
|---|---|---|---|---|---|---|
| **Elite Tithe** | Rare | Enemy | — | Elites always drop everything uncommon or better in their pockets. Elites hit you 25% harder. | the elite's blue is yours | the elite |
| **Fresh Kill** | Rare | Rules | — | A corpse looted within 8 seconds of the kill rolls its loot twice. After that it holds nothing but the quest. | double on every kill | you loot mid-pull |
| **The Tenth Corpse** | Epic | Rules | — | Corpses hold nothing until the tenth. The tenth holds everything the nine before it carried. | nine loot windows you never open — the run is faster | die before the tenth and it is all gone; nothing drops to save you on the way |
| **Tribute** | Rare | Spawn | — | Every 25th kill, a treasure chest appears at the corpse. Opening it draws two scavengers. | a chest | Carrion's scavengers, chosen |
| **Wanted** | Rare | Enemy | — | Every fourth fight, the biggest thing in it is Wanted. Kill it within 30 seconds and it drops a guaranteed uncommon or better and **banks a reroll charge**. Let the 30 seconds pass and it heals to full and hits half again as hard until it dies. | loot, and the offer economy's own currency | a clock with teeth |
| **Quartermaster** | Rare | Rules | — | Every 20 kills, a supply crate: a healing potion, a mana potion and food of your level. You may buy none of the three. | no vendor trips — the run is faster | supplies are rationed by kills |
| **Mimic** | Rare | Spawn | — | Every third treasure chest you open is a Mimic. It fights. It drops what it was pretending to hold, twice. | double chest | the chest fights back |
| **Blood Price** | Rare | Attrition | — | Opening a corpse costs 3% of your health. A corpse opened below half health rolls its loot twice. | loot low, loot double | looting is the dangerous act |
| **Trophy Hunter** | Uncommon | Enemy | — | While a rare creature is alive within a hundred yards you take 15% more damage. Killing one drops a treasure chest and banks a reroll charge. | the silver dragon is a payday | it is a silver dragon |
| **Dragon's Hoard** | Epic | Bargain | — | Every elite you kill leaves a treasure chest. Every chest you open makes every elite 10% stronger, for the rest of the run. | chests feed danger feeds chests | you author the escalation, and it never comes down |
| **Reliquary** | Epic | Bargain | — | Dungeon bosses drop one extra roll of their loot for every four affixes you carry. You take 25% more damage from dungeon bosses. | the run pays itself back | boss fights |
| **The Vault** | Legendary | Bargain | — | Nothing you kill drops anything. At every tenth tier a Vault appears holding ten rolls of everything the run has killed since the last one. | the biggest chest in the game, eight times a run | ten levels at a time with no drops at all — no upgrades, no potions, no luck |

#### Card by card

**Elite Tithe** — the brief's own example. `OnItemRoll` with the item id: for a
corpse whose source is `isElite()`, every candidate of `ITEM_QUALITY_UNCOMMON`
or better goes to 100%. `DamageTakenMult` pays the 25% when the attacker is an
elite. Bench: the target is the module's Ambusher — the bench needs a second,
elite target entry for this card, or a probe that promotes its target the way
Champions does. Boon: none; the card is its own.

**Fresh Kill** — `OnKill` records (guid, time). `OnLoot` within the window
calls `FillLoot` again with the creature's `lootid`; outside it, clears `items`
and leaves `quest_items`. Solo corpses only, per §7.1's rule. It chains with
the Grudge redesign — both say *loot fast* — and Frenzy's chain is what a
mid-pull loot costs you. Boon: **movement speed**. Bench: the kill probe
followed by a loot-window probe on the real corpse, which the bench does not
yet have (§7.4).

**The Tenth Corpse** — the same two hooks, with a counter in the state store
and a `CTR` on the HUD. Nine corpses are cleared (quest items kept) and their
`lootid`s recorded; the tenth is filled ten times. Exclusive with Fresh Kill
through a new key, `loot-rhythm`: two cards that rewrite when a corpse pays are
one card twice. It is an epic because it changes how the whole run loots, and
it is the card in this document most likely to be *too* good: nine skipped
loot windows is real speed, and the risk lands only on death. Boon: none.

**Tribute** — `OnKill` counts; the 25th summons the chest for the player's
level at the corpse with a two-minute despawn. `OnLoot` with a game-object guid
the card owns summons two of Carrion's scavengers at the chest — Carrion's
spawn path, reused, not copied. Boon: **movement speed**, Carrion's, for the
same reason.

**Wanted** — `OnEnterCombat` counts fights; on the fourth, the attacker with the
most maximum health is marked (`EVT` countdown, `SUMMON`-style light on the
HUD naming it). `OnKill` of the marked guid inside the window: `OnItemRoll`
guarantees the best-quality candidate and `RunKeys::RerollCharges` goes up by
one. Timeout: `SetHealth(GetMaxHealth())` and a `DamageDoneMult`-side buff read
from the guid. The card ties the loot axis to the reroll economy — the same
currency skipping pays in — which is what makes it more than a bounty.
Boon: **damage**.

**Quartermaster** — `OnKill` counts; the 20th calls `Player::AddItem` for the
potion pair of the player's level and a food item of the same band.
`OnPlayerBeforeBuyItemFromVendor` refuses the same three classes of item by
zeroing the id — the idiom to confirm at implementation. Boon: none; the crate
is the boon. It chains with Blood for Bread, which forbids the food it hands
you — a real anti-synergy the builder prices, not one to soften.

**Mimic** — `OnLoot` with a chest guid counts chests. On the third: the chest
is despawned before the window fills, a hostile creature stands in its place
— the module's own Ambusher entry with a mimic's name, since a creature cannot
wear a game object's display — and its corpse's `Loot` is filled twice from
the chest's `lootid` out of `LootTemplates_Gameobject`. The one card here whose
first version may fail in a way only a client shows: whether an emptied,
despawned chest leaves a stray loot window open. Boon: **damage**.

**Blood Price** — `OnLoot`: cost 3% (never lethal — floored at 1 health, the way
Blood Magic's cost is), and if the player is under half, `FillLoot` again.
`OnPlayerStoreNewItem` is the alternative seam, per item rather than per
window, and is rejected: a window is one decision, an item is not. Boon: none.

**Trophy Hunter** — the uncommon: a trade with a condition. `CreatureTemplate::
rank` of `CREATURE_ELITE_RARE` or `RAREELITE` within a hundred yards is the
condition, read on the tick the way Keen-nosed reads its grid; `OnKill` of one
summons the chest and banks the charge. Boon: none.

**Dragon's Hoard** — the Gungeon curse, made explicit: "greed spawns a
stalker", here greed spawns a stronger world. `OnKill` of an elite summons a
chest; `OnLoot` of one the card owns increments `hoard.opened` in the state
store — persisted, because "for the rest of the run" has to survive a logout;
`DamageTakenMult` from elites and `DamageDoneMult` against them read it. The
escalation is *chosen per chest*, which is the whole of rule 5, and it never
comes down, which is the whole of the risk. Bargain, so tier 30 and up, which
is right: it needs a run worth escalating. Boon: none.

**Reliquary** — `OnLoot` on a corpse whose source `IsDungeonBoss()`: one extra
`FillLoot` per four carried affixes. `DamageTakenMult` from a boss pays the
25%. The design first had the boss's health scale with the run and dropped it:
a boss made harder by one player's affix is harder for the whole group, which
is §2.9's role burden exactly. The price stays personal. Boon: none.

**The Vault** — the legendary, and the maddest thing here on purpose: every
corpse is emptied (quest items kept, solo corpses only), and every `lootid` is
tallied — a count per id, not a list, so a run cannot fill memory — until
the tier crosses a multiple of ten, when a chest for the level is summoned and,
the first time it is opened, filled from the tally: ten rolls of every table
the run has killed, capped at the loot window's own `MAX_NR_LOOT_ITEMS`. The
fill goes through `OnLoot` on a chest the card owns, which is the same seam
Tribute uses, so nothing about the game object itself changes. Ten levels
without a single drop is a run with no upgrades and no potions from the world
for a tenth of the game at a time; the Vault is what makes it a choice
instead of a punishment. One per run, as the plan says a legendary is.

### 7.4 What the harness needs for these

Two probes, both generic, both missing today:

- **A loot-window probe.** After the bench's real kill, call
  `Mgr::OnLootWindow(player, corpse->GetGUID(), &corpse->loot)` and report
  whether the loot moved (item count before and after). It reaches Fresh Kill,
  The Tenth Corpse, Blood Price, Reliquary, The Vault — and Carrion, which the
  handoff's §8 already lists as a card the bench reports "spawned nothing" for,
  very likely because nothing ever opens the corpse.
- **A chest probe.** Summon a chest of the player's level, open it through the
  same hook with the game object's guid, and read the footprint: Tribute,
  Mimic, Dragon's Hoard, The Vault.

And the shape tests for `GauntletRules.h`: *Fresh Kill's window is longer than
Grudge's* (or the two cards, carried together, contradict); *a Wanted mark
always pays more than an ordinary kill*; *Dragon's Hoard's escalation is
bounded by the aggregate caps, not by itself* — which the caps already do, and
the test says so; *The Vault's fill never exceeds the window*.

### 7.5 What this does to the table

Seventeen new offers: two commons, three uncommons, seven rares, four epics
and one legendary, against the rarity plan's targets of sixty, thirty, forty,
fifteen and eight. The Bargain family gains three — Dragon's Hoard, Reliquary,
The Vault — against a cap of two per run (`CAP_BARGAIN`), which the plan's §1
already called a dead end at two rows; **the cap should move to three with
them**, and the family weight (2) is worth re-measuring in the sweep once they
exist. Every card above is a real mechanic, so the "only ~30 are C++ work"
estimate of the rarity plan's §6 gains twelve of those thirty here — with the
four trades and Blood for Bread on the table-row side.
