# Phase 8 report — the code that could not be tested

Branch `feature/affix-redesign`. Five commits, `aabf950` through this report.

Phase 7's report said the next thread was that a console command building
throwaway instances is the only way to test anything needing a `Player`, and it
had just found eight bugs. This phase took the other half of that observation:
**a lot of the module could not be tested for no better reason than which file
it happened to live in.**

---

## 0. Definition of done

| Asked | Result |
|---|---|
| Test the wire logic that had already broken once | **done** — 6 tests, and it holds |
| Test the module's inbound surface | **done** — 4 tests, and it holds |
| Remove the duplication that "must answer identically" | **done** |
| Unit tests pass | **129 pass**, up from 114 |
| Worldserver starts clean | deployed |

---

## 1. The shape of the problem

`tests/run-tests.sh` compiles every `src/*.cpp` that does not include a core game
header. That rule is what makes the harness possible at all — no core build, no
database, a second and a half to run — and it has a side effect nobody chose:
**a function's testability is decided by whichever translation unit it was first
written in.**

Four functions were sitting in `GauntletAddon.cpp` behind `#include "Player.h"`,
and none of them needs a `Player`:

| | What it does | Why it matters |
|---|---|---|
| `SplitDescription` | cuts a description into wire chunks | has already produced a bug a player read on screen |
| `ParseUInt` | parses `PICK <i>` | the module's **entire** inbound surface |
| `TrimList` | cuts a run's conducts to 255 bytes | the one place a finished run's epitaph is truncated |
| `Utf8Floor` | backs a cut off to a character boundary | what keeps the other two from leaving half a character on the wire |

They are in `src/GauntletWire.cpp` now, which is free of `Player.h`, and they
have fifteen tests between them.

---

## 2. The property that had been kept by hand in two languages

The server splits a description on a space and drops it; the addon rejoins the
pieces with exactly one. That rule is written in C++ in one repository and Lua in
another, and it has been broken once in a way a player read — *"in dungeons.In
exchange"* — because a trailing space does not survive the trip to
`CHAT_MSG_ADDON`.

It is one line to assert:

```cpp
JoinDescription(SplitDescription(text)) == text
```

`JoinDescription` exists only so that can be written; it is a copy of the
addon's one-line rule and the test is what stops the two drifting.

**Two shapes the old splitter got wrong**, neither findable by playing:

- **A run of spaces produced an empty chunk**, which rejoins as a second space —
  so the text would grow a gap every time it crossed the wire.
- **A hard cut could land inside a UTF-8 sequence.** Not reachable today (every
  player-facing string in the module is ASCII) and reachable by the first
  em-dash anyone types, arriving as replacement characters and getting reported
  as "the addon shows garbage", investigated nowhere near this file.

**One shape cannot round-trip**: a single word longer than a chunk has no space
to cut at, so the cut is hard and the rejoin inserts a space that was never
there. The answer is not a cleverer splitter — it is never to write a
200-character word — so `.gauntlet debug cards` checks it live across all
sixty-nine mechanics at every rank. Against the deployed build:

```
69 mechanic(s), 0 dead rank(s), 0 unsplittable word(s).
```

---

## 3. Nothing was broken in the inbound parser, and that is the result

`ParseUInt` is careful code: a length cap, a per-character digit test, and a
running limit check. It had never been run against a hostile string. It holds —
`"+1"`, `" 1"`, `"1\n"`, `"0x10"`, an Arabic-Indic digit and eleven digits are
all refused, the accumulator cannot wrap, and a refused parse leaves its output
alone so a caller that ignored the return value would not read a half-parsed
value.

Worth finding out on purpose rather than assuming. These are the three functions
in the module that touch data nobody here has promised to keep clean.

---

## 4. Two copies that "must answer identically"

`GauntletMgr.cpp` had `LivePlayerView` and `GauntletCommands.cpp` had
`CommandPlayerView`, with a comment saying the two must agree, the talent
encoding pasted between them, and a `TODO` to hoist one out. **The TODO stood
from Phase 1 to Phase 8.**

A divergence would not crash. `.gauntlet debug offers` would simply report offers
the player would never see, and the two copies would quietly disagree about which
class curses are relevant — and twenty-three of the sixty-nine rows gate on a
talent tree. One class, exported from `GauntletMgr.h`, nothing left to drift.

---

## 5. One live bug, from Phase 6

`RankNumeral` covered I, II and III and fell back to a decimal. Phase 6 added a
fourth rank and did not add a numeral, so a run ending with one was recorded as
`"Falling Sky 4"` in `gauntlet_leaderboard` — the one place a finished run's
epitaph is read — while the addon showed `IV` for the same affix all game.

A `static_assert` on `MAX_RANK` makes the next raise fail there instead of
shipping.

---

## 6. What Phase 9 should know

1. **Ask of any function: could this be tested?** If the answer is "no, because
   of the file it is in", that is a reason to move it, not a fact about the
   function. Fifteen tests came out of asking it four times.
2. **Still not playtested.** Fifth phase ending on it, and the honest note is
   that the remaining list in `docs/checklists.md` is almost entirely things
   that need a screen — the classes of fault a machine can find here have been
   getting found.
3. **`.gauntlet debug cards` is the pattern for the rest.** Anything that needs
   a `Player` and produces text or a number can be audited the same way.
4. **`CAP_CLASS` is still 3 and still `TODO(design)`.** Five phases now.
