#!/usr/bin/env bash
# mod-gauntlet - compile the module against the real core, in seconds
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tests/syntax-check.sh and tests/run-tests.sh can only touch the twelve
# translation units that are free of Player.h; the other eleven need boost, the
# mysql headers and a generated revision_data.h that only the Docker build has.
# Phase 1 therefore wrote hundreds of lines that nothing could check, and three
# separate build failures -- `Condition` colliding with a core global,
# `MECHANIC_NONE` colliding, `QueryResult.h` missing -- were each found a whole
# Docker build apart.
#
# This script closes that gap. It keeps one long-lived container built from the
# core's own `build` stage, bind-mounts this repository over
# /azerothcore/modules/mod-gauntlet inside it, and drives the build tree's
# existing ninja. Nothing is copied, no image is rebuilt, and the container's
# object files survive between runs, so the second compile of a file is
# incremental.
#
#   tests/compile-check.sh                    every module translation unit
#   tests/compile-check.sh src/GauntletMgr.cpp one file (paths or bare names)
#   tests/compile-check.sh --anchors           the anchor and ladder audits, no Docker
#   tests/compile-check.sh --rebuild-image     rebuild the base image, then all
#   tests/compile-check.sh --stop              remove the helper container
#
# Measured on this machine: a cold run -- container created, cmake re-run, all
# twenty-three objects built from nothing -- is 14 s. After that a single file
# is 0.5-7 s and a whole no-op pass is 0.5 s, because the image carries
# ccache and the build tree lives in the container's writable layer. The
# partial link is 0.2 s and the anchor audit 0.03 s with no Docker at all. A
# full `docker compose build ac-worldserver` is minutes.

set -euo pipefail

CORE="${AC_CORE:-/mnt/c/Users/3302/azerothcore-wotlk}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE="${GAUNTLET_COMPILE_IMAGE:-ac-gauntlet-build}"
CONTAINER="${GAUNTLET_COMPILE_CONTAINER:-mod-gauntlet-compile}"

# Where the module is mounted, and the ninja object prefix that follows from it.
MODULE_PATH=/azerothcore/modules/mod-gauntlet
OBJ_PREFIX=modules/CMakeFiles/modules.dir/mod-gauntlet

# ---------------------------------------------------------------------------
# The ladder audit.
#
# A rank table is `constexpr T X[] = { a, b, c, d }` with a static_assert on its
# length beside it. The compiler checks the length. Nothing checks the values,
# and the values are where the mistakes are: Phase 6 hand-wrote eighty fourth
# ranks across thirty-four files, and a transposed digit -- 1.15 where 1.55 was
# meant -- compiles, links, passes every test, and ships a rank IV weaker than
# its rank III.
#
# So every ladder must be monotonic. Going up or going down are both fine; a
# ladder that changes direction part-way is a typo unless it is deliberate, and
# a deliberate one says so with LADDER-SENTINEL on the line above.
#
# Three tables are deliberate today, all the same shape: 0 is not a smaller
# number on those ladders, it is "denied outright" -- Feign Death, Vanish and
# Blink at their top rank. That is a sentinel, and a sentinel has to be marked
# rather than tolerated, or the next real inversion hides among them.
#
# No Docker, no build. Runs with the anchor audit.
# ---------------------------------------------------------------------------
ladder_audit() {
  python3 - "$ROOT" <<'PYEOF'
import re, sys, glob, os

root = sys.argv[1]
bad = []
checked = 0

SENTINEL = "LADDER-SENTINEL"
NAMES = {"true": 1, "false": 0, "UNHAPPY": 1, "CONTENT": 2}

for path in sorted(glob.glob(os.path.join(root, "src/mechanics/**/*.cpp"), recursive=True)):
    src = open(path).read()
    for m in re.finditer(r'constexpr\s+[\w:]+\s+(\w+)\[\]\s*=\s*\{([^}]*)\};', src):
        name, body = m.group(1), m.group(2)

        # The marker sits on any line of the comment block above the table.
        head = src[:m.start()].rsplit("\n\n", 1)[-1]
        if SENTINEL in head:
            continue

        toks = [t.strip().rstrip("fu") for t in body.split(",") if t.strip()]
        vals = []
        for t in toks:
            try:
                vals.append(float(eval(t, {"__builtins__": {}}, NAMES)))
            except Exception:
                vals = []
                break
        if len(vals) < 3:
            continue

        checked += 1
        up = all(b >= a for a, b in zip(vals, vals[1:]))
        down = all(b <= a for a, b in zip(vals, vals[1:]))
        if not (up or down):
            rel = os.path.relpath(path, root)
            bad.append(f"{rel}: {name} = {{{', '.join(t for t in toks)}}}")

if bad:
    print(f"LADDER  FAIL  {len(bad)} ladder(s) change direction part-way:")
    for b in bad:
        print(f"              {b}")
    print("              A rank that is weaker than the one below it is a typo. If it is")
    print(f"              deliberate, say so with {SENTINEL} in the comment above the table.")
    sys.exit(1)

print(f"LADDER  PASS  {checked} rank ladder(s), every one monotonic")
PYEOF
}

