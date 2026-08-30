# mod-gauntlet

A hardcore roguelike challenge module for [AzerothCore](https://www.azerothcore.org/).

One life. A new affix every level — drawn from a registry of **69 mechanics
across seven families**, never a fixed list. Two runs are never the same.

An affix is not a stat penalty rolled off a table. It is a **mechanic**: a
change to how an encounter plays out, with a button or a movement that answers
it, rather than a multiplier bolted onto your character sheet. A shade that
hunts you between fights. A strike that lands where you were standing. A
paladin whose Consecration burns twice as hot for half as long.

> **Status.** **All 69 mechanics are live and offerable.** No registry row is
> flagged `MF_NotImplemented`, and the `OFFERABLE` list in
> `tests/RegistryTest.cpp` is what enforces that. Phases 0–4 are complete and
> reported in `docs/`; Phase 5 is the pacing pass — config, tuning and
> measurement — and `docs/phase-5-progress.md` tracks it.
>
> What is *not* done is the playtesting. Every mechanic compiles, links and
> passes its unit tests, and almost none has been seen working on a screen.
> `docs/checklists.md` is the list, in priority order.

## How it works

You start hardcore: **death permanently retires the character.**

Every `TierInterval` levels — **1 by default, so every single level** — you
reach a new **tier** and are offered three affixes generated for it. You pick
one. It is permanent, and it stacks with everything you already carry.

Eighty offers over a full run is more than any character should wear at once,
so a run carries at most `Gauntlet.MaxAffixes` (16) affixes. Reaching that
number does not stop the choosing: a full set is still offered rank-ups and
swaps, so the late run becomes *deepen what you have, or trade something away*
rather than *collect one more*.

Only mechanics with a working implementation are ever offered. A registry row
flagged `MF_NotImplemented` is invisible to the generator, so every affix you
are offered actually does something.

## The affix families

Every mechanic belongs to one of seven families, each pulling on a different
lever:

| Family | The lever | Live |
|---|---|---|
| **Spawn** | Something appears and comes after you: a shade that hunts you down, an ambush waiting around the next corner. | 5 / 5 |
| **Enemy** | Ordinary enemies behave differently: they hit harder in packs, they notice you from farther away, they don't die the way you expect. | 8 / 8 |
| **Tempo** | Pressure on position and pacing rather than raw numbers: a telegraphed strike you have to move out of, a clock you have to beat. | 5 / 5 |
| **Attrition** | A cost with a counterplay button, not a flat tax: a wound only rest heals, health spent where mana should have been, healing that comes from killing rather than resting. | 3 / 3 |
| **Rules** | A restriction on what you're allowed to do rather than a number: no auction house, no partying up. | 3 / 3 |
| **Bargain** | A curse you choose on purpose, because of what it pays out. | 2 / 2 |
| **Class** | A curse written for one class specifically, leaning on the thing that class actually struggles with. | 43 / 43 |

Every mechanic has up to four **ranks**. If an affix you already carry comes
up again in a later offer, you are never offered a duplicate — you are offered
its next rank instead, and taking it replaces what you hold in that slot with
the stronger version. Eight mechanics stop at three, because their third rank
already ends the ladder — Vanish cannot be denied harder than never — and a
rank-up that changes nothing is worse than no rank-up. Simulated over 240,000 offer sets, a run reaches the
sixteen-affix cap around level 49 and spends the rest of the climb deepening
and trading rather than collecting.

The generator also limits how much of one *kind* of pressure a run can carry,
so it stays varied instead of turning into a pile of the same idea: at most one
creature stalking you at a time, at most two on-kill effects, at most two tempo
mechanics, one role-specific tax, one rule, three class curses, and two
bargains.

Where affixes instead move a scalar you already have — damage taken, damage
dealt, healing received, maximum health, enemy speed — the cap is on the
**combined** total from every source rather than on a count. However many
affixes push your damage taken up, it never crosses `Gauntlet.Caps.DamageTaken`
(double, by default); the others have equivalent floors and ceilings.
`.gauntlet status` prints the totals as they actually stand, ceiling included.

Not every mechanic is offered to every class. Attrition affixes that lean on a
cast bar or a mana pool are never offered to a class without one; class curses
only ever go to the class they are written for, and three of them further
require a specific talent tree; Rules are relevant to everyone. An affix that
doesn't apply to you simply never comes up.

Families also open at different points in a run. Rules are early and one-rank —
Iron Purse is gone by tier 15, Self-found by 20, Lone Wolf by 30 — while the
whole Bargain family stays shut until tier 30, whatever an individual card's
own window says.

At tiers 20, 40 and 60, one of your three offers is a **swap**: take it, and
discard an affix you already carry instead of stacking it on top of everything
else. It is the game's way of letting you undo a pick you have grown to regret,
without a reroll button.

## What is implemented

All sixty-nine rows. A row and its implementation are switched on in the same
commit, so the table has never promised a curse the module could not deliver.

**Spawn** — The Shade, Echo, Carrion, Reinforcements, Ambush
**Enemy** — Champions, Craven, Call to Arms, Death Rattle, Grudge, Nimble, Cunning, Keen-nosed
**Tempo** — Falling Sky, Frenzy, Overextended, Falter, Hubris
**Attrition** — Deep Wounds, Blood Magic, Killing Floor
**Rules** — Self-found, Lone Wolf, Iron Purse
**Bargain** — Last Rites, Cursed Hoard
**Class** — four each for warrior, paladin, hunter, rogue, priest, death
knight, shaman, mage, warlock and druid; Faint for every mana user; and two
class bargains, Ankh Pact for shamans and Stone of the Damned for warlocks.

A handful are honestly narrower than the card that describes them, always
because the core has no seam for the missing half, and in every case the
mechanic's own blurb describes what it does rather than what the card wished
for. They are listed in `docs/checklists.md` §10 so a tester does not file one
as a bug.

### Every affix

Generated from the registry — see `tests/tools/README-affix-table.md` — because
a table that disagrees with the code is worse than no table.

**"What it does to you" is the card's own line at rank I.** Every mechanic has
up to four ranks and the numbers move with them, so a character carrying rank IV
of Falling Sky is on twelve seconds where the table says twenty-five. In game
the panel shows the sentence written at the rank you actually hold; this is the
design, not a readout of your run.

**"Levels" is the window it can be offered in**, and one tier is one level.
**"Who"** is class relevance: an affix is never offered to a character it does
not apply to. Some rows additionally need a specific spell or talent tree, which
the table does not show — `.gauntlet debug give-class` reports those live.

<!-- AFFIX-TABLE-BEGIN -->
| # | Affix | Family | Who | Levels | Ranks | What it does to you | What it pays |
|---|---|---|---|---|---|---|---|
| 1 | **The Shade** | Spawn | any | 20&ndash;80 | 4 | A Shade rises behind you every few minutes and hunts you until you kill it or leave it behind. | +15&ndash;60% experience |
| 2 | **Echo** | Spawn | any | 30&ndash;80 | 4 | Every 25th enemy you kill returns as an echo of yourself. | +15&ndash;60% experience |
| 3 | **Carrion** | Spawn | any | 1&ndash;50 | 4 | Every 4th corpse you loot draws scavengers. | +25&ndash;100% gold |
| 4 | **Reinforcements** | Spawn | any | 25&ndash;80 | 4 | Fights longer than 30 seconds draw another enemy every 15 seconds. | +8&ndash;32% damage dealt |
| 5 | **Ambush** | Spawn | any | 4&ndash;45 | 4 | Resting in the wild attracts an ambush. | +5&ndash;20% maximum health |
| 6 | **Champions** | Enemy | any | 1&ndash;80 | 4 | Every 8th fight you start opens against a Champion: twice the health, harder hits, double the reward. | +15&ndash;60% experience |
| 7 | **Craven** | Enemy | any | 12&ndash;60 | 4 | Enemies flee at 25% health, and come back with friends. | +8&ndash;32% damage dealt |
| 8 | **Call to Arms** | Enemy | any | 25&ndash;65 | 4 | Killing an enemy alerts its nearest kin. | +15&ndash;60% experience |
| 9 | **Death Rattle** | Enemy | melee | 20&ndash;60 | 4 | Corpses burst two seconds after death, hurting anyone within five yards. | +25&ndash;100% gold |
| 10 | **Grudge** | Enemy | melee | 8&ndash;50 | 4 | Everything you kill leaves a ghost on its corpse that drains your health while you stand near it. | +10&ndash;40% healing received |
| 11 | **Nimble** | Enemy | any | 30&ndash;80 | 3 | Enemies move 30% faster. | +5&ndash;15% maximum health |
| 12 | **Cunning** | Enemy | Paladin, Hunter, Priest, Shaman, Mage, Warlock, Druid | 30&ndash;80 | 4 | Enemies in melee range kick the spell you are casting, once every 12 seconds each. | +8&ndash;32% damage dealt |
| 13 | **Keen-nosed** | Enemy | any | 4&ndash;55 | 4 | Enemies notice you from further away. | +25&ndash;100% gold |
| 14 | **Falling Sky** | Tempo | any | 25&ndash;80 | 4 | In combat, every 20 seconds the sky marks your spot; three seconds later it strikes. | +5&ndash;20% move speed |
| 15 | **Frenzy** | Tempo | any | 8&ndash;80 | 4 | Each kill within 8 seconds of the last stacks Frenzy: +6% damage dealt and +6% damage taken per stack. | +4&ndash;10% damage dealt |
| 16 | **Overextended** | Tempo | any | 1&ndash;60 | 4 | Each enemy attacking you beyond the first increases the damage you take by 20%. | +10&ndash;40% healing received |
| 17 | **Falter** | Tempo | any | 25&ndash;65 | 4 | Every 45 seconds in combat your hands fail you for three seconds. | +5&ndash;20% maximum health |
| 18 | **Hubris** | Tempo | any | 1&ndash;50 | 4 | Enemies below your level give no experience; enemies above give 40% more. | +20&ndash;50% experience |
| 19 | **Deep Wounds** | Attrition | any | 10&ndash;60 | 4 | A third of the damage you take becomes a wound that only rest can heal. | &mdash; |
| 20 | **Blood Magic** | Attrition | Paladin, Hunter, Priest, Shaman, Mage, Warlock, Druid | 25&ndash;60 | 4 | Spells cost 3% of your maximum health in addition to mana. | +8&ndash;32% damage dealt |
| 23 | **Self-found** | Rules | any | 1&ndash;20 | 1 | You cannot trade, mail, or use the auction house. | +25% gold |
| 24 | **Lone Wolf** | Rules | any | 1&ndash;30 | 1 | Half health in a group; more experience alone. | +15% experience |
| 25 | **Iron Purse** | Rules | any | 1&ndash;15 | 1 | Repairs cost double. | &mdash; |
| 26 | **Last Rites** | Bargain | any | 40&ndash;80 | 4 | A hit that would kill you leaves you at 1 health instead, once per level. | a second life |
| 27 | **Cursed Hoard** | Bargain | any | 30&ndash;80 | 4 | Chests give twice as much loot, but opening one makes you take triple damage until you kill three enemies. | +25&ndash;100% gold |
| 28 | **Red Mist** | Class | Warrior | 15&ndash;80 | 4 | At 100 rage you lose your mind for three seconds and your rage empties. | +15&ndash;60% resource regeneration |
| 29 | **Berserker's Bargain** | Class | Warrior | 25&ndash;80 | 4 | Below 35% health you deal 25% more damage, but your panic buttons will not answer. | +25% damage dealt |
| 30 | **Iron Discipline** | Class | Warrior | 20&ndash;80 | 4 | Changing stance has a ten-second cooldown. | +15&ndash;60% resource regeneration |
| 31 | **Deafening Roar** | Class | Warrior | 20&ndash;80 | 4 | Your shouts wake every enemy within thirty yards. | a bespoke buff to the ability it names |
| 32 | **Long Forbearance** | Class | Paladin | 15&ndash;80 | 4 | Forbearance lasts three minutes, and Divine Shield empties your mana. | +10% a bespoke buff to the ability it names |
| 33 | **Consecrated Ground** | Class | Paladin | 25&ndash;80 | 4 | You take 25% more damage while not standing in your own Consecration. | a bespoke buff to the ability it names |
| 34 | **No Sanctuary** | Class | Paladin | 15&ndash;60 | 3 | Your Hearthstone will not answer under Divine Shield. | a shorter cooldown on the ability it names |
| 35 | **Commitment** | Class | Paladin | 20&ndash;80 | 4 | Hammer of Justice roots you for its duration. | a shorter cooldown on the ability it names |
| 36 | **Half-Tamed** | Class | Hunter | 15&ndash;80 | 4 | An unhappy pet turns on you. | your pet's damage |
| 37 | **Dead Weight** | Class | Hunter | 20&ndash;80 | 3 | Feign Death has a three-minute cooldown. | a shorter cooldown on the ability it names |
| 38 | **Wide Dead Zone** | Class | Hunter | 20&ndash;80 | 4 | Ranged attacks cannot be used within ten yards. | +8&ndash;32% damage dealt |
| 39 | **Blood Bond** | Class | Hunter | 25&ndash;80 | 4 | A fifth of the damage your pet takes is dealt to you. | +10&ndash;40% healing received |
| 40 | **Cold Trail** | Class | Rogue | 20&ndash;80 | 3 | Vanish has a ten-minute cooldown. | a shorter cooldown on the ability it names |
| 41 | **Poisoned Blades** | Class | Rogue | 20&ndash;80 | 4 | A quarter of the poison damage you deal ticks on you as well. | +8&ndash;32% damage dealt |
| 42 | **Exposed Back** | Class | Rogue | 15&ndash;80 | 4 | Attacks from behind you deal 50% more damage. | a chance to avoid a blow outright |
| 43 | **Slow Hands** | Class | Rogue | 20&ndash;80 | 3 | Energy does not regenerate while you move in combat. | +15&ndash;45% resource regeneration |
| 44 | **Frail Soul** | Class | Priest | 15&ndash;80 | 4 | Weakened Soul lasts 30 seconds. | +10&ndash;40% healing received |
| 45 | **Faithless Form** | Class | Priest | 30&ndash;80 | 4 | Leaving Shadowform has a thirty-second cooldown. | +8&ndash;32% damage dealt |
| 46 | **Penance of Silence** | Class | Priest | 20&ndash;80 | 4 | Healing yourself silences you for two seconds. | +10&ndash;40% healing received |
| 47 | **Whispers of the Deep** | Class | Priest | 25&ndash;80 | 4 | Below 20% health you lose your mind and flee for three seconds, once per fight. | a shorter cooldown on the ability it names |
| 48 | **Rune-starved** | Class | Death Knight | 60&ndash;80 | 4 | While all six runes are on cooldown you take 30% more damage. | +15&ndash;60% resource regeneration |
| 49 | **Grave Call** | Class | Death Knight | 60&ndash;80 | 4 | The dead you do not claim rise against you. | a shorter cooldown on the ability it names |
| 50 | **Cold Presence** | Class | Death Knight | 60&ndash;80 | 4 | Changing presence costs all your runic power and has a ten-second cooldown. | a bespoke buff to the ability it names |
| 51 | **One Ward** | Class | Death Knight | 60&ndash;80 | 4 | Anti-Magic Shell and Icebound Fortitude share a cooldown. | a bespoke buff to the ability it names |
| 52 | **One Totem** | Class | Shaman | 15&ndash;80 | 4 | Only one totem may stand at a time. | a bespoke buff to the ability it names |
| 53 | **Totemic Anchor** | Class | Shaman | 20&ndash;80 | 4 | You take 30% more damage when more than fifteen yards from your totems. | a bespoke buff to the ability it names |
| 54 | **Elemental Overload** | Class | Shaman | 20&ndash;80 | 4 | Casting the same spell twice in a row costs double. | +8&ndash;32% damage dealt |
| 55 | **Spirit Debt** | Class | Shaman | 25&ndash;80 | 4 | Every hit consumes a shield charge, and each consumed charge costs you 2% health. | a bespoke buff to the ability it names |
| 56 | **Cold Feet** | Class | Mage | 15&ndash;80 | 3 | Blink costs 15% of your maximum health. | a shorter cooldown on the ability it names |
| 57 | **Fickle Sheep** | Class | Mage | 20&ndash;80 | 4 | Polymorph breaks after five seconds, and the sheep comes back angry. | a bespoke buff to the ability it names |
| 58 | **Mana Burn** | Class | Mage | 20&ndash;80 | 3 | Half the damage you take also burns your mana. | +8&ndash;24% damage dealt |
| 59 | **Arcane Frailty** | Class | Mage | 30&ndash;80 | 4 | Thirty percent less health, thirty percent more spell damage. | +8&ndash;32% damage dealt |
| 60 | **Fel Pact** | Class | Warlock | 20&ndash;80 | 4 | Your demon's binding frays with every kill it makes, and after twenty it turns on you. | your pet's damage |
| 61 | **Affliction of the Self** | Class | Warlock | 20&ndash;80 | 4 | Your curses and corruption afflict you too, at a fifth of their strength. | +8&ndash;32% damage dealt |
| 62 | **Shard Economy** | Class | Warlock | 20&ndash;80 | 4 | Every summon and every Healthstone costs a Soul Shard, and shards drop only from your level up. | +15&ndash;60% resource regeneration |
| 63 | **Shared Blood** | Class | Warlock | 25&ndash;80 | 4 | While your demon lives you take 25% more damage, and it deals 40% more. | your pet's damage |
| 64 | **Bound Skin** | Class | Druid | 15&ndash;80 | 4 | Shapeshifting has a six-second cooldown. | +5&ndash;20% maximum health |
| 65 | **Nature's Toll** | Class | Druid | 20&ndash;80 | 4 | Every kill made as a beast leaves you bleeding until you calm. | +8&ndash;32% damage dealt |
| 66 | **Commitment of Roots** | Class | Druid | 15&ndash;60 | 1 | Entangling Roots holds you as long as it holds them. | +15% resource regeneration |
| 67 | **Two Faces** | Class | Druid | 15&ndash;60 | 4 | By day your spells are weaker; by night your claws are. | +8&ndash;32% damage dealt |
| 68 | **Faint** | Class | Paladin, Hunter, Priest, Shaman, Mage, Warlock, Druid | 15&ndash;80 | 4 | When your mana hits zero in combat you black out for two seconds. | +15&ndash;60% resource regeneration |
| 70 | **Ankh Pact** | Class | Shaman | 40&ndash;80 | 1 | Reincarnation works once in this run, and when it does every boon you carry is burned away. | a second life |
| 71 | **Stone of the Damned** | Class | Warlock | 40&ndash;80 | 1 | A Soulstone will bring you back once, and whoever kills you will be waiting. | a second life |
| 74 | **Killing Floor** | Attrition | any | 10&ndash;80 | 4 | Nothing heals you while something you have wounded still lives. Every kill gives health back instead. | &mdash; |
<!-- AFFIX-TABLE-END -->

## Boons

Every implemented mechanic pays for itself. A curse names an upside and the
mechanic behind it delivers that upside — there is no generically rolled boon
anywhere in the module, and no aggregate that pays one on a mechanic's behalf.
The thirteen kinds are damage, healing, move speed, experience, money, maximum
health and resource regeneration, plus five the redesign added for cards the
first seven could not express: avoidance, a shorter cooldown on one named
ability, a bespoke buff to one ability, pet damage, and a second life.

Where a boon is bespoke — *Consecration doubled and halved*, *Polymorph is
instant* — the mechanic's own `Describe()` says what it actually does, because
the registry blurb describes the curse and has nowhere to put the gift.

## Determinism

Every character rolls a seed at creation. The three affixes offered at a given
tier are reproducible from `(seed, tier, the affixes you already carry, your
class, the realm's family switches and carry cap, and the generator's version)`
— the same inputs always produce the same offer, so a run can be reproduced or
handed to someone else as a challenge. `.gauntlet status` shows yours. Two
realms configured differently will produce different runs from the same seed,
and each of them reproducibly.

What you actually pick, though, is stored, not regenerated. The mechanic, rank,
condition and boon you end up with are written to your character the moment you
choose them, and read back on every login exactly as written. This matters: an
earlier version of this module derived your affixes from the seed every time
they were needed, which meant that tuning the generator — even changing a
single number — silently rewrote the affixes of every character already
playing. That can no longer happen: the stored values are the only thing ever
read for a character that already exists.

The generator's version number only goes up when something that would change
what a given `(seed, tier, …)` produces actually changes — the mechanic
registry, the family weights, or the offer algorithm itself. A run started
under an older version keeps the columns it already has and is completely
unaffected by the bump; only offers made from that point on use the new
version.

Events — the timed and triggered things that happen mid-run — are the
deliberate exception. They come from real-time state (how long you have been in
a fight, how many kills you have racked up), not from the seed, and are not
meant to be reproducible. Determinism is a promise about your *choices*; it was
never meant to cover the clock.

## The event scheduler

Timed and triggered mechanics do not each run their own timer. A per-player
scheduler owns them all, which is what keeps a late run playable:

- **Spacing.** No two events for the same player may land closer together than
  `Gauntlet.Events.MinSpacing` seconds, so unrelated timers cannot fire in the
  same tick. An event that has been waiting keeps its place in line rather than
  being starved by whatever was armed after it.
- **Budget.** The effective interval between events stretches by
  `Gauntlet.Events.BudgetStep` for every timed affix beyond the first, so event
  pressure rises with tier instead of piling up.
- **Grace.** Nothing fires for `Gauntlet.Grace.Seconds` after a login or a zone
  change, so a character is never ambushed before the player has taken control.
- **Summon cap.** At most `Gauntlet.Summons.MaxAlive` affix-spawned creatures
  are alive for one player across every spawn mechanic combined, and a kill on
  one of them is worth `Gauntlet.Summons.XpRate` of normal experience so they
  cannot be farmed.

## Leaderboard

When a run ends, the character, level, tier, cause of death and **conducts** are
recorded. Conducts are the class curses the run was carrying when it ended —
the run's epitaph, and the reason a place on the list means more than a number.

`.gauntlet top` shows the ten furthest runs, with each run's conducts under it.
With the addon installed, `/gauntlet top` opens a panel instead and puts the
conducts in a tooltip, where a list that long can actually be read. Death is a
score, not just a loss.

## The addon

`addon/GauntletUI` is an optional client addon. Copy it into
`World of Warcraft/Interface/AddOns/` and it gives you:

- **A chooser** — affix offers as clickable buttons with full descriptions,
  instead of chat text.
- **A HUD** — live state for the mechanics that have any: Frenzy's stacks, Deep
  Wounds' wound, Champions' fight counter, Ambush's countdown, the stalker
  light. None of these are visible in the default UI, because no mechanic in
  this module applies an aura and so none carries a native buff icon. The HUD
  is their buff frame. It is movable, and hides itself when there is nothing to
  report.
- **An affix browser and minimap button** — everything you carry, with a
  per-mechanic icon and description.

`/gauntlet` opens the panel (`top`, `pick` and `config` are subcommands);
`/gauntlethud` shows the HUD, and `/gauntlethud reset` recentres it.

The addon's affix table is generated from the same registry the server reads,
via `.gauntlet debug export-addon`, so its names and descriptions cannot drift
from the server's.

The module is fully playable without the addon — it falls back to chat prompts
— but several mechanics are much harder to read that way, and each one says so
in its own description.

## Commands

| Command | Description |
|---|---|
| `.gauntlet pick <n>` | Commit to one of the offered affixes |
| `.gauntlet status` | Your seed, tier, aggregate totals and every affix you carry |
| `.gauntlet top` | The ten furthest runs on the server |

With `Gauntlet.Debug.Enable = 1`, a gamemaster-only `.gauntlet debug` subtree is
available for testing and tuning affixes without waiting on a real run:
`give`, `give-class`, `remove`, `rank`, `dump`, `offers`, `seed`, `fire`, `set`,
`events`, `hurt` and `export-addon`. All three audits take an optional character
name, so they can be run from the server console against anyone online. The commands are restricted to gamemasters
whether the setting is on or off; the setting decides whether they answer at all.

Two of them are audits rather than cheats, and both are worth running after any
change to the registry or to a mechanic:

| Command | What it checks |
|---|---|
| `.gauntlet debug cards` | Every affix's offer text at every rank: ranks that read identically, and words too long for the addon's wire protocol to split. Needs no character. |
| `.gauntlet debug leaks` | Attaches every affix, detaches it, and reports anything the character did not get back — a held cooldown, a leftover aura, an orphaned summon, a bent multiplier. Run `.gauntlet debug leaks self` first: it checks the audit can see the character at all. |
| `.gauntlet debug soak` | The same audit with each mechanic driven in between — ticked, and its own scheduled events released. Slower and noisier, and the only one of the two that can catch a hook-driven curse leaving something behind. It reports how many events it actually released, because a clean soak that drove nothing is not a result. |

## Installation

```bash
cd <azerothcore>/modules
git clone https://github.com/ngc7052/mod-gauntlet.git
cd <azerothcore>/build
cmake .. -DMODULES=static && make -j$(nproc) && make install
```

Then:

1. Apply `data/sql/db-characters/base/gauntlet.sql` to your **characters**
   database, followed by everything in `data/sql/db-characters/updates/`.
2. Apply `data/sql/db-world/base/gauntlet_creatures.sql` to your **world**
   database — the spawn family needs its creature templates.
3. Copy `conf/mod_gauntlet.conf.dist` to `mod_gauntlet.conf` in your server's
   `etc/modules` directory.

> The directory must be named `mod-gauntlet`. AzerothCore derives the script
> loader symbol from the folder name.

## Configuration

| Setting | Default | Description |
|---|---|---|
| `Gauntlet.Enable` | `1` | Master switch |
| `Gauntlet.Hardcore` | `1` | Death permanently retires the run |
| `Gauntlet.TierInterval` | `1` | Levels between affix tiers |
| `Gauntlet.ChoicesPerTier` | `3` | Affixes offered per tier; `1` removes choice |
| `Gauntlet.MaxAffixes` | `16` | Most affixes a run may carry at once |
| `Gauntlet.Announce` | `1` | Broadcast picks and deaths server-wide |
| `Gauntlet.PlayersOnly` | `1` | Exclude bots from the challenge |

Turning `ChoicesPerTier` down to `1` makes the run pure fate rather than
strategy — harsher, and worth trying once.

Raising `TierInterval` does not break anything — an affix still unlocks at the
same tier, just reached later — but it thins the run out. At `5` a character
sees sixteen offers over eighty levels and most of the table never comes up.

Each of the seven families has its own switch, all on by default:
`Gauntlet.Family.Spawn.Enable`, `.Enemy.`, `.Tempo.`, `.Attrition.`, `.Rules.`,
`.Bargain.`, `.Class.`. Turning one off removes it from future offers without
touching anything a character already carries.

### Aggregate caps

These are the ceilings and floors on the *combined* total described under *The
affix families*. Each applies to every contributing source multiplied together,
never to a single mechanic.

| Setting | Default | Applies to |
|---|---|---|
| `Gauntlet.Caps.DamageTaken` | `2.0` | Ceiling on damage taken |
| `Gauntlet.Caps.DamageDone` | `0.6` | Floor on damage dealt |
| `Gauntlet.Caps.HealTaken` | `0.5` | Floor on healing received |
| `Gauntlet.Caps.MaxHealth` | `0.6` | Floor on maximum health |
| `Gauntlet.Caps.EnemySpeed` | `1.4` | Ceiling on affected creature run speed |

### Scheduler, summons and grace

| Setting | Default | Description |
|---|---|---|
| `Gauntlet.Events.Enable` | `1` | Master switch for the event scheduler |
| `Gauntlet.Events.MinSpacing` | `12` | Minimum seconds between two events for one player |
| `Gauntlet.Events.BudgetStep` | `0.25` | Interval stretch per timed affix beyond the first |
| `Gauntlet.Grace.Seconds` | `60` | Post-login / zone-in window in which nothing fires |
| `Gauntlet.Summons.MaxAlive` | `4` | Affix-spawned creatures alive per player, all mechanics combined |
| `Gauntlet.Summons.XpRate` | `0.5` | Experience multiplier for kills on affix-spawned creatures |

### Per-mechanic

| Setting | Default | Description |
|---|---|---|
| `Gauntlet.Bargain.CursedHoard.EscapeSeconds` | `10` | Seconds out of combat that lift Cursed Hoard's curse, in addition to the kills its card asks for. `0` restores the card exactly: three kills or nothing. |
| `Gauntlet.Debug.Enable` | `0` | Enable the gamemaster `.gauntlet debug` subtree. Leave off on a public realm. |

## Development

```sh
tests/syntax-check.sh    # g++ -fsyntax-only over every core-header-free source
tests/run-tests.sh       # googletest: registry, generator, aggregate, state, scheduler
tests/compile-check.sh   # compile every translation unit against the real core, in seconds
```

`tests/compile-check.sh --anchors` runs the two source audits alone, with no
Docker at all and in a twentieth of a second. The **anchor** audit is the one
that catches *the mechanic is offered and does nothing*; the **ladder** audit
checks that every rank table moves in one direction, because the compiler checks
a table's length and nothing checked its values — and a transposed digit ships a
rank IV weaker than its rank III. A table that changes direction on purpose says
so with `LADDER-SENTINEL`; three do, all of them "0 means the button is gone".

`compile-check.sh` is the one that matters for a new mechanic. It keeps a
long-lived container built from the core's own build stage, bind-mounts the
repository into it and drives the existing ninja, so a single file recompiles
in under a second instead of a full Docker build. `tests/compile-check.sh
--anchors` runs the anchor audit alone, with no Docker at all — that is the
check that catches *the mechanic is offered and does nothing*, which is the
failure mode this codebase produces most easily.

`docs/` carries the design and the record: `affix-design.md` is the full card
set, `implementation-plan.md` the phasing, `checklists.md` what still has to be
tried in-game, and `phase-0-report.md` onward what each phase actually found —
including the bugs, the wrong answers tried first, and the numbers measured on
a live realm.

Two generators keep documentation from drifting from the code:
`tests/tools/export_addon_standalone.cpp` writes `addon/GauntletUI/Data.lua`, and
`tests/tools/affix_table_standalone.cpp` writes the affix table above — both
from the registry, both with a README of their own in `tests/tools/`.

`tests/tools/sweep_standalone.cpp` is the tuning tool. It simulates runs and
reports, per tier, how often the offer builder had to relax a rule, how many
slots came back empty and how often a tier had no reward-shaped offer — 240,000
offer sets in about a second, with every bound on the command line. Every
argument about the shape of the run has been settled with it;
`tests/tools/README-sweep.md` has the recipes, including how to measure the
compile-time knobs.

## Notes and limitations

- Affixes apply to **players only**. Bots are unaffected.
- Mechanics don't run through spell auras — no client patches, no new spell
  visuals — so they carry no native buff icon. `.gauntlet status` always shows
  what you carry, and the addon's HUD shows live state for the mechanics that
  have any.
- Where a mechanic extends or shortens an existing aura, the client's tooltip
  still quotes the DBC duration rather than the real one. Every such mechanic
  says the real number in its own description.
- Class curses key on 3.3.5a base spell ids normalised through
  `GetFirstSpellInChain`. 3.3.5 spells live in DBC rather than SQL, so most
  cannot be verified outside the game; the failure mode is benign and visible
  — the curse simply stops reacting to that one spell.
- The four flat coefficients the module started life as — Exposed (damage
  taken), Feeble (damage dealt), Withering (healing received) and Forgetful
  (experience gained) — no longer exist. Deep Wounds replaces Withering and
  Hubris replaces Forgetful, both with counterplay the originals did not have.
  Their registry ids are permanently retired rather than reused, so a stored
  affix row from any past run still resolves to the mechanic it named.

## License

AGPL-3.0-or-later, matching AzerothCore.
