#!/usr/bin/env bash
# Tests the Linux release package in dist/ the way a player uses it: unpack, check the missing-game-files message,
# add game_files/, start ./run.sh, then drive the default flow into a ride with the emulated pad.
#   scripts/test_release.sh v0.1.0
# Results in logs/release-test-<version>/: summary.txt, linux.log, Vulkan frame dumps and linux_frames.png to look at.
# The flow (default settings, so the intro plays and is skipped with Start): Start x4 (intro, legal, title,
# Choose a Look) -> A (main menu: Career -> RIDE!, Millicent 1) -> A x3 (level, challenge card, goals board) -> ride,
# then accelerate and steer. Presses go through RECOMP_PAD_LIVE (as scripts/press.sh), no window focus needed.
#   RUN_SECS=<n>   how long the game runs (default 150)
#   AMPED2_SETTINGS=<file>   graphics settings to test with (default: none in the test folder = the defaults, so a
#                  player's own ~/.config/amped2-recomp/settings.ini does not change the result)
source "$(dirname "$0")/_common.sh"
VERSION="${1:?Usage: scripts/test_release.sh <version, e.g. v0.1.0>}"
ARCHIVE="$ROOT/dist/Amped2-Recomp-$VERSION-linux-x86_64.tar.gz"
OUT="$ROOT/logs/release-test-$VERSION"
RUN_SECS="${RUN_SECS:-150}"
[ -f "$ARCHIVE" ] || { echo "$ARCHIVE missing – first run scripts/package_release.sh $VERSION" >&2; exit 1; }
[ -f "$XBE" ] || { echo "game_files/default.xbe missing – the test needs your own game files" >&2; exit 1; }
python3 -I -c "import PIL" || { echo "python3 needs pillow" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/pkg" "$OUT/dumps"
SUMMARY="$OUT/summary.txt"; : > "$SUMMARY"
say() { echo "$*" | tee -a "$SUMMARY"; }

say "== Linux: $(basename "$ARCHIVE")"
tar -C "$OUT/pkg" -xzf "$ARCHIVE"
P="$OUT/pkg/Amped2-Recomp-$VERSION-linux-x86_64"
if (cd "$P" && ./run.sh) > "$OUT/nofiles.log" 2>&1; then say "  FAIL: run.sh started without game files"
else say "  missing game files: $(grep -q 'default.xbe missing' "$OUT/nofiles.log" && echo 'stops with the message (ok)' || echo 'FAIL, see nofiles.log')"; fi

# The disc's files only (not our own analysis output next to them)
for f in "$GAME_FILES"/*; do
  case "$(basename "$f")" in README*|*_analysis.json|*.txt) continue ;; esac
  ln -sfn "$f" "$P/game_files/$(basename "$f")"
done

PAD="$OUT/pad_live.txt"
# <seconds after start> <button> – generous gaps, loading times vary
SCRIPT=(28 start  34 start  40 start  46 start  52 start  58 a  64 a  76 a  82 a  88 a  96 a:3000
        100 lleft:1500  102 a:4000  107 lright:1500  110 a:4000)
set +e
export AMPED2_SETTINGS="${AMPED2_SETTINGS:-$OUT/settings.ini}"
say "  settings: $AMPED2_SETTINGS$([ -f "$AMPED2_SETTINGS" ] || echo ' (missing = defaults)')"
(cd "$P" && RECOMP_PAD_LIVE="$PAD" AMPED2_VK_DUMP="$OUT/dumps/f" AMPED2_VK_DUMP_EVERY=240 \
   timeout -k 10 -s INT "$RUN_SECS" ./run.sh) > "$OUT/linux.log" 2>&1 &
GAME=$!
t0=$SECONDS
for ((i = 0; i < ${#SCRIPT[@]}; i += 2)); do
  while (( SECONDS - t0 < SCRIPT[i] )); do sleep 0.2; done
  kill -0 "$GAME" 2>/dev/null || break
  echo "${SCRIPT[i+1]}" >> "$PAD"
done
wait "$GAME"; rc=$?
set -e

case $rc in 124|130|137) say "  run: still running after ${RUN_SECS}s (ok)" ;;
            *) say "  run: exit $rc  <- CRASHED or quit early (139 = segfault)" ;; esac
say "  pad: $(grep -a -m1 -o 'PAD: port [0-9] <- .*' "$OUT/linux.log" || echo 'no host pad (keyboard/emulated pad only)')"
say "  fps lines: $(grep -a -o -E '\[VK\] FPS: [0-9.]+ \| draws: [0-9]+' "$OUT/linux.log" | tail -3 | tr '\n' ';')"
grep -a -q -E '\[CRASH\]|SIGSEGV|Segmentation' "$OUT/linux.log" && say "  CRASH: $(grep -a -m1 -E '\[CRASH\]|SIGSEGV|Segmentation' "$OUT/linux.log")"
ls "$OUT"/dumps/*.bmp >/dev/null 2>&1 &&
  python3 -I "$ROOT/tests/release/contact_sheet.py" "$OUT/linux_frames.png" $(ls -v "$OUT"/dumps/*.bmp) | tee -a "$SUMMARY"
echo; echo "Results: $OUT (summary.txt, linux_frames.png)"
