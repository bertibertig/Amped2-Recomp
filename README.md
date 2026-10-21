# Amped2-Recomp

[![CI](https://github.com/bertibertig/Amped2-Recomp/actions/workflows/ci.yml/badge.svg)](https://github.com/bertibertig/Amped2-Recomp/actions/workflows/ci.yml)

Experimental **static recompilation of Amped 2 (original Xbox, 2003)** into a native x86-64 PC program,
based on [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) (Git submodule under `xboxrecomp/`).
The game's own code is translated to C once and compiled natively; the xboxrecomp runtime and our own
code replace the Xbox kernel, GPU, audio and input. No emulator.

> This repo contains **no game code and no game data**. You need your own, legally obtained
> Amped 2 Xbox disc. The C code is generated locally from your `default.xbe` and never committed.

It follows the same route as SSX3-Recomp (our SSX 3 port with the same toolkit): Linux first
(Steam Deck), Windows next, macOS later.

## Status

| Milestone | |
|---|---|
| Repo, toolkit (xboxrecomp v0.14.0 + our runtime patch), scripts | ✅ |
| Game data in `game_files/` (PAL, title ID 0x4D530041, XDK 5558) | ✅ |
| Pipeline: 14,365 functions lifted, 0 translation errors, 1.96 M lines of C | ✅ |
| Host and input ported from SSX3-Recomp, Linux build (CPU renderer, multi-threaded) | ✅ |
| Boot: XAPI (media check bypassed), D3D, XACT/WMA audio, XNet offline, splash screen | ✅ |
| Movies through the NV2A video overlay, real-time (logos, intro) | ✅ |
| Legal screen, title, main menu, career menu, level select | ✅ |
| Vulkan renderer: 60 fps in the menus (CPU renderer ~6 fps, kept as the reference) | ✅ |
| 3D rider model in the menus and on the slope (matches the Xbox) | ✅ |
| Riding (Millicent 1): HUD, challenge cards, goals board, ~55 fps on Vulkan | ✅ |
| Graphics settings: resolution 1x-5x, 4:3 / 16:9 / 16:10 / 21:9, unlocked frame rate, window modes (TUI) | ✅ |
| Rider shadow, steady 60 fps | ⬜ |
| Music without dropouts (DirectSound DPC race fixed), proper stereo downmix | ✅ |
| Releases: [v0.1.0, v0.2.0](https://github.com/bertibertig/Amped2-Recomp/releases) (Linux x86-64, Windows preview) | ✅ |
| Windows build (MinGW): boots, menus and a ride under Wine/Proton, software renderer only (~2-6 fps) | 🟡 |

Plan and open items: [docs/TODO.md](docs/TODO.md), running log: [docs/notes.md](docs/notes.md).

## Playing a release

1. Download the package for your system from the [releases page](https://github.com/bertibertig/Amped2-Recomp/releases)
   and unpack it.
2. Put **your own** game files into its `game_files/` folder – see [Preparing the game files](#preparing-the-game-files).
3. Start it with `./run.sh` (Linux; `./run.sh --settings` for the graphics settings) or on Windows by
   double-clicking `run.bat` or with `run.ps1` (right-click → *Run with PowerShell*, or
   `powershell -ExecutionPolicy Bypass -File .\run.ps1`); on Windows the console output goes to `run.log`.

Needs glibc 2.39 or newer (Ubuntu 24.04+, Fedora 40+, Arch, SteamOS 3.6+), SDL2 and a Vulkan driver
(`sudo apt install libsdl2-2.0-0 libvulkan1 mesa-vulkan-drivers`).

**Windows (10/11) is experimental and not playable yet**: software renderer only (about 4-6 fps in the menus, 2 fps
on the slope), no graphics settings yet (`--settings` does not work there), so far only tested under Wine and Proton, where the
movies stay black and the music may be missing.

## Preparing the game files

The game data always comes from **your own Amped 2 disc**; nothing of it is in this repository or in a release.
Supported is the **PAL/EU version** (title ID 4D530041, `default.xbe` SHA-1
`38e63cd016bf084d999e0a911933a52191bfc378`); other versions are not tested.

1. **Make a disc image** (`.iso`) of your Amped 2 Xbox disc, e.g. on a modded Xbox or with an Xbox-compatible DVD
   drive. Both a full disc image (Redump style) and a game-partition XISO work.
2. **Extract the image** with [extract-xiso](https://github.com/XboxDev/extract-xiso) (AUR `extract-xiso-git`) or
   [xdvdfs](https://github.com/antangelo/xdvdfs) (`cargo install xdvdfs-cli`):

   ```bash
   extract-xiso -x "Amped 2.iso" -d game_files
   ```

   or `xdvdfs unpack "Amped 2.iso" game_files`. Already have an extracted disc folder? Copy its contents instead.
3. **Put it in the right place**: a folder named `game_files`
   - **release package**: next to `run.sh` / `run.bat` / `run.ps1` in the unpacked package folder;
   - **build from source**: in the repository root (`scripts/extract_iso.sh <image.iso | folder>` does steps 2–3).
4. **Check the layout**: `default.xbe` must sit directly in `game_files/`, not in a subfolder (no
   `game_files/Amped 2/default.xbe`). The whole disc is about 4 GB; keep every file and folder as extracted (the
   letter case of the names does not matter).

```text
Amped2-Recomp-v0.2.0-linux-x86_64/
├── amped2_recomp
├── run.sh
└── game_files/
    ├── default.xbe
    ├── Audio/
    ├── Levels/
    ├── Movies/
    └── … (the rest of the disc)
```

Without `game_files/default.xbe`, `./run.sh` (`run.bat`, `run.ps1`) stops with a message telling you where the files belong.
Saves and the game's cache go to `~/.local/share/xboxrecomp` (Windows: `%LOCALAPPDATA%\xboxrecomp`), not into
`game_files/`.

## Quick start (Linux)

```bash
git clone --recurse-submodules <this-repo>        # or locally: git submodule update --init --recursive (xboxrecomp, Vulkan-Headers, volk)
cd Amped2-Recomp

scripts/setup.sh                                   # Python venv + capstone, tool check
bash xboxrecomp/tools/linux/install_deps.sh        # SDL2, libepoxy, OpenSSL, ffmpeg (sudo); plus a Vulkan driver and glslc

scripts/extract_iso.sh "/path/to/Amped 2 (Xbox).iso"   # or an already extracted disc folder -> game_files/
scripts/run_pipeline.sh                            # parse, disasm, func_id, abi, recomp -> src/recomp/gen/
scripts/doctor.sh                                  # bring-up report
```

Without the build dependencies on the host: `scripts/docker.sh scripts/run_pipeline.sh` and
`scripts/docker.sh scripts/build.sh` do the same in an Ubuntu 24.04 container (docker or podman), the binary goes to
`build-docker/` and `scripts/run.sh` picks it up. Release packages: [docs/RELEASING.md](docs/RELEASING.md).

`scripts/run.sh` starts the game (Vulkan renderer, logos skipped; `AMPED2_RENDERER=cpu` for the CPU reference). Useful switches: `AMPED2_SKIP_INTRO=1` (skip the
intro movie too), `AMPED2_SKIP_LOGOS=0` (show the MGS/XSN logos). While it runs, `scripts/press.sh start`
(or `a`, `down`, `a:400`, `lt+rt` …) sends controller input; the keyboard and game controllers work as well
(see Controls).

## Graphics settings

`scripts/run.sh --settings` (in a release package `./run.sh --settings`) opens the settings screen in the terminal
before the game starts. Arrow keys move and change values, Tab switches between the categories and the options,
Enter saves and starts, S saves, Q quits. The choices are saved in `~/.config/amped2-recomp/settings.ini`
(Windows: `%APPDATA%\amped2-recomp\settings.ini`) and used on every later start.

| Category | Options |
|---|---|
| Resolution Scaling | Internal resolution 1x-5x (640x480 up to 3200x2400 at 4:3) |
| Aspect Ratio | 4:3 (original), 16:9, 16:10, 21:9. Rides become wider (Hor+: more of the slope at the sides, the HUD stays 4:3 in the middle); the menus and movies stay 4:3 |
| Framerate | Original (60 fps) or unlocked, with a limit: the monitor's refresh rate (vsync), 60-360 fps or none. The game runs on its own clock, so gameplay speed does not change |
| Display | Windowed, fullscreen or borderless fullscreen, and the monitor to use. Alt+Enter or F11 switch in the game |

How high an unlocked frame rate gets depends on the CPU: on a ride it is about 90-120 fps for now (menus several
hundred), the guest code and the NV2A thread are the limit, not the GPU. One run can override the file with
environment variables, e.g. `AMPED2_RESOLUTION_SCALE=3 AMPED2_ASPECT_RATIO=21:9 scripts/run.sh`
(`AMPED2_FRAMERATE`, `AMPED2_FRAME_LIMIT`, `AMPED2_WINDOW_MODE`, `AMPED2_DISPLAY`; `AMPED2_SETTINGS=<file>` uses
another file). The Vulkan renderer only; the CPU renderer and the Windows build ignore resolution and aspect ratio.

## Controls

What the buttons do in Amped 2 (the picture on the level loading screen): left stick = turn / spin / flip / butter,
A = accelerate (release to jump), B = jib / rail slide, Y = lip trick, right stick = grabs, Back = reset boarder,
Start = pause. In the menus A or Start selects, B goes back. Game Options has vibration and a second button layout.

**Keyboard** (pad 1, on by default with `RECOMP_KEYBOARD=1`; keys only count while the game window has the focus):

| Key | Xbox | In the game |
|---|---|---|
| W A S D (or keypad 8 4 2 6) | Left stick | Turn, spin, flip, butter |
| Space (or Z) | A | Accelerate / jump, select |
| O (or X) | B | Jib / rail slide, back |
| U (or V) | Y | Lip trick |
| C | X | |
| I J K L | Right stick | Grabs |
| Arrows | D-pad | Menus |
| Enter or Escape | Start | Pause, select |
| Backspace or Tab | Back | Reset boarder |
| 1 / 3 | Left / right trigger | |
| Q / E | White / Black | |
| Shift / Ctrl | Left / right stick click | |

Space, U, O, Escape and Tab are Amped 2's own additions (`src/input/amped2_keys.c`, by key position, so the same
place on QWERTZ/AZERTY); the rest is the runtime's map for every title (`xboxrecomp/src/input/keyboard.c`, by key
label: on QWERTZ the Z key sits where QWERTY has Y).

**Controllers**: any XInput-style pad (Xbox 360 / One / Series, 8BitDo in X-input mode, Steam Deck through Steam
Input – not yet tested on a Deck) and anything else SDL2 knows as a game controller (Windows: XInput). Pads can be
plugged in or switched on at any time; each takes the lowest free Xbox port (`PAD: port 1 <- "…"` in the log). The
keyboard keeps working on port 1 alongside a pad.

| Your pad | Xbox (Controller S) |
|---|---|
| A / B / X / Y | A / B / X / Y |
| LB / RB | White / Black |
| LT / RT | Left / right trigger (analog) |
| Start (Menu) / Back (View) | Start / Back |
| Sticks, stick clicks, D-pad | Same |

- Rumble: the game's vibration goes to the pad's motors (SDL rumble / XInput vibration).
- `AMPED2_PAD_TRACE=1` prints every button and rumble change per port – handy to check a new controller.
- `config/gamecontrollerdb.txt` (or `AMPED2_PAD_DB=<file>`) adds SDL mappings for unknown pads
  ([SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) format).
- `scripts/test_pad.sh` checks hotplug, the map and rumble with a virtual pad, no hardware needed.

Code: `src/input/host_pad.c` (same as SSX3-Recomp's `ssx3_pad.c`; replaces the runtime's `xinput_device.c`).

Tests and CI: `scripts/ci.sh` builds with the host tests on and runs them with ctest (settings file, virtual pad);
no game data needed. Without generated code it is a host-only build: the patched runtime and all of our sources
compile, no game executable is linked. GitHub Actions (`.github/workflows/ci.yml`) runs it on Linux (GCC, Vulkan) and
Windows (MSYS2 MinGW-w64) for every push to `main` and every pull request.

Optional: `scripts/fetch_references.sh` (reference ports into `reference/`), `scripts/ghidra.sh`
(Ghidra names for the generated code, needs Ghidra under `/opt/ghidra` or `GHIDRA_HOME`).

## Working with Claude Code

Start `claude` in the repo root: [CLAUDE.md](CLAUDE.md) explains the goal, layout, workflow and rules.

## References

- xboxrecomp: https://github.com/sp00nznet/xboxrecomp (MIT)
- SSX3-Recomp (our SSX 3 port): https://github.com/bertibertig/SSX3-Recomp
- SSX3-recomp by Koen Hendriks and Corné Dorrestijn: https://github.com/koenhendriks/SSX3-recomp (GPL-2.0-or-later, findings only)
- SSX Tricky PC: https://github.com/MatiasRiveraC/SSX-Tricky-PC · OpenTricky: https://github.com/GiZcesi/OpenTricky (GPL-3.0-only)
- [xemu](https://xemu.app) as the behaviour reference

## Legal

Unofficial fan project, not affiliated with or endorsed by Microsoft. Amped and Xbox are trademarks of
Microsoft. The repository holds only original code, tooling and notes.

## License

This project's own code is licensed under the **GNU General Public License v3.0 or later** – see
[LICENSE](LICENSE). The xboxrecomp submodule (MIT, LGPL-2.1 parts) and the other third-party code (volk: MIT,
Vulkan-Headers: Apache-2.0/MIT) keep their own licenses.
