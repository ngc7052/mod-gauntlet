# mod-gauntlet

A hardcore roguelike challenge module for [AzerothCore](https://www.azerothcore.org/).

One life. Every few levels, a new affix — drawn from a registry of **71
mechanics across seven families**, never a fixed list. Two runs are never the
same.

## How it works

You start hardcore: **death permanently retires the character.**

Every `TierInterval` levels (5 by default, so levels 5, 10, 15 … 80) you reach
a new **tier** and are offered three affixes generated for that tier. You pick
one. It is permanent, and it stacks with everything you already carry. By
level 80 you are wearing sixteen of them.

An affix is not a stat penalty rolled off a table — it is a **mechanic**: a
change to how an encounter plays out, with a button or a movement that
answers it, not a multiplier bolted onto your character sheet. Only
mechanics with a working implementation are ever offered, so every affix
you're offered actually does something. See *The affix families*, below,
for what the seven families are and how carrying more of them changes a run.

## The affix families

Every mechanic belongs to one of seven families, each pulling on a different
lever:

- **Spawn** — something appears and comes after you: a shade that hunts you
  down, an ambush waiting around the next corner.
- **Enemy** — ordinary enemies behave differently: they hit harder in
  packs, they notice you from farther away, they don't die the way you
  expect.
- **Tempo** — pressure on position and pacing rather than raw numbers: a
  telegraphed strike you have to move out of, a clock you have to beat.
- **Attrition** — a cost with a counterplay button, not a flat tax: take
  more damage below half health, pay health instead of mana for your
  spells.
- **Rules** — a restriction on what you're allowed to do, rather than a
  number: no auction house, no partying up.
- **Bargain** — a curse you choose on purpose, because of what it pays out.
- **Class** — a curse written for one class specifically, leaning on the
  thing that class actually struggles with.

Every mechanic has up to three **ranks**. If an affix you already carry comes
up again in a later offer, you are never offered a duplicate of it — you are
offered its next rank instead, and taking it replaces what you have in that
slot with the stronger version. By the end of a run you typically carry
somewhere around seven to nine distinct mechanics, most of them ranked up,
rather than sixteen unrelated ones.

The game also limits how much of one *kind* of pressure you can carry at
once, so a run stays varied instead of turning into a pile of the same idea:
at most one creature stalking you at a time, at most two on-kill effects, at
most two tempo mechanics, one role-specific tax, one rule, one class curse,
and two bargains. Where a family instead reduces a scalar you already
have — damage taken, damage dealt, healing received, and the like — the cap
is on the *combined* total from every source rather than a count: however
many affixes push your damage taken up, it never crosses
`Gauntlet.Caps.DamageTaken` (double, by default); damage dealt and healing
received have equivalent floors. `.gauntlet status` prints the totals as
they actually stand, ceiling included.

Not every mechanic is offered to every class. Attrition affixes that lean on
a cast bar or a mana pool are never offered to a class without one, class
curses only ever go to the class they're written for, and Rules are relevant
to everyone. An affix that doesn't apply to your class simply never comes
up as an offer.

At tiers 4, 8 and 12, one of your three offers is a **swap**: take it, and
discard an affix you already carry, instead of stacking it on top of
everything else. It's the game's way of letting you undo a pick you've grown
to regret, without a reroll button.

## Determinism

Every character rolls a seed at creation. The three affixes offered at a
given tier are reproducible from `(seed, tier, the affixes you already
carry, your class, and the generator's version)` — the same inputs always
produce the same offer, so a run can be reproduced or handed to someone else
as a challenge. `.gauntlet status` shows yours.

What you actually pick, though, is stored, not regenerated. The mechanic,
rank, condition and boon you end up with are written to your character the
moment you choose them, and read back on every login exactly as written.
This matters: an earlier version of this module derived your affixes from
the seed every time they were needed, which meant that tuning the generator
— even changing a single number — silently rewrote the affixes of every
character already playing. That can no longer happen: the stored values are
the only thing ever read for a character that already exists.

The generator's version number only goes up when something that would
change what a given `(seed, tier, …)` produces actually changes — the
mechanic registry, the family weights, or the offer algorithm itself. A run
started under an older version keeps the columns it already has and is
completely unaffected by the bump; only offers made from that point on use
the new version.

Events — timed or triggered things that happen mid-run, once the scheduler
exists to drive them — are the deliberate exception: they come from
real-time state (how long you've been in a fight, how many kills you've
racked up), not from the seed, and are not meant to be reproducible.
Determinism is a promise about your sixteen *choices*; it was never meant to
cover the clock.

## Leaderboard

When a run ends, the character, level, tier and cause of death are recorded.
`.gauntlet top` shows the furthest runs on the server. Death is a score, not
just a loss.

## The addon

`addon/GauntletUI` is an optional client addon. Copy it into
`World of Warcraft/Interface/AddOns/` and affix choices appear as clickable
buttons with descriptions instead of chat text. `/gauntlet` shows your
status.

The module is fully playable without it — the addon only replaces the chat
prompt with a panel.

## Commands

| Command | Description |
|---|---|
| `.gauntlet pick <n>` | Commit to one of the offered affixes |
| `.gauntlet status` | Your seed, tier and every affix you carry |
| `.gauntlet top` | The ten furthest runs on the server |

## Installation

```bash
cd <azerothcore>/modules
git clone https://github.com/ngc7052/mod-gauntlet.git
cd <azerothcore>/build
cmake .. -DMODULES=static && make -j$(nproc) && make install
```

Then apply `data/sql/db-characters/base/gauntlet.sql` to your **characters**
database, and copy `conf/mod_gauntlet.conf.dist` to `mod_gauntlet.conf` in your
server's `etc/modules` directory.

> The directory must be named `mod-gauntlet`. AzerothCore derives the script
> loader symbol from the folder name.

## Configuration

| Setting | Default | Description |
|---|---|---|
| `Gauntlet.Enable` | `1` | Master switch |
| `Gauntlet.Hardcore` | `1` | Death permanently retires the run |
| `Gauntlet.TierInterval` | `5` | Levels between affix tiers |
| `Gauntlet.ChoicesPerTier` | `3` | Affixes offered per tier; `1` removes choice |
| `Gauntlet.Announce` | `1` | Broadcast picks and deaths server-wide |
| `Gauntlet.PlayersOnly` | `1` | Exclude bots from the challenge |

Turning `ChoicesPerTier` down to `1` makes the run pure fate rather than
strategy — harsher, and worth trying once.

Each of the seven families above has its own switch, all on by default:
`Gauntlet.Family.Spawn.Enable`, `.Enemy.`, `.Tempo.`, `.Attrition.`,
`.Rules.`, `.Bargain.`, `.Class.`. Turning one off removes it from future
offers without touching anything a character already carries.

`Gauntlet.Caps.DamageTaken` (`2.0`), `.DamageDone` (`0.6`), `.HealTaken`
(`0.5`), `.MaxHealth` (`0.6`) and `.EnemySpeed` (`1.4`) are the ceilings and
floors on the combined total described under *The affix families* — they
apply to every source added together, never to a single mechanic.

`Gauntlet.Events.Enable`, `.MinSpacing` (`12`), `.BudgetStep` (`0.25`),
`Gauntlet.Summons.MaxAlive` (`4`), `.XpRate` (`0.5`) and
`Gauntlet.Grace.Seconds` (`60`) configure the event scheduler, summon limits
and post-login grace window. They ship now with their intended defaults, but
nothing reads them yet; they take effect once that part of the module lands.

`Gauntlet.Debug.Enable` (default `0`) compiles in the `.gauntlet debug`
subtree used to test and tune affixes without waiting on a real run. The
commands stay gamemaster-only either way — leave this off on a public
realm.

## Notes and limitations

- Affixes apply to **players only**. Bots are unaffected.
- Mechanics don't run through spell auras — no client patches, no new spell
  visuals — so they carry no native buff icon. `.gauntlet status` always
  shows what you carry; the optional addon additionally shows a custom icon
  per mechanic.
- Withering (healing received) and Forgetful (experience gained), the
  effects from the previous version of this module, are never offered on a
  new pick; a character that already carries one keeps it working exactly
  as before.

## License

AGPL-3.0-or-later, matching AzerothCore.
