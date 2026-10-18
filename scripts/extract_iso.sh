#!/usr/bin/env bash
# Puts the contents of your own Amped 2 Xbox disc into game_files/.
#   scripts/extract_iso.sh "/path/to/Amped 2 (Xbox).iso"     # XISO / redump image (extract-xiso or xdvdfs)
#   scripts/extract_iso.sh "/path/to/extracted/disc/"         # an already extracted disc (copied as is)
# Afterwards game_files/default.xbe must exist; the rest of the disc goes next to it.
source "$(dirname "$0")/_common.sh"
SRC="${1:?Usage: scripts/extract_iso.sh <image.iso | extracted-disc-folder>}"
mkdir -p "$GAME_FILES"
if [ -d "$SRC" ]; then
  [ -f "$SRC/default.xbe" ] || { echo "No default.xbe in $SRC – is this the disc root?" >&2; exit 1; }
  cp -a "$SRC"/. "$GAME_FILES"/
elif [ -f "$SRC" ]; then
  if command -v extract-xiso >/dev/null; then
    extract-xiso -x "$SRC" -d "$GAME_FILES"
  elif command -v xdvdfs >/dev/null; then
    xdvdfs unpack "$SRC" "$GAME_FILES"
  else
    echo "Neither extract-xiso nor xdvdfs found." >&2
    echo "  extract-xiso: https://github.com/XboxDev/extract-xiso  (AUR: extract-xiso-git)" >&2
    echo "  xdvdfs:       https://github.com/antangelo/xdvdfs  (cargo install xdvdfs-cli)" >&2
    echo "Or extract the disc elsewhere and pass the folder instead." >&2
    exit 1
  fi
else
  echo "Not found: $SRC" >&2; exit 1
fi
ls -la "$GAME_FILES" | head -30
[ -f "$XBE" ] && echo "OK: default.xbe found" || echo "WARNING: default.xbe not in the root directory"
echo "SHA-1 of the XBE (record it in docs/notes.md):"; sha1sum "$XBE" 2>/dev/null || true
echo "Next step: scripts/run_pipeline.sh parse"
