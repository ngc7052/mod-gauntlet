# Regenerating the README's affix table

`README.md` lists all sixty-nine mechanics with what each does and what it pays.
It is generated, never written by hand, for the reason `addon/GauntletUI/Data.lua`
is: a table that disagrees with the registry is worse than no table, because it
is the first thing anyone reads. Phase 6 alone changed eighty rank values and
three `maxRank`s; Phase 8 changed a boon and retired a row.

The boon magnitudes come from `GauntletGenerator`'s own `BoonMagnitude`, not
from a second copy of the numbers, so the column cannot disagree with what a
player is actually paid.

```bash
CORE=/mnt/c/Users/3302/azerothcore-wotlk
g++ -std=c++2a -O2 -Wall -I src -I "$CORE/src/common" \
    tests/tools/affix_table_standalone.cpp src/GauntletGenerator.cpp \
    src/GauntletRegistry.cpp src/GauntletNames.cpp -o build/affix_table
build/affix_table > /tmp/affixes.md
```

Then replace the block in `README.md` between

```
<!-- AFFIX-TABLE-BEGIN -->
<!-- AFFIX-TABLE-END -->
```

with the output. The markers are there so the replacement is mechanical.

## What the "What it does to you" column is

`MechanicDef::blurb` — the registry's own one-line summary, which is written at
**rank I**. That is deliberate and it is why the addon does not use it: the live
panel gets `ADESC`, the mechanic's own sentence at the rank you are actually
carrying, because "every 15 seconds" is the rank I number and a player holding
rank IV is on twelve.

A README is a description of the design rather than a readout of one run, so the
rank I line is the right one here — but it is stated in the README so nobody
reads the table as a promise about their own character.
