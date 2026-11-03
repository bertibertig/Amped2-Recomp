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
  **no game files included** warning (own disc, title ID 4D530041, in the package's version: PAL/EU or NTSC-U) and the Getting started section.
- Platform status: say exactly what was tested where (GPU/driver too); never claim an untested platform.
- Full changelog link (from the second release on): `https://github.com/bertibertig/Amped2-Recomp/compare/<last>...<version>`.
- License line: GPL-3.0-or-later, third-party licenses in `licenses/`, unofficial fan project.
- Also update `README.md` (Status table, Playing a release) and `scripts/release/README.txt` (the player README in
  the package: known issues, options).

## Build

```bash
scripts/package_release.sh <version> > logs/package-<version>.log 2>&1    # background, ~10-15 min
```

`<disc>` is `pal` or `ntsc-u`, the disc the generated code was made from (a package runs only with that disc; for
both, run the pipeline and the packaging once per disc). Linux is built in the Ubuntu 24.04 container (glibc 2.39+),
Windows with the host's MinGW (`SKIP_WINDOWS=1` leaves it out). Checks:

```bash
tail -3 logs/package-<version>.log; cat dist/SHA256SUMS-<version>.txt
tar tvzf dist/Amped2-Recomp-<version>-<disc>-linux-x86_64.tar.gz
tar tzf dist/Amped2-Recomp-<version>-<disc>-linux-x86_64.tar.gz | grep -iE '\.(xbe|iso|xiso|xpr|xmv|wma|res|json)$|/gen/' && echo "GAME DATA FOUND - STOP"
objdump -T dist/Amped2-Recomp-<version>-<disc>-linux-x86_64/amped2_recomp | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   # <= 2.39
objdump -p dist/Amped2-Recomp-<version>-<disc>-linux-x86_64/amped2_recomp | grep NEEDED
x86_64-w64-mingw32-objdump -p dist/Amped2-Recomp-<version>-<disc>-windows-x86_64/amped2_recomp.exe | grep 'DLL Name'   # XINPUT1_4, UCRT, no MinGW DLLs
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

## Test (Windows, by hand for now)

**Native Windows 10/11 first** – the package's default renderer is Vulkan (needs a current GPU driver). Unpack the
zip into a fresh folder, put the disc files into its `game_files/` (or set `game_dir` in `settings.ini`), and start
`run.bat`, once also `run.ps1` (right-click → *Run with PowerShell*) and once the exe by double-click. Play the
default flow: intro and legal screen skipped with Start → title → main menu → Career → Millicent 1 → a ride; create
a boarder, quit, start again and load it (no duplicate boarders, deleting one leaves the others intact); the
soundtrack list shows songs and the music plays normally. `run.log` next to the exe has a `[VK] FPS` line every
5 s: menus and rides near 60 fps at the original frame rate, unlocked well above it (README, Status). A crash, a run
that no longer reaches a ride, a boarder that does not load, or missing or distorted music is a regression.

**Wine / Proton (optional, from Linux):** unpack the zip into `logs/release-test-<version>-win/pkg`, link the disc
files into its `game_files/`, then start `run.bat` with an own prefix – `wine cmd /c Z:\…\run.bat`
(`WINEPREFIX=…`) and `umu-run cmd.exe /c Z:\…\run.bat` (`PROTONPATH=GE-Proton`, own `WINEPREFIX`); pass
`RECOMP_PAD_LIVE=Z:\…\pad_live.txt` and append presses to the pad file (hold them: `start:800`, `a:800`). Not
tested with the Vulkan renderer yet; v0.1.0 (software renderer) reached a ride at ~2-6 fps with black movies and no
music.

## Work log
Add a `docs/notes.md` entry (date, version, test results) before the release commit.

## Publish

```bash
git push origin main
git tag -a <version> -m "Amped2-Recomp <version>" && git push origin refs/tags/<version>
gh release create <version> --verify-tag --title "Amped2-Recomp <version>" \
  --notes-file docs/release-notes/<version>.md --latest \
  dist/Amped2-Recomp-<version>-<disc>-linux-x86_64.tar.gz dist/Amped2-Recomp-<version>-<disc>-windows-x86_64.zip \
  dist/SHA256SUMS-<version>.txt
```
