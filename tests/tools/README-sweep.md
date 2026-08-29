# The offer-density sweep

`sweep_standalone.cpp` simulates runs and reports how often the offer builder
had to relax a rule, how many slots came back empty, and how often a tier had no
reward-shaped offer to give. It is the tool the tuning arguments in
`docs/phase-4-report.md` §4 and `docs/phase-5-report.md` §2 were settled with.

## Why it is not the test

`tests/OfferInvariantsTest.cpp` runs the same simulation and prints the same
census on its way past. That is the right home for the *assertions* and the
wrong home for tuning: a test fixes every knob at its shipped value, because a
test whose inputs move is not a test. This one takes them from the command line.

The two share `PickIndex` and `ApplyPick` by copy, deliberately, so they walk the
same runs and their numbers can be compared. If either moves, both move.

## Build

No core build, no gtest. Three module sources, none of which touch a `Player`.

```bash
CORE=/mnt/c/Users/3302/azerothcore-wotlk
g++ -std=c++2a -O2 -Wall -I src -I "$CORE/src/common" \
    tests/tools/sweep_standalone.cpp src/GauntletGenerator.cpp \
    src/GauntletRegistry.cpp src/GauntletNames.cpp -o build/sweep
```

## Run

```
build/sweep [--seeds N] [--tiers N] [--choices N] [--max-carried N]
            [--family-mask 0xNN] [--full] [--summary]
```

`--seeds 300` is 240,000 offer sets and takes about a second; the numbers are
stable to a few tenths of a point against `--seeds 2000`. `--full` includes
`MF_NotImplemented` rows, which is now the same table as the live one and stays
as an option because a future phase will add rows before it implements them.

Columns: `sets` simulated at that tier, `relaxed` (the share that had to relax
any rule), `empty` (slots that came back as `MECHANIC_NONE`), `noReward` (the
share of sets with no `MF_RewardShaped` offer), `carried` (mean carried-set
size).

## Knobs the command line cannot reach

`MAX_RANK`, `CAP_CLASS`, `CAP_TEMPO`, `CAP_ON_KILL`, `CAP_BARGAIN` and the
registry's own tier windows are compile-time. To measure one, copy `src/` aside,
edit the copy, and build the sweep against it — the tool needs only the three
sources above, so a scratch copy costs a second:

```bash
cp -r src /tmp/variant/src
sed -i 's/CAP_CLASS = 3;/CAP_CLASS = 4;/' /tmp/variant/src/GauntletGenerator.cpp
g++ -std=c++2a -O2 -I /tmp/variant/src -I "$CORE/src/common" \
    tests/tools/sweep_standalone.cpp /tmp/variant/src/*.cpp ... -o build/sweep_cc4
```

Raising `MAX_RANK` also needs every registry row's `maxRank` raised with it, or
`Eligible`'s `held->rank >= def.maxRank` stops the rank-up before the ceiling
does and the measurement shows no change. Note also that the mechanics' own rank
tables are `constexpr T X[MAX_RANK] = { a, b, c }` — raising the constant makes
those zero-fill silently, which does not matter to this tool (it compiles none
of them) and matters enormously to the module.
