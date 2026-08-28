# Gauntlet affix design

Research, principles, and a concrete affix set that changes behaviour instead of
numbers. Written against the module as of commit `c334a9f` and the AzerothCore
tree at `/mnt/c/Users/3302/azerothcore-wotlk` (`9fb906bb`, 2026-08-21).

---

## 0. The verdict in ten lines

1. Your four effects are the same affix because they all modify a *slope*. A good
   affix modifies a *decision*: it asks for a verb the player already has (move,
   kite, CC, kill in order, don't loot yet, heal earlier).
2. The single most useful design rule from every system studied: **the affix should
   change the encounter, not the character.** Enemy-side and world-side modifiers
   feel like "the world is more dangerous"; character-side stat cuts feel like "I am
   worse", and players resent the second even when the maths is identical.
3. In a permadeath game, **variance is the real killer, not mean difficulty**.
   Prefer deterministic triggers (fixed timers, fixed counts, fixed thresholds) to
   random procs. Every proposal below is deterministic and telegraphed.
4. **Never remove the universal escape** (running, mounting, leashing). Most other
   affixes' counterplay ultimately reduces to "leave"; an affix that takes leaving
   away silently breaks every other one.
5. Keep **damage taken** as the baseline scalar (with conditions, never `Always`),
   keep **damage dealt** only as the *price* of a boon or bargain, replace
   **healing received** with a wound mechanic that has counterplay, and cut
   **experience gain** from the standalone pool — it is the always-safe pick that
   poisons the choice.
6. 71 proposals follow, in seven families by verb: things that spawn, enemies
   that behave differently, tempo and position, attrition with counterplay,
   rules, bargains, and forty-four class curses — four per class, each hitting
   a different verb, plus cross-class and class-bargain entries. Each has
   counterplay, a tier range, and the hooks that implement it.
7. Stack by **slots and ranks, not by count**: an affix already carried is re-offered
   as rank II/III of itself, never duplicated. Sixteen picks then produce roughly
   seven to nine distinct mechanics, each escalating — the Slay the Spire curve,
   not a pile of unrelated taxes.
8. Put all timed events through **one per-player scheduler** with a minimum
   spacing, so three "something appears" affixes never fire in the same second.
   That single rule is what keeps a late run coherent.
9. **Class relevance** must be a filter on offers. A silence affix offered to a
   warrior is Hades' "free heat" problem: the choice degenerates.
10. Every summon/cast/aura primitive you assumed is confirmed in the core, plus
    several hooks you did not list that matter (`DealDamage` for cheat-death,
    `OnBeforeRollMeleeOutcomeAgainst` for crit/dodge, `GetCreatureAI` for stalker
    AI, `LANG_ADDON` whispers for a real data channel to the addon).

---

## 1. Why the current four feel like a tax

Read `Mgr::Multiplier` in `src/GauntletMgr.cpp:203-241`: every affix collapses
into one `float` per effect, summed additively and applied in three hooks. From the
player's seat the system therefore has exactly one verb — *scale a number* — and
sixteen picks become sixteen coefficients on the same two lines of the combat
formula. Three structural problems fall out of that:

- **No moment.** Nothing happens *at a time*. The player never sees an affix act;
  they only infer it from a run feeling harder. Memorable difficulty is an event.
- **No verb to answer with.** "+21% damage taken" can be answered only by having
  more stats or by playing the same way more carefully. Neither is a decision.
- **The safe pick.** With `ExperienceGain` in the pool, one of the three offers is
  usually a pure time tax with no survival cost. In a permadeath game a rational
  player takes it every time it appears, which means the affix system is choosing
  for them.

Two things the current design already gets right and should keep: the *condition*
axis (a "below half health" gate turns a scalar into a threshold, and thresholds
create decisions — "50% is the new 0%, heal earlier"), and the boon axis (a
trade-off is a decision even when the underlying effect is scalar).

---

## 2. What the comparable systems teach

Sources are numbered and listed in §8. Where a claim is community consensus
rather than a developer statement it is marked *(community)*.

### 2.1 Mythic+ affixes (WoW, Legion → The War Within)

- Blizzard's stated direction is that the challenge should come from the
  dungeon and affixes should be "relatively minor changes from week to week
  instead of the thing that defined a run" [1]; The War Within rework set out to
  "minimize the mechanical overlap between affixes and dungeon trash design" and
  retired Afflicted, Incorporeal, Entangling, Storming, Volcanic and Spiteful in
  one patch [2]. Tyrannical and Fortified — the pure scalars — are what remains
  as the baseline that defines a key. They are your `Exposed`/`Feeble`: a floor,
  not sixteen of sixteen.
- The removals over five years cluster into role burdens and untelegraphed
  punishment. Necrotic "was originally intended to be an affix that challenges
  tanks, but … routing responsibilities already place tremendous pressure on
  tank players"; Explosive became "a 'healer problem'" once groups learned to
  ignore the orbs, and was retired rather than redesigned; Inspiring "can feel
  especially punishing while you're still learning" [3][4]. Bursting, Bolstering
  and Sanguine "go against what mythic+ is" because they punish the big pulls the
  format otherwise rewards *(community)* [5].
- The replacement design is explicitly kiss/curse. Michael Bybee on Xal'atath's
  Bargain: players asked "Can we do kiss-curse mechanics? … we want something
  that actually gives us a benefit"; on Oblivion: "You can play that mechanic
  badly and it makes the game harder, but you can play it better and it makes
  the game easier" [6]. The blue post: "If players disrupt these orbs, they
  claim this effect for themselves" [7]. Task with a reward, punishment on
  neglect.
- Blizzard cut a set of tested bonuses that favoured particular damage schools
  because "applying bonuses unevenly to players feels exclusionary regardless of
  overall impact" [7]. Two lessons: affixes must be relevant to whoever holds
  them, and in a *group* a personal affix that only bites one member will read as
  unfair even when the maths is small.
- Seasonal affixes players remember fondly spawned something that paid out when
  engaged — Reaping was "just fun. Big damage", Awakened was "deciding where to
  drop the portals" *(community)* [8]. Prideful is the cautionary version of the
  same idea: loved for the power moment, hated because a body-pull could promote
  a miniboss mid-fight — "omega-punished you for body-pulls", "a binary pass/fail
  check" *(community)* [8][9]. The Champions affix below is built around that
  critique.
- Noise is a design failure in its own right. The Season 2 post cites feedback
  that "players were faced with too many mechanics at once" [10]; the Frenzied
  affix was cut in beta for visual clarity and Ascendant's orb count was halved
  [11]. Combinations hurt more than singles — Tyrannical+Bolstering,
  Necrotic+Fortified, Grievous+Bursting are the ones players name *(community)*
  [12]. Test the pairs.

### 2.2 Hades — Pact of Punishment

- Heat is granular, player-selected and tied to a concrete reward: the Superstar
  update linked the Pact to Bounties, and Welcome to Hell made bounty rewards
  "specific to clearing each Underworld region" while reworking Middle
  Management to "affect each mini-boss encounter in a distinct way" and Extreme
  Measures to "affect bosses region-by-region" [13][14]. Supergiant iterated the
  flavourful conditions toward *content changes* and left the numeric ones
  alone.
- The community's "free heat" list is precise: "Convenience Fee and Damage
  Control don't add much difficulty and can be taken pretty safely"; EM1 is free
  because it changes only one boss; the hated conditions are the build-wreckers
  — "forcing conditions that RUIN your build by removing your boons … is absurd"
  (Approval Process, Underworld Customs) *(community)* [15][16]. Your
  pick-one-of-three has the same failure mode: if one offer is fake for this
  class it is always taken, and the choice degenerates. Class-relevance
  filtering (§4.5) is the fix.
- Stacking is where the difficulty actually lives: "Jury Summons, Calisthenics
  Program, and Benefits Package aren't too bad on their own, but once you stack
  them all together it can make for some pretty grueling rooms"; "over 20 heat,
  there aren't really any 'easy' pact options left" *(community)* [15].
- Hades II keeps the split: most Vows are "easy Fear value" numerics, while Vow
  of Rivals gives "stronger-than-ever Guardians, each with unique surprises" and
  Forfeit/Frenzy "demand behavioral adaptation" [17][18].

### 2.3 Slay the Spire — Ascension 1–20

- Anthony Giovannetti describes the ladder as "stratified difficulty" tuned on
  win-rate data per player group [19]. Structurally, A1–9 are stat and economy
  taxes cycling through normal/elite/boss enemies; A10 (Ascender's Bane) pollutes
  the deck; A11–16 tax potions, upgrades, gold, max HP, events and shops; A17–19
  give enemies "more challenging movesets"; A20 doubles the final boss [20].
- The levels players describe as changing decisions are the ones that touch a
  system they operate: "Being forced to deal with more elites meant I had to
  rethink which cards I added to my deck" [21]; A17–19 are beaten by reading —
  "after reading the info and restructuring my thinking a bit i smashed it"
  *(community)* [22]. Game Developer's summary is the principle: progressive
  difficulty works because "each level of difficulty tweaks an aspect of the
  game", with the warning that "the higher the difficulty grows; the amount of
  viable choices shrinks" [23].
- The economy taxes work because StS's economy is tight. WotLK gold is tight at
  level 10 and irrelevant at 50, so a gold tax has a shelf life (Iron Purse, §3).
- Ordering note. StS and M+ both put numbers early and rule changes late
  (Blizzard moved "periodic" affixes to +7 and "death or health range" affixes to
  +14 "with the goal of lessening their overall impact" [4]). Those are
  cross-run ladders — each rung is a whole run. Your tiers are rungs *inside*
  one run, which changes the answer; see §4.6.

### 2.4 Risk of Rain 2 — Artifacts and Eclipse

- Hopoo separated novelty from difficulty on purpose. Artifacts "won't prioritize
  balance" and exist "to be fun and to add replayability" [24]; Eclipse "is
  designed entirely for challenge enthusiasts" and "has no unlockables,
  achievements, or trophies. It's purely for the challenge" [25]. One system
  serving both goals is what makes an affix pool feel muddled; your boon and
  bargain axes are the novelty channel and should be labelled as such.
- Each Eclipse level is one legible new rule with a documented counter: E1 start
  at 50% health, E2 teleporter radius −50%, E3 lethal fall damage, E4 enemy speed
  +40%, E5 healing −50%, E6 gold −20%, E7 enemy cooldowns −50%, E8 permanent
  damage per hit [26]. E8 is the celebrated one because it changes the goal from
  "have enough HP" to "do not get hit" — the model for Deep Wounds.
- Artifacts that rewire a run — Kin, Swarms, Vengeance (a doppelgänger hunts
  you), Sacrifice, Glass, Spite (enemies drop bombs on death) — are rule
  changes, not knobs, and are remembered as such *(general knowledge)*.

### 2.5 Binding of Isaac — curses

- Curses remove information rather than add threat: Blind hides pickups, Lost
  removes the map, Maze teleports you, Darkness dims the floor, Unknown hides HP;
  only Labyrinth (a double floor) gives something back [27]. The community
  verdict is uniform: "artificial difficulty by reducing the QoL"; "none of the
  curses actually add difficulty, they just make the game way more annoying";
  Labyrinth is "the only one that offers something" *(community)* [28]. Demand
  for counterplay shows up as mods named *Curse Counterplay* and *Curse
  Disabler* [29].

### 2.6 Diablo 4, Diablo 3, Path of Exile

- D4 pulled Resource Burn, Cold Enchanted and Backstabbers in 1.1.1 because
  they "claim a disproportionate amount of player lives" and, in Resource
  Burn's case, "effectively took away the ability to use the skills you had
  built" [30][31][32]. The 1.2.0 repairs are a template: direct damage from
  major affixes "can now be passively dodged"; Lightning Storm "will only begin
  once players are in combat", spawns only where "the player has a direct path",
  and "avoiding shock damage in the bubble grants 35% bonus Movement Speed"
  [33]. Telegraph, only in combat, reward for dodging — Falling Sky (§3) is
  that pattern.
- D3 removed Invulnerable Minions because elites "can feel like a brick wall"
  [34]; Waller, Arcane in corridors and Jailer+Desecrator remain the guides'
  worst-affix examples — positional denials with no answer [35].
- PoE's most-rerolled map mods are the build-bricks: reflect means "your
  character kills itself by hitting monsters"; no regen/leech is "unplayable if
  your build relies on leech or regen" [36]; players avoid entire archetypes
  because of one mod ("the reason why I never play ignite builds") [37]. You
  have no reroll: a hard counter to one class in a permanent, stacking system is
  a run-ender by design.

### 2.7 Smaller systems worth stealing from

- **Dead Cells' Boss Stem Cells** change one resource rule per cell (fountains
  every other passage → none but a flask charge → three charges total → none,
  and enemies teleport to you) [38]. **Malaise** (5 BC) was reworked from
  hit-punishment into a pacing clock that should "stay in-between 3 and 7 during
  most of the run" and that no longer kills directly [39][40]: pressure with a
  release valve.
- **Dead Cells' cursed chests**: double loot, then any hit kills you until ten
  kills. The best risk/reward loop in the genre — the player authors the danger.
- **Spelunky's ghost** and **FTL's rebel fleet**: pressure that shapes routes
  and lets the player decide when to pay.
- **Enter the Gungeon's curse** and the Lord of the Jammed: greed spawns a
  stalker.
- **Shadow of Mordor's nemesis**: an enemy that remembers you and returns
  stronger if it fails. Cheap with a seed and a counter; very memorable.

### 2.8 Principles extracted

1. **Answerable by a verb.** If the only counter is "have more stats", it is a
   tax. Every affix should name the button or movement that answers it.
2. **Encounter, not character.** Modify enemies, spawns and the world; keep
   character-side scalars as a baseline floor [1][2][24].
3. **Deterministic in permadeath.** Fixed timers, counts and thresholds. No
   random procs, no crit-variance affixes. The addon shows the countdown.
4. **Telegraph, then punish.** Two to four seconds of warning and a visible
   actor. The player must know what killed them [33].
5. **Player-authored triggers beat clock triggers.** Keying on loot, kills,
   engage, resting and looting lets the player choose *when* to pay.
6. **Task with a reward.** Punish neglect, reward engagement [6][7][8].
7. **Pressure with a release valve**, not walls or denials [34][39].
8. **Never remove the universal escape.** Mounts, leashing and running stay
   sacred.
9. **No build-bricks; class-relevant offers only.** Soft counters are fine; hard
   counters are excluded; irrelevant affixes are never offered [7][16][36].
10. **Information removal is not difficulty** [27][28].
11. **Introduce, then intensify.** New verbs while the run is cheap, ranks
    later; a late run should have a handful of escalated mechanics, not sixteen
    unrelated ones [20][23].
12. **One event at a time.** Throttle and serialise events per player [10][11].
13. **Design the pairs.** Combinations are where difficulty lives and where
    unfairness hides; test them by name [12][15].

### 2.9 Known-bad patterns (do not build)

| Pattern | Example | Why it fails |
|---|---|---|
| Flat scalar, always on | Fortified/Tyrannical as *every* affix, Hard Labor, your `Everlasting Feeble` | No moment, no verb ("free heat") |
| Role-exclusive burden | Necrotic, Skittish, Grievous, Explosive | One player pays for the group [3][4] |
| Random punishment | Quaking mid-cast, "5% chance enemies crit for 3×" | Variance ends permadeath runs; not a decision |
| Information removal | Isaac curses | Guessing is not skill [28] |
| Build-brick | PoE reflect / no regen, D4 Resource Burn, "enemies immune to CC", "no stealth" | Unwinnable for one class, free for others [31][36] |
| Removing the escape | "enemies never leash", "cannot mount", "you move 20% slower" | Breaks every other affix's counterplay |
| Denial without decision | "cannot use potions", D3 Invulnerable Minions | Removes a tool, adds nothing [34] |
| Snowballing punishment | Infested, Prideful body-pulls, Bolstering | One mistake ends the run [8][9] |
| Chores | hearthstone cooldowns, repair costs at L60, no rested XP | Costs time, not skill |
| Per-mob phases | "every enemy has a shield phase at 50%" | Fine on one boss, tedious across four thousand mobs |
| Visual noise | Frenzied, "14 orbs at once" | Cannot read the fight [11] |

### 2.10 Personal curses — what the conduct systems teach

Class curses (family C) have their own lineage: the roguelike *conduct*, the
point-buy *defect*, the hereditary *trait*, the character-as-ruleset. Their
lessons differ from the encounter-side ones above in useful ways.

- **Price by what it costs *this* character, and never pay twice.** Stone Soup's
  Ru prices each sacrifice from a base table, then adds piety proportional to the
  skill XP the character actually trained, and refuses overlapping payment
  (Nimbleness pays more if you already gave up Durability; Love pays 1 piety if
  you have already lost Summonings and Artifice) [41][42]. "Sacrificing anything
  that lies outside a character's build" is intended play, just paid less; the
  known failure is pricing by trained XP and missing innate value [43]. Rogue
  Legacy 2 does the same with gold: cosmetic traits +25–88%, Vertigo-class
  +75–262%, Pacifist +150–525%, One-Hit Wonder +200–700%, and balanced trade-off
  traits get nothing [52][53].
- **Subtraction alone is ignored; a removed verb needs a granted one.** Rogue
  Legacy 1's traits "acted more as surprises" and heirs with hindrances "would
  not be picked"; RL2 "added a gold bonus … to encourage players to always test
  themselves", and One-Hit Wonder became the most popular pick [51][52]. Every
  NecroDancer character pairs its rule with compensation — Bolt's double tempo
  with a spear and fewer enemies, Tempo's kill clock with 999 damage, Eli's no
  weapons with infinite bombs [55].
- **Close the loopholes and re-add pressure where a rule removes it.** Dove
  cannot kill, so exits unlock and bosses are skipped — "however, to avoid making
  the game too easy for her, trapdoors do not appear, and she will die when the
  song ends" [55]. Blizzard did not trust honour either: Classic Hardcore
  disables Reincarnation and Soulstone outright and blocks the Hearthstone
  under Divine Shield, Divine Protection and Blessing of Protection, justified by
  parity with the shaman's loss [60]. No Sanctuary (C7) is therefore Blizzard's
  own rule, and Ankh Pact / Stone of the Damned (C43–44) sell back exactly what
  Blizzard removed.
- **Never attack perception.** Rogue Legacy's Vertigo — "you may as well have
  played it blindfolded"; "most gamers simply ignored an heir if they had that
  trait" — was later removed with Glaucoma and Tunnel Vision after nausea reports
  [51][54]. Caves of Qud's most-hated defects are Myopic (an item-slot tax) and
  Nerve Poppy (hidden HP) *(community)* [49]. Threshold and resource curses, by
  contrast, are the ones players describe as builds.
- **Thresholds on a visible resource are the genre's best personal curse.**
  Darkest Dungeon's resolve check at 100 stress is the direct precedent for Red
  Mist and Whispers of the Deep: Red Hook planned to hide stress numbers, then
  found that showing "+20 Stress" gave players "a system to strategize around"
  [57]. The afflictions are not pure penalties (Abusive hits +20% harder), and a
  quarter of resolve checks are *virtues* — "the heroic reversals are what
  memorable adventures are built on" [57][58]. Variance on the upside is safe in
  permadeath; the family can afford a virtue roll.
- **Cap stacking rather than repricing it.** Caves of Qud allows one defect at
  creation by default [47]; Ru stops offering at the piety cap [41]. One class
  curse per run, ranked up rather than joined, is the same instinct.
- **Record conducts.** NetHack tracks conducts and prints them at game end for
  "extra bragging rights" — winning without is "perfectly acceptable" [45]. The
  Gauntlet leaderboard should carry the run's class curses by name.
- **Hostile twins are old and beloved.** Caves of Qud's Evil Twin, a perfect copy
  that appears on entering new zones [48]; Isaac's Dark Esau, who "will perform a
  fiery charge attack in which he will pause for a brief moment with a sound cue"
  and doubles as a weapon against enemies [59]. Echo (S2) and Fel Pact (C33)
  stand on that ground; the sound cue is the part to copy.
- **Hardcore WoW deaths are class-shaped, and the guides know it.** Official
  first-month counts put Hunter (401,980), Warrior (332,970), Mage (294,082) and
  Rogue (279,304) on top of 1.98 million deaths, with the caveat that this
  tracks popularity as much as fragility [61]; rate-based Deathlog data has
  paladins doing well and shamans and warlocks worst [63]. The top killers are
  falling and drowning, not class skill tests [62]. Per class, the guides name
  the loop that kills: Feign Death resisted by "all engaged enemies within 40
  yards" and Cheetah's daze; a warrior "constantly rage starved at low levels";
  a mage with shields down "with frost-nova and blink on cooldown"; "a string of
  parries" on a low rogue; warlock fear-kiting into adds; shaman totem aggro;
  priest fear as a positional tool [64]. Each class's four curses below are
  aimed at that loop.

---

## 3. Affix proposals

Format: code · **Name** · *player-facing line*. Then mechanic, counterplay, tier
range (tier *t* is reached at level 5*t*), severity ladder, hooks, and notes.
Codes encode the family, which matters for stacking (§4); they are not a sequence.

Hook names are AzerothCore's. `OnPlayerUpdate` is assumed throttled to a 500 ms
accumulator per player. "Owner" means the player who carries the affix; summoned
creatures are owner-bound (they only ever attack their owner, despawn when the
owner dies, logs out or leaves the map, and give reduced XP to prevent farming).

### Family S — things that spawn

**S1 · The Shade** · *A Shade rises behind you every few minutes and hunts you
until you kill it or leave it behind.*

- Mechanic. A per-player clock runs whenever the owner is alive, out of a rest
  area and not mounted. When it lands and the owner is out of combat, a Shade
  (custom `creature_template`, ghostly display, level = owner's, ~1.5× a normal
  mob's health, ~1.2× its damage) spawns 35 yd behind the owner with an emote and
  chases at 85% player run speed. It despawns after 120 s if never engaged, or
  when the owner is more than 150 yd away for 15 s. Killing it grants
  *Vindication*: +25% experience for five minutes.
- Counterplay. It is slower than you and much slower than a mount: outpace it,
  or turn and fight it on cleared ground before your next pull. The addon shows
  the countdown, so never let it land mid-pull. CC works (it is a humanoid-typed
  undead: fear, stun, root all apply).
- Nemesis rule (rank III). The Shade is one named creature per run (name from the
  seed). Each time it despawns without dying it returns one rank stronger; each
  time you kill it, it stays dead for two tiers. Losing to it becomes a story.
- Tiers 4–16. Severity: interval 15 → 10 → 7 min; health ×1.5 → ×2 → ×2.5.
- Hooks: `OnPlayerUpdate` (clock), `SummonCreature`, `AllCreatureScript::
  GetCreatureAI` or a `CreatureScript` on the template (owner-bound chase AI,
  taunt-immune, ignores everyone else), `OnBeforeCreatureSelectLevel` (level =
  owner), `OnPlayerCreatureKill` (reward), `OnPlayerLogout`/`OnPlayerUpdateZone`
  (despawn).
- Note. This is the out-of-combat pressure affix. Only one stalker mechanic may be
  active per run (§4).

**S2 · Echo** · *Every 25th enemy you kill returns as an echo of yourself.*

- Mechanic. A kill counter (shown by the addon). On the 25th kill, a Doppelgänger
  spawns 20 yd away: cloned appearance (cast spell 45204 "Clone Me" and 41055
  copy-weapon on it — both used by the core's mirror-image script), level =
  owner's, ~2× a normal mob's health, a tiny per-class kit (auto-attack plus one
  signature: Mortal Strike / Frostbolt / Shadow Bolt / Sinister Strike etc.).
  Killing it grants five kills' worth of XP and rolls one guaranteed uncommon
  item of the owner's level.
- Counterplay. You author the moment: make the 25th kill land at full health,
  on easy ground, with cooldowns up. It is humanoid: sap, polymorph, fear all
  work. Kite it into guards. Or run — it leashes like any mob.
- Tiers 6–14. Severity: every 30 → 25 → 18 kills; health ×2 → ×2.5 → ×3.
- Hooks: `OnPlayerCreatureKill` (counter), `SummonCreature`, `CastSpell` (clone),
  custom AI, `OnPlayerGiveXP`/`Player::GiveXP` (reward).
- Note. Differs from the Shade in *who pulls the trigger*: the player does.

**S3 · Carrion** · *Every 4th corpse you loot draws scavengers. Corpses are
richer.*

- Mechanic. Count distinct creature corpses whose loot window the owner opens.
  On the Nth, two Scavengers (fast, fragile, level = owner's, hyena/vermin
  display) spawn 25 yd away and charge. Boon built in: +50% money and +25% item
  drop chance on creature loot.
- Counterplay. Skip looting trash when hurt; loot in cleared areas with your back
  to a wall; loot at full health, not at 40% mid-eat. The addon shows "scavengers
  in 2 loots".
- Tiers 2–10. Severity: every 5 → 4 → 3 loots; 2 → 2 → 3 scavengers.
- Hooks: `OnPlayerBeforeSendLoot` (`lootGuid.IsCreature()`), `SummonCreature`,
  `OnPlayerBeforeLootMoney`, `GlobalScript::OnItemRoll` (drop chance).

**S4 · Reinforcements** · *Fights longer than 30 seconds draw another enemy every
15 seconds.*

- Mechanic. An in-combat clock. At 30 s, and every 15 s after, a copy of the
  owner's current victim's creature entry spawns 20 yd away and attacks the owner
  (cap three per fight). Skipped if the victim is elite, a boss, a summon, a
  vehicle or a quest-flagged creature. Copies give half XP.
- Counterplay. Burst. Pull small. Use CC to shorten fights, not lengthen them.
  Leaving combat resets the clock, so disengage-and-reset is always available.
  Fight long targets near an exit.
- Tiers 5–14. Severity: 45/15 s → 30/15 s → 20/10 s; cap 2 → 3 → 4.
- Hooks: `OnPlayerUpdate`, `OnPlayerEnterCombat`/`OnPlayerLeaveCombat`,
  `Unit::GetVictim`, `Creature::isElite`/`IsDungeonBoss`/`IsSummon`,
  `SummonCreature`.
- Note. Summoning a copy of *the thing you are already fighting* keeps it zone-
  appropriate and legible ("another Defias Thug arrives") with no bestiary work.

**S5 · Ambush** · *Resting in the wild attracts an ambush.*

- Mechanic. When the owner is out of combat, not moving, not in a rest area and in
  the open world for 20 s, a 4 s warning fires ("You hear footsteps"), then one
  Ambusher (a normal mob of the owner's level) spawns 12 yd away and attacks. The
  rest clock then resets to 60 s.
- Counterplay. The warning is the counterplay: stand up and move three steps. Rest
  in inns, near guards, or in short bursts. Rest *before* you are low, since being
  surprised at 40% is the actual threat. Fight it at full health and it is a free
  mob.
- Tiers 3–9. Severity: 30 → 20 → 12 s of stillness.
- Hooks: `OnPlayerUpdate`, `Unit::isMoving`, `Player::HasRestFlag`,
  `Map::IsDungeon` (never in dungeons), `SummonCreature`.
- Note. Deliberately restricted to the open world: punishing between-pull rest in
  a dungeon group is a role burden.

### Family E — enemies that behave differently

**E1 · Champions** · *Every 8th fight you start opens against a Champion: twice
the health, harder hits, double the reward.*

- Mechanic. Count fights, not creatures: the counter advances when a creature
  engages the owner while the owner is *out of combat*. The Nth such creature
  (never an elite, boss or quest creature) is promoted: scale ×1.3, a visible
  enrage aura (spell 8599 "Enrage", used by six core scripts), max health ×2,
  +25% damage dealt to the owner. It grants ×2 XP and a guaranteed extra coin
  roll. A body-pull mid-fight can never promote, so the affix cannot snowball —
  the specific complaint players had about Prideful [8][9].
- Counterplay. The counter is visible, so *you* choose which fight opens against
  the Champion: make it a caster, a low mob, or one you can fight alone with
  cooldowns up. Opening the 8th fight at 60% health is the mistake.
- Tiers 2–16 (evergreen). Severity: every 10 → 8 → 6; health ×2 → ×2.5 → ×3.
- Hooks: `OnPlayerEnterCombat(enemy)` (check `player->IsInCombat()` before the
  core sets it, or track the previous tick's state), `Unit::SetObjectScale`,
  `AddAura`, `SetMaxHealth`/`SetFullHealth`, `ModifyMeleeDamage`/
  `ModifySpellDamageTaken` (attacker check), `OnPlayerGiveXP`,
  `OnPlayerAfterCreatureLootMoney`.
- Note. The Prideful pattern with the snowball removed. This should be one of the
  first affixes players meet: it teaches that affixes are content, not tax.

**E2 · Craven** · *Enemies flee at 25% health, and come back with friends.*

- Mechanic. When a non-elite creature fighting the owner first drops below 25%,
  it emotes and flees for 5 s (`MotionMaster::MoveFleeing`). When the flee ends,
  if any idle hostile creature of the same faction is within 15 yd of it, the
  nearest one (two at Dire) is pulled onto the owner.
- Counterplay. Execute-range awareness: snare, root, stun or burst at 30% so
  nothing flees. Ranged classes finish fleeing targets; melee learn Hamstring.
  Fight away from packs so there is nobody to fetch.
- Tiers 4–12. Severity: flee at 20 → 25 → 35%; fetches 1 → 1 → 2.
- Hooks: `UnitScript::OnDamage` (pre-apply: compare `health − damage`),
  `MoveFleeing`, `OnAllCreatureUpdate` or a delayed event for the fetch,
  `Creature::AI()->AttackStart`.
- Note. WoW humanoids already flee at ~15%; this generalises it and adds the
  consequence, which is what makes it a decision.

**E3 · Call to Arms** · *Killing an enemy alerts its nearest kin.*

- Mechanic. On each kill, the nearest idle, hostile, same-faction, non-elite
  creature within R yd of the corpse attacks the owner (two at Dire). A camp
  becomes a rolling fight.
- Counterplay. Kill order and pull geometry: kill the outermost mob first so its
  nearest kin is far; peel with CC; retreat between kills so the alerted mob
  leashes. This is the affix that turns a camp into a puzzle.
- Tiers 5–13. Severity: R = 20 → 30 → 40 yd; 1 → 1 → 2 kin.
- Hooks: `OnPlayerCreatureKill`, grid search (`Cell::VisitGridObjects` with
  `AnyUnfriendlyUnitInObjectRangeCheck`), `AI()->AttackStart`.

**E4 · Death Rattle** · *Corpses burst two seconds after death, hurting anyone
within five yards.*

- Mechanic. Two seconds after a kill, a visible burst fires at the corpse (a World
  Trigger — entry 21252 is used throughout the core scripts — casts an existing
  ground visual). If the owner is within the radius they take a fixed fraction
  of their max health.
- Counterplay. Step back after every kill. Kill at range. Never finish two mobs
  at once in melee; never finish a mob while low.
- Tiers 4–12. Severity: 8/12/18% of max health; 5/6/8 yd.
- Hooks: `OnPlayerCreatureKill`, `OnPlayerUpdate` (delay), `SummonCreature`,
  `CastSpell(x,y,z)`, `Unit::DealDamage`.
- Note. Melee-weighted; a hunter or warlock barely notices. Mark it as relevant
  only to classes that fight in melee (§4.5) so it is never a free pick.

**E5 · Grudge** · *The dead linger. Standing where an enemy died saps you.*

- Mechanic. On a kill, a passive, non-attackable Restless Spirit (ghost display)
  stands on the corpse for 25 s. While the owner is within 4 yd of any spirit
  they lose 3% max health per second and receive 50% less healing.
- Counterplay. Fight on fresh ground: pull mobs to you rather than fighting in
  place, step away after each kill, and *decide when to loot* — loot now and eat
  ticks, or wait 25 s.
- Tiers 3–10. Severity: 2 → 3 → 5% per second.
- Hooks: `OnPlayerCreatureKill`, `SummonCreature` (visual only), `OnPlayerUpdate`
  (proximity), `ModifyHealReceived`.
- Note. Sanguine for one player. Mutually exclusive with Death Rattle — both are
  on-kill positional rules and together they are just "melee is bad".

**E6 · Nimble** · *Enemies move 30% faster.*

- Mechanic. Every non-elite creature that enters combat with the owner has its run
  speed raised (`SetSpeedRate(MOVE_RUN, …)`) for the duration; restored on evade
  or leaving combat. Never above 140%; mounts are untouched, so a mounted escape
  always works.
- Counterplay. Snares matter again; fight in melee instead of kiting; choose
  fights before you take them because backing out on foot now costs health.
- Tiers 6–14. Severity: 20 → 30 → 40%.
- Hooks: `OnPlayerEnterCombat`, `UnitScript::OnUnitExitCombat`,
  `OnUnitEnterEvadeMode`, `Unit::SetSpeedRate`.
- Note. A soft counter to kiting classes, which is the point; capped so it is
  never a hard one.

**E7 · Cunning** · *Enemies in melee range kick the spell you are casting, once
every 12 seconds each.*

- Mechanic. Each attacker within 5 yd has a personal 12 s kick cooldown. When the
  owner has been casting for at least 0.5 s and more than 0.5 s remains, the
  nearest ready attacker interrupts the cast (`InterruptNonMeleeSpells`) and
  locks that school for 3 s (`ProhibitSpellSchool`).
- Counterplay. Cast at range before they close; instants and DoTs; fake-cast
  (the 0.5 s arming window means a cancelled cast eats the kick — that is the
  skill); root or stun the kicker and cast freely.
- Tiers 6–14. Severity: cooldown 15 → 12 → 8 s; lock 2 → 3 → 4 s.
- Hooks: `OnPlayerUpdate`, `Unit::getAttackers`, `HasUnitState(UNIT_STATE_CASTING)`,
  `InterruptNonMeleeSpells`, `ProhibitSpellSchool`.
- Note. Relevant only to classes with cast-time spells. Shares the single
  "role tax" slot with Falter (D1): never both.

**E8 · Keen-nosed** · *Enemies notice you from further away.*

- Mechanic. Each tick, idle hostile creatures within (normal aggro range + X yd)
  that could normally not see the owner yet are alerted, unless the owner is
  stealthed (`HasStealthAura`) or mounted. Elites and bosses excluded.
- Counterplay. Routing and pull geometry: hug edges, pull singles with a ranged
  opener, use stealth, use the mount for transit. The map you already know is
  suddenly wrong by eight yards, and re-learning it is the content.
- Tiers 3–11. Severity: +5 → +8 → +12 yd.
- Hooks: `OnPlayerUpdate`, grid search, `Creature::CanStartAttack`,
  `AI()->AttackStart`.

### Family T — tempo and position

**T1 · Falling Sky** · *In combat, every 20 seconds the sky marks your spot; three
seconds later it strikes.*

- Mechanic. A fixed in-combat cadence. At each mark a World Trigger at the owner's
  feet casts an existing visible ground effect; three seconds later, if the owner
  is within 4 yd of the mark, they take a fixed fraction of max health. Never
  fires while mounted or out of combat.
- Counterplay. Move five yards. Casters interrupt their own cast. The cadence is
  fixed and the addon counts it down, so plan bursts and heals around it.
- Tiers 5–16 (evergreen; the ladder is the danger). Severity: 25 → 35 → 50% of
  max health; cadence 25 → 20 → 15 s.
- Hooks: `OnPlayerUpdate`, `SummonCreature` (trigger), `CastSpell(x,y,z)`,
  `Unit::DealDamage`.
- Note. Volcanic for levelling, and the purest "don't stand in it" teacher in the
  set. At Dire it is the affix that ends careless runs, deterministically.
  Diablo 4's 1.2.0 repair of Lightning Storm — only in combat, only where you
  have a path, a movement-speed bonus for dodging it — is the same shape [33]; a
  small speed reward for a clean dodge is worth testing.

**T2 · Frenzy** · *Each kill within 8 seconds of the last stacks Frenzy: +6% damage
dealt and +6% damage taken per stack.*

- Mechanic. Kill timestamps; stacks (max 5) build while kills chain and fall off 8 s
  after the last. Both multipliers apply in the damage hooks.
- Counterplay. It is a dial the player turns: chain-pull for speed, or pause to
  drop stacks before a dangerous target. The decision recurs every pull.
- Tiers 3–16. Severity: 4 → 6 → 8% per stack.
- Hooks: `OnPlayerCreatureKill`, `ModifyMeleeDamage`, `ModifySpellDamageTaken`.
- Note. The risk/reward version of the "vengeful enemies" idea; the punitive version
  (enemies hit harder per recent kill) was cut because it only ever says "slow down".

**T3 · Overextended** · *Each enemy attacking you beyond the first increases the
damage you take by 20%.*

- Mechanic. In the damage hooks, multiplier = 1 + s·(attackers − 1) using
  `getAttackers().size()`. Attackers on your pet do not count.
- Counterplay. Pull discipline, kill order (drop the count fast), CC, pets tanking.
  This is what a scalar looks like when it maps to a verb.
- Tiers 3–12. Severity: 15 → 20 → 30% per extra attacker.
- Hooks: `ModifyMeleeDamage`, `ModifySpellDamageTaken`,
  `ModifyPeriodicDamageAurasTick`, `Unit::getAttackers`.

**T4 · Falter** · *Every 45 seconds in combat your hands fail you for three seconds:
disarmed if you fight with weapons, silenced if you cast. You are warned two
seconds ahead.*

- Mechanic. A fixed in-combat cadence with a two-second warning (emote + addon).
  Then a 3 s Disarm (spell 676 with `Aura::SetDuration`) for weapon users or a
  3 s Silence for casters. Class decides which; druids in forms get the disarm.
- Counterplay. It is scheduled, so it is a planning problem: do not be mid-burst or
  low when it lands; pre-cast the heal; pop a defensive; melee classes swap to
  the shield-bash / kick they never use.
- Tiers 5–13. Severity: cadence 60 → 45 → 30 s; duration 2 → 3 → 4 s.
- Hooks: `OnPlayerUpdate`, `CastSpell`/`AddAura`, `Aura::SetDuration`.
- Note. Shares the "role tax" slot with Cunning (E7). Deterministic on purpose —
  a random-proc silence would be a coin flip against the run.

**T5 · Hubris** · *Enemies below your level give no experience; enemies above give
40% more.*

- Mechanic. In `OnPlayerGiveXP`: victim below the owner's level → ×0 (×0.25 at
  Minor); above → ×1.4. Quest XP untouched.
- Counterplay. Route: level in the zone one step ahead, fight orange and red with
  cooldowns, or accept quest-only XP for a stretch. A pure route rule with real
  risk attached — the kind of XP affix worth having.
- Tiers 2–10. Severity: ×0.5/×1.2 → ×0.25/×1.3 → ×0/×1.4.
- Hooks: `OnPlayerGiveXP` (victim is passed).

### Family A — attrition with counterplay

**A1 · Deep Wounds** · *A third of the damage you take becomes a wound that only
rest can heal.*

- Mechanic. Per-player `wound` accumulator. A fraction of every hit (after
  mitigation) is added; the wound is capped at 40% of max health and subtracted
  from max health (`OnPlayerAfterUpdateMaxHealth`, then `UpdateMaxHealth` when it
  changes). It decays at 2%/s while resting in an inn or city and clears on
  level-up.
- Counterplay. Avoid damage rather than out-heal it: CC, range, single pulls,
  mitigation cooldowns. Plan the town trip. Eclipse 8's rule, which is the one
  Risk of Rain players talk about.
- Tiers 4–12. Severity: 30 → 40 → 50% of damage becomes wound.
- Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken`/`ModifyPeriodicDamageAurasTick`
  (as observers), `OnPlayerAfterUpdateMaxHealth`, `OnPlayerUpdate` (decay),
  `OnPlayerLevelChanged`.
- Note. This replaces flat healing reduction. Same pressure, but the answer is
  "get hit less", which is a skill.

**A2 · Blood Magic** · *Spells cost 3% of your maximum health in addition to mana.*

- Mechanic. In `OnPlayerSpellCast`, any spell with a power cost deals the fraction
  to the caster as unmitigated self-damage that cannot reduce health below 1.
  Heals included (the loop of paying health to heal is the interesting part).
- Counterplay. Fewer, bigger casts; wands and melee; more deliberate downtime.
  Pair with a spell-power boon in every offer.
- Tiers 5–12. Severity: 2 → 3 → 5%.
- Hooks: `OnPlayerSpellCast`, `Unit::DealDamage`/`ModifyHealth`.
- Note. B-tier. Include only with its boon and only for mana users; it is the one
  character-side tax here that touches a system the player actively manages.

**A3 · Exposed (reworked)** · *You take X% more damage below half health* (and
other state conditions).

- Keep the existing effect, but never with `Everlasting` (`Condition::Always`) or
  `Embattled` (`InCombat`), which are flat taxes. Restrict it to state conditions
  that map to a decision: below half health (heal earlier), while moving (stand
  and fight or commit to running), while stationary, while solo, at night, in
  dungeons. Minimum magnitude 15%: below that it is unfelt and therefore a fake
  affix.
- Also fix `VersusElites` (currently excluded): evaluate it at the damage site,
  where the attacker is known (`attacker->ToCreature()->isElite()`). "Elites hit
  35% harder" is a genuine route and grouping decision in WotLK levelling.

**A4 · Feeble (reworked)** · *You deal X% less damage while …*

- Keep it only as the *price* attached to a boon or bargain, or with a state
  condition (`Desperate Feeble` — weak when low — says "disengage instead of
  finishing the fight", which is a decision). Never offer it as a standalone
  `Everlasting` affix: slower kills mean more damage taken and more downtime,
  which is a tax on a tax.

### Family R — rules

These do not create moments, and the set should carry at most one or two of them,
early, always with a boon. They exist because hardcore players already impose them
on themselves and enjoy the identity.

**R1 · Self-found** · *You cannot trade, mail, or use the auction house. Coin drops
are 30% richer.*

- Hooks: `OnPlayerCanInitTrade`, `OnPlayerCanSendMail`, `OnPlayerCanPlaceAuctionBid`
  (all veto hooks), `OnPlayerBeforeLootMoney`. Tiers 1–4, before it costs anything.

**R2 · Lone Wolf** · *You cannot join a group. You gain 20% more experience.*

- Hooks: `GroupScript::OnAddMember` (remove on join), `OnPlayerCanGroupInvite`,
  `OnPlayerGiveXP`. Tiers 1–6.
- Note. This removes dungeons for the rest of the run. Offer it, but as a server
  option; on a server where the levelling dungeons are the point it is a
  build-brick.

**R3 · Iron Purse** · *Repairs cost double.*

- Hooks: `OnPlayerBeforeDurabilityRepair` (`discountMod`). Tiers 1–3 only: WotLK
  gold is tight at L10 and irrelevant at L50, so the affix has a shelf life.
- Note. Training-cost variants need trainer price data that no hook hands you;
  not worth it.

### Family B — bargains

Bargains are the carrot in the offer set: heavy upside, heavy price, and a moment
built in. They are rare (weighted ~1 in 6 offers), only from tier 6, and each may
be taken once per run.

**B1 · Last Rites** · *Once per level, a killing blow leaves you at 1 health instead.
For ten minutes afterwards you take 50% more damage and cannot be healed above
half.*

- Mechanic. `UnitScript::DealDamage` runs before health is applied (`Unit.cpp:984`)
  and returns the damage; when it would be lethal and the charge is up, return
  `health − 1` and apply the Mark. `ModifyHealReceived` clamps heals during the
  Mark; the damage hooks apply the ×1.5.
- Counterplay. The affix *is* the counterplay; its cost is the Mark window, which
  is the most memorable ten minutes of any run (run, hide, or die anyway).
- Tiers 8–16. Severity: charge per level → per two levels → per three.
- Note. Deliberately tempting. Rare, late, once per run. It converts one certain
  death into a chase; it never removes the stakes.

**B2 · Cursed Hoard** · *Chests hold twice the loot, but opening one curses you:
until you kill three enemies, any hit deals triple damage.*

- Mechanic. `OnPlayerBeforeSendLoot` with a game-object guid of chest type sets
  the curse; `OnPlayerCreatureKill` counts it down; the damage hooks apply ×3.
  Loot doubling via `GlobalScript::OnAfterCalculateLootGroupAmount`.
- Counterplay. Open chests at full health with three easy kills within reach, or
  walk past. Dead Cells' cursed chest, which is the best risk/reward loop in the
  genre.
- Tiers 4–14. Severity: 3 → 4 → 5 kills.
- Note. Chests are rarer than corpses while levelling; that rarity makes each one
  an event.

Frenzy (T2), Champions (E1), Echo (S2), Carrion (S3) and Hubris (T5) are also
bargains in structure — they pay out for engagement — and should be tagged so the
offer builder (§4.4) can guarantee at least one "reward-shaped" offer per tier.

### Family C — class curses

Class-specific affixes are the one place where a *character-side* rule earns its
place, because a class kit is a system the player operates every second — the
deck in Slay the Spire's A10 Ascender's Bane [20][23]. Blizzard's "uneven bonuses
feel exclusionary" finding [7] does not apply: it was about a shared modifier
that favoured some group members; these are personal, chosen, and paid for. The
closest precedents are the personal-conduct systems of the classic roguelikes —
NetHack's conducts, Stone Soup's Ru sacrifices, Caves of Qud's defects, Rogue
Legacy's traits — all covered in §2.10.

Three rules govern the family:

- **Remove the shortcut, not the engine.** Each class below opens with its
  *engine* (the loop that must survive) and its *shortcuts* (fair game). Taking
  Blink, Vanish or a demon's loyalty forces the neglected half of the kit into
  use; taking Frostbolt, Stealth or the Voidwalker from a levelling warlock
  removes the loop and is a build-brick (§2.6).
- **Tax before deny.** A cost creates a decision every time the button is
  pressed; a removal creates one decision at pick time. Ladders therefore run
  *price → higher price → removal*, so the player can stop one rank short.
- **Threshold on a resource the class manages.** Rage caps, empty mana, all
  runes spent, pet happiness, energy while moving — states the player already
  watches. Deterministic, legible, and the counter is a button they already own
  but rarely press.

Four more rules come from the conduct systems in §2.10:

- **Price the boon by the rank, and by the build.** The boon scales with the
  ladder (Rogue Legacy 2's +25% → +700%), and a curse that is cheap for this
  spec — Faithless Form for a priest without Shadowform, Consecrated Ground
  before Consecration is trained — is not offered, or offered with the boon
  halved (Ru's "outside the build" discount). Relevance uses trained spells and
  talent trees, which the server can read.
- **Never pay twice.** Two curses on the same shortcut are exclusive: Long
  Forbearance and No Sanctuary both touch Divine Shield; Bound Skin and Nature's
  Toll both touch shifting; Dead Weight and Wide Dead Zone both touch kiting.
- **A virtue roll on thresholds.** Threshold curses may resolve upward one time
  in four — Red Mist becomes six seconds of +30% damage instead of confusion,
  Whispers becomes a Fear Ward. Variance on the upside is safe in permadeath and
  it is where the stories come from.
- **Record the conduct.** The leaderboard line carries the run's class curses by
  name and rank: "fell at 64 on tier 12, bearing Cold Trail III".

One class affix at a time (a slot, §4.1); never before tier 3, when the kit
exists; always with a boon, because these are the self-imposed rules hardcore
players already brag about, and a boon turns "no Blink" from a handicap into an
identity. Each class gets four curses covering four verbs — a **threshold**, a
**shortcut** tax, a **companion or anchor** rule, and an **identity** rule — so
the offer builder can guarantee variety within the slot. *Build priority* marks
which to implement first (A) and which are good but second-wave (B).

#### Warrior

Engine: auto-attack, rage from damage, Heroic Strike, Charge, Hamstring. The
classic death: rage-starved at low levels, then a Charge into one mob too many
with no way out [64]. Shortcuts: stances,
shouts, Bloodrage, Shield Wall, Last Stand, Retaliation, Enraged Regeneration,
Intercept.

**C1 · Red Mist** (Warrior) · *At 100 rage you lose your mind for three seconds
and your rage empties.*

- Mechanic. When rage reaches the cap the warrior is confused
  (`SetControlled(true, UNIT_STATE_CONFUSED)`) for 3 s and rage is set to zero.
  Once per 15 s. Rank: cap at 100 → 90 → 80 rage.
- Counterplay. Spend it. Heroic Strike and Cleave exist to dump rage; Bloodrage
  and Berserker Rage become timing decisions instead of free buttons. Sitting at
  cap while auto-attacking — the lazy levelling loop — is exactly what dies.
- Leaves intact: every ability. Boon: +10% rage from damage taken.
- Tiers 3–14. Hooks: `OnPlayerUpdate` (`GetPower(POWER_RAGE)`, stored ×10),
  `SetControlled`, `SetPower`.
- Note. Threshold verb. Build priority A. Precedent: Darkest Dungeon's resolve
  check at 100 stress (§2.10).

**C2 · Berserker's Bargain** (Warrior) · *Below 35% health you deal 25% more
damage, but Shield Wall, Last Stand and Enraged Regeneration will not answer.*

- Mechanic. While health is below 35% (rank: 30 → 35 → 40%), damage dealt ×1.25
  and the three panic buttons are held on cooldown. Above the line they work.
- Counterplay. Pop defensives *before* the line, not after — which is when they
  should be used anyway. Below it, the class becomes a race it is good at
  winning: Execute, Victory Rush, Bloodrage. Or leave: Intercept and Hamstring
  still work.
- Leaves intact: Execute, Victory Rush, Charge, Intercept. Boon: the damage
  bonus is the boon.
- Tiers 5–16. Hooks: `OnPlayerUpdate` (`GetHealthPct`), `AddSpellCooldown`/
  `RemoveSpellCooldown`, `ModifyMeleeDamage`.
- Note. Shortcut verb. Build priority A.

**C3 · Iron Discipline** (Warrior) · *Changing stance has a ten-second cooldown.*

- Mechanic. Casting any stance puts the other two on a 6 → 10 → 20 s cooldown by
  rank. Rage is retained across the switch.
- Counterplay. Stance-dancing is gone: decide Battle, Defensive or Berserker
  before the pull. Defensive for the elite, Berserker for the camp, and no
  mid-fight Shield Wall unless you were already in Defensive.
- Leaves intact: all three stances. Boon: no rage loss on stance change.
- Tiers 4–14. Hooks: `OnPlayerSpellCast` (2457/71/2458), `AddSpellCooldown`.
- Note. Identity verb. Build priority B.

**C4 · Deafening Roar** (Warrior) · *Your shouts wake every enemy within thirty
yards.*

- Mechanic. Battle, Commanding, Demoralizing and Intimidating Shout alert every
  idle hostile creature within 30 yd (rank: 20 → 30 → 40 yd; elites and bosses
  excluded). Shouts last four minutes and cost no rage.
- Counterplay. Buff at the camp's edge, then walk in; re-shout in cleared
  ground. Demoralizing Shout becomes a deliberate multi-pull tool rather than a
  free debuff. A warrior who shouts in the middle of a camp pulls it, and knows
  why.
- Leaves intact: every shout. Boon: shouts free and long.
- Tiers 4–14. Hooks: `OnPlayerSpellCast`, grid search,
  `AI()->AttackStart`.
- Note. Anchor/positional verb. Build priority A — cheap, thematic, and creates
  a story every time it fires.

#### Paladin

Engine: Seal and Judgement, melee, Holy Light, Consecration. The classic death:
trusting the bubble-hearth — which Classic Hardcore now blocks outright [60]. Shortcuts: Divine Shield, Lay on Hands, Hand of
Protection, Hand of Freedom, Divine Protection, Hammer of Justice, Avenging
Wrath.

**C5 · Long Forbearance** (Paladin) · *Forbearance lasts three minutes, and
Divine Shield empties your mana.*

- Mechanic. On `OnAuraApply` of Forbearance (25771) the duration is set to 2 → 3 →
  5 min by rank; casting Divine Shield (642) sets mana to zero.
- Counterplay. Divine Shield, Lay on Hands and Hand of Protection become one
  decision rather than three: which one, and when. Bubble-hearth still works; it
  just costs the run's next three minutes of immunity.
- Leaves intact: all three. Boon: Holy Light 10% cheaper.
- Tiers 3–14. Hooks: `UnitScript::OnAuraApply`, `Aura::SetDuration`/
  `SetMaxDuration`, `OnPlayerSpellCast`, `SetPower`.
- Note. Shortcut verb. Build priority A.

**C6 · Consecrated Ground** (Paladin) · *You take 25% more damage while not
standing in your own Consecration.*

- Mechanic. Damage taken ×1.15 → ×1.25 → ×1.4 by rank whenever the paladin is
  more than 8 yd from the centre of a live Consecration they cast
  (`GetDynObject(26573)`). Consecration lasts twice as long and costs half.
- Counterplay. Fight where you consecrate. Kiting off the circle is dangerous;
  pulling *onto* it is the play. Each fight opens with a placement decision, and
  the eight-second refresh becomes a rhythm.
- Leaves intact: everything. Boon: Consecration doubled and halved as above.
- Tiers 5–14 (Consecration is trained at 20). Hooks: `ModifyMeleeDamage`/
  `ModifySpellDamageTaken`, `Unit::GetDynObject`, `OnPlayerSpellCast`.
- Note. Anchor verb. Build priority A — the class fantasy as a rule.

**C7 · No Sanctuary** (Paladin) · *Your Hearthstone will not answer under Divine
Shield.*

- Mechanic. Casting Hearthstone (8690) is refused while Divine Shield or Hand of
  Protection is on the paladin. Rank II: also while Forbearance is up. Rank III:
  Divine Shield breaks on your first attack.
- Counterplay. Escape becomes running, Hand of Freedom, and Hammer of Justice —
  the way every other class escapes. The famous hardcore taboo, enforced.
- Leaves intact: Divine Shield, Hearthstone. Boon: Divine Shield cooldown −1 min.
- Tiers 3–12. Hooks: `OnPlayerSpellCast` (interrupt via `InterruptNonMeleeSpells`
  when the hearth cast starts), `HasAura`.
- Note. Identity verb. Build priority B.

**C8 · Commitment** (Paladin) · *Hammer of Justice roots you for its duration.*

- Mechanic. Casting Hammer of Justice (853) applies a root to the paladin for the
  stun's length (3 → 4 → 6 s by rank). Hammer's cooldown is halved.
- Counterplay. Stun-and-run becomes stun-and-finish: the button that used to buy
  an escape now buys six free seconds of melee. To flee, use Hand of Freedom
  and legs.
- Leaves intact: Hammer of Justice. Boon: cooldown halved.
- Tiers 4–14. Hooks: `OnPlayerSpellCast`, `SetControlled(true, UNIT_STATE_ROOT)`.
- Note. Threshold-style commit rule. Build priority B.

#### Hunter

Engine: ranged auto and Steady Shot, the pet, Aspects, traps. The classic death:
Feign Death resisted — every engaged enemy within 40 yd rolls — or Cheetah's
daze with three mobs behind you [64]; the most-died class in the official
count [61]. Shortcuts: Feign
Death, Disengage, Concussive Shot, Wing Clip, Freezing Trap, Deterrence, Aspect
of the Cheetah, Mend Pet.

**C9 · Half-Tamed** (Hunter) · *An unhappy pet turns on you.*

- Mechanic. When the pet's happiness state reaches Unhappy, it breaks the leash:
  the pet is dismissed and a hostile copy of it (same entry, the owner's level)
  attacks for 15 s, then despawns; the real pet can be called back afterwards.
  Rank: also at Content (rank III), with the hostile copy lasting 25 s.
- Counterplay. Feed it — the button nobody presses after level 20. Carry food.
  Keep it alive (dying costs happiness). A hunter who manages loyalty never sees
  this affix act; that is the point.
- Leaves intact: the pet. Boon: Happy pets deal +10% damage.
- Tiers 3–14. Hooks: `OnPlayerUpdate` (`GetPet()->GetHappinessState()`),
  `RemovePet`, `SummonCreature`, `OnPlayerCreatureKilledByPet`.
- Note. Companion verb. Build priority A.

**C10 · Dead Weight** (Hunter) · *Feign Death has a three-minute cooldown.*

- Mechanic. Feign Death (5384) is held on a 3 → 5 min cooldown by rank; rank III
  removes it (permanent cooldown, re-applied each tick).
- Counterplay. Disengage, Frost Trap, Concussive Shot and a pet that holds
  aggro — the kite the class was built for, without the reset button. Fewer
  over-pulls, because the over-pull now has to be fought.
- Leaves intact: Disengage, traps, Deterrence. Boon: Disengage cooldown halved.
- Tiers 4–16. Hooks: `AddSpellCooldown`/`HasSpellCooldown`, `OnPlayerUpdate`.
- Note. Shortcut verb. Build priority A.

**C11 · Wide Dead Zone** (Hunter) · *Ranged attacks cannot be used within ten
yards.*

- Mechanic. Ranged shots are refused when the target is within 8 → 10 → 15 yd by
  rank (`InterruptNonMeleeSpells` on cast start with a range check). Ranged
  damage beyond 20 yd +10%.
- Counterplay. Kiting is a skill again: Wing Clip and Raptor Strike matter,
  Concussive Shot buys distance, the pet's job is to keep things off you. Melee
  hunters exist and this affix creates them.
- Leaves intact: all shots, at range. Boon: +10% beyond 20 yd.
- Tiers 4–14. Hooks: `OnPlayerSpellCast` (ranged attack spells),
  `GetDistance`, `InterruptNonMeleeSpells`.
- Note. Anchor/positional verb. Build priority A — the TBC dead zone as a
  chosen rule.

**C12 · Blood Bond** (Hunter) · *A fifth of the damage your pet takes is dealt to
you.*

- Mechanic. In the damage hooks, when the victim is the hunter's pet, 20 → 30 →
  40% of the damage is also dealt to the hunter. Mend Pet heals the hunter for
  half its value.
- Counterplay. The pet stops being a free buffer: pull with intent, Mend Pet
  early, Growl off when the pull is bad. A voidwalker-style "let it tank
  everything" loop becomes a shared health pool you have to watch.
- Leaves intact: the pet as a tank. Boon: Mend Pet heals you too.
- Tiers 5–14. Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken` (victim
  `IsPet()` and `GetOwner()` is the hunter), `Unit::DealDamage`,
  `ModifyHealReceived`.
- Note. Companion/identity verb. Build priority B.

#### Rogue

Engine: Stealth, energy, combo points, Sinister Strike and Eviscerate. The
classic death: "a string of parries" on a low-health rogue with Vanish on
cooldown [64]. Shortcuts:
Vanish, Sprint, Evasion, Gouge, Blind, Kidney Shot, Cloak of Shadows,
Preparation, Kick.

**C13 · Cold Trail** (Rogue) · *Vanish has a ten-minute cooldown.*

- Mechanic. Vanish (1856) is kept on a 10 → 30 min cooldown by rank; rank III
  removes it (permanent cooldown re-applied each tick, so Preparation cannot
  reset it and the button greys out client-side with no patch).
- Counterplay. Escape becomes Sprint, Gouge, Blind, Evasion and route choice
  rather than a reset button. Stealth openers are untouched, so the class still
  chooses its fights; it just cannot un-choose them.
- Leaves intact: Stealth, Sprint, Evasion. Boon: Sprint cooldown halved.
- Tiers 4–16. Hooks: `AddSpellCooldown`/`HasSpellCooldown`, `OnPlayerUpdate`,
  `OnPlayerSpellCast`.
- Note. Shortcut verb. Build priority A.

**C14 · Poisoned Blades** (Rogue) · *A quarter of the poison damage you deal
ticks on you as well.*

- Mechanic. Each periodic poison tick the rogue's poisons deal to an enemy deals
  25 → 35 → 50% of that amount to the rogue (unmitigated, cannot kill). Poisons
  deal +30%.
- Counterplay. Poison choice becomes a decision: Crippling and Mind-numbing cost
  nothing, Instant and Deadly cost blood. Multi-DoTting a camp is the greedy
  play and bleeds accordingly. Unpoisoned blades are always an option.
- Leaves intact: every poison. Boon: +30% poison damage.
- Tiers 4–14. Hooks: `ModifyPeriodicDamageAurasTick` (attacker is the rogue,
  spell family Rogue poisons), `Unit::DealDamage`.
- Note. Threshold-shaped risk/reward. Build priority B.

**C15 · Exposed Back** (Rogue) · *Attacks from behind you deal 50% more damage.*

- Mechanic. Melee and spell damage from an attacker outside the rogue's front
  arc (`Position::HasInArc(π, attacker)` false) ×1.3 → ×1.5 → ×1.75 by rank.
  Dodge chance +5%.
- Counterplay. Back to a wall, Blind the second mob, Gouge and reposition, never
  let a camp surround you. The class that teaches positioning to enemies now has
  to learn it itself.
- Leaves intact: everything. Boon: +5% dodge.
- Tiers 3–14. Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken`,
  `Position::HasInArc`.
- Note. Anchor/positional verb. Build priority A.

**C16 · Slow Hands** (Rogue) · *Energy does not regenerate while you move in
combat.*

- Mechanic. Energy regeneration ×0.5 → ×0 → ×0 (rank III also halts combo
  point generation while moving) whenever the rogue has moved in the last
  second while in combat. Maximum energy +20.
- Counterplay. Stand and fight or leave; the kite-and-poke middle ground is
  gone. Kidney Shot and Gouge buy stationary seconds. Sprint is for escaping,
  not for repositioning every three seconds.
- Leaves intact: everything. Boon: +20 max energy.
- Tiers 4–14. Hooks: `OnPlayerUpdate` (`isMoving`), `ModifyPower` or an
  energy-regen aura (`SPELL_AURA_MOD_POWER_REGEN_PERCENT` via `spell_dbc`).
- Note. Threshold/tempo verb. Build priority B.

#### Priest

Engine: Smite or Mind Flay, Shadow Word: Pain, Renew, Flash Heal, Power Word:
Shield. The classic death: a Psychic Scream that brings the camp back with
friends — the guides teach fear as a positional tool for exactly that reason
[64]. Shortcuts: Power Word: Shield cycling, Psychic Scream, Fade, Desperate
Prayer, Dispersion, Shadowform.

**C17 · Frail Soul** (Priest) · *Weakened Soul lasts 30 seconds.*

- Mechanic. On `OnAuraApply` of Weakened Soul (6788) the duration is set to 20 →
  30 → 45 s by rank.
- Counterplay. Power Word: Shield becomes a decision about *when*, not a reflex
  before every pull: pre-shield and pull fast, or save it for the second mob and
  open with Renew. Shadow priests lose the shield-cycling safety net and gain a
  reason to press Psychic Scream.
- Leaves intact: the shield. Boon: Renew 20% stronger on yourself.
- Tiers 3–14. Hooks: `UnitScript::OnAuraApply`, `Aura::SetDuration`.
- Note. Shortcut verb. Build priority A.

**C18 · Faithless Form** (Priest) · *Leaving Shadowform has a thirty-second
cooldown.*

- Mechanic. Cancelling or leaving Shadowform (15473) puts it, and re-entering,
  on a 15 → 30 → 60 s cooldown by rank. Shadow damage +10% in form.
- Counterplay. Healing means committing out of form for half a minute: heal at
  50% and plan, or ride the form and trust Vampiric Embrace and Dispersion.
  Powershifting priests are the target.
- Leaves intact: Shadowform. Boon: +10% shadow damage in form.
- Tiers 6–14 (Shadowform at 40). Hooks: `UnitScript::OnAuraRemove` (15473),
  `AddSpellCooldown`.
- Note. Identity verb. Build priority B.

**C19 · Penance of Silence** (Priest) · *Healing yourself silences you for two
seconds.*

- Mechanic. Any direct heal cast on self applies a 2 → 3 → 4 s silence by rank
  (Renew excluded at rank I). Self-heals +20%.
- Counterplay. Heal-then-attack becomes heal-then-wand. Renew before the pull
  is free; Flash Heal mid-fight costs the next cast. The decision "heal now or
  push" is made explicit every time.
- Leaves intact: every heal. Boon: +20% self-healing.
- Tiers 4–14. Hooks: `OnPlayerSpellCast` (`IsPositive()` and target self),
  `CastSpell` (silence aura with `SetDuration`).
- Note. Tempo verb. Build priority B.

**C20 · Whispers of the Deep** (Priest) · *Below 20% health you lose your mind
and flee for three seconds, once per fight.*

- Mechanic. When health first drops below 20% in a fight (rank: 15 → 20 → 30%),
  the priest is feared (`SetControlled(true, UNIT_STATE_FLEEING)`) for 3 s. Once
  per combat.
- Counterplay. Heal earlier — the line is 20%, and a feared priest near a camp
  is a dead priest. Fear Ward on yourself prevents it (a button priests forget
  they have). Desperate Prayer *before* the line, not after.
- Leaves intact: everything. Boon: Fear Ward cooldown halved.
- Tiers 5–14. Hooks: `UnitScript::DealDamage` or `OnDamage` (threshold
  crossing), `SetControlled`, `HasAura` (Fear Ward 6346).
- Note. Threshold verb. Build priority A — "lose your mind" for the class whose
  lore is losing it.

#### Death Knight

Engine: runes and runic power, diseases, Death Strike, the ghoul. The classic
death: every rune spent with nothing left when the second elite arrives.
Shortcuts: Death Grip, Anti-Magic Shell, Icebound Fortitude, Chains of Ice,
Death Pact, Army of the Dead, Lichborne, presences.

**C21 · Rune-starved** (Death Knight) · *While all six runes are on cooldown you
take 30% more damage.*

- Mechanic. In the damage hooks, if `GetRuneCooldown(i) > 0` for all six runes,
  damage taken ×1.2 → ×1.3 → ×1.4 by rank. The addon shows the state.
- Counterplay. Keep one rune. Blood Tap and Empower Rune Weapon become defensive
  tools; Death Strike is planned rather than spammed. The "press everything on
  cooldown" rotation is what the affix punishes.
- Leaves intact: everything. Boon: runic power decays 50% slower.
- Tiers 12–16 (the class starts at 55). Hooks: `ModifyMeleeDamage`/
  `ModifySpellDamageTaken`, `Player::GetRuneCooldown`.
- Note. Threshold verb. Build priority A.

**C22 · Grave Call** (Death Knight) · *The dead you do not claim rise against
you.*

- Mechanic. Five seconds after a kill, the corpse rises as a hostile ghoul
  (level = owner's, weak) unless the knight has consumed it: Raise Dead, Corpse
  Explosion, Death Pact or Army of the Dead within those five seconds claims it.
  Rank: 8 → 5 → 3 s; rank III ghouls are not weak.
- Counterplay. Corpse economy. A kill is not finished until the corpse is used,
  and every corpse-consuming ability becomes a rotation piece. Or walk away —
  the risen ghoul leashes like any mob.
- Leaves intact: everything. Boon: Raise Dead cooldown halved.
- Tiers 12–16. Hooks: `OnPlayerCreatureKill` (schedule), `OnPlayerSpellCast`
  (claim), `SummonCreature`.
- Note. Companion/identity verb. Build priority A — the class fantasy inverted:
  the dead are yours only if you take them.

**C23 · Cold Presence** (Death Knight) · *Changing presence costs all your runic
power and has a ten-second cooldown.*

- Mechanic. Casting a presence sets runic power to zero and puts the other two on
  a 6 → 10 → 20 s cooldown by rank. Presence bonuses +25%.
- Counterplay. Blood for the elite, Frost to hold, Unholy to travel — chosen
  before the fight, not during. Rune Strike and Death Coil dumps happen *before*
  the switch.
- Leaves intact: every presence. Boon: +25% presence effects.
- Tiers 12–16. Hooks: `OnPlayerSpellCast` (48266/48263/48265), `SetPower`,
  `AddSpellCooldown`.
- Note. Identity verb. Build priority B.

**C24 · One Ward** (Death Knight) · *Anti-Magic Shell and Icebound Fortitude
share a cooldown.*

- Mechanic. Casting either puts both on the longer of the two cooldowns (2 min);
  rank III adds Lichborne to the set. Whichever is used lasts 50% longer.
- Counterplay. Read the fight: a caster pack is a Shell fight, a melee elite is
  a Fortitude fight. One defensive per two minutes is the class's actual budget
  — the affix just stops the pretence that it is two.
- Leaves intact: both. Boon: +50% duration.
- Tiers 12–16. Hooks: `OnPlayerSpellCast`, `AddSpellCooldown`, `Aura::SetDuration`.
- Note. Shortcut verb. Build priority B.

#### Shaman

Engine: Lightning Bolt and shocks, Windfury melee, totems, Ghost Wolf. The
classic death: totem aggro pulling the second group; by rate, one of the two
worst-surviving classes in Deathlog data [63][64]. Shortcuts: Earth
Shield, Ghost Wolf, Earthbind, Stoneclaw, Frost Shock kiting, Wind Shear, Hex.

**C25 · One Totem** (Shaman) · *Only one totem may stand at a time.*

- Mechanic. When a totem is summoned, the others are removed
  (`UnsummonAllTotems` before the new one lands, via the four totem summon
  slots). Rank II: the standing totem dies to one hit; rank III: totems cost
  double mana.
- Counterplay. Each fight opens with a choice: Stoneclaw to tank, Searing to
  kill, Healing Stream to last, Earthbind to run. Four buttons the class usually
  presses as a set become a real allocation.
- Leaves intact: every totem. Boon: the standing totem lasts twice as long.
- Tiers 3–14. Hooks: `OnPlayerSpellCast` (totem summon effects),
  `UnsummonAllTotems`, `Unit::m_SummonSlot`.
- Note. Identity verb. Build priority A.

**C26 · Totemic Anchor** (Shaman) · *You take 30% more damage when more than
fifteen yards from your totems.*

- Mechanic. Damage taken ×1.2 → ×1.3 → ×1.4 by rank while no totem the shaman
  owns is within 15 yd. Totem effects +30%.
- Counterplay. The spirits protect their circle: drop totems where the fight
  will be, pull to them, and re-drop when you move. Kiting out of the circle
  costs; kiting *around* it is the skill.
- Leaves intact: everything. Boon: +30% totem effects.
- Tiers 4–14. Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken`,
  `Unit::m_SummonSlot` (four totem slots), `GetDistance`.
- Note. Anchor verb. Build priority A. Composes with One Totem (one totem, one
  anchor).

**C27 · Elemental Overload** (Shaman) · *Casting the same spell twice in a row
costs double.*

- Mechanic. A spell cast immediately after the same spell costs ×1.5 → ×2 → ×3
  mana by rank. Alternating spells deal +15%.
- Counterplay. Lightning Bolt spam is the tax; weaving Bolt, shock, Lava Burst
  and totem drops is the reward. The rotation the class *should* have, enforced.
- Leaves intact: everything. Boon: +15% for alternating.
- Tiers 4–14. Hooks: `OnPlayerSpellCast` (track last spell id), `ModifyPower`.
- Note. Tempo/threshold verb. Build priority B. Could be offered to mages too.

**C28 · Spirit Debt** (Shaman) · *Earth Shield and Lightning Shield charges are
consumed by every hit, and each consumed charge costs you 2% health.*

- Mechanic. Every damage event (including DoT ticks) consumes a shield charge;
  each consumption deals 2 → 3 → 4% max health to the shaman. Shields carry
  three extra charges.
- Counterplay. Shields become a resource with a price, not a passive: reapply
  them only when you need the heal or the proc, and get out of DoTs. Enemies
  that hit fast (rogues, beasts) are the ones to CC first.
- Leaves intact: both shields. Boon: +3 charges.
- Tiers 5–14. Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken`/
  `ModifyPeriodicDamageAurasTick`, `Aura::ModCharges`, `Unit::DealDamage`.
- Note. Companion-style rule (the shield is the companion). Build priority B.

#### Mage

Engine: Frostbolt or Fireball, Frost Nova, conjured food and water, Blink. The
classic death: shields down "with frost-nova and blink on cooldown" [64]. Shortcuts: Blink,
Ice Block, Polymorph, Invisibility, Counterspell, Mana Shield, Ice Barrier,
Evocation, Cold Snap.

**C29 · Cold Feet** (Mage) · *Blink costs 15% of your maximum health.*

- Mechanic. Casting Blink (1953) deals 15 → 25% of max health as self-damage
  (cannot kill); rank III removes Blink (permanent cooldown as in C13).
- Counterplay. Blink becomes a decision instead of a reflex; Frost Nova,
  Polymorph, Ice Block and positioning carry the weight they carried before
  everyone learned to Blink through everything. At rank III the class plays like
  a warlock without a pet, which is a real style and still winnable.
- Leaves intact: Frost Nova, Ice Block, Polymorph. Boon: Frost Nova cooldown
  −25%.
- Tiers 3–16. Hooks: `OnPlayerSpellCast`, `Unit::DealDamage`,
  `AddSpellCooldown`.
- Note. Shortcut verb. Build priority A.

**C30 · Fickle Sheep** (Mage) · *Polymorph breaks after five seconds, and the
sheep comes back angry.*

- Mechanic. Polymorph's duration is set to 5 → 4 → 3 s by rank
  (`OnAuraApply` 118 and ranks); when it ends the target gains a 20% damage
  enrage for 10 s. Polymorph is instant.
- Counterplay. CC buys time, not neutralisation: sheep to reposition, to finish
  the first target, to bandage for three seconds — then deal with an angrier
  second mob. Kill order and Frost Nova timing matter more than the sheep.
- Leaves intact: Polymorph. Boon: instant cast.
- Tiers 4–14. Hooks: `UnitScript::OnAuraApply`/`OnAuraRemove`, `Aura::
  SetDuration`, `AddAura` (8599 Enrage on the target).
- Note. Shortcut verb. Build priority B.

**C31 · Mana Burn** (Mage) · *Half the damage you take also burns your mana.*

- Mechanic. In the damage hooks, 30 → 50 → 100% of damage taken is removed from
  mana as well (`ModifyPower`). Spell damage +10%.
- Counterplay. Armor and shields matter: Mana Shield becomes central, Ice
  Barrier is a mana-saver, and taking hits at all is what runs you dry. The
  glass cannon has to be glass that does not get touched.
- Leaves intact: everything. Boon: +10% spell damage.
- Tiers 4–14. Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken`,
  `ModifyPower`.
- Note. Threshold/anchor verb. Build priority A.

**C32 · Arcane Frailty** (Mage) · *Thirty percent less health, thirty percent
more spell damage.*

- Mechanic. Max health ×0.8 → ×0.7 → ×0.6 by rank; spell damage +20 → +30 →
  +40%. Risk of Rain's Artifact of Glass, for the class that is already glass.
- Counterplay. Kill first. Every mob that reaches you is a mistake you can
  afford fewer of; CC, range and Nova timing are the whole game.
- Leaves intact: everything. Boon: the damage is the boon.
- Tiers 6–16. Hooks: `OnPlayerAfterUpdateMaxHealth`, `ModifySpellDamageTaken`
  (attacker side).
- Note. Identity verb, bargain-shaped. Build priority B.

#### Warlock

Engine: DoTs and Shadow Bolt, the demon, Life Tap, Drain Life, Fear. The classic
death: fear-kiting into adds after the Voidwalker loses aggro [64]. Shortcuts:
Fear, Howl of Terror, Death Coil, Sacrifice, Demonic Circle, Healthstone,
Soulstone, Shadow Ward.

**C33 · Fel Pact** (Warlock) · *Your demon's binding frays with every kill it
makes. After twenty it breaks free and turns on you, unless you re-bind it
first.*

- Mechanic. Kills by the pet advance a visible counter. At 20 → 15 → 10 by rank,
  the demon is dismissed and a hostile copy of it (same entry, the owner's level)
  attacks for 15 s. Dismissing and re-summoning the demon before the count —
  which costs a Soul Shard — resets it.
- Counterplay. A shard versus a fight, every twenty kills: the resource decision
  the class is built around, made visible. Health Funnel and the counter tell
  you when to pay. Or fight your own Voidwalker at full health on cleared ground
  — it is a mob you know intimately.
- Leaves intact: every demon. Boon: the demon deals +10% damage.
- Tiers 4–16. Hooks: `OnPlayerCreatureKilledByPet` (counter), `RemovePet`,
  `SummonCreature`, `OnPlayerSpellCast` (summon spells reset).
- Note. Companion verb. Build priority A.

**C34 · Affliction of the Self** (Warlock) · *Your curses and corruption afflict
you too, at a fifth of their strength.*

- Mechanic. Each DoT tick the warlock deals (Corruption, Curse of Agony,
  Immolate, Unstable Affliction) deals 20 → 30 → 40% of that amount to the
  warlock (cannot kill). DoTs deal +25%.
- Counterplay. Multi-DoTting a camp is the greedy play and bleeds accordingly;
  Drain Life is the antidote the kit already carries; Shadow Bolt and the demon
  are free. The number of targets you dot becomes a health decision.
- Leaves intact: every DoT. Boon: +25% DoT damage.
- Tiers 4–14. Hooks: `ModifyPeriodicDamageAurasTick` (attacker is the
  warlock, spell family Warlock), `Unit::DealDamage`.
- Note. Threshold-shaped risk/reward. Build priority B.

**C35 · Shard Economy** (Warlock) · *Every summon and every Healthstone costs a
Soul Shard, and shards drop only from enemies of your level or higher.*

- Mechanic. Imp summons and Healthstone use consume a shard; Drain Soul yields a
  shard only from targets at or above the warlock's level (rank: −2 → 0 → +1
  level). Drain Soul yields two shards.
- Counterplay. Shards become lives: spend them on the demon, on the stone, or
  on the Soulstone that no longer works anyway. Hunting higher-level mobs for
  shards is a risk the class chooses.
- Leaves intact: everything. Boon: double shards.
- Tiers 4–14. Hooks: `OnPlayerSpellCast`, `OnPlayerCanCastItemUseSpell`
  (Healthstone), `Player::DestroyItemCount` (shard 6265),
  `OnPlayerCreatureKill`.
- Note. Identity verb. Build priority B.

**C36 · Shared Blood** (Warlock) · *While your demon lives you take 25% more
damage, and it deals 40% more.*

- Mechanic. Damage taken ×1.15 → ×1.25 → ×1.4 by rank while a demon is summoned;
  demon damage +40%.
- Counterplay. Sacrifice, dismiss or Soulshatter before the dangerous fight —
  the demon is a weapon you sheathe, not armour you wear. Fear and Drain Life
  carry the fight without it.
- Leaves intact: every demon. Boon: +40% demon damage.
- Tiers 5–14. Hooks: `ModifyMeleeDamage`/`ModifySpellDamageTaken`,
  `Player::GetPet`.
- Note. Companion/identity verb. Build priority B.

#### Druid

Engine: forms, Moonfire and Wrath, Rejuvenation and Regrowth, Entangling Roots,
Prowl. The classic death: early mana, then shifting out to heal at 15% and being
hit mid-cast [64].
Shortcuts: powershifting, Travel Form, Dash, Barkskin, Nature's Grasp, Bash,
Feral Charge, Hibernate, Cyclone, Innervate.

**C37 · Bound Skin** (Druid) · *Shapeshifting has a six-second cooldown.*

- Mechanic. On `OnUnitSetShapeshiftForm`, every form spell is put on a 4 → 6 →
  10 s cooldown by rank (Bear, Cat, Travel, Aquatic, Moonkin, Tree).
- Counterplay. Powershifting is gone; leaving Cat to heal is a commitment, not a
  flicker. The class decides *before* the pull whether this is a Bear fight or a
  Cat fight, and learns to heal at 50% instead of 15%.
- Leaves intact: every form. Boon: +10% max health while shapeshifted.
- Tiers 3–14. Hooks: `UnitScript::OnUnitSetShapeshiftForm`, `AddSpellCooldown`.
- Note. Identity verb. Build priority A.

**C38 · Nature's Toll** (Druid) · *Every kill made as a beast leaves you bleeding
until you calm.*

- Mechanic. A kill in Cat or Bear form applies a bleed of 2 → 3 → 4% max health
  per second that ends only when the druid returns to caster form. Feral forms
  deal +10%.
- Counterplay. Shift out after the fight, heal, shift back — a rhythm the class
  used to skip. Chain-pulling in Cat is possible and costs exactly what it
  should. Mutually exclusive with Bound Skin, which would make it a tax.
- Leaves intact: every form. Boon: +10% feral damage.
- Tiers 4–14. Hooks: `OnPlayerCreatureKill`, `GetShapeshiftForm`,
  `OnPlayerUpdate` (tick), `OnUnitSetShapeshiftForm` (clear).
- Note. Tempo verb. Build priority B.

**C39 · Commitment of Roots** (Druid) · *Entangling Roots holds you as long as it
holds them.*

- Mechanic. Casting Entangling Roots applies a root to the druid for the same
  duration (breaks when the enemy's breaks). Roots cost half.
- Counterplay. Root-and-kite becomes root-and-fight, or root-and-heal: the
  druid decides what the seconds are for. Nature's Grasp is unaffected, so the
  escape root still exists.
- Leaves intact: Roots, Nature's Grasp. Boon: Roots cost half.
- Tiers 3–12. Hooks: `OnPlayerSpellCast` (339), `SetControlled(true,
  UNIT_STATE_ROOT)`, `UnitScript::OnAuraRemove`.
- Note. Commit rule. Build priority B.

**C40 · Two Faces** (Druid) · *By day your spells are weaker; by night your claws
are.*

- Mechanic. During the day (server time, as `AtDay`), spell damage and healing
  ×0.8; at night, feral damage ×0.8. The other face is +10% at the same time.
  Rank raises the penalty to 30% and 40%.
- Counterplay. Play the clock: caster levelling by night, feral by day, or accept
  the weaker face for the hour. A route rule tied to the world's cycle, which
  no other affix uses.
- Leaves intact: everything. Boon: +10% to the favoured face.
- Tiers 3–12. Hooks: existing `AtDay`/`AtNight` condition, `ModifyMeleeDamage`/
  `ModifySpellDamageTaken`/`ModifyHealReceived` (attacker side).
- Note. Identity verb. Build priority B — flavourful; the moment-to-moment
  content is thin, which is why it sits at rank B.

#### Every class

**C41 · Faint** (all mana users) · *When your mana hits zero in combat you black
out for two seconds.*

- Mechanic. At `GetPower(POWER_MANA) == 0` while in combat, a 2 → 3 → 4 s stun
  (`SetControlled(true, UNIT_STATE_STUNNED)`), once per 10 s.
- Counterplay. Wand, melee or Shoot before the bar is empty; drink discipline;
  Evocation and Innervate become insurance rather than afterthoughts. Running
  dry — the most common way a levelling caster dies anyway — now has a tell
  before the death.
- Leaves intact: everything. Boon: +15% mana regeneration while casting.
- Tiers 3–14. Hooks: `OnPlayerUpdate`, `SetControlled`.
- Note. Threshold verb. Build priority A.

**C42 · Unspent** (all classes) · *You receive a talent point every second level.
Each point you leave unspent makes you 2% stronger.*

- Mechanic. `OnPlayerCalculateTalentsPoints` halves the points granted (rank:
  two of three → one of two → one of three); every unspent point grants +2%
  damage and healing. The addon shows the bank.
- Counterplay. Build-shaping as a run-long decision: the 31-point capstone now
  arrives at 60, so is it worth it, or is a bank of ten points and +20% the
  better character? Every level asks the question again.
- Leaves intact: the talent trees. Boon: the bank.
- Tiers 2–8 (it needs the run ahead of it). Hooks:
  `OnPlayerCalculateTalentsPoints`, `OnPlayerLearnTalents`,
  `GetFreeTalentPoints`, damage hooks.
- Note. Identity verb on the meta-system. Build priority B. The only affix here
  that touches character building rather than combat, which is why it comes
  early.

#### Class bargains

Two abilities the Gauntlet already disables — a shaman's Reincarnation and a
warlock's Soulstone both die to `OnPlayerJustDied` — can be sold back at a
price. Offered from tier 8, once per run, family B rules.

**C43 · Ankh Pact** (Shaman) · *Reincarnation works once in this run. When it
does, every boon you carry is burned away.*

- Mechanic. `OnPlayerJustDied` defers `EndRun` if the shaman has a Reincarnation
  charge; on the self-resurrect, all boons on carried affixes are zeroed and the
  charge is consumed. Rank: the price is boons → boons plus the next tier's
  offers being Dire → boons plus one extra affix rolled immediately.
- Counterplay. Knowing there is a second life is the whole trap; the price is
  paid in the run's power for the remaining tiers. Use it to save a level-70
  character, not a level-30 one.
- Leaves intact: everything. Boon: the second life.
- Tiers 8–16. Hooks: `OnPlayerJustDied`, `OnPlayerResurrect`, `OnPlayerCan-
  Resurrect`.

**C44 · Stone of the Damned** (Warlock) · *A Soulstone will bring you back once.
Whoever kills you will be waiting.*

- Mechanic. As C43 for a Soulstone resurrection. On return, the creature that
  killed the warlock is summoned again beside them at full health as a Champion
  (E1 rules); the Soulstone cannot be recreated for the rest of the run.
- Counterplay. The second life is a rematch, not an escape. Prepare the
  Soulstone before dangerous content, and be ready to win the fight you just
  lost.
- Leaves intact: everything. Boon: the second life.
- Tiers 8–16. Hooks: `OnPlayerJustDied`, `OnPlayerResurrect`, `SummonCreature`.

**Considered for this family and cut.** *No shield / no swords / no axes for a
warrior*: restricting one weapon type is a loot rule with no in-combat moment
(fine as a tier 1–3 rules affix, `OnPlayerCanEquipItem` already has a precedent
in mod-challenge-modes, but weak); restricting all three is a brick. *A demon
that "may" attack you*: random is the wrong trigger in permadeath — Fel Pact is
the deterministic version. *No Voidwalker*: removes the levelling warlock's
engine. *Stealth breaks near enemies* and *no Feign Death at rank I*: the
rogue's and the hunter's engines. *No Travel Form / no Ghost Wolf / Ghost Wolf
instead of mounts*: tedium, or removes the mounted escape. *Hammer of Justice
stuns you too*: funny once, a coin-flip death by tier 10 — Commitment (C8) is
the version with a decision in it. *Death Grip pulls you instead*: wonderful on
paper, but the core has no clean way to redirect a spell's movement effect
without a client-visible spell swap.

### 3.7 Ideas considered and rejected

- **Enemies never leash / you cannot mount / you move slower.** Removes the
  universal escape. Every other affix's counterplay quietly dies with it.
- **Chance-based anything** ("enemies have 10% chance to crit for triple"). In
  permadeath, variance is the killer. Also unlegible: the player cannot tell the
  affix from bad luck.
- **Hide health / names / minimap.** Isaac curses. Noise, not difficulty.
- **Enemies immune to CC; no stealth; no pets.** Build-bricks.
- **No potions / no bandages.** Denial without decision.
- **Thorns / damage reflect.** PoE's most rerolled mod; a melee brick.
- **Hearthstone cooldown, no rested XP, training costs.** Chores.
- **Per-mob shield phases.** Fine once; tedious across a levelling curve.
- **Vengeful enemies** (harder per recent kill). Only ever says "slow down";
  Frenzy says the same thing as a choice.
- **"Coward's mark" for fleeing.** Fleeing must stay free.
- **Time-boxed levelling** ("reach L20 within N hours"). Keyed to a clock the
  player does not control; punishes the social play the announce feature
  encourages.

---

## 4. Stacking and pacing

### 4.1 Slots and ranks, not a pile

Sixteen unrelated mechanics is incoherent; sixteen coefficients is boring. The
middle path is Slay the Spire's: **introduce, then intensify**.

- Every mechanic has a family (S/E/T/A/R/B/C) and up to three ranks (the severity
  ladders above).
- An affix already carried is never offered again as a duplicate; it is offered as
  its next rank, which *replaces* it in the same slot.
- Family caps: one active stalker (S1 or S2, not both), two on-kill mechanics
  (S3/E3/E4/E5, with E4 and E5 mutually exclusive), two tempo mechanics
  (S4/T1/T4), one role tax (E7 or T4), one rule affix, one class curse, two
  bargains. Attrition and reworked scalars share a cap on their *summed*
  magnitude rather than a count.
- Result: by tier 16 a run carries roughly seven to nine distinct mechanics, most at
  rank II or III, plus a couple of conditional scalars. Legible and escalated.

### 4.2 One scheduler per player

The single most important coherence rule: **one event at a time**.

- All timed and triggered events (Shade, Echo, Ambush, Reinforcements, Falling Sky,
  Falter, Carrion, Death Rattle) go through one per-player queue with a minimum
  spacing (12 s) and a priority order. If Falling Sky is due while Falter's warning
  is up, Falling Sky waits.
- An *event budget* stretches intervals as timed affixes accumulate: effective
  interval = base × (1 + 0.25 × (timed affixes carried − 1)). Three timed affixes
  therefore each fire 1.5× less often than alone. Pressure per minute rises with
  tier, but sub-linearly.
- At most one uninvited creature from the stalker/ambush group alive per player at
  a time; at most four affix-spawned creatures in total.
- No events while mounted, in flight, in a sanctuary, dead, in the first 60 s after
  login or zone-in, or during the affix-choice prompt.

### 4.3 Global caps on the scalars

`Mgr::Multiplier` sums magnitudes additively with only a floor of 0.05. Sixteen
damage-taken affixes could stack to +800%. Cap the *aggregate*: damage taken
≤ ×2.0 from all sources including Champions, Frenzy, Overextended and the Mark;
damage dealt ≥ ×0.6; healing received ≥ ×0.5; max health ≥ 60% of base after
wounds; enemy speed ≤ 140%. Print the effective totals in `.gauntlet status` so
the player can see the ceiling.

### 4.4 Building an offer

Deterministic from `(seed, tier)`, as now, but structured:

1. Roll three *families* first, distinct from one another, respecting caps and
   the tier-unlock table below. Then roll the mechanic within the family, its
   condition, severity (floor by tier, as now) and boon.
2. Slot A is a **rank-up** of something carried once the run holds at least three
   mechanics (always, from tier 9). Slot B is a **new** mechanic. Slot C is either
   a bargain (weight 1/6) or a second new mechanic.
3. At tiers 4, 8 and 12, slot C is a **swap**: take this affix *and discard one you
   carry*. It keeps sixteen picks meaningful and lets a player repair a tier-2
   mistake without a reroll button.
4. Filter every offer by **class relevance** (§4.5); reroll within the family from
   the same stream if it fails, so the seed still reproduces the run.
5. Guarantee at least one reward-shaped offer (tagged as in §3, family B) per tier.

### 4.5 Class relevance

Extend `IsImplemented` to `IsRelevant(player, mechanic)`. Cunning, Falter's silence
and Blood Magic need cast-time or mana; Death Rattle and Grudge need a melee
class; Nimble is soft-relevant to everyone but weighted up for kiters; rules are
always relevant; class curses (family C) are relevant to exactly one class, or to
mana users for Faint. An affix that is irrelevant to the class is never offered.

### 4.6 Tier curve

Ordering first, because the evidence cuts two ways. The cross-run ladders studied
(Slay the Spire's ascensions, M+ key levels) put numbers early and rule changes
late, because each rung is a whole run and a new player must be able to finish
one [4][20]. Your tiers are rungs *inside* a single run, so the analogue is a
run's floors, not its ascension: mechanics should arrive while the run is still
cheap to lose (an hour at level 15, forty hours at level 70) — but only once the
class owns the button that answers them. A level-8 character has no snare, no CC
and often no self-heal, which is why tiers 1–2 carry conditional scalars and
counter-driven content but no interrupts, silences or stalkers. Blizzard's
placement rule — periodic affixes at +7, death-or-health-range affixes at +14 —
translates here to: timed events from tier 5, health-threshold punishments from
tier 4, and nothing that can end a run in a single event before tier 6.

| Tiers | Levels | What unlocks | Why |
|---|---|---|---|
| 1–2 | 5–10 | Champions, Hubris, one rule, Carrion, conditional Exposed | Teach that affixes are content; the UI; the counter idiom |
| 3–5 | 15–25 | Ambush, Grudge, Overextended, Frenzy, Keen-nosed, Deep Wounds; Shade at 4 | First positional and out-of-combat pressure |
| 6–10 | 30–50 | Full pool: Echo, Reinforcements, Falling Sky, Falter, Cunning, Nimble, Craven, Call to Arms; bargains from 6 | The run has its identity by tier 8 |
| 11–16 | 55–80 | Rank-ups dominate; swaps at 12; Last Rites available | Outland and Northrend mobs are already harder; escalate what exists rather than adding verbs |

### 4.7 Replacement and scaling down

- Ranks replace. Swaps replace. Nothing else is silently removed — a hardcore
  player must be able to trust that what they see in the addon is what is acting.
- Timed intervals scale down through the event budget above, not through hidden
  nerfs to individual affixes.
- The leaderboard records the run's class curses by name and rank (NetHack's
  conducts, §2.10): a cheap line in `gauntlet_leaderboard` and the only reward
  the family needs beyond its boons.
- Two affixes are allowed to *compound* on purpose, because compounding is where
  the stories come from: Call to Arms + Craven (a camp that fetches and flees),
  Champions + Frenzy (a promoted mob at five stacks), Shade + Deep Wounds (a chase
  you cannot rest away). Play-test those pairs specifically.

### 4.8 The counterplay audit

Before shipping any affix, answer four questions on its card: *what button or
movement answers it; does the answer exist for every class it is offered to; does
it still work with every other affix it can coexist with; does the player know
which affix acted when they die.* If the fourth answer is "no", the addon needs a
line, not the affix a nerf.

---

## 5. The current four

| Effect | Verdict | Why |
|---|---|---|
| Damage taken (`Exposed`) | **Keep**, conditional only | The honest baseline; with a state condition it is a threshold, and thresholds are decisions. Never `Always`/`InCombat`. Fix `VersusElites` at the damage site. |
| Damage dealt (`Feeble`) | **Demote** to a price | Standalone it is a tax on a tax (longer fights → more damage → more downtime). As the cost of a boon or bargain, or under `Desperate`, it earns its place. |
| Healing received (`Withering`) | **Replace** with Deep Wounds | Same pressure, but the answer becomes "get hit less" instead of "have more". Keep the flat version only inside Grudge and the Last Rites Mark. |
| Experience gained (`Forgetful`) | **Cut** from the standalone pool | No moment, lengthens exposure without changing play, and it is the always-safe pick that makes the other two offers irrelevant. Replace with Hubris; use XP as boon currency and bargain price. |

### How a scalar earns its place when it is used

1. **Thresholds, not slopes.** "+35% below half health" is a line the player can
   play around; "+12% always" is weather.
2. **Count-based, not flat.** Overextended (per attacker) and Frenzy (per recent
   kill) are scalars whose value the player controls each pull.
3. **Big and few.** Minimum 15%. Five 5% affixes are unfelt individually and
   unattributable collectively; one 25% conditional affix is a rule.
4. **Visible when active.** The condition should light up in the addon the moment
   it is true (the `LANG_ADDON` channel in §6 makes this a one-line broadcast).
   A scalar you cannot see acting is a scalar you cannot learn from.
5. **Always a trade.** Pair standalone scalars with a boon; the pick is then "what
   do I want to be good at", which is a build decision.
6. **Capped in aggregate** (§4.3), so the late run is hard because of what
   happens, not because a coefficient crossed 3.0.

---

## 6. Implementation notes (verified against the core)

Everything below was checked in the tree the sync script copies to.

**Assumed primitives — confirmed.**
`WorldObject::SummonCreature` (`Object.h:639-640`), `Unit::CastSpell` and
`CastCustomSpell` (`Unit.h:1676-1686`), `Unit::AddAura` (`Unit.h:1351`),
`RemoveAurasDueToSpell`, `KnockbackFrom`, `SetSpeedRate` (`Unit.h:1742`),
`InterruptNonMeleeSpells` (`Unit.h:1597`), `ProhibitSpellSchool` (`Unit.h:1585`),
`AddThreat`/`SetInCombatWith`, `MotionMaster::MoveFleeing` (`MotionMaster.h:239`),
`Unit::getAttackers` (`Unit.h:901`), `HasStealthAura` (`Unit.h:1841`),
`WorldObject::IsOutdoors` (`Object.h:524`), `Player::HasRestFlag`
(`Player.h:1221`), `Unit::IsInSanctuary`, `SetObjectScale`, `SetDisplayId`,
`GetNativeDisplayId`, `Aura::SetDuration` (`SpellAuras.h:134`). Player inherits all
of it; your grep of `Player.h` alone simply missed the base classes.

**Hooks you did not list that matter.**

- `UnitScript::DealDamage(attacker, victim, damage, type) → uint32` runs before
  health is applied (`Unit.cpp:984`). This is the cheat-death hook for Last Rites
  and the cleanest place for Deep Wounds' observer.
- `UnitScript::OnBeforeRollMeleeOutcomeAgainst` exposes crit, miss, dodge, parry and
  block chance by reference — the "crit information" you thought was missing, on
  the *chance* side. Not used above on purpose (variance), but available.
- `UnitScript::OnAuraApply/OnAuraRemove` — react to CC on the player.
- `PlayerScript::OnPlayerEnterCombat(player, enemy)` / `OnPlayerLeaveCombat` —
  the natural place for engage counters and the in-combat clocks.
- `AllCreatureScript::GetCreatureAI` and `OnBeforeCreatureSelectLevel` — custom AI
  for stalkers without touching the world DB's script names, and level-to-owner.
- `GlobalScript::OnItemRoll`, `OnAfterCalculateLootGroupAmount` — loot boons.
- Veto hooks for rules: `OnPlayerCanInitTrade`, `OnPlayerCanSendMail`,
  `OnPlayerCanPlaceAuctionBid`, `OnPlayerCanGroupInvite`, `GroupScript::OnAddMember`.
- `OnPlayerBeforeSendLoot(player, lootGuid, loot)` fires for corpses and chests
  alike; branch on `lootGuid.IsCreature()` / `IsGameObject()`.

**A real data channel to the addon.** The core already sends server→client addon
messages as a `LANG_ADDON` whisper from the player to themselves
(`AddonChannelCommandHandler::Send`, `Chat.cpp:1104-1109`). Reuse the pattern with
a `GAUNTLET\t` prefix; the 3.3.5 client raises `CHAT_MSG_ADDON` and the addon can
show countdowns, counters, wound, Frenzy stacks and "condition active" lights
without parsing system chat. Test the prefix/payload split once — it is the one
thing here I could not execute.

**Custom creatures without client patches.** Ship `creature_template` rows in
`data/sql/db-world/` using existing display ids (the Shade a ghost model, the
Doppelgänger any humanoid then cloned with 45204/41055, Scavengers a hyena).
Display ids and spell visuals already exist client-side; only the template is
new. Entry 12999 exists in the base world DB and 21252 is the World Trigger used
by the core's own scripts for invisible casters.

**Reusable spell ids seen in core scripts** (verify in-game before committing):
8599 Enrage (visible red glow, used by six scripts), 45204 Clone Me and 41055
copy-weapon (mirror image), 676 Disarm, 8269 Frenzy. Server-side `spell_dbc` rows
are supported if you need a bespoke aura, but the client shows no icon for them —
use the addon channel for state, and existing spells for anything that must be
seen.

**Costs.** Grid searches (`Keen-nosed`, `Call to Arms`) every 500 ms for a handful
of real players is cheap; do not run them for bots (already excluded by
`IsEligible`). Keep summons `TEMPSUMMON_TIMED_OR_DEAD_DESPAWN`, owner-bound, and
despawn them in `OnPlayerLogout`, `OnPlayerJustDied` and `OnPlayerUpdateZone`.

**Determinism.** Offers stay seed-reproducible. Events are not — combat never was.
Say so in the README.

**Generator shape.** `affix = family → mechanic × condition × severity(rank) ×
boon`, with `IsRelevant(player)` as a filter and `rank` replacing the free-standing
severity for mechanics. The condition axis stays and remains the multiplier on
variety; several new conditions are cheap and decision-shaped: *surrounded*
(≥2 attackers), *indoors*, *after a kill* (10 s), *low resource* (<30% primary
power), *against elites* (at the damage site).

---

## 7. What to build first

A four-affix vertical slice proves the thesis in a weekend: **Champions** (engage
counter, reward), **Falling Sky** (scheduler, warning, ground strike), **The
Shade** (owner-bound summon and AI, despawn rules), **Deep Wounds** (max-health
observer). Between them they exercise every primitive the other twenty-four need:
counters, the scheduler, summons, the addon channel, and a max-health modifier.
If those four feel good at level 20, the rest is content.

---

## 8. Sources

Developer statements are preferred; community threads are marked as such in the
text. Several Blizzard and Hopoo statements were reachable only through mirrors
and are cited that way.

1. Raider.io interview with Morgan Day (Dragonflight S3), recap:
   https://wowcarry.com/blog/dragonflight/raiderio-interview-recap-adjusting-mythic-affix-emphasis
   (original: https://www.wowhead.com/news/mythic-will-put-less-emphasis-on-affixes-raider-io-interview-recap-336001)
2. The War Within dungeon and affix updates (blue post, mirrored):
   https://www.mmo-champion.com/content/12362-The-War-Within-Dungeon-and-Affix-Updates
3. Necrotic and Inspiring removed from Dragonflight S1 (Blizzard reasoning):
   https://www.wowhead.com/news/necrotic-and-inspiring-mythic-affixes-currently-missing-from-dragonflight-season-328968
4. Patch 10.1 PTR development notes (Explosive as a "healer problem"; affix
   placement at +7/+14):
   https://www.bluetracker.gg/wow/topic/us-en/1541379-dragonflight-embers-of-neltharion-ptr-development-notes/
5. EU forums, "What mythic+ affixes do you like and not like" *(community)*:
   https://eu.forums.blizzard.com/en/wow/t/what-mythic-affixes-do-you-like-and-not-like/73485
6. Icy Veins interview with Michael Bybee on Xal'atath's Bargain:
   https://www.icy-veins.com/wow/news/exclusive-war-within-interview/
7. Mythic+ affix system updates in The War Within (blue post, mirrored):
   https://www.icy-veins.com/forums/topic/80220-mythic-affix-system-updates-in-the-war-within/
8. US forums, "What is your favorite M+ seasonal affix so far" *(community)*:
   https://us.forums.blizzard.com/en/wow/t/what-is-your-favorite-m-seasonal-affix-so-far/1088152
9. US forums, "Which BfA mythic+ seasonal affix you liked/hated the most"
   *(community)*:
   https://us.forums.blizzard.com/en/wow/t/which-bfa-mythic-seasonal-affix-you-likedhated-the-most/423000
10. The War Within Season 2 Mythic+ updates (Blizzard):
    https://worldofwarcraft.blizzard.com/en-us/news/24174877/the-war-within-season-2-mythic-updates-ahead
11. Blizzard Watch on the Xal'atath affixes (Frenzied cut, Ascendant orbs halved):
    https://blizzardwatch.com/2024/07/09/war-within-mythic-xlatath-affixes/ ;
    "14 orbs at once" feedback thread *(community)*:
    https://eu.forums.blizzard.com/en/wow/t/xal%E2%80%99atath%E2%80%99s-bargain-affix-feedback/520586
12. MMO-Champion, "Your most hated M+ affix combos" *(community)*:
    https://www.mmo-champion.com/threads/2605333-Your-most-Hated-M-Affix-combos
13. Supergiant, Hades Superstar Update patch notes:
    https://www.supergiantgames.com/blog/hades-superstar-update-patch-notes/
14. Supergiant, Hades Welcome to Hell Update patch notes:
    https://www.supergiantgames.com/blog/hades-welcome-to-hell-update-patch-notes/
15. Steam discussion on Pact of Punishment heat value *(community)*:
    https://steamcommunity.com/app/1145360/discussions/0/3194742039148987767
16. Game Rant, Hades Pact of Punishment ranking *(community)*:
    https://gamerant.com/hades-pact-of-punishment-best/
17. Supergiant, Hades II Unseen Update:
    https://www.supergiantgames.com/blog/hades2-unseen-update/
18. TheGamer, Hades II Oath of the Unseen guide:
    https://www.thegamer.com/hades-2-oath-of-the-unseen-and-testaments-complete-guide/
19. Anthony Giovannetti on ascension tuning (Hope in Source):
    https://hopeinsource.com/games/
20. Slay the Spire wiki, Ascension: https://slaythespire.wiki.gg/wiki/Ascension
21. Frostilyte, "More games should handle difficulty like Slay the Spire":
    https://frostilyte.ca/2020/04/16/more-games-should-handle-difficulty-like-slay-the-spire/
22. Steam discussion on A17+ movesets *(community)*:
    https://steamcommunity.com/app/646570/discussions/0/2590022385670206526/
23. Game Developer, "How modern roguelikes are becoming more accessible":
    https://www.gamedeveloper.com/design/how-modern-roguelikes-are-becoming-more-accessible
24. Hopoo Games, Development Thoughts #13 (Artifacts):
    https://store.steampowered.com/news/app/632360/view/2092426528669403662
25. Hopoo Games, Development Thoughts #16 (Eclipse):
    https://devtrackers.gg/risk-of-rain/p/3a6c1b24-hopoo-games-development-thoughts-16
26. Risk of Rain 2 wiki, Eclipse: https://riskofrain2.wiki.gg/wiki/Eclipse
27. Binding of Isaac wiki, Curses: https://bindingofisaacrebirth.wiki.gg/wiki/Curses
28. Steam discussions on curses *(community)*:
    https://steamcommunity.com/app/250900/discussions/0/5673948479046349445/ ;
    https://steamcommunity.com/app/250900/discussions/0/3068614788759140796
29. Steam Workshop, "Curse Counterplay" mod:
    https://steamcommunity.com/sharedfiles/filedetails/?id=3490291607
30. PCGamesN, Joe Piepiora on nightmare dungeon affixes:
    https://www.pcgamesn.com/diablo-4/nightmare-dungeon-affixes
31. Maxroll, Diablo 4 patch 1.1.1 campfire chat recap:
    https://maxroll.gg/d4/news/diablo-4-patch-1-1-1-campfire-chat-recap
32. Dot Esports on the removed D4 affixes:
    https://dotesports.com/diablo/news/3-of-the-most-annoying-nightmare-dungeon-affixes-in-diablo-4-are-finally-getting-removed
33. Vulkk, Diablo 4 update 1.2.0 patch notes:
    https://vulkk.com/2023/10/11/diablo-4-update-1-2-0-season-of-blood-changes-overview-and-patch-notes/
34. PC Gamer, Diablo 3 patch 1.0.4:
    https://www.pcgamer.com/diablo-3-patch-1-0-4-to-make-diablo-3-easier-reduce-repair-costs-improve-items/
35. Maxroll, Diablo 3 elite affixes: https://maxroll.gg/d3/resources/elite-affixes ;
    Blizzard Watch, D3 elite monster affixes:
    https://blizzardwatch.com/2015/07/11/combat-elite-monster-affixes-diablo-3/
36. Maxroll, Path of Exile "How to roll maps":
    https://maxroll.gg/poe/getting-started/how-to-roll-maps
37. Path of Exile forums on map mods *(community)*:
    https://www.pathofexile.com/forum/view-thread/3394128
38. Dead Cells wiki, Boss Stem Cells: https://deadcells.wiki.gg/wiki/Boss_Stem_Cells
39. Dead Cells patch notes 21 (Malaise rework): https://dead-cells.com/patchnotes/21
40. Dead Cells patch notes 12 (Malaise no longer kills): https://dead-cells.com/patchnotes/12

41. CrawlWiki, Ru: http://crawl.chaosforge.org/Ru
42. Stone Soup source, `sacrifice-data.h`:
    https://github.com/crawl/crawl/blob/master/crawl-ref/source/sacrifice-data.h
43. Stone Soup bug 11156 (Sacrifice Stealth mispriced):
    https://crawl.develz.org/mantis/view.php?id=11156
44. CrawlWiki, Trog: http://crawl.chaosforge.org/Trog ; Cheibriados:
    http://crawl.chaosforge.org/Cheibriados
45. NetHack Guidebook 3.6.7 (conducts): https://nethack.org/v367/Guidebook.html ;
    NetHackWiki, Conduct: https://nethackwiki.com/wiki/Conduct
46. NetHackWiki, Pacifist: https://nethackwiki.com/wiki/Pacifist
47. Caves of Qud wiki, Mutations (defect costs and the one-defect cap):
    https://wiki.cavesofqud.com/wiki/Mutations
48. Caves of Qud wiki, Evil Twin: https://wiki.cavesofqud.com/wiki/Evil_Twin ;
    Game Developer on Qud's emergent narrative:
    https://www.gamedeveloper.com/design/tapping-into-the-potential-of-procedural-generation-in-caves-of-qud
49. Steam guide to Qud defects *(community)*:
    https://steamcommunity.com/sharedfiles/filedetails/?id=2996794610
50. Rogue Legacy wiki, Traits: https://roguelegacy.wiki.gg/wiki/Traits
51. PlayStation LifeStyle interview with Kenny Lee (Vertigo):
    https://www.playstationlifestyle.net/2014/08/08/rogue-legacy-interview-souls-inspiration-vertigo-polygon-counts/
52. GoNintendo, Teddy Lee on Rogue Legacy 2's trait gold bonus:
    https://www.gonintendo.com/contents/12288-rogue-legacy-2-dev-explains-the-game-s-change-to-traits
53. Rogue Legacy 2 Trait Compendium *(community)*:
    https://steamcommunity.com/sharedfiles/filedetails/?id=2802998355
54. Destructoid, Vertigo removed from Rogue Legacy 2:
    https://www.destructoid.com/rogue-legacy-2-vertigo-trait-removed-dragon-lancer-redesign-update/
55. Crypt of the NecroDancer wiki, Characters:
    https://necrodancer.miraheze.org/wiki/Characters
56. Kotaku on Coda ("Impossible, Right?"):
    https://kotaku.com/crypt-of-the-necrodancer-speedrunner-makes-near-impossi-1784329931
57. Game Developer, Darkest Dungeon affliction system deep dive (Red Hook):
    https://www.gamedeveloper.com/design/game-design-deep-dive-i-darkest-dungeon-s-i-affliction-system
58. Darkest Dungeon wiki, Affliction: https://darkestdungeon.wiki.gg/wiki/Affliction ;
    Virtue: https://darkestdungeon.wiki.gg/wiki/Virtue
59. Binding of Isaac wiki, Tainted Jacob:
    https://bindingofisaacrebirth.wiki.gg/wiki/Tainted_Jacob ; Tainted Lost:
    https://bindingofisaacrebirth.wiki.gg/wiki/Tainted_Lost ; Keeper:
    https://bindingofisaacrebirth.wiki.gg/wiki/Keeper
60. Blizzard, "Rules of Engagement: Classic Hardcore":
    https://news.blizzard.com/en-us/article/23973734/rules-of-engagement-classic-hardcore-is-coming-to-world-of-warcraft
61. Official first-month hardcore deaths by class (via Wowcarry):
    https://wowcarry.com/blog/wow-classic/a-month-of-hardcore-perils-wow-classic ;
    Icy Veins caveat on popularity:
    https://www.icy-veins.com/forums/topic/75090-classic-hardcore-official-first-month-death-stats-by-class/
62. GameLeap, top killers at 3 million deaths:
    https://www.gameleap.com/articles/wow-hardcore-classic-has-over-3-million-deaths-officially ;
    Warcraft Tavern, top causes of death:
    https://www.warcrafttavern.com/wow-classic/news/the-top-causes-of-death-in-hardcore-wow/
63. Deathlog addon wiki (rate-based class survival):
    https://github.com/aaronma37/Deathlog/wiki
64. Per-class hardcore guides — HC Guides (hunter):
    https://www.hcguides.com/specs/hunter/beast-mastery ; Warcraft Tavern
    hardcore overview:
    https://www.warcrafttavern.com/wow-classic/guides/an-introduction-and-overview-of-hardcore-classic/ ;
    Boosting Ground (priest):
    https://boosting-ground.com/wow-classic/guides/hardcore-guides/hardcore-priest-guide
65. Fextralife, Hades II Oath of the Unseen (vow values):
    https://hades2.wiki.fextralife.com/Oath+of+the+Unseen
