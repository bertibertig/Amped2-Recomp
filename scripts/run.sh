#!/usr/bin/env bash
# Starts the built port from the repo root (paths in src/main.c are relative to it).
#   scripts/run.sh                          # output to the console
#   scripts/run.sh 2>&1 | tee logs/run.log  # for doctor.sh / seed_from_log
# Runtime switches (details: docs/notes.md, xboxrecomp/docs). Defaults are the SSX3-Recomp ones:
#   RECOMP_TITLE_KEVENTS=1  title-built KEVENTs get a host shadow (SSX 3 crashed in SetEvent on POSIX without)
#   RECOMP_VBLANK=1         60 Hz VBlank interrupt; D3D_BlockUntilVerticalBlank waits for it
#   RECOMP_AC97_READY=1     AC'97 codec ready + emulated APU; without it DirectSoundCreate fails
#   RECOMP_PB_EXEC=1        decode/execute the NV2A pushbuffer – otherwise no picture
#   RECOMP_FB_WINDOW=1      framebuffer window (SDL2) for the CPU rasteriser
#   RECOMP_RASTER_THREADS=n rasteriser worker threads (default: CPUs-4, POSIX pool from the runtime patch)
#   RECOMP_HEAP_RECLAIM=1   frees really release memory (heap + contiguous arena). Amped 2 probes the largest free
#                           contiguous block at boot and frees it again; without this it keeps ~47 MB for good and
#                           XACT/USB allocations fail -> the game launches its error screen Boom.xbe
#   RECOMP_FB_DUMP=<prefix> BMP frame dumps
#   RECOMP_USB=1 / RECOMP_KEYBOARD=1   emulated USB pad; keyboard = pad 1 (Space/Z=A, Enter/Esc=Start, WASD … – README "Controls")
#   RECOMP_USB_PADS=n       more emulated pads on the USB bus, default 1
#   AMPED2_SKIP_LOGOS=1     skip the MGS / XSN logo movies (default here); AMPED2_SKIP_INTRO=1 skips the intro too
#   AMPED2_PAD_TRACE=1      log every controller button change
#   RECOMP_PAD_LIVE=<file>  emulated pad 1 presses every line appended to <file> – default logs/pad_live.txt,
#                           emptied at every start; send keys with scripts/press.sh start / a:400 / down …
#   RECOMP_PAD_SCRIPT=40000:start,44000:a   timed presses (ms after the pad enumerates), or =@file
#   RECOMP_WATCHDOG_SECS=n / RECOMP_KERNEL_LOG_BUDGET=n   hang watchdog / longer kernel trace
source "$(dirname "$0")/_common.sh"
[ -x "$ROOT/build/amped2_recomp" ] || { echo "No build. First run: scripts/build.sh" >&2; exit 1; }
cd "$ROOT"
export RECOMP_TITLE_KEVENTS="${RECOMP_TITLE_KEVENTS:-1}"
export RECOMP_VBLANK="${RECOMP_VBLANK:-1}"
export RECOMP_AC97_READY="${RECOMP_AC97_READY:-1}"
export RECOMP_HEAP_RECLAIM="${RECOMP_HEAP_RECLAIM:-1}"
export RECOMP_PB_EXEC="${RECOMP_PB_EXEC:-1}"
export RECOMP_FB_WINDOW="${RECOMP_FB_WINDOW:-1}"
export RECOMP_USB="${RECOMP_USB:-1}"
export RECOMP_KEYBOARD="${RECOMP_KEYBOARD:-1}"
export AMPED2_SKIP_LOGOS="${AMPED2_SKIP_LOGOS:-1}"   # MGS/XSN logos off (AMPED2_SKIP_INTRO=1: the intro too)
export RECOMP_PAD_LIVE="${RECOMP_PAD_LIVE:-$ROOT/logs/pad_live.txt}"
mkdir -p "$(dirname "$RECOMP_PAD_LIVE")" && : > "$RECOMP_PAD_LIVE"
exec ./build/amped2_recomp "$@"
