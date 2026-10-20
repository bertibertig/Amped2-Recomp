# Amped2-Recomp work log

## Game version
| Field | Value |
|---|---|
| Title / region | "Amped 2", region "Rest of World" (PAL/EU), media DVD_X2, disc 0, cert. version 2 |
| Build | 2003-10-03 05:49:49 UTC, certificate 2003-10-07; path `c:\depot\yeti\dailybuild\Temp\Release\YetiGame\Default.exe` (engine code name "Yeti") |
| Languages | English, French, German, Japanese (`Audio/XACTData/*`, `splash_{eng,fre,ger,jan}.xpr`) |
| default.xbe SHA-1 | `38e63cd016bf084d999e0a911933a52191bfc378` (3,710,976 B). Certificate unmodified (region 0x4, media flags 0x2 – unlike the softmod-patched SSX 3 XBE), so most likely a clean dump |
| Title ID | 0x4D530041 ("MS" 0x41) |
| Base address / image size | 0x00010000 / 0x003E9520 (4,101,408) |
| Entry point / kernel thunk / TLS index | 0x00254899 / 0x00336DE0 / 0x00388C34 (TLS zero fill 272 B, no TLS data) |
| Stack commit / heap | 0x10000 / reserve 0x100000, commit 0x1000 |
| .text size | 2,611,764 B |
| XDK version | 5558 for all libraries – the same as SSX 3, so the XDK-level findings should apply |
| Libraries | XAPILIB, XBOXKRNL, LIBCMT, DSOUND, XONLINES, XVOICE, XMV, D3D8LTCG, XGRAPHCL, XACTENLT |
| Kernel imports | 158 |
| .text / functions | 14,365 functions (14,488 translated incl. aliases), 1.96 M lines of C |
| XDK sections swept by disasm | DSOUND, XONLINE, XNET, XMV, WMADEC, XACTENG, XGRPH, D3D, XPP (each checked: starts with x86 code) |

Note: D3D8**LTCG** – the D3D library is built with link-time code generation, so its functions may be inlined into
game code and their shapes differ from SSX 3's (plain D3D8). XGRAPHCL/XACTENLT are the "LT" variants as well.

Other XBEs on the disc (not ported, same title ID):
| File | What | Build |
|---|---|---|
| Boom.xbe | Second game executable, `c:\depot\yeti\dev\source\Boom\Release\Boom.exe` – role unknown (relaunched by default.xbe?) | 2003-09-17 |
| cdx.xbe | Microsoft "CDX" demo/extras launcher (`cdxmedia/`: videos, demos, music) | 2003-06-01 |
| dashupdate.xbe, update.xbe | Xbox Live dashboard / title updater | 2003-06-01 |

Data: XACT audio (`.xwb`/`.xsb`), 317 `.wma` music tracks (decoded by WMADEC in the XBE), 60 `.xmv` movies
(XMV player in the XBE), `.xpr` resources, `.res`/`.gfx`/`.tex` game formats.

## Sections (xbe_parser)
| Name | VA | VSize | Flags |
|---|---|---|---|
| .text | 0x00011000 | 2,611,764 | PRE, X, HRO |
| DSOUND | 0x0028EA40 | 55,276 | W, PRE, X |
| XONLINE | 0x0029C240 | 127,700 | PRE, X, TRO |
| XNET | 0x002BB520 | 73,704 | PRE, X, HRO |
| XMV | 0x002CD520 | 163,156 | W, PRE, X |
| WMADEC | 0x002F5280 | 102,524 | W, PRE, X |
| XACTENG | 0x0030E300 | 43,200 | W, PRE, X |
| XGRPH | 0x00318BC0 | 5,076 | W, PRE, X |
| D3D | 0x00319FA0 | 82,284 | W, PRE, X |
| XPP | 0x0032E120 | 36,008 | W, PRE, X |
| .rdata | 0x00336DE0 | 236,376 | PRE, X |
| .data | 0x00370940 | 512,916 (raw 99,032 → BSS part) | W, PRE, X |
| DOLBY | 0x003EDCE0 | 29,056 | PRE, X, TRO – DSP code, not x86 |
| XON_RD | 0x003F4E60 | 6,864 | PRE, X, HRO – XOnline data (Xbox Live host names), not code |
| .data1 | 0x003F6940 | 224 | W, PRE, X |
| $$XTIMAGE / .XTLID | 0x003F6A20 / 0x003F8A40 | | resources |

