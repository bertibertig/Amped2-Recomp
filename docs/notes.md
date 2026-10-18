# Amped2-Recomp work log

## Game version
_(fill in from `scripts/extract_iso.sh` and `scripts/run_pipeline.sh parse`)_

| Field | Value |
|---|---|
| Region / version / build date | |
| Languages | |
| default.xbe SHA-1 | |
| Title ID | |
| Base address / image size | |
| Entry point / kernel thunk / TLS index | |
| .text size / functions | |
| XDK version (per library) | |
| Kernel imports | |
| XDK sections swept by disasm | |

## Sections (xbe_parser)
| Name | VA | VSize | Flags |
|---|---|---|---|

## Checklist
- [ ] `scripts/setup.sh` run (venv, capstone); Linux deps (`xboxrecomp/tools/linux/install_deps.sh`)
- [ ] Disc copied/extracted to `game_files/`, `default.xbe` present, SHA-1 recorded
- [ ] Is `default.xbe` an original or patched (region/media flags, softmod patches)? Compare with a clean dump if possible
- [ ] `parse` run, values filled in above
- [ ] disasm / names / func_id / **abi_analysis** / recomp successful, `scripts/doctor.sh` read
- [ ] Host, CMake, build/run scripts ported from SSX3-Recomp; first build compiles
- [ ] First start: logos / intro / front end / riding

## Carried over from SSX3-Recomp
Findings from the SSX 3 bring-up (2026-10-17/18, same toolkit commit v0.14.0, XDK 5558) that are expected to apply
here too. They are toolkit/XDK-level, not game-level: **re-verify each one** for Amped 2, addresses do not carry over.

Pipeline
- disasm expects the parse output as `<xbe-stem>_analysis.json` next to the XBE (`default_analysis.json`) – the script does that.
- `--text-only` alone misses the XDK library sections (D3D, D3DX, XGRPH, DSOUND, XPP …): their functions get cut off
  (SSX 3: `Direct3D_CreateDevice` failed silently, 231 → 8 unresolved call targets after the fix). `run_pipeline.sh`
  passes every preloaded executable non-dot section except DOLBY (DSP code) as `--extra-sections`; override with `XDK_SECTIONS=`.
- `--split 250` for recomp (1000 runs out of RAM while compiling). `PYTHONUNBUFFERED=1`, or redirected logs stay empty.
- `abi_analysis` must never be skipped.
- Ghidra names: `scripts/ghidra.sh` (Linux replacement for the Windows-only `run_ghidra.sh`; installs the Jython
  extension for Ghidra 12, heap 8G). `run_pipeline.sh` applies the names after every disasm once the export exists.
  Read-only queries: `scripts/ghidra.sh classify|xrefs|decomp <VA…>`. MinGW `math.h` old names (`fpclass`, `chgsign` …)
  collide with Ghidra names – fixed in the lifter by the runtime patch.

Seeds (`config/seed_functions.json`, generated with `tools.seed_from_log` from a runtime log, then verified in the disassembly)
- Thread start routines (`PsCreateSystemThreadEx` StartContext1), C++ static initialisers called through `_initterm`
  (`mov [global], imm; ret`), and **XPP class-driver callbacks** in the XPP function-pointer tables (they follow a `ret`,
  only a static scan of the tables finds all of them – otherwise USB pads stop working after `SET_CONFIGURATION`).
- A NULL-function-pointer endless loop (hundreds of millions of ICALLs to 0) meant initialisers had not run → seeds.

Host / runtime
- Template issues: `recomp_dispatch_init` undeclared (GCC ≥ 14 error), `_GNU_SOURCE` for dladdr/REG_RIP,
  `-fno-strict-aliasing`, ENABLE_EXPORTS, MinGW `-Wa,-mbig-obj`.
- Load the XBE and the memory layout **before** opening any SDL/GL/Vulkan window: Mesa can otherwise occupy the
  address of the contiguous-memory window (guest offset + 0x80000000) → layout "failed (error 8)".
