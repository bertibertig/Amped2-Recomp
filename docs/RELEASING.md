# Releasing Amped2-Recomp

Project recipe for the global `/release` skill (it follows these steps where they are more specific than its own).
Same scheme as SSX3-Recomp's `docs/RELEASING.md`.

## Hard rules
- **Never upload game data**: no XBE, ISO, `game_files/` content or generated code (`src/recomp/gen/`) – not in the
  repo, not as a release asset. Packages hold only our binary, the launchers (`run.sh`; Windows: `run.bat`,
  `run.ps1`), READMEs and licenses
  (`scripts/package_release.sh` builds exactly that; check the listing anyway).
- Players start the game with `run.sh` (release-package mode: binary, `run.sh` and `game_files/` side by side) or,
  on Windows, `run.bat` / `run.ps1` (same defaults – keep the three in sync), so the release notes and the tests use
  those, never the bare binary.

## Preconditions
- Branch `main`, clean tree, not behind `origin/main`.
- `game_files/default.xbe` and `src/recomp/gen/recomp_funcs.h` exist. If the pipeline inputs changed since the last
  release (`config/`, `tools/` hunks of `patches/xboxrecomp/`, the xboxrecomp submodule), run
  `scripts/run_pipeline.sh` first (~25 min, background).
- Tools: docker (or podman), python3 with `pillow`; for a Windows package also `x86_64-w64-mingw32-gcc` and `zip`.

## Release notes
- Sources besides git: the `docs/notes.md` entries dated after the last release and `docs/TODO.md`.
- Write `docs/release-notes/<version>.md`, based on the previous file there. Always include the
  **no game files included** warning (own PAL/EU disc, title ID 4D530041) and the Getting started section.
- Platform status: say exactly what was tested where (GPU/driver too); never claim an untested platform.
- Full changelog link (from the second release on): `https://github.com/bertibertig/Amped2-Recomp/compare/<last>...<version>`.
- License line: GPL-3.0-or-later, third-party licenses in `licenses/`, unofficial fan project.
- Also update `README.md` (Status table, Playing a release) and `scripts/release/README.txt` (the player README in
  the package: known issues, options).

## Build

```bash
scripts/package_release.sh <version> > logs/package-<version>.log 2>&1    # background, ~10-15 min
```

Linux is built in the Ubuntu 24.04 container (glibc 2.39+), Windows with the host's MinGW (`SKIP_WINDOWS=1` leaves
it out). Checks:

```bash
tail -3 logs/package-<version>.log; cat dist/SHA256SUMS-<version>.txt
tar tvzf dist/Amped2-Recomp-<version>-linux-x86_64.tar.gz
tar tzf dist/Amped2-Recomp-<version>-linux-x86_64.tar.gz | grep -iE '\.(xbe|iso|xiso|xpr|xmv|wma|res|json)$|/gen/' && echo "GAME DATA FOUND - STOP"
objdump -T dist/Amped2-Recomp-<version>-linux-x86_64/amped2_recomp | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   # <= 2.39
objdump -p dist/Amped2-Recomp-<version>-linux-x86_64/amped2_recomp | grep NEEDED
x86_64-w64-mingw32-objdump -p dist/Amped2-Recomp-<version>-windows-x86_64/amped2_recomp.exe | grep 'DLL Name'   # XINPUT1_4, UCRT, no MinGW DLLs
```

## Test (Linux)

```bash
scripts/test_release.sh <version> > logs/test-release-<version>.log 2>&1   # background, ~3 min
cat logs/release-test-<version>/summary.txt
```

It unpacks the package like a player, runs with default graphics settings (`AMPED2_SETTINGS` points to a missing
file in the test folder, so the developer's own `~/.config/amped2-recomp/settings.ini` does not count), checks the missing-game-files message, links the disc files into its
`game_files/`, starts `./run.sh` with default settings (intro plays) and drives intro → legal → title →
Choose a Look → main menu → Career → Millicent 1 → challenge card → goals board → ride with the emulated pad
(`RECOMP_PAD_LIVE`, no window focus needed), with Vulkan frame dumps. Look at
`logs/release-test-<version>/linux_frames.png`.

Pass criteria: missing-files message; still running at the time limit (no crash); frames show the menus and **a
ride** with rider and HUD; fps lines near 55-60. Do not publish a build that does not reach a ride.

## Test (Windows under Wine and Proton, by hand for now)

Unpack the zip into `logs/release-test-<version>-win/pkg`, link the disc files into its `game_files/`, then start
`run.bat` with an own prefix – `wine cmd /c Z:\…\run.bat` (`WINEPREFIX=…`) and
`umu-run cmd.exe /c Z:\…\run.bat` (`PROTONPATH=GE-Proton`, own `WINEPREFIX`). Pass
`RECOMP_PAD_LIVE=Z:\…\pad_live.txt` and `RECOMP_FB_DUMP=Z:\…\dumps\w RECOMP_FB_WINDOW_DUMP_EVERY=120`, append
presses to the pad file (hold them: `start:800`, `a:800` – the game polls at ~5 fps) and look at the dumps. Movies
are not in the dumps (video overlay). v0.1.0 state: boot, new profile saved, menus and a ride on Millicent 1 at
~2-6 fps, black movies, no music, stray triangles. A crash or a run that no longer reaches a ride is a regression.

## Work log
Add a `docs/notes.md` entry (date, version, test results) before the release commit.

## Publish

```bash
git push origin main
git tag -a <version> -m "Amped2-Recomp <version>" && git push origin refs/tags/<version>
gh release create <version> --verify-tag --title "Amped2-Recomp <version>" \
  --notes-file docs/release-notes/<version>.md --latest \
  dist/Amped2-Recomp-<version>-linux-x86_64.tar.gz dist/Amped2-Recomp-<version>-windows-x86_64.zip \
  dist/SHA256SUMS-<version>.txt
```
