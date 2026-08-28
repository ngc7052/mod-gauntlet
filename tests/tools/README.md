# tests/tools

## dump_legacy_rolls

Dumps `Gauntlet::Roll(seed, tier, i)` over seeds `1, 7, 42, 1337, 65535,
2147483647`, tiers `1..16` and roll index `0, 1, 2` (288 rows) as JSON, using
only the module's own `*Name` functions to add human-readable fields.

`tests/fixtures/legacy_rolls.json` was generated from the committed
`src/GauntletAffix.cpp` at the current HEAD, with:

```bash
CORE=/mnt/c/Users/3302/azerothcore-wotlk
g++ -std=c++2a -I src -I "$CORE/src/common" \
    tests/tools/dump_legacy_rolls.cpp src/GauntletAffix.cpp -o build/dump_legacy_rolls
./build/dump_legacy_rolls > tests/fixtures/legacy_rolls.json
```

Regenerate the fixture only if the *legacy* algorithm itself is ever
corrected — it should never be. `Roll` becomes `LegacyRoll` in
`src/GauntletLegacy.cpp` for the storage migration, and the migration is
correct only if it reproduces this fixture exactly.

This file is the reason a migrated character keeps the affixes it had.
