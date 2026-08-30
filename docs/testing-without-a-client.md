# Testing without a game client

Most of what this module does needs a `Player`. `tests/run-tests.sh` builds a
deliberately Player-free set, so the unit tests cannot reach `OnAttach`,
`OnDetach`, or any of the hooks — and for ten phases that meant "needs a person
sitting at the game", which in practice meant untested.

It does not. **mod-playerbots puts real `Player` objects in the world with no
client attached**, and the server console can drive commands against them. This
file is how, written down after the first run of it found three bugs.

---

## What makes it work

Four facts, each of which had to be true:

| | |
|---|---|
| **Bots are real Players** | Not stubs. `GetAppliedAuras()`, `GetSpellCooldownMap()`, aggregates, the scheduler — all of it reads the same as a person's character. |
| **Console commands run in the world thread** | `sWorld->QueueCliCommand` executes inside the world update, so a command that captures state three times sees no bot AI between the captures. Nothing interleaves. There is no race to design around. |
| **`Gauntlet.PlayersOnly = 0` makes bots eligible** | `Mgr::IsEligible` is the only thing keeping them out. With it off, a bot logging in gets a `RunState` created like anyone else — which the audit needs somewhere to attach into. |
| **Commands can name their target** | `.gauntlet debug leaks <name> …` takes an `Optional<PlayerIdentifier>`, so the console — which has no player of its own — can still address one. |

## The isolation rules

The realm this module runs on is live and has real characters on it. **Nothing
here touches it.** Everything is new, differently named, and separate:

- a **copy** of the database volume, never the original;
- the client-data volume mounted **`:ro`**, so it cannot be written;
- container names prefixed `gt-`, on their own network — the compose project's
  own `ac-worldserver` and `ac-database` are never started, stopped, or removed;
- no ports published at all, because the console is reached through the
  container's stdin rather than over the network.

## Standing it up

```bash
# 1. An isolated copy of the database. The live one must be stopped, which it
#    is when the realm is down; copying a running MySQL data dir is not safe.
docker volume create gauntlet-test_db
docker run --rm -v azerothcore-wotlk_ac-database:/from:ro \
                -v gauntlet-test_db:/to alpine cp -a /from/. /to/

# 2. Its own database.
docker network create gauntlet-test-net
docker run -d --name gt-db --network gauntlet-test-net \
  -v gauntlet-test_db:/var/lib/mysql -e MYSQL_ROOT_PASSWORD=password mysql:8.4

# 3. A config tree of its own: copy env/dist/etc and set
#      mod_gauntlet.conf   Gauntlet.PlayersOnly = 0     bots become eligible
#                          Gauntlet.Debug.Enable = 1
#      playerbots.conf     AiPlayerbot.MinRandomBots = 60   all ten classes
#                          AiPlayerbot.MaxRandomBots = 60
#                          AiPlayerbot.RandomBotLoginAtStartup = 1
#      worldserver.conf    Console.Enable = 1
#                          Updates.EnableDatabases = 0  the copy is already current
```

The worldserver is started with its stdin on a file that never ends, which is
what makes the console scriptable — commands are appended to the file from
anywhere, and output lands in the log:

```bash
: > cmds.txt
tail -f -n +1 cmds.txt | docker run -i --rm --name gt-world \
  --network gauntlet-test-net \
  -e AC_LOGIN_DATABASE_INFO="gt-db;3306;root;password;acore_auth" \
  -e AC_WORLD_DATABASE_INFO="gt-db;3306;root;password;acore_world" \
  -e AC_CHARACTER_DATABASE_INFO="gt-db;3306;root;password;acore_characters" \
  -e AC_PLAYERBOTS_DATABASE_INFO="gt-db;3306;root;password;acore_playerbots" \
  -v "$PWD/realm/etc":/azerothcore/env/dist/etc \
  -v "$PWD/realm/logs":/azerothcore/env/dist/logs \
  -v /path/to/core/modules:/azerothcore/modules:ro \
  -v azerothcore-wotlk_ac-client-data:/azerothcore/env/dist/data:ro \
  acore/ac-wotlk-worldserver:master > ws.log 2>&1 &

echo '.gauntlet debug cards' >> cmds.txt        # and read ws.log
```

The `modules` mount is not optional and the failure is obscure: mod-playerbots
runs its own database updater at startup, it is **not** gated by
`Updates.EnableDatabases`, and without the SQL source directory it shuts the
worldserver down with `DBUpdater: The given source directory … does not exist`.

## Using it

```bash
# One bot of each class, highest level online.
docker exec gt-db mysql -uroot -ppassword -N -B acore_characters -e \
  "SELECT class, name FROM characters c WHERE online=1 AND guid=(
     SELECT guid FROM characters WHERE online=1 AND class=c.class
     ORDER BY level DESC LIMIT 1) ORDER BY class;"
```

Then, for each name: `.gauntlet debug leaks <name> self` once, then
`.gauntlet debug leaks <name> all 4`, then `.gauntlet debug soak <name> all 4`.

Strip the colours before reading the log, and mind that the count is welded to
its colour code — `|cffff20201 leaked` — so a lazy `s/|cff[0-9a-f]*//g` eats the
digit and reports a blank. Match exactly eight hex digits:

