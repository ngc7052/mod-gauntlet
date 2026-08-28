# Regenerating `addon/GauntletUI/Data.lua`

The addon's mechanic table is generated from the registry, never written by
hand, so the two cannot drift. There are two ways to produce it and they run the
same code.

## On a server (the normal way)

```
.gauntlet debug export-addon /tmp/Data.lua
```

`SEC_GAMEMASTER`, gated on `Gauntlet.Debug.Enable = 1`, and `Console::Yes` — it
reads the registry and writes a file, so it needs no logged-in character and can
be run from the worldserver console. Then `docker cp ac-worldserver:/tmp/Data.lua
addon/GauntletUI/Data.lua`.

## Without a server

`export_addon_standalone.cpp` compiles the exporter's own source out of
`src/GauntletCommands.cpp` — the block between the `GAUNTLET_EXPORT_BEGIN` and
`GAUNTLET_EXPORT_END` markers — and runs it against the real registry. It exists
because the module's Player-dependent translation units cannot be built without
the full core, while this block deliberately depends on nothing but the registry.

```bash
CORE=/mnt/c/Users/3302/azerothcore-wotlk
sed -n '/GAUNTLET_EXPORT_BEGIN/,/GAUNTLET_EXPORT_END/p' src/GauntletCommands.cpp > /tmp/export_block.inc
g++ -std=c++2a -I src -I /tmp -I "$CORE/src/common" \
    tests/tools/export_addon_standalone.cpp src/GauntletRegistry.cpp src/GauntletNames.cpp \
    -o /tmp/export_addon
/tmp/export_addon > addon/GauntletUI/Data.lua
```

The output is byte-identical between runs; ids ascend, so a regeneration produces
no spurious diff. If the two paths ever disagree, the standalone stub of
`Addon::Version` in the harness has drifted from `src/GauntletAddon.h`.
