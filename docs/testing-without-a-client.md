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

Then, for each name: `.gauntlet debug leaks <name> self` once, and
`.gauntlet debug leaks <name> all 4`.

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

## What it still cannot do

The audit attaches and detaches. It never makes the player cast, kill, get hit,
or change zone, so a curse whose whole behaviour is on a hook reports **inert** —
63 of 69 on a typical character. Inert is not a pass; it means there was nothing
to look at.

Driving the hooks is the next thing worth building on this rig, and it is the
part `docs/checklists.md` is still holding.
