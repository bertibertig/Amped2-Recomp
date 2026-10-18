# Amped2-Recomp

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
| 3D rider model in the menus | 🟡 black silhouette |
| Riding | ⬜ |

Plan and open items: [docs/TODO.md](docs/TODO.md), running log: [docs/notes.md](docs/notes.md).

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

`scripts/run.sh` starts the game (Vulkan renderer, logos skipped; `AMPED2_RENDERER=cpu` for the CPU reference). Useful switches: `AMPED2_SKIP_INTRO=1` (skip the
intro movie too), `AMPED2_SKIP_LOGOS=0` (show the MGS/XSN logos). While it runs, `scripts/press.sh start`
(or `a`, `down`, `a:400`, `lt+rt` …) sends controller input; the keyboard and game controllers work as well
(see Controls).

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

Optional: `scripts/fetch_references.sh` (reference ports into `reference/`), `scripts/ghidra.sh`
(Ghidra names for the generated code, needs Ghidra under `/opt/ghidra` or `GHIDRA_HOME`).

## Working with Claude Code

Start `claude` in the repo root: [CLAUDE.md](CLAUDE.md) explains the goal, layout, workflow and rules.

## References

- xboxrecomp: https://github.com/sp00nznet/xboxrecomp (MIT)
- SSX3-Recomp (our SSX 3 port, private): https://github.com/bertibertig/SSX3-Recomp
- SSX3-recomp by Koen Hendriks and Corné Dorrestijn: https://github.com/koenhendriks/SSX3-recomp (GPL-2.0-or-later, findings only)
- SSX Tricky PC: https://github.com/MatiasRiveraC/SSX-Tricky-PC · OpenTricky: https://github.com/GiZcesi/OpenTricky (GPL-3.0-only)
- [xemu](https://xemu.app) as the behaviour reference

## Legal

Unofficial fan project, not affiliated with or endorsed by Microsoft. Amped and Xbox are trademarks of
Microsoft. The repository holds only original code, tooling and notes.

License of our own files: still to be decided (xboxrecomp: MIT).
