#!/usr/bin/env bash
# Send controller input to the running game (pad 1 unless prefixed "p2-" …).
#   scripts/press.sh start                 # tap Start (150 ms)
#   scripts/press.sh a:400                 # hold A for 400 ms
#   scripts/press.sh down a                # several presses, in order (one per line, read in turn)
#   scripts/press.sh lt+rt                 # buttons together
#   scripts/press.sh --wait 500 down down a  # 500 ms between presses
# Buttons: up down left right start back lthumb rthumb, a b x y black white lt rt,
# lleft lright lup ldown / rleft rright rup rdown (sticks at full throw).
# How it works: scripts/run.sh starts the game with RECOMP_PAD_LIVE=logs/pad_live.txt (the emulated
# USB pad reads each line appended to that file and presses it), so this only appends lines.
# Timed sequences from the start instead: RECOMP_PAD_SCRIPT=40000:start,44000:a (ms after enumeration).
source "$(dirname "$0")/_common.sh"
LIVE="${RECOMP_PAD_LIVE:-$ROOT/logs/pad_live.txt}"
[ -f "$LIVE" ] || { echo "No live pad file ($LIVE) – is the game running via scripts/run.sh?" >&2; exit 1; }
wait_ms=250
[ "${1:-}" = "--wait" ] && { wait_ms="$2"; shift 2; }
[ $# -gt 0 ] || { sed -n '2,11p' "$0"; exit 1; }
first=1
for b in "$@"; do
  [ $first = 1 ] || sleep "$(awk "BEGIN{print $wait_ms/1000}")"
  first=0
  echo "$b" >> "$LIVE"
  echo "pressed: $b"
done
