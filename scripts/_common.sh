#!/usr/bin/env bash
# Shared paths for all scripts (loaded with `source`).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLKIT="$ROOT/xboxrecomp"
GAME_FILES="$ROOT/game_files"
GEN_DIR="$ROOT/src/recomp/gen"
VENV="${AMPED2_VENV:-$ROOT/.venv}"          # the build container brings its own (docker/Dockerfile)
XBE="$GAME_FILES/default.xbe"
export PYTHONUNBUFFERED=1   # write logs live (otherwise Python buffers when redirected to files)

use_venv() {
  if [ -f "$VENV/bin/activate" ]; then
    # shellcheck disable=SC1091
    . "$VENV/bin/activate"
  elif [ -f "$VENV/Scripts/activate" ]; then   # Windows venv (Git Bash / MSYS2)
    # shellcheck disable=SC1091
    . "$VENV/Scripts/activate"
  else
    echo "No .venv found – run first: scripts/setup.sh" >&2
    exit 1
  fi
}

need_xbe() {
  if [ ! -f "$XBE" ]; then
    echo "game_files/default.xbe missing. First run: scripts/extract_iso.sh <Amped2-Xbox.iso | extracted-disc-folder>" >&2
    exit 1
  fi
}

# Which Amped 2 build game_files/default.xbe is. Statically recompiled code is tied to one XBE: every
# hand-written address (src/version/<v>/, config/<v>/) belongs to one build. Known builds, by SHA-1:
#   pal     38e63cd016bf084d999e0a911933a52191bfc378  (Europe, 2003-10-03)
#   ntsc_u  5f432b86775b7b334212a3bc09d2063f670865b7  (North America, 2003-09-30; dumped with region/media
#                                                      flags opened and the debug entry-point key)
xbe_version() {
  local sha
  sha="$(sha1sum "$XBE" | cut -d' ' -f1)"
  case "$sha" in
    38e63cd016bf084d999e0a911933a52191bfc378) echo pal ;;
    5f432b86775b7b334212a3bc09d2063f670865b7) echo ntsc_u ;;
    *) echo "Unknown default.xbe (SHA-1 $sha): only the PAL and NTSC-U builds listed in scripts/_common.sh" \
            "are supported. Their addresses live in src/version/ and config/." >&2
       return 1 ;;
  esac
}

# Our runtime changes live in patches/xboxrecomp/*.patch, not in the submodule (CLAUDE.md: the submodule stays
# upstream). Apply them before anything uses xboxrecomp, so a fresh clone or a submodule update just builds.
# Idempotent: an already applied patch is skipped. If the submodule has edits that match neither state (runtime
# work in progress), it warns and builds what is there – export the edits with:
#   git -C xboxrecomp diff -- . ':!tools/linux' > patches/xboxrecomp/0001-linux-x86_64-mmio-traps-casefold-mingw-oldnames.patch
apply_runtime_patches() {
  local p
  [ -f "$TOOLKIT/CMakeLists.txt" ] || git -C "$ROOT" submodule update --init xboxrecomp
  for p in "$ROOT"/patches/xboxrecomp/*.patch; do
    [ -f "$p" ] || continue
    if git -C "$TOOLKIT" apply --reverse --check "$p" 2>/dev/null; then
      continue                                   # already applied
    elif git -C "$TOOLKIT" apply --check "$p" 2>/dev/null; then
      git -C "$TOOLKIT" apply "$p"
      echo "Applied runtime patch: $(basename "$p")"
    else
      echo "WARNING: $(basename "$p") is neither applied nor applicable – xboxrecomp has other local edits." >&2
      echo "         Building the submodule as it is. If those edits are intended, re-export the patch (see _common.sh)." >&2
    fi
  done
}