- The template never calls `xbox_OhciInit()` (no USB → no input) nor `xbox_InputInit()` (no host pads). SSX3-Recomp
  replaces the runtime's `xinput_device.c` with its own pad code (hotplug, correct White/Black mapping).
- `RECOMP_TITLE_KEVENTS=1` when the title builds KEVENTs itself (SSX 3 did: SIGSEGV in `SetEvent` on POSIX without it).
- D3D is linked statically into XDK titles → the route is the **pushbuffer executor** (`RECOMP_PB_EXEC=1`), not the D3D8 HLE.
  Early on, `D3D_BlockOnFence` hangs because nobody executes the pushbuffer: `xbox_Nv2aMirrorFence(<device ptr>, 0x2C, 0x30)`
  with the D3D global device pointer of *this* XBE (SSX 3: 0x0024B0D8; find it via `Direct3D_CreateDevice`).
- `RECOMP_VBLANK=1`, else `D3DDevice_BlockUntilVerticalBlank` hangs. Runtime patch keeps the vblank pending until D3D
  acknowledges it (otherwise boot took 0.7 s … >70 s at random). The vblank clock runs at ~50 Hz by accident – open issue.
- `RECOMP_AC97_READY=1`, else XDK DirectSound init fails (crash in `CreateSoundBuffer` on a 0xFFFFFFFF pointer).
  On Linux x86-64 this needs the MMIO decoder for ucontext (runtime patch; APU, AC'97 bus-master trap, OHCI).
- POSIX path lookup must be case-insensitive (runtime patch; SSX 3 asked for `intro_dj.mad`, disc had `Intro_DJ.mad`).

Graphics / performance
- Disabled vertex attributes must return the last `SET_VERTEX_DATA*` value (NV2A behaviour), not (0,0,0,1) – SSX 3's
  2D/FMV vertex program read its matrix from them (runtime patch).
- Texture-stage pitch only for linear formats (runtime patch) – a stale video pitch broke textures after the intro.
- CPU rasteriser is far too slow for 60 fps (SSX 3: ~15 fps with a POSIX thread pool) → Vulkan back end from
  SSX3-Recomp (`src/render/`, interface `Nv2aBatchExt` in the runtime patch). Hash raw guest texture bytes and decode
  only on change; per-batch post-transform vertex cache + pre-decoded vertex programs (runtime patch).
- Profile with `perf record -e cycles:u -g --user-callchains`; gdb SIGSTOP samples only show wait states and misled us.
  Measure fps with wall-clock time (`clock()` is CPU time on POSIX).

Tools that helped: `RECOMP_KERNEL_LOG_BUDGET`, `RECOMP_WATCHDOG_SECS`, `RECOMP_FRAME_TRACE`, `RECOMP_VSH_TRACE`,
frame dumps (`RECOMP_FB_DUMP` / the Vulkan dump), gdb breakpoints on generated `sub_*` symbols.

## Amped 2 specifics to watch (unverified expectations)
- **Xbox Live / XSN Sports**: the title links XOnline/XNet (likely own sections). Online services are gone – expect the
  network stack to need stubs or a forced "offline" path; it must not block boot.
- **Music**: Amped 2 streams a large licensed indie soundtrack and supports custom soundtracks from the Xbox HDD
  (`XFindFirstSoundtrack` & co.) – check the audio format (WMA/XMA/ADPCM) and whether a decoder lives in the XBE.
- **Movies**: check the FMV format (Bink? WMV?) once the data is extracted.
- Saves/profiles on the HDD (`UDATA`/`TDATA`), as with SSX 3.

## Log
_(date – what was done, what was found, next step)_

- 2026-10-18 – Repo created: xboxrecomp v0.14.0 submodule (eb0aa1d, same as SSX3-Recomp), runtime patch 0001 from
  SSX3-Recomp (applies cleanly), scripts adapted (`extract_iso.sh` accepts an image or an extracted folder; XDK sections
  derived from the parse output instead of SSX 3's fixed list). Next step: copy the game data, run `parse`.
