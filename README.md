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
`give`, `remove`, `rank`, `dump`, `offers`, `seed`, `fire`, `set`, `events`,
`hurt` and `export-addon`. The commands are restricted to gamemasters whether
the setting is on or off; the setting decides whether they answer at all.

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
