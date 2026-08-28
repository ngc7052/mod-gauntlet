# mod-gauntlet tests

Two scripts, no Docker, no sudo, no core build.

## `./tests/syntax-check.sh`

Fast sanity check. Runs `g++ -std=c++2a -fsyntax-only` over every
`src/*.cpp` that does not include a core game header (`Player.h`,
`ScriptMgr.h`, `Chat.h`, `DatabaseEnv.h`, `Config.h`, `Unit.h`, `Map.h`,
`Group.h`, `World*.h`, `GameTime.h`), against `-I src -I "$AC_CORE/src/common"`
only. Prints `PASS`/`FAIL`/`SKIP` per file and exits non-zero on any failure.
Nothing is compiled to an object file or linked; this only proves the
translation unit parses and type-checks against `src/common`.

`AC_CORE` defaults to `/mnt/c/Users/3302/azerothcore-wotlk` (same default and
override variable as `sync-to-server.sh`).

## `./tests/run-tests.sh [gtest args...]`

The real thing: fetches googletest v1.14.0 into `build/_deps/googletest` on
first run (pinned tag, `git clone --depth 1`), compiles it and the module's
Player-free sources plus every `tests/*.cpp` with `g++ -std=c++2a`, links
against gtest, and runs the resulting binary. `build/` is gitignored and
reused between runs — object files rebuild only when their source is newer.
Any arguments are forwarded to the binary, e.g.:

```sh
tests/run-tests.sh --gtest_filter=HarnessSmoke.*
```

## What neither script can cover

Any translation unit that includes `Player.h` or another core game header —
today that is `GauntletMgr.cpp` and `GauntletScripts.cpp` — needs the core's
full include set (game, shared, server headers, generated revision info) to
even parse. Neither script attempts it; both skip those files with a visible
line rather than silently ignoring them. The only way to check that code is
to read the headers it calls into and let the coordinator's real core build
(`docker compose build ac-worldserver`) catch what reading missed.

## Relationship to the plan's `ACORE_MODULE_TEST_SOURCES` registration

`docs/implementation-plan.md` §5.1 registers `tests/*.cpp` with AzerothCore's
own build via `set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
...)` in `mod-gauntlet.cmake`, so the same test files also build and run
inside a real core build's `unit_tests` binary, linked against the `modules`
library. That registration is a separate worker's job (step 9, file
ownership in the branch's worker contract) and is the path these tests take
in CI and in the coordinator's build — it is authoritative.

This harness is an addition the plan does not name. It exists because the
only local compiler during Phase 0 development is `g++ 9.4` with no gtest
package, no sudo, and a real core build measured in hours that only the
coordinator runs. Six workers are editing Player-free C++ on one branch at
once and need a feedback loop measured in seconds, not hours, so this harness
builds the same `tests/*.cpp` files standalone against `src/common` alone.
It is not a replacement for the CMake registration or the core build — a
file can pass here and still fail to link inside `unit_tests` if it
accidentally depends on something `src/common` provides differently than the
full core tree, or on a symbol the `modules` library doesn't export the way
this harness assumes. Treat a green run here as "the fast loop is happy," and
the coordinator's real build as the final word.
