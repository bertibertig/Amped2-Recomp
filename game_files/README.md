# game_files/

This holds the **contents of your own Amped 2 Xbox disc** (`default.xbe`, data, movies, music …).
The contents of this folder are excluded via `.gitignore` and must never go into the repo.

    scripts/extract_iso.sh "/path/to/Amped 2 (Xbox).iso"    # needs extract-xiso or xdvdfs
    scripts/extract_iso.sh "/path/to/extracted/disc/"        # or copy an already extracted disc

Expected afterwards: `game_files/default.xbe` plus the rest of the disc next to it, with the original
folder structure. Prefer an unmodified dump; if the XBE was patched (region/media flags, softmod), note it
in `docs/notes.md`.
