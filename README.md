# mod-gauntlet

A hardcore roguelike challenge module for [AzerothCore](https://www.azerothcore.org/).

One life. Every few levels, a new curse — drawn at random from a generator with
**over 10,000 possible affixes**, never a fixed list. Two runs are never the same.

## How it works

You start hardcore: **death retires the character permanently.**

Every `TierInterval` levels (5 by default, so levels 5, 10, 15 … 80) you reach a
new **tier** and are offered three randomly generated affixes. You pick one. It
is permanent, and it stacks with everything you already carry. By level 80 you
are wearing sixteen of them.

Affixes are not chosen from a table — they are *generated*:

```
affix = effect × condition × severity × optional boon
        14     ×    16     ×    6     ×      8        = 10,752 combinations
```

- **Effect** — what it does: max health, damage taken, damage dealt, healing
  received, movement and attack speed, mana, regeneration, experience, money,
  durability, threat.
- **Condition** — *when* it bites, which is where the character comes from.
  `Everlasting` is simple. `Desperate` only applies below half health.
  `Solitary` only when you are alone. `Nocturnal` only at night. `Delving` only
  in dungeons. Rarely-active affixes roll proportionally harsher numbers.
- **Severity** — Trivial through Dire. The floor drifts upward with your tier,
  so the run escalates without ever becoming predictable.
- **Boon** — roughly one affix in three carries an upside, turning the pick into
  a real trade-off rather than a choice of least harm. *Wrathful Desperate
  Brittle* costs you maximum health but pays out damage when you are cornered.

Names are generated from the roll, so what you are carrying is legible at a
glance: `Solitary Withering (Major)`, `Nocturnal Leaden (Minor)`,
`Avaricious Embattled Exposed (Severe)`.

### Run seeds

Every character rolls a **seed**, and affixes derive deterministically from
`(seed, tier, choice)`. The same seed always produces the same offers, so a run
can be reproduced or handed to someone else as a challenge. `.gauntlet status`
shows yours.

### Leaderboard

When a run ends, the character, level, tier and cause of death are recorded.
`.gauntlet top` shows the furthest runs on the server. Death is a score, not
just a loss.

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

Turning `ChoicesPerTier` down to `1` makes the run pure fate rather than
strategy — harsher, and worth trying once.

## Notes and limitations

- Affixes apply to **players only**. Bots are unaffected.
- `Outmatched` (versus elites) is evaluated where the target is known and is
  treated as inactive for ambient stat queries.
- Effects are applied through damage, healing and experience hooks rather than
  auras, so they carry no client-side icon. Your affixes are visible through
  `.gauntlet status`.

## License

AGPL-3.0-or-later, matching AzerothCore.