# ---------------------------------------------------------------------------
# The anchor audit.
#
# The most dangerous failure this codebase has. The module is archived into
# libmodules.a and linked plainly, so a translation unit nothing references is
# dropped by the linker, its GAUNTLET_MECHANIC registrar never runs, and
# MakeMechanic answers nullptr for a mechanic whose source is right there in the
# tree -- with no error anywhere. The affix is then offered to a live hardcore
# character and does nothing.
#
# GauntletScripts.cpp's AnchorMechanics() is what prevents it, one line per
# mechanic. This checks, by text and before anything is compiled, that every
# GAUNTLET_MECHANIC in the tree has both its declaration and its call there.
# It needs no Docker, so it also runs on a machine that has none.
# ---------------------------------------------------------------------------
anchor_audit() {
  local scripts="$ROOT/src/GauntletScripts.cpp"
  local fail=0 found=0

  if [ ! -f "$scripts" ]; then
    echo "ANCHOR  FAIL  $scripts is missing"
    return 1
  fi

  # Every anchor name the macros define, derived the same way the macro does:
  # GAUNTLET_MECHANIC(id, Type)  -> AddSC_gauntlet_mechanic_<Type>
  # GAUNTLET_MECHANIC_FN(id, fn) -> AddSC_gauntlet_mechanic_<fn>
  #
  # The first argument may be a literal or a named constant -- most mechanic
  # files spell their own id once, at the top -- so it is matched as either.
  local src name id rest
  while IFS='|' read -r src id name; do
    found=$((found + 1))

    if ! grep -q "AddSC_gauntlet_mechanic_${name}();" "$scripts"; then
      echo "ANCHOR  FAIL  id $id in $(basename "$src"): GauntletScripts.cpp never names"
      echo "              AddSC_gauntlet_mechanic_${name}(). Without it the linker drops the"
      echo "              file and MakeMechanic($id) silently answers nullptr."
      fail=1
      continue
    fi

    # Declared *and* called: a declaration alone leaves an unreferenced symbol,
    # and a call alone will not link. Both live in GauntletScripts.cpp, so the
    # count of occurrences is what tells them apart.
    local hits
    hits="$(grep -c "AddSC_gauntlet_mechanic_${name}();" "$scripts")"
    if [ "$hits" -lt 2 ]; then
      echo "ANCHOR  FAIL  id $id ($name): named once in GauntletScripts.cpp, needs both a"
      echo "              declaration in namespace Gauntlet and a call in AnchorMechanics()."
      fail=1
    fi
  done < <(grep -rn 'GAUNTLET_MECHANIC\(_FN\)\?[[:space:]]*(' "$ROOT/src" --include='*.cpp' |
           sed -nE 's|^([^:]+):[0-9]+:.*GAUNTLET_MECHANIC(_FN)?[[:space:]]*\([[:space:]]*([A-Za-z0-9_]+)[[:space:]]*,[[:space:]]*([A-Za-z_][A-Za-z0-9_]*).*|\1\|\3\|\4|p')

  # And the other way round: an anchor called but no longer defined is an
  # undefined symbol at link, which is loud -- but an anchor left behind for a
  # mechanic that was deleted is worth catching here rather than there.
  while IFS= read -r name; do
    if ! grep -rqE "GAUNTLET_MECHANIC(_FN)?[[:space:]]*\([[:space:]]*[A-Za-z0-9_]+[[:space:]]*,[[:space:]]*${name}[[:space:],)]" \
         "$ROOT/src" --include='*.cpp'; then
      echo "ANCHOR  FAIL  AnchorMechanics() calls AddSC_gauntlet_mechanic_${name}(), but no"
      echo "              GAUNTLET_MECHANIC in src/ defines it."
      fail=1
    fi
  done < <(grep -o 'AddSC_gauntlet_mechanic_[A-Za-z0-9_]*' "$scripts" |
           sed 's/AddSC_gauntlet_mechanic_//' | sort -u)

  if [ "$fail" -eq 0 ]; then
    echo "ANCHOR  PASS  $found registered mechanic(s), every one anchored in AnchorMechanics()"
  fi
  return "$fail"
}

