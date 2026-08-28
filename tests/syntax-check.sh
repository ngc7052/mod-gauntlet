#!/usr/bin/env bash
# mod-gauntlet - fast per-file syntax check for the Player-free module sources
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs `g++ -fsyntax-only` over every src/*.cpp that does not include a core
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

for src in "$ROOT"/src/*.cpp; do
  [ -e "$src" ] || continue
  name="$(basename "$src")"

  if grep -E -q "$CORE_HEADER_PATTERN" "$src"; then
    echo "SKIP  $name (skipped, needs core game headers)"
    continue
  fi

  checked=$((checked + 1))
  cmd=(g++ -std=c++2a -fsyntax-only -I "$ROOT/src" -I "$CORE/src/common" "$src")
  if "${cmd[@]}"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name"
    echo "      command: ${cmd[*]}"
    fail=1
  fi
done

if [ "$checked" -eq 0 ]; then
  echo "No Player-free source files found under src/."
fi

exit "$fail"
