#!/usr/bin/env bash
# What CI runs (.github/workflows/ci.yml), also usable locally: applies the runtime patches, configures with the host
# tests on, builds and runs ctest. Without generated code in src/recomp/gen this is the host-only build (runtime +
# our sources, no game executable); with it, the full game build plus the tests.
#   scripts/ci.sh                                   # native: build-ci/
#   CI_TARGET=mingw scripts/ci.sh                   # Linux → Windows cross build: build-ci-mingw/ (tests need wine)
# Windows (MSYS2 MINGW64 shell) builds natively with the first form.
source "$(dirname "$0")/_common.sh"
apply_runtime_patches
# apply_runtime_patches only warns about a patch that does not apply (local runtime work); in CI that is an error.
for p in "$ROOT"/patches/xboxrecomp/*.patch; do
  git -C "$TOOLKIT" apply --reverse --check "$p" || { echo "Runtime patch not applied: $(basename "$p")" >&2; exit 1; }
done
ARGS=(-DCMAKE_BUILD_TYPE=Release -DAMPED2_TESTS=ON)
case "${CI_TARGET:-native}" in
  native) BUILD_DIR="${BUILD_DIR:-$ROOT/build-ci}" ;;
  mingw)  BUILD_DIR="${BUILD_DIR:-$ROOT/build-ci-mingw}"
          ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TOOLKIT/cmake/mingw-w64-x86_64.cmake") ;;
  *)      echo "CI_TARGET must be native or mingw" >&2; exit 1 ;;
esac
cmake -S "$ROOT" -B "$BUILD_DIR" "${ARGS[@]}" ${CMAKE_GENERATOR:+-G "$CMAKE_GENERATOR"}
cmake --build "$BUILD_DIR" --parallel "${JOBS:-$(nproc)}"
ctest --test-dir "$BUILD_DIR" --output-on-failure