# ---------------------------------------------------------------------------
# Docker plumbing
# ---------------------------------------------------------------------------
have_image() { docker image inspect "$IMAGE" >/dev/null 2>&1; }

build_image() {
  echo "==> building $IMAGE from $CORE (this is the slow one; it happens once)"
  [ -d "$CORE" ] || { echo "core not found: $CORE" >&2; exit 1; }
  # The same stage docker-compose builds the worldserver from, stopped one
  # target early so the build tree itself survives in the image.
  ( cd "$CORE" && docker build --target build -f apps/docker/Dockerfile -t "$IMAGE" . )
}

container_state() { docker inspect -f '{{.State.Status}}' "$CONTAINER" 2>/dev/null || echo none; }

ensure_container() {
  case "$(container_state)" in
    running) return 0 ;;
    exited|created|paused) docker rm -f "$CONTAINER" >/dev/null ;;
    none) ;;
  esac

  have_image || build_image

  # Read-only: the compiler only ever reads the module, and a build that can
  # write back into the working tree is a build that can surprise you.
  docker run -d --name "$CONTAINER" \
    -v "$ROOT:$MODULE_PATH:ro" \
    "$IMAGE" sleep infinity >/dev/null

  # The image's build tree was configured against whatever the module looked
  # like when the image was built, so the first thing a fresh container does is
  # re-glob. AzerothCore collects module sources with a plain file(GLOB) and no
  # CONFIGURE_DEPENDS ($CORE/src/cmake/macros/AutoCollect.cmake:29), so a new
  # .cpp is invisible to ninja until cmake runs again.
  docker exec "$CONTAINER" sh -c "cd /azerothcore/build && cmake . >/tmp/cmake.log 2>&1" ||
    { docker exec "$CONTAINER" tail -30 /tmp/cmake.log; exit 1; }
}

# Every module object ninja knows about, newline separated.
known_objects() {
  docker exec "$CONTAINER" sh -c \
    "cd /azerothcore/build && ninja -t targets all 2>/dev/null" |
    grep -o "^$OBJ_PREFIX/src/[^:]*\.cpp\.o" | sort
}

# Re-glob when the set of sources on disk no longer matches what ninja holds,
# and delete the objects of sources that are gone.
#
# The prune is not tidiness. The container's build tree outlives any single run,
# so a deleted .cpp leaves its .o behind -- and the partial link below globs the
# object directory, so it would go on linking code whose source no longer
# exists and go on reporting its symbols as defined. A file deleted in the
# working tree has to be deleted here too or the link check is a lie.
sync_targets() {
  local want have orphans
  want="$(cd "$ROOT" && find src -name '*.cpp' | sed "s|^|$OBJ_PREFIX/|; s|\$|.o|" | sort)"
  have="$(known_objects)"

  if [ "$want" != "$have" ]; then
    echo "==> source set changed; re-running cmake"
    docker exec "$CONTAINER" sh -c "cd /azerothcore/build && cmake . >/tmp/cmake.log 2>&1" ||
      { docker exec "$CONTAINER" tail -30 /tmp/cmake.log; exit 1; }
  fi

  # Unconditional, and not folded into the branch above: cmake only has to run
  # again when the *target* set moved, but an orphaned object can outlive that
  # by a whole invocation -- ninja forgets a deleted source the moment cmake
  # re-runs, while the .o it built is still sitting on disk.
  orphans="$(comm -13 <(printf '%s\n' "$want") \
                      <(docker exec "$CONTAINER" sh -c \
                          "cd /azerothcore/build && find $OBJ_PREFIX/src -name '*.o' 2>/dev/null" |
                        sort))"
  if [ -n "$orphans" ]; then
    echo "==> pruning $(printf '%s\n' "$orphans" | wc -l) object(s) whose source is gone"
    docker exec "$CONTAINER" sh -c "cd /azerothcore/build && rm -f $(printf '%s ' $orphans)"
  fi
}

