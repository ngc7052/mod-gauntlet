#!/usr/bin/env bash
# mod-gauntlet - local gtest harness
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds and runs the module's Player-free unit tests without a core build.
# Fetches and compiles googletest from source into build/_deps on first run,
# then compiles the module's Player-free src/*.cpp plus every tests/*.cpp
# against it. Any extra arguments are forwarded to the test binary, so
#   tests/run-tests.sh --gtest_filter=HarnessSmoke.*
# works.
set -euo pipefail

CORE="${AC_CORE:-/mnt/c/Users/3302/azerothcore-wotlk}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
DEPS="$BUILD/_deps"
GTEST_SRC="$DEPS/googletest"
OBJDIR="$BUILD/obj"
BIN="$BUILD/gauntlet_tests"

CXX=g++
CXXFLAGS=(-std=c++2a -Wall -I "$ROOT/src" -I "$CORE/src/common")

# Header pattern that marks a src/*.cpp as needing the full core build; kept
# in sync with tests/syntax-check.sh by hand since bash has no shared-const
# story worth the indirection here.
CORE_HEADER_PATTERN='#include[[:space:]]*"(Player\.h|ScriptMgr\.h|Chat\.h|DatabaseEnv\.h|Config\.h|'\
'Unit\.h|Map\.h|Group\.h|World[A-Za-z]*\.h|GameTime\.h)"'

mkdir -p "$DEPS" "$OBJDIR"

if [ ! -d "$GTEST_SRC" ]; then
  echo "==> Fetching googletest v1.14.0 into $GTEST_SRC"
  git clone --depth 1 --branch v1.14.0 https://github.com/google/googletest "$GTEST_SRC"
else
  echo "==> googletest already present at $GTEST_SRC (skipping clone)"
fi

GTEST_ROOT="$GTEST_SRC/googletest"
GTEST_OBJ="$OBJDIR/gtest-all.o"
GTEST_MAIN_OBJ="$OBJDIR/gtest_main.o"
GTEST_CXXFLAGS=(-std=c++2a -isystem "$GTEST_ROOT/include" -isystem "$GTEST_ROOT")

# True when $1 (an object) needs rebuilding: it is missing, its source is newer,
# or any header the compiler recorded as a prerequisite is newer. Comparing only
# against the .cpp is how a Gauntlet.h change once left every object considered
# fresh and reported five passing tests that had been compiled against the
# previous header.
needs_rebuild() {
  local obj="$1" src="$2" dep="${1%.o}.d"
  [ -f "$obj" ] || return 0
  [ "$src" -nt "$obj" ] && return 0
  [ -f "$dep" ] || return 0
  local prereq
  # Strip the "target:" prefix and the line-continuation backslashes, then test
  # each remaining path.
  for prereq in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$dep"); do
    [ -e "$prereq" ] || continue
    [ "$prereq" -nt "$obj" ] && return 0
  done
  return 1
}

compile() {
  local src="$1" obj="$2"
  shift 2
  local cmd=("$CXX" "$@" -MMD -MF "${obj%.o}.d" -c "$src" -o "$obj")
  echo "==> ${cmd[*]}"
  if ! "${cmd[@]}"; then
    echo "FAILED: ${cmd[*]}"
    exit 1
  fi
}

if [ ! -f "$GTEST_OBJ" ] || [ "$GTEST_ROOT/src/gtest-all.cc" -nt "$GTEST_OBJ" ]; then
  compile "$GTEST_ROOT/src/gtest-all.cc" "$GTEST_OBJ" "${GTEST_CXXFLAGS[@]}"
fi
if [ ! -f "$GTEST_MAIN_OBJ" ] || [ "$GTEST_ROOT/src/gtest_main.cc" -nt "$GTEST_MAIN_OBJ" ]; then
  compile "$GTEST_ROOT/src/gtest_main.cc" "$GTEST_MAIN_OBJ" "${GTEST_CXXFLAGS[@]}"
fi

# Player-free module sources: same detection rule as tests/syntax-check.sh.
module_srcs=()
for src in "$ROOT"/src/*.cpp; do
  [ -e "$src" ] || continue
  if grep -E -q "$CORE_HEADER_PATTERN" "$src"; then
    echo "==> skipping $(basename "$src") (needs core game headers)"
    continue
  fi
  module_srcs+=("$src")
done

test_srcs=("$ROOT"/tests/*.cpp)
if [ ! -e "${test_srcs[0]:-}" ]; then
  echo "No test sources under tests/*.cpp; nothing to run."
  exit 0
fi

objs=("$GTEST_OBJ" "$GTEST_MAIN_OBJ")

for src in "${module_srcs[@]}" "${test_srcs[@]}"; do
  obj="$OBJDIR/$(basename "${src%.cpp}").o"
  if needs_rebuild "$obj" "$src"; then
    compile "$src" "$obj" "${CXXFLAGS[@]}" -isystem "$GTEST_ROOT/include"
  fi
  objs+=("$obj")
done

link_cmd=("$CXX" -std=c++2a -pthread "${objs[@]}" -o "$BIN")
echo "==> ${link_cmd[*]}"
if ! "${link_cmd[@]}"; then
  echo "FAILED: ${link_cmd[*]}"
  exit 1
fi

echo "==> $BIN $*"
"$BIN" "$@"