## Checklist
- [ ] `scripts/setup.sh` run (venv, capstone); Linux deps (`xboxrecomp/tools/linux/install_deps.sh`)
- [x] Disc copied/extracted to `game_files/`, `default.xbe` present, SHA-1 recorded
- [x] `default.xbe` looks original (certificate region/media flags untouched; code not compared against a second dump)
- [x] `parse` run, values filled in above
- [x] disasm / names / func_id / **abi_analysis** / recomp successful, `scripts/doctor.sh` read
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
- 2026-10-18 – **parse + pipeline** (logs `logs/01_parse.log` … `07_doctor.log`, gitignored). Values above.
  - `XON_RD` is listed with the X flag but holds XOnline data (Xbox Live host-name format strings) → added to the
    not-code list in `run_pipeline.sh`. The other 9 non-dot sections all start with plausible x86 (checked with capstone).
  - Careful: piping `run_pipeline.sh` into `head` kills the parser with SIGPIPE before it writes the JSON.
  - disasm (with the 9 XDK sections): 1,111,847 instructions (.text 867,835; XONLINE 43,921; XMV 65,783; WMADEC 38,442;
    D3D 21,761; DSOUND 18,956; XACTENG 15,596; XNET 25,583; XPP 12,051; XGRPH 1,919), 214,116 xrefs (2,161 kernel calls),
    **14,365 functions**. Full pipeline 11:15 min.
  - func_id: 0 RenderWare (own "Yeti" engine), 11 CRT, 7,640 game_vtable, 929 game_network, 809 game_audio, 386 game_ui,
    290 game_render, 7,178 unknown. abi_analysis: 14,365 entries, 2,569 thiscall.
  - recomp (`--split 250`): 14,488/14,488 translated, **0 failed**, 1,955,105 lines, 58 files, 7 unresolved call targets,
    47 unimplemented instructions.
  - The 7 unresolved targets are all decode artefacts: 0xE807B7BF/0xE82FE861/0x010FE869 lie outside the image,
    0x00066EDF/0x00087106 are in data decoded as code (`sub_00066890`/`sub_00087060`, full of in/out), 0x00227A85 is
    mid-instruction, 0x00258FF6 the `pop esi; ret 4` tail of a function – all reached only via `jo`/`jp`/`js` from garbage.
  - Unimplemented instructions (addresses in `xboxrecomp/tools/recomp/output/summary.json`, decimal there): most are in
    data decoded as code (in/out/bound/aad/aam/les/salc/verw/ltr in low .text). Real ones to watch during bring-up:
    `cpuid` in `sub_0026A2F0` (CRT CPU feature check – an override may be needed), cli/sti in `sub_002563B8` (XAPI) and
    `sub_003349E4` (XPP), `sub_00258E14` (cli/sgdt/ljmp – reboot path, irrelevant), `wbinvd` in `sub_0031F8C0` (D3D,
    no-op is fine), pushfd/popfd in `sub_002BE3A5` (XNET).
  - Ghidra (`scripts/ghidra.sh`, 5:49 min): 10,321 functions, 108,079 symbols; merge: 2,028 names (190 FidDb/library,
    1,838 symbol), **190 applied** (CRT: memmove, sprintf, rand …), pipeline re-run from `names` (2:41 min), same counts.
  - doctor: only item "47 unimplemented instructions". Next step: port host/CMake/scripts from SSX3-Recomp, first build.
