# Every card must tempt: the greed redesign

**Direction decided; every number is judgement.** The brief, in the words it
was given: *hardcore is hard by design, but offers should give fun, and make you
faster while putting you at risk.* This document turns that into a test, runs
the whole table through it, and writes the redesigns for the cards that fail.

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
  card's 30 s.
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
2. **Craven and Grudge** — the two brakes felt on every single kill.
3. **Falter and Cunning** — the role taxes.
4. **Ambush.**
5. **The three sharpenings** (Nimble, Call to Arms, Blood Magic).
6. **The class pass**, one class at a time, with the kit open.

Steps 2–5 want to land **after** the rarity plan's step 4 (rank removal): every
one of those cards carries a ladder today, and writing a new single value beside
an old ladder only to delete the ladder a step later is the same card touched
twice. Step 1 has no such dependency and is where this starts. The rarity
column above feeds the plan's §7.4 pass directly — the cards with a greed loop
inside them are the epic candidates, and Blood for Bread arrives as one.