```bash
sed 's/\x1b\[[0-9;]*m//g; s/|c[0-9a-fA-F]\{8\}//g; s/|r//g' ws.log
```

## What it caught, first time out

- `Scheduler::Arm` refuses `MECHANIC_NONE`, so the self-test's own probe armed
  nothing — the self-test failed on itself before it was ever pointed at the
  module.
- The clean/inert split never fired, because the carried-affix count always
  moves while an affix is attached.
- Three class curses cleared cooldowns they had never set. One of them,
  Berserker's Bargain, showed up **only on the first pass** — the run itself
  consumed the real Shield Wall cooldown that made it visible, so the second
  pass came back clean. Run it against a bot that has been fighting, and run it
  more than once.

## Driving the hooks: `soak`

`leaks` attaches and detaches and nothing else, which is why 63 of 69 read inert.
`.gauntlet debug soak` is the same audit with each mechanic made to act first —
forty ticks of its own clock, and up to three of its own scheduled events
released through the same path `.gauntlet debug fire` uses.

It reports how many events it actually released. That number is the point: a
soak that drove nothing and found nothing has proved nothing, and most curses
never arm anything at all. On a typical run it is 3 events across 1 mechanic;
on a character carrying several timed affixes, 12 across 4.

It found Falter leaving its disarm on a player who no longer carried it —
invisible to `leaks`, because `leaks` never lets Falter fire.

Both sweeps skip class curses the character's class cannot be offered. Without
that filter the audit invents bugs: Half-Tamed attached to a warlock dismissed
the demon, took Fel Vitality's mana bonus with it, and reported a permanent max
power drop — all true, none of it reachable in play.

## `bench`: every card, through every hook

`leaks` proves detach puts things back. `soak` adds the card's own clock.
Neither answers the question that matters as the registry grows: **is this card
reachable at all, and by what?**

`.gauntlet debug bench` attaches one card and drives the whole of `Mgr`'s
dispatch surface at it — experience for a quest and for a kill, healing, max
health, the lethal path, loot rolls, the repair bill, talent points, the economy
vetoes, all six aggregate products flat, against a target, and again while
wounded, combat entry, damage taken, pet damage, periodic damage, damaging a
creature, killing one, a pet killing one, leaving combat, a zone change, a group
change, forty ticks in combat and forty out, and its own scheduled events.

**Nothing in it is written per card.** A card added next year is covered the day
its registry row lands. The summary ends with the list of cards *no probe
reached* — that is the number to watch, because each entry is either a card
needing a condition the bench cannot produce or a card that does nothing.

Two readings back the verdict: the player's `Footprint`, and a mark on the
target (health, max health, level, aura count, faction, speed, alive). The second
exists because a whole family never touches the player — Champions promotes a
creature, Craven makes one run, Grudge answers when one dies.

### Four things that silently cost coverage

Each of these made the bench under-report, and each is worth knowing before
writing another probe:

- **The clock must run *during* combat.** Every Spawn and Tempo card arms on
  entering combat and disarms on leaving it. Ticking after `OnLeaveCombat`
  released nothing and reported both families "reached by nothing".
- **The target must be hostile.** `ENTRY_RESTLESS` is the module's visual-only
  creature and the combat manager will not engage with it, so `SetInCombatWith`
  did nothing. Reinforcements' own counters said "out of combat" on a probe that
  had just called it both ways. Use `ENTRY_AMBUSHER`.
- **`Mgr::Tick` suppresses every event** while the player is mounted or has an
  offer pending. Playerbots are usually mounted and a fresh run always has an
  offer. `BenchQuiet` clears both — **once around the sweep, never per card**,
  because dismounting changes the run speed `Footprint` records and doing it
  inside a card's before/after makes every card leak a speed change.
- **Control the character's state, don't inherit it.** Killing Floor heals a
  share of max health on a kill, which is worth nothing on a character already
  at full — so it answered on a bot that happened to be hurt and went silent on
  one that was not. The bench wounds to 15% *before* the kill probes.

## Three measurement artifacts to know about

**A harness that skips a step the live code takes will blame the code.**
`Mgr::Load` and `Mgr::Pick` call `RefreshStats` after `OnAttach`; the audit
helpers did not, and a `MaxHealth` factor does not reach `GetMaxHealth` until the
stat chain reruns. The bench reported Arcane Frailty halving a mage's max health
and Faint leaking the restore — 13850 → 6925 then 6925 → 13850 on consecutive
cards. An inverse pair on adjacent cards is the signature; suspect the harness.

**Auras the core re-applies asynchronously.** Straight after a disarm or silence
is lifted, weapon-dependent passives can read "removed and not restored": the
capture is synchronous and the core puts them back on a later update. A second
pass comes back clean. Do not chase one of these without reproducing it.

**A leak can consume the condition that reveals it.** Berserker's Bargain showed
up on the first pass and not the second, because the run itself cleared the real
Shield Wall cooldown that made it visible. Run against a bot that has been
fighting, and run more than once.

## What it still cannot do

Hooks that need another unit — kills, damage taken, spell casts, combat entry —
are not driven, because handing a mechanic a fabricated enemy tests the
fabrication. That is the remaining gap, and it is most of what
`docs/checklists.md` is still holding.