- 2026-10-18 – **First build and starts (CPU renderer).** Host ported from SSX3-Recomp: `CMakeLists.txt` (target
  `amped2_recomp`, no Vulkan yet), `src/main.c` (entry 0x00254899; SSX 3's fence mirror and boot-movie mask removed),
  `src/recomp_manual.c`, `src/input/host_pad.c` (= SSX3's pad code, `AMPED2_PAD_TRACE`/`AMPED2_PAD_DB`),
  `scripts/build.sh`, `run.sh` (defaults: KEVENTS, VBLANK, AC97_READY, PB_EXEC, FB_WINDOW, USB, KEYBOARD),
  `build_win.sh`, `run_win.sh`.
  - Build 1 failed: `_fa`/`_fas` undeclared in functions whose only flag setter is `lock xadd` (InterlockedIncrement,
    e.g. `sub_000135D0`): capstone calls it "lock xadd", the translator's declaration check only knew "xadd".
    Fixed in `tools/recomp/translator.py` (runtime patch). Then: build 1:13 min (12 jobs), binary 26 MB.
  - Start 1: SIGSEGV before `boot_and_run` printed anything, inside SDL's D-Bus init. Ghidra/FidDb named seven CRT
    functions `read`, `write`, `close`, `lseek`, `isatty`, `tzset`, `fsopen`; with `-rdynamic` the executable's exports
    interpose libc for every caller, so D-Bus called the guest's `read`. Fixed by reserving POSIX/stdio/time/ctype/wchar
    names in `tools/recomp/lifter.py` (`_FUNC_RESERVED_IDENT`, runtime patch) – they now get the `_<addr>` suffix.
    Check after every rename pass: `nm -D --defined-only build/amped2_recomp` against libc/libSDL2 exports.
  - Start 2: XAPI exits through `XLaunchNewImage(NULL)` → `HalReturnToFirmware(2)` (back to the dashboard) before main().
    `FUN_00255202` (called from 0x002553DC) is the media check: MODE SENSE page 0x3E passes (runtime), but the cert
    dword at +0x1D8 has bit 0 set, so XAPI also reads the security sectors (READ DVD STRUCTURE 0xAD, 0x664 B) and checks
    a SHA-1 over them against the certificate (`FUN_0025514B`) – impossible without a drive. SSX 3's XBE had a softmod
    patch at the same spot. Fix: hand-written `sub_00255202` in `src/recomp_manual.c` returning STATUS_SUCCESS;
    `run_pipeline.sh` now passes `--exclude-manual src/recomp_manual.c` so hand-written bodies replace generated ones
    for direct calls too. `recomp_manual.c` includes `recomp/gen/recomp_types.h` (TLS registers) instead of own externs.
  - Start 3: past XAPI init (TDATA/UDATA, TitleMeta/TitleImage, `z:` cache partition). Worker thread start
    0x00259939 (CRT `_threadstart`, after `int3`, SEH prolog) unknown → seeds via `tools.seed_from_log`
    (`config/seed_functions.json`: 0x00254825, 0x00259939).
  - Start 4 (with seeds, pipeline 19 min): the game copies `d:\image.res`, `guipackages\*.rui[rr]`, `gameinfo.res`,
    `levels.res` to the `z:` cache (the `.res` files are ZIP archives), creates the D3D device (contiguous 14 MB + 1.3 MB
    + 800 KB, `KeConnectInterrupt` vector 3 → ISR 0x003220C0), the executor follows the pushbuffer (DMA_PUT moves).
    Then `[ICALL] Failed to resolve 0x0031CB60` (D3D section, after a `ret 4`, referenced from the D3D pointer table at
    0x00329858) and no further frames. Seeded (3 seeds); a static scan of that table found no other missing targets.
    `[0x0032CEF8]` looks like the D3D global device pointer (put/limit at +0/+4) – the candidate for a fence mirror.
  - `wbinvd` at 0x0031FFEF (D3D) is reached – a cache flush, harmless as a no-op.
- 2026-10-18 – **D3D fence, memory, network, first picture.**
  - Start 5 (3 seeds): no ICALL gap, but no frame. gdb (with `set disable-randomization off` – without ASLR the
    run faults at guest VA 0 in `sub_00256354`, a gdb artefact): the game thread spins in `sub_0031E7E0` =
    D3D_BlockOnFence, device pointer `[0x0032CEF8]`, fence counter +0x2C, fence word pointer +0x30 – the SSX 3 layout.
    → `xbox_Nv2aMirrorFence(0x0032CEF8, 0x2C, 0x30)` in `src/main.c`.
  - Start 7: shaders, DSP image (`audio\DSP\yetidsp.bin`), WMA music, XACT banks load; then the contiguous arena runs
    out and the game launches **`Boom.xbe`** (`XLaunchNewImage("\Device\CdRom0;Boom.xbe")`) – Boom is its error
    screen. `FUN_00139640` probes the largest free contiguous block (60 MB down in 8 KB steps, 0x0013A181), frees it,
    takes 8 KB less, and frees that again – but the runtime only releases contiguous memory with
    **`RECOMP_HEAP_RECLAIM=1`** (`frees=0` in every `[CONTIG]` line). Now a default in `run.sh`/`run_win.sh`.
  - Start 9: XAPI/XACT/USB fine, splash screen drawn, then 150 M calls each to KeRaiseIrqlToDpcLevel/KfLowerIrql:
    perf (`perf record -e cycles:u -g --user-callchains`; plain `-g` is refused at perf_event_paranoid=2) → hot spot
    `sub_002BEE4D` (XNET). Breakpoint backtraces: game thread `sub_00026770` → `sub_001CA300` → `sub_001BE680`:
    `while (XNetGetTitleXnAddr(&x) == XNET_GET_XNADDR_PENDING);` (0x001BE997) – no NIC, so XNet never comes up.
    → hand-written `sub_002BB66E` (XNetGetTitleXnAddr stub) returns XNET_GET_XNADDR_NONE (1) with the XNADDR zeroed,
    what the original returns itself when XNet is not running (0x002BB570) – an offline console without a cable.
  - **First picture**: the Amped 2 splash (`splash_eng.xpr`, 512x512 DXT1, full-screen quad) is correct in the CPU
    rasteriser dumps (`RECOMP_FB_DUMP=logs/fb/f RECOMP_FB_DUMP_FLIPS=1`). The POSIX rasteriser pool runs
    (`[GPU] rasteriser: 12 thread(s)`).
  - **Movies use the NV2A video overlay**, not the 3D pipe: `d:\Movies\mgs.xmv` (640x480, 10.2 s, six WMA tracks at
    44.1 kHz) is decoded by the XMV section (`sub_002CE5BB` …) and handed to D3DDevice_UpdateOverlay – PVIDEO registers
    (gdb dump): BUFFER 1, OFFSET(0) 0x01F17100, SIZE_IN/OUT 640x480, FORMAT 0x00010500 (pitch 1280, YUY2). No flips
    happen meanwhile, and the runtime had no overlay at all (the aperture is plain memory). New runtime module
    `src/kernel/nv2a_overlay.c` (runtime patch): latches the pending buffer at every vblank (copies the frame, clears
    the BUFFER bit as the hardware does – GetOverlayUpdateStatus waits on it), honours STOP; the SDL window composes
    it (YUY2/UYVY → XRGB, scaling, colour key) over the shown surface. `RECOMP_OVERLAY_DUMP=<prefix>` writes every
    30th frame as BMP. The MGS logo movie now shows correctly.
  - **Open: movies run at ~1/6 speed.** The game calls UpdateOverlay every vblank (~53/s), but after 40 s only 2.65 of
    4.34 MB are read; the guest main thread mostly sits in D3DDevice_BlockUntilVerticalBlank (`sub_0031AE90`), XMV
    decode costs ~2 % CPU. Most likely XMV paces video to the DirectSound stream clock and the emulated APU/stream
    voices advance too slowly (six audio tracks; DSP is a passthrough stub; the title loads its own DSP image).
    Next: trace the XMV audio stream (packets submitted/completed per second), compare with 44.1 kHz.
  - Other notes: XPP class-driver callbacks 0x0032E3A4, 0x0032EDAB, 0x003306DF are still unresolved ICALLs (seed them
    with the next disasm run); `T:\PrevSessionInfo.lua`, `TDATA\FFFE0000\MUSIC\ST.DB` (dashboard soundtracks) and three
    SFX files are not found – expected (not on the disc / no soundtracks).
    The runtime's `nv2a_ack_thread` busy-polls (`sched_yield` + GetTickCount, ~50 % of one core), as in SSX3-Recomp.
- 2026-10-18 – **xemu reference tooling**: skill `.claude/skills/xemu/SKILL.md` + `scripts/xemu.py`. Modelled on the
  xemu skill of koenhendriks/SSX3-recomp (`reference/SSX3-recomp-koen/.claude/skills/xemu/SKILL.md`, GPL – idea only,
  code and text are our own). Separate Flatpak xemu instance (own config/HDD copy in
  `~/.var/app/app.xemu.xemu/data/amped2-ref/`, QMP 127.0.0.1:4455, muted) as an X11 client so `xdotool --window` keys
  and `import -window` captures work without focus. Findings:
  - The Flatpak renders with Mesa llvmpipe: host NVIDIA driver 615.78.08, only the 615.71.09 GL runtime installed
    (`flatpak install flathub org.freedesktop.Platform.GL.nvidia-615-78-08` fixes it). Under X11, Mesa then needs
    `__GLX_VENDOR_LIBRARY_NAME=mesa`.
  - xemu auto-binds real gamepads to free ports (adds a USB hub to the VM state) → `auto_bind = false`, otherwise
    snapshots only load with that pad plugged in. `-loadvm` on the command line fails for the same reason (the keyboard
    pad is hot-plugged later) → loadvm over the monitor after start.
  - No QMP `screendump` in xemu. savevm/loadvm ~0.2 s. Boot order in xemu (PAL): MGS logo → XSN Sports logo → intro
    movie → (Start) legal/ESRB screen → (Start) title "PRESS START TO BEGIN" → (Start) Main Menu.
  - Snapshots: intro_movie, title, main_menu, millicent1_menu, millicent1_ride.
- 2026-10-18 – **Movies at full speed, input, logo/intro skip, performance.**
  - Root cause of the slow movies (not the renderer: they were just as slow with `RECOMP_PB_EXEC` off): **DPC
    latency**. The runtime's timer thread drained the DPC queue only after a fixed `Sleep(10)` (more on Linux). XMV
    feeds DirectSound ~10 ms audio packets; DirectSound appends the next stream segments from the DPC behind the APU
    interrupt, so the stream voices ran dry (`RECOMP_APU_SSL_STATS=1`: up to 1113 starved fetches per 5 s), XMV's audio
    clock stalled and the picture with it (the intro ran at ~1/6 speed). Runtime patch: `KeInsertQueueDpc` sets an
    event the timer thread waits on (10 ms timeout as before). Now 0 starved fetches, the intro reads 27.9 MB in 61 s
    (real time), 30 new frames/s; the movie loop and vblank run at ~60 Hz instead of ~48 (the accidental 50 Hz vblank
    of SSX3-Recomp's notes came from the same Sleep).
  - Ruled out on the way (measured, not guessed): XMV decode is fast (`AMPED2_XMV_STATS=1`: ~48 calls/s, 1-3 ms each);
    DirectSound's SSL clock ticks were never lost (23.4/s) – a tick backlog was added anyway (`ssl_backlog` in the APU,
    one tick per acknowledge instead of merging into a still-pending ISTS bit); the frontend loader during the intro is
    not the bottleneck.
  - XMV in Amped 2 (`FUN_00150FF0`): asks `XGetAudioFlags` and configures 6 mix bins (5.1) for track 0 plus tracks 1
    and 2..5 by speaker setup; per iteration GetNextFrame (`0x002CDE60` via adapter `0x00150E80`), block until vblank
    while `GetOverlayUpdateStatus` says busy, `UpdateOverlay`; `this+0x24 == 1` aborts (button skip). The intro's
    stream voices are 16-bit stereo 44.1 kHz (voices 69-71, only 69 audible). Output is mixed to stereo for now;
    real 5.1 output is a TODO.
  - Overlay cost: the game re-submits the same frame every vblank; composing it every time took 33 % of all CPU.
    Now the latch compares the frame (`memcmp`) and converts YUV → XRGB once per new frame (table-driven), the window
    composes only on a new overlay frame or flip, unscaled rows are a `memcpy`. `RECOMP_OVERLAY_DUMP` now dumps every
    30th *new* frame.
  - `nv2a_ack_thread` (POSIX): yields as before while DMA_PUT moves or a vblank is pending, sleeps
    `RECOMP_ACK_IDLE_US` (default 200 µs) after ~4000 quiet rounds. D3D's vblank handler `0x00322390` spins until the
    ack thread clears PMC_INTR bit 24, hence "not while a vblank is pending".
  - **Logo/intro skip**: `FUN_00210E00` (boot state) plays MGS (`0x00211030`), XSN (`0x002110E0`, file not on the PAL
    disc) and the intro (`0x00210F60`) while `this+0x3D` (from global `0x00381CB8`) is set. Wrappers in
    `src/recomp_manual.c` (the recompiler keeps the originals as `sub_X_gen`): `AMPED2_SKIP_LOGOS=1` (default in
    `run.sh`) and `AMPED2_SKIP_INTRO=1`.
  - **Input**: `scripts/run.sh` starts the game with `RECOMP_PAD_LIVE=logs/pad_live.txt`; `scripts/press.sh start`,
    `a:400`, `down a`, `lt+rt` … append to it (the emulated USB pad presses each new line). `RECOMP_PAD_SCRIPT` for
    timed sequences. `scripts/test_pad.sh` (virtual SDL pad, from SSX3-Recomp) passes.
  - Reached: legal/ESRB screen and the title "PRESS START TO BEGIN", correct in the CPU rasteriser at ~6 fps.
    Profile: ~80 % rasteriser (sample_tex, xf_rows, nv2a_rc_eval, read_rgb, blending), the rest D3D spins waiting
    for it → the Vulkan back end is next. XPP callback `0x0032FC8B` seeded (9 seeds; needs a disasm run).
- 2026-10-18 – **Vulkan back end, 60 fps, main menu.**
  - `src/render/` and the Vulkan-Headers/volk submodules (same commits) taken over from SSX3-Recomp with `AMPED2_*`
    names; `AMPED2_RENDERER=vulkan` is the default in `run.sh` (`=cpu` keeps the CPU rasteriser as the reference).
    Details: `docs/vulkan-backend.md` → "Amped 2".
  - Video overlay in Vulkan: overlay image composed at every present; new optional `idle` callback in `Nv2aBackend`
    (runtime patch) presents new overlay frames between flips. Checked with the Mesa screenshot layer: the intro shows.
  - Frame pacing: on the GPU the title flipped ~300×/s – Amped 2 relies on FLIP_STALL waiting for the vblank. New
    `RECOMP_FLIP_VSYNC=1` (default in `run.sh`). Then ~55 fps, because the timer thread checked the vblank every
    ≤10 ms; it now waits exactly until the next vblank (16.67 ms average, resync after stalls) → **60 fps** in the menus.
  - Seed `0x0032FC8B` (XPP callback; 9 seeds, pipeline 22:47 min): live input now also arrives on the Vulkan run with a
    real pad connected (pad reports were no longer requested after that ICALL failure).
  - Reached with `AMPED2_SKIP_INTRO=1` + `scripts/press.sh start ×3, a`: legal → title → **Choose a Look → Main Menu →
    Career → RIDE! (Millicent 1)**. Vulkan frames match the CPU reference.
  - **Open: the 3D rider model in the menus** is a black silhouette with a wrongly textured head – identical in the
    CPU rasteriser, so it is the shared executor path (vertex program / texture coordinates / lighting), not Vulkan.
    Textures in use are DXT1/DXT5/A8R8G8B8 only (no palettes). Next: VSH trace of those batches, xemu comparison.
- 2026-10-18 – **Controls: keyboard and game controllers.**
  - State before: the runtime keyboard pad (`RECOMP_KEYBOARD=1`) and `src/input/host_pad.c` (SSX3's pad code) were in
    place. Checked under Xvfb with `xdotool` key events into the Vulkan window (`SDL_VIDEODRIVER=x11`,
    `AMPED2_PAD_TRACE=1`): keys reach the window (`vk_backend.c` pumps `xbox_InputSdlEvent`), the pad state and the game.
  - Reference (xemu, loading screen of Millicent 1): left stick = turn / spin / flip / butter, A = accelerate (release
    to jump), B = jib / rail slide, Y = lip trick, right stick = grabs, Back = reset boarder, Start = pause. Game Options:
    Vibration (on), Camera distance, Button layout ("B to slide on rails" or the other), Hints.
  - The runtime map has A on Z – the left hand would steer (WASD) and accelerate at once. **`src/input/amped2_keys.c`**
    adds keys on top (the runtime map stays): Space = A, U = Y, O = B, Escape = Start, Tab = Back. Read as SDL scancodes
    from `SDL_GetKeyboardState` (the runtime turns only letters/digits/a few named keys into its key state; SDL keeps
    the state per focused window and clears it on focus loss); on Windows from the runtime's VK state. Hooked in
    `host_pad.c` through `keyboard_poll()` → `game_keys_merge()` (all three keyboard paths).
  - Rumble taken over from SSX3-Recomp (its commit 7b677b7): runtime `usb_gamepad_output()` (ep2 OUT and HID
    SET_REPORT data stage) → `xbox_InputSetState()`; `host_pad.c` holds SDL rumble until the game changes it.
    `scripts/test_pad.sh` (virtual pad: hotplug, map, rumble) passes; MinGW syntax check of both input files clean.
  - Verified in the game (keyboard): Escape skips the intro → legal → Enter → main menu; Space selects Career; O goes
    back; arrows and S (left stick) move the menu cursor; Space/U/O/Escape/Tab/Z/X/C/V/1/3/Q/E/Enter/Backspace all
    show up as the right buttons in the trace. Level select → Millicent 1 → Evergreen loads and shows the challenge
    list and the Media Challenge card.
  - **Blocked: riding.** Entering the session crashes: `[ICALL] Failed to resolve VA 0x000A5A70` (×4), `0x0024F240`,
    then SIGSEGV in `sub_00241910` (called from `sub_00098870`). Needs seeds + a pipeline run; riding controls and
    rumble in a race are still unverified, as is a real pad in the menus (no one pressing its buttons).
  - Lesson: `pkill -f amped2_recomp` / `pgrep -f <pattern>` inside a Bash tool call match the tool's own shell (its
    command line contains the pattern) and kill it – use PIDs. Two sessions sharing `logs/pad_live.txt` drive each
    other's game: give a second instance its own `RECOMP_PAD_LIVE`.
- 2026-10-18 – **Rider model in the menus (partly fixed).**
  - Black rider: its base pass uses the **dot-product texture shaders** – stage program `0x74621` = stage 0 2D,
    stages 1-2 `DOT_PRODUCT`, stage 3 `DOT_STR_CUBE` (normal map → lighting cube map); the gloss pass `0x64621` ends in
    `DOT_RFLCT_SPEC`. The executor read zero for every dot mode, so the combiner output (`t3 × c0`) was black and the
    light pass (`DST_COLOR × tex1`) multiplied black. Runtime patch: `SET_DOT_RGBMAPPING` (0x1E74),
    `SET_SHADER_OTHER_STAGE_INPUT` (0x1E78) and `SET_EYE_VECTOR` (0x181C) are tracked; `rc_stage_fetch` implements
    DOT_PRODUCT, DOT_ZW, DOT_ST, DOT_STR_3D (as 2D), DOT_STR_CUBE, DOT_RFLCT_SPEC(_CONST) after xemu's psh.c (dot
    mappings sign1/2/3). **CPU path: pants, shoes and head now textured and lit correctly.** Vulkan path still draws
    them black: the shader (`nv2a.frag`) needs the same modes and the back end real cube maps (it samples face 0).
  - The light pass itself is fine: its 1×1 texture (`0x8121A680`) is white, the combiner is `tex1 × dot(tex0, luma)`.
  - **Fence**: the SSX 3 fence mirror is gone. `src/recomp_manual.c` replaces D3D_BlockOnTime (`0x0031E7E0`) as the
    toolkit's d3d8ltcg-device-context.md describes: the executor writes the real semaphore (`device+0x30`,
    `nv2a_pb_set_semaphore_target`), the wait kicks off the newest fence first (`FUN_0031E5A0(0)`) and is bounded at
    250 ms. Log: `[D3D] GPU semaphore at 0x8070A000`. Menus still run at ~50-60 fps on Vulkan.
  - `dma_resolve` (runtime patch) now resolves pages the title named through `MmGetPhysicalAddress` for a heap
    address as RAM, like the APU already does (`xbox_PhysRamNameMap`) – heap VA and contiguous physical numbers
    overlap in the runtime.
  - **Still open: the rider's upper body (jacket, arms) is missing**, and the head has no base pass. Ruled out: the
    renderer (same in CPU and Vulkan), FFP batches (none in the menus), missing/partial data (`z:\CharWad\CharWad.bdr`
    is optional – the game falls back to `MedHiResCharacter.gfx`; the `z:` cache matches the disc;
    `u:\<profile>\<profile>.res` is also missing but optional?). A method trace of a whole frame shows 37
    SET_BEGIN_END pairs and 37 drawn batches: **the game never submits those draws**, so the cause is upstream
    (game logic / data), not in the executor. Our profile is a new one ("Player1"); the xemu reference shows a saved
    profile (Sasha) – compare with a fresh profile in xemu next. ~15 of 1300 traced batches read freed memory
    (`0xFDFDFDFD`) even with the real semaphore (0x82252080, a model block loaded via zlib and freed later).
  - Note: the runtime's save root `~/.local/share/xboxrecomp` (TDATA/UDATA/Cache = `z:`) is shared with SSX3-Recomp;
    titles are separated by title ID in TDATA/UDATA, but `Cache` (`z:`) is common to both ports.
