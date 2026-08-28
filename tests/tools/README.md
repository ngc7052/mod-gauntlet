# tests/tools

## dump_legacy_rolls — deleted in Phase 2

It dumped `Gauntlet::Roll(seed, tier, i)` over a fixed cross product so that
`tests/fixtures/legacy_rolls.json` could hold the generator-1 affixes every live
character was carrying, and the golden test in `tests/GeneratorTest.cpp` held
`LegacyRoll` to that fixture field by field. The migration ran once, in Phase 0,
and converted 21 rows across two characters with exact fidelity; the four
mechanics that could still read a generator-1 magnitude were deleted in Phase 2,
and `LegacyRoll`, the fixture, the golden test and this tool went with them.

Nothing on any realm was rolled by generator 1 any more. It is in git if it is
ever wanted: `git log --diff-filter=D -- tests/tools/dump_legacy_rolls.cpp`.