# A path, a bare filename or a mechanic name -> the ninja object it builds.
object_for() {
  local arg="$1" rel
  rel="${arg#"$ROOT"/}"
  rel="${rel#./}"

  if [ -f "$ROOT/$rel" ]; then
    printf '%s/%s.o\n' "$OBJ_PREFIX" "$rel"
    return 0
  fi

  # Bare name: find it under src/.
  local hit
  hit="$(cd "$ROOT" && find src -name "$(basename "$arg")" -o -name "$(basename "$arg").cpp" | head -1)"
  if [ -n "$hit" ]; then
    printf '%s/%s.o\n' "$OBJ_PREFIX" "$hit"
    return 0
  fi

  echo "no such module source: $arg" >&2
  return 1
}

# ---------------------------------------------------------------------------
# The partial link.
#
# Compiling each file alone cannot see a symbol defined twice, and it cannot see
# an anchor that was declared and never defined. `ld -r` over the module's own
# objects catches both in a fifth of a second, and it is the only link this
# module can be given locally: the objects reference the core's symbols, which
# only worldserver resolves.
# ---------------------------------------------------------------------------
partial_link() {
  docker exec "$CONTAINER" sh -c "
    set -e
    cd /azerothcore/build
    objs=\$(find $OBJ_PREFIX/src -name '*.o' | sort)
    [ -n \"\$objs\" ] || { echo 'LINK    SKIP  nothing compiled yet'; exit 0; }
    if ! ld -r -o /tmp/gauntlet-partial.o \$objs 2>/tmp/ld.err; then
      echo 'LINK    FAIL'
      cat /tmp/ld.err
      exit 1
    fi
    undef=\$(nm -C /tmp/gauntlet-partial.o | grep ' U .*Gauntlet::' || true)
    if [ -n \"\$undef\" ]; then
      echo 'LINK    WARN  module symbols the module itself does not define:'
      echo \"\$undef\" | sed 's/^/              /'
    fi
    echo \"LINK    PASS  \$(echo \"\$objs\" | wc -l) objects, no duplicate definitions\"
  "
}

# ---------------------------------------------------------------------------
main() {
  local mode=all
  local -a files=()

  while [ $# -gt 0 ]; do
    case "$1" in
      --anchors)       mode=anchors ;;
      --rebuild-image) build_image ;;
      --stop)          docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
                       echo "removed $CONTAINER"; exit 0 ;;
      --all)           mode=all ;;
      -h|--help)       sed -n '2,30p' "${BASH_SOURCE[0]}"; exit 0 ;;
      *)               files+=("$1"); mode=some ;;
    esac
    shift
  done

  anchor_audit || exit 1
  ladder_audit || exit 1
  [ "$mode" = anchors ] && exit 0

  ensure_container
  sync_targets

  local -a targets=()
  if [ "$mode" = some ]; then
    local f
    for f in "${files[@]}"; do
      targets+=("$(object_for "$f")")
    done
  else
    mapfile -t targets < <(cd "$ROOT" && find src -name '*.cpp' | sort |
                           sed "s|^|$OBJ_PREFIX/|; s|\$|.o|")
  fi

  echo "==> ninja ${#targets[@]} object(s)"
  if ! docker exec "$CONTAINER" sh -c \
       "cd /azerothcore/build && ninja $(printf '%s ' "${targets[@]}")"; then
    echo "COMPILE FAIL"
    exit 1
  fi
  echo "COMPILE PASS  ${#targets[@]} object(s)"

  partial_link
}

main "$@"