- 2026-10-18 – **Vulkan: dot-product texture modes and cube maps.**
  - `nv2a.frag` ports the CPU path's dot modes (`stage_fetch` now sees the earlier stages; `stage_dot`, `dotmap`,
    reflection with the per-stage `q` or SET_EYE_VECTOR). Cube-map stages sample real cube views (bindings 5-8).
  - Runtime patch: `Nv2aTexture.cube_stride` (bytes between faces, `tex_face_stride`; 0 for 2D) and
    `Nv2aBatchExt.eye_vector`. The back end decodes the six faces one by one through `nv2a_backend_decode_texture`.
  - Result (Career menu, Vulkan vs. CPU dumps): the rider's pants and shoes are lit and textured the same in both;
    49 fps in the menu as before. Not checked: the Vulkan validation layer (not installed here).
- 2026-10-19 – **Rider complete (upper body, gloves, head, hair) – matches xemu.**
  - xemu reference: a fresh profile ("Player1", default Choose a Look) shows a blonde rider in a red jacket, gloves and
    the same grey pants (snapshot `player1_menu`). The profile file `U:\<id>\Career.parts` lists `f_tops_jacket_bulky`,
    gloves, head, ponytail: data and profile were fine. (`z:\CharWad\CharWad.bdr` is only built when a boarder is
    created or changed – also optional, not the cause.)
  - Cause 1: the upper body, head and hair are drawn with XDK **RunPushBuffer** (`FUN_00328690`): a JUMP from the ring
    into a prebuilt pushbuffer (e.g. `0x01398880`), which JUMPs back at its end. The segment scan treated every JUMP as
    the ring's wrap and ended the segment – those draws (and everything after them up to the next PUT) were lost, which
    looked like "the game never submits them" (only the third, additive pass sat elsewhere). Runtime patch: a JUMP out
    of the ring is followed until its JUMP back (`nv2a_pb_scan.c`, `s_ring_lo/hi`, `pb_target_va`); the ring wrap scans
    up to the wrap JUMP instead of the highest PUT seen.
  - Cause 2 (the glitching after that: two poses in different colour channels, zeroed state, frames without draws):
    RunPushBuffer patches the pushbuffer's JUMP back (and fixups) per run. While the pushbuffer may still be in use it
    instead emits `NO_OPERATION` with a parameter in front of the JUMP – a **software method**; D3D's GPU interrupt handler
    (`FUN_00322830` → `FUN_003285F0`) patches it when the GPU gets there. The runtime raises no GPU interrupts, so the
    executor ran each run with a later run's return address. Runtime patch: `nv2a_pb_set_software_method_handler`
    (NV097 0x0100 with a parameter); `src/recomp_manual.c` `amped2_d3d_software_method` does the handler's work for kinds
    0x0C/0x0D/0x0E (records at `0x0032ACC0` + slot·20 or `(param >> 5) | 0x80000000`, pending count `0x0032AAAC`).
  - Performance: the rider is ~10 000 indices × 3 passes of CPU vertex programs. Vertex-cache misses are now transformed
    on the raster thread pool (`vxf_job`); the executor answers the busy bits (flush 0x100410, vblank) every 256 words
    and after every draw – D3D's flush (`0x0031EC30`) and vblank handler (`0x00322390`) spun until the end of each
    segment (58 % of CPU time, 15–40 fps). PGRAPH_STATUS (0x400700) now reads busy from a flush until the following PUT
    has executed, so D3D's idle waits (KickOff `0x0031EA90`, BlockUntilIdle) see the executor's real progress.
  - Result: main menu at a steady 60 fps / 55 draws over 60 s on Vulkan, frames match xemu; CPU renderer matches too;
    default flow (logos, intro, title, menus, Career → RIDE!) unchanged.
  - Lessons: frame trace + `RECOMP_PB_SEGLOG=1` (each RunPushBuffer's target and return); a gdb watchpoint
    (`watch -l` on mem_offset + VA) found the tail's writer. A frame whose state reads zero (pitch 0, blend eq 0) means
    data executed as commands upstream, not a shader bug.
- 2026-10-19 – **Riding: 7 → ~55 fps, HUD / goals board / rider complete (matches xemu).**
  - Test: Millicent 1 → Evergreen (Media Challenge card → "CHALLENGES" board → ride). xemu reference shots of the card,
    the board and the ride (snapshot `millicent1_menu`, `a` ×3); our side driven by `scripts/press.sh` with
    `AMPED2_VK_DUMP`. Before: 7-9 fps, no HUD, no board, rider bald / without upper body in most frames (the user saw
    them come and go).
  - **Missing HUD, board, hair, hands, upper body = vertex ring overflow.** The Vulkan back end's per-frame vertex ring
    (32 MB) ran full in the race; every draw after that was dropped for the rest of the frame (one "vertex ring full"
    line per run, easy to miss). HUD, board, the rider's RunPushBuffer meshes come late in the frame. Now a ring that
    refused allocations grows (doubling, up to 1 GB) at the next frame start (`ring_reset`, both rings); the race needs
    64 MB on the CPU path. Ruled out on the way (with new log lines that stay): RunPushBuffer returns outside the
    segment or past ring data (`[SEG] ... rest dropped` / `... bytes skipped`, never seen).
  - **Texture re-validation**: a 512x512 texture of format 0x30 (`LU_IMAGE_DEPTH_Y16_FIXED`, the rider's shadow map)
    cannot be decoded, so `texture_view` hashed its 1 MB again at every draw: ~2.9 GB/s, 70 % of the NV2A thread.
    Failed decodes are now remembered for the frame and first sights are hashed only after a successful decode; the
    hash runs four independent lanes. The 5-s `[VK]` line shows `hashed: n, MB`. 7 → 13-15 fps.
  - **Vertex programs on the GPU** (the rest: perf showed the NV2A thread at 100 %, ~40 % in the CPU interpreter for
    ~160 000 program runs a frame, ~30 % converting/clipping/copying every triangle corner; the pool ran 2.4x one
    thread – spin-waiting workers and chunked work split were tried, measured no gain, and reverted). New optional
    back-end entry `draw_program` (runtime patch, `nv2a_backend.h` `Nv2aProgramBatch`): for program batches without
    CXT_WRITE the executor only gathers the attribute spans, a rebased triangle-list index buffer and the decoded program
    (`nv2a_vsh_export`, program/constant generations). `src/render/shaders/nv2a_vsh.vert` interprets the program per
    vertex with the CPU interpreter's semantics (same fields, ops, defaults, fog unit) and outputs clip space
    (screen position × w), so the GPU clips at w > 0. Second pipeline `pipeline_vsh`, bindings 9-12 (constants,
    program, attribute formats, vertex bytes), indexed draws. FFP, screen-space and CXT_WRITE batches keep the CPU path.
    `RECOMP_VSH_CPU=1` (executor) or `AMPED2_VK_CPU_VSH=1` (back end) force the CPU transform for comparisons.
    **Result: ~55 fps on the slope** (54-58 in the 5-s log); the NV2A thread is ~60 % busy, the game thread waits in
    D3D KickOff; the rest to 60 is vsync pacing (menus were ~55 before this too).
  - Checked: card, board, HUD and rider against xemu; default flow (MGS/XSN logos and intro played, Start skips →
    legal → title → menus); CPU renderer (`AMPED2_RENDERER=cpu`) main menu with the rider; MinGW syntax check of the
    changed runtime files. Not checked: Vulkan validation layers (not installed), the Steam Deck.
  - Still different from xemu: **no rider shadow** – the shadow map is a D16 depth texture (0x30) the back end cannot
    sample yet (needs the depth surface as a texture with a compare sampler, NV097 shadow compare function).
  - Lesson: a back end that drops work on a full ring must say so loudly and grow; "comes and goes" rendering under load
    points at per-frame budgets, not at the pushbuffer walk.

- 2026-10-20 – **Release v0.1.0 (Linux x86-64).**
  - New release tooling (from SSX3-Recomp): `docker/Dockerfile` + `scripts/docker.sh` (Ubuntu 24.04 build container,
    `AMPED2_VENV`), `scripts/package_release.sh` (Linux package; `WINDOWS=1` for the MinGW one), `scripts/release/`
    (player README, `game_files/` note), `scripts/test_release.sh`, `docs/RELEASING.md`. `run.sh` also runs from an
    unpacked package (binary next to it) and stops with a message when `game_files/default.xbe` is missing.
    License: GPL-3.0-or-later (`LICENSE`), as SSX3-Recomp.
  - Package: binary, `run.sh`, READMEs, licenses – no game data. Highest symbol GLIBC_2.38; NEEDED only libSDL2,
    libc, libm (Vulkan through volk at run time).
  - `scripts/test_release.sh v0.1.0`: missing-files message ok; default flow (intro played, Start skips) → legal →
    main menu → Career → Millicent 1 → Media Challenge card → goals board → ride, rider and HUD complete, 55-60 fps,
    still running after 150 s. Tested on Garuda Linux, RTX 2080 Ti (NVIDIA driver), with the existing profile
    (`Player1` in `~/.local/share/xboxrecomp`) – a first start without a save was not part of this run.
  - Windows: `scripts/build_win.sh` compiles and links (warnings only) but was not run, so no Windows package.
  - New known issue (user report): the music stops suddenly; it comes back later, very quiet and crackling (TODO).
- 2026-10-20 – **Saving/loading checked against xboxrecomp PR 237** (POSIX opens relative to a `RootDirectory`
  handle, the SSX 3 "Unable to save" fix; not in our runtime patch).
  - Amped 2 does not hit that bug: every save-file open uses a full path (`U:\<profile>\Career.*`, `*_Progress.lua`,
    `SaveMeta.xbx`, `T:\PrevSessionInfo.lua`). Over all logs: no bare-name opens, no failed U:/T: create or
    overwrite (disp 2/3/5 all `0x00000000`), and the profile written on 2026-10-19 (overwritten in place) loads at
    boot in the v0.1.0 release test. Not exercised yet: deleting a save, a first start without any save.
  - The PR's fix is generic; worth taking into our patch anyway so both ports share the same runtime behaviour.
- 2026-10-20 – **Windows package added to v0.1.0 (experimental), tested under Wine 11.20 and GE-Proton 11-11.**
  - MinGW linked `xinput1_3.dll` (DirectX 2010 redistributable only); CMakeLists now links `xinput1_4` as
    SSX3-Recomp does. Imports: d3d11, D3DCOMPILER_47, XAudio2_8, XINPUT1_4, MFPlat/MFReadWrite, UCRT – no MinGW DLLs.
  - Under Wine the default layout fails: `tiled aperture at 0xF0000000 failed (error 487)` (host 0x1_0000_0000 is
    taken; RAM mirrors 26/27 fail too) and the game crashes writing the first intro-movie frame through 0xF1F33100.
    The runtime's opt-in `RECOMP_WIN_PLACEHOLDER=1` (VirtualAlloc2 placeholder for the whole 4 GB window) maps it and
    the alias check passes; `scripts/release/run.bat` sets it by default. Untested on real Windows.
  - Result (Wine and Proton, own prefixes, `run.bat`, pad through `RECOMP_PAD_LIVE`, holds of 800 ms – 150 ms taps
    get lost at ~5 fps): missing-files message; title; new profile (boarder select, name) saved under
    `%LOCALAPPDATA%\xboxrecomp`; main menu and Career with the complete rider; Millicent 1 → Main Street → Media
    Challenge → ride. Software renderer only: 4-6 fps menus, ~2 fps riding.
  - Seen by the user while watching: movies stay black, no music (sound effects play), and the software renderer
    draws stray triangles stretched out from the trees (probably also on Linux with `AMPED2_RENDERER=cpu`). In TODO.
  - `scripts/package_release.sh` builds the Windows zip by default now (`SKIP_WINDOWS=1`); manual Wine/Proton test
    steps in `docs/RELEASING.md`. The zip was added to the existing v0.1.0 release (tag unchanged; the build changes
    are listed in its notes under "For developers").
