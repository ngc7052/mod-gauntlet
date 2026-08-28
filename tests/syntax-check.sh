#!/usr/bin/env bash
# mod-gauntlet - fast per-file syntax check for the Player-free module sources
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs `g++ -fsyntax-only` over every .cpp under src/ (recursively, so the
# mechanics tree is covered) that does not include a core
# game header (Player.h and friends), so the fast loop stays honest as new
# Player-free files land beside GauntletAffix.cpp. Files that do include one
# of those headers need the core's full include set and cannot be checked
# this way; see tests/README.md.
set -euo pipefail

CORE="${AC_CORE:-/mnt/c/Users/3302/azerothcore-wotlk}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Any of these in a translation unit means it needs the full core build to
# check, not just src/common.
CORE_HEADER_PATTERN='#include[[:space:]]*"(Player\.h|ScriptMgr\.h|Chat\.h|DatabaseEnv\.h|Config\.h|'\
'Unit\.h|Map\.h|Group\.h|World[A-Za-z]*\.h|GameTime\.h)"'

fail=0
checked=0
objs=()
OBJDIR="$ROOT/build/syntax-check"
mkdir -p "$OBJDIR"

for src in $(find "$ROOT/src" -name "*.cpp" | sort); do
  [ -e "$src" ] || continue
  name="$(basename "$src")"

  if grep -E -q "$CORE_HEADER_PATTERN" "$src"; then
    echo "SKIP  $name (skipped, needs core game headers)"
    continue
  fi

  checked=$((checked + 1))
  obj="$OBJDIR/${name%.cpp}.o"
  cmd=(g++ -std=c++2a -I "$ROOT/src" -I "$CORE/src/common" -c "$src" -o "$obj")
  if "${cmd[@]}"; then
    echo "PASS  $name"
    objs+=("$obj")
  else
    echo "FAIL  $name"
    echo "      command: ${cmd[*]}"
    fail=1
  fi
done

# Compiling each file alone cannot see a symbol defined twice. That matters
# right now: the legacy generator lives in both GauntletAffix.cpp and
# GauntletLegacy.cpp until the switchover deletes the first, and a duplicate
# definition would only surface when the core linked the module. So link the
# lot together against a trivial main.
if [ "$fail" -eq 0 ] && [ "${#objs[@]}" -gt 0 ]; then
  printf 'int main() { return 0; }\n' > "$OBJDIR/link_probe.cpp"
  if g++ -std=c++2a -c "$OBJDIR/link_probe.cpp" -o "$OBJDIR/link_probe.o" &&
     g++ "$OBJDIR/link_probe.o" "${objs[@]}" -o "$OBJDIR/link_probe"; then
    echo "PASS  link (${#objs[@]} objects, no duplicate or missing symbols)"
  else
    echo "FAIL  link"
    fail=1
  fi
fi

if [ "$checked" -eq 0 ]; then
  echo "No Player-free source files found under src/."
fi

exit "$fail"
