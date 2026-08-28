# mod-gauntlet

A hardcore roguelike challenge module for [AzerothCore](https://www.azerothcore.org/).

One life. Every few levels, a new curse — drawn at random from a generator with
**2,880 possible affixes**, never a fixed list. Two runs are never the same.

## How it works

You start hardcore: **death retires the character permanently.**

Every `TierInterval` levels (5 by default, so levels 5, 10, 15 … 80) you reach a
new **tier** and are offered three randomly generated affixes. You pick one. It
is permanent, and it stacks with everything you already carry. By level 80 you
are wearing sixteen of them.

Affixes are not chosen from a table — they are *generated*:

```
affix = effect × condition × severity × optional boon
         4     ×    15     ×    6     ×      8        = 2,880 combinations
```

- **Effect** — what it does. Four are active: **damage taken**, **damage
  dealt**, **healing received** and **experience gained**. The enum carries a
  wider vocabulary (movement, attack and cast speed, max health, mana,
  regeneration, money, durability, threat) reserved for future work; those are
  excluded from rolls, so **every affix you are offered actually does
  something**.
- **Condition** — *when* it bites, which is where the character comes from.
  `Everlasting` is simple. `Desperate` only applies below half health.
  `Solitary` only when you are alone. `Nocturnal` only at night. `Delving` only
  in dungeons. Rarely-active affixes roll proportionally harsher numbers.
- **Severity** — Trivial through Dire. The floor drifts upward with your tier,
  so the run escalates without ever becoming predictable.
- **Boon** — roughly one affix in three carries an upside, turning the pick into
  a real trade-off rather than a choice of least harm. *Wrathful Desperate
  Brittle* costs you maximum health but pays out damage when you are cornered.

Each affix carries a generated name plus a plain-language description, so
there is no vocabulary to memorise:

```
Rivalrous Exposed
  [Major] You take 21% more damage in battlegrounds and arenas.

Wrathful Desperate Withering
  [Severe] Healing on you is 34% weaker below half health.
           In exchange, you deal 19% more damage.
```

### Run seeds

Every character rolls a **seed**, and affixes derive deterministically from
`(seed, tier, choice)`. The same seed always produces the same offers, so a run
can be reproduced or handed to someone else as a challenge. `.gauntlet status`
shows yours.

### Leaderboard

When a run ends, the character, level, tier and cause of death are recorded.
`.gauntlet top` shows the furthest runs on the server. Death is a score, not
just a loss.

## The addon

`addon/GauntletUI` is an optional client addon. Copy it into
`World of Warcraft/Interface/AddOns/` and affix choices appear as clickable
buttons with descriptions instead of chat text. `/gauntlet` shows your status.

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

Turning `ChoicesPerTier` down to `1` makes the run pure fate rather than
strategy — harsher, and worth trying once.

## Notes and limitations

- Affixes apply to **players only**. Bots are unaffected.
- `Outmatched` (versus elites) needs the target, which ambient stat queries do
  not have, so it is excluded from rolls rather than shipped inert.
- Effects are applied through damage, healing and experience hooks rather than
  auras, so they carry no client-side icon. Your affixes are visible through
  `.gauntlet status`.

## License

AGPL-3.0-or-later, matching AzerothCore.
