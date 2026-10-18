#!/usr/bin/env bash
# Fetches (shallow) reference repos into reference/ – read-only, not part of this repo.
source "$(dirname "$0")/_common.sh"
mkdir -p "$ROOT/reference"; cd "$ROOT/reference"
clone() { [ -d "$2" ] && echo "already present: $2" || git clone --depth 1 "$1" "$2"; }
# Our own SSX 3 port (private, same toolkit version): host, Vulkan back end, input, runtime patch, work log.
# If the clone fails (no access / gh not logged in), a local checkout is linked instead.
if [ ! -e SSX3-Recomp ]; then
  git clone --depth 1 https://github.com/bertibertig/SSX3-Recomp.git SSX3-Recomp 2>/dev/null \
    || { [ -d "$HOME/Coding/SSX3-Recomp" ] && ln -s "$HOME/Coding/SSX3-Recomp" SSX3-Recomp && echo "linked local SSX3-Recomp"; } \
    || echo "WARNING: SSX3-Recomp not available" >&2
fi
clone https://github.com/koenhendriks/SSX3-recomp.git SSX3-recomp-koen     # xboxrecomp port of SSX 3, GPL-2.0-or-later: findings only
clone https://github.com/MatiasRiveraC/SSX-Tricky-PC.git SSX-Tricky-PC   # xboxrecomp port of SSX Tricky: D3D11 renderer, RE notes
clone https://github.com/GiZcesi/OpenTricky.git OpenTricky                # continuation of it, GPL-3.0-only: findings only
