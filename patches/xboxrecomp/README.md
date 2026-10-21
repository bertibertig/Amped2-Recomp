# Local patches for the xboxrecomp submodule

Taken over unchanged from SSX3-Recomp (same submodule commit, v0.14.0); game mentions below refer to SSX 3,
where each change was found. Keep both repos' copies in sync. The submodule is not committed in modified form. Our own changes live here as a patch.
`scripts/build.sh`, `build_win.sh` and `run_pipeline.sh` apply it automatically (`apply_runtime_patches` in
`scripts/_common.sh`): skipped when already applied, applied on a fresh clone / after a submodule update, a warning
when the submodule has other local edits (runtime work in progress). By hand:

```bash
git -C xboxrecomp apply ../patches/xboxrecomp/0001-linux-x86_64-mmio-traps-casefold-mingw-oldnames.patch
```

After changing the runtime, re-export the patch so the change is kept:

```bash
git -C xboxrecomp diff -- . ':!tools/linux' > patches/xboxrecomp/0001-linux-x86_64-mmio-traps-casefold-mingw-oldnames.patch
```

All of them are candidates for an upstream PR to sp00nznet/xboxrecomp.

## 0001 – Linux x86-64: trapped MMIO, case-insensitivity, MinGW old names
- `src/platform/mmio_decode.h`: x86-64 decoder no longer restricted to `_WIN32`. Context abstracted
  (`mmio_ctx_t`, `MMIO_CTX_PC`, `MMIO_CTX_EFLAGS`, `mmio_ctx_reg`): Win32 `CONTEXT` or Linux `ucontext_t.gregs[REG_*]`.
  Decoder logic unchanged.
- `src/apu/apu_mmio_hook.c`: APU MMIO hook on Linux x86-64 as well, new entry point `apu_hook_handle_mmio_posix`.
- `src/kernel/xbox_memory_layout.c`: `xbox_PosixMmioFault` and the AC'97 bus-master write trap on Linux x86-64
  (previously arm64 only) – without this, XDK DirectSound crashes with `RECOMP_AC97_READY=1` on the first APU register.
- `src/usb/ohci.c`: OHCI trap on Linux x86-64 (`OHCI_TRAP_POSIX_X64`).
- `src/kernel/kernel_path.c`: POSIX `xbox_translate_path` resolves path components case-insensitively when the
  exact path is missing (FATX/XDVDFS and Win32 are case-insensitive; SSX 3 opens `intro_dj.mad`, on the disc it is called
  `Intro_DJ.mad`).
- `tools/recomp/lifter.py`: MSVC "old names" from MinGW `math.h` (`chgsign`, `fpclass`, `finite`, `j0/j1/jn/y0/y1/yn`)
  in `_FUNC_RESERVED_IDENT` – otherwise a MinGW build breaks on Ghidra names like `fpclass`.

- `src/kernel/nv2a_pb_exec.c`: **disabled vertex attributes** deliver, as vertex-program input, the value last set via
  `SET_VERTEX_DATA*` (NV2A/xemu behaviour) instead of (0,0,0,1). SSX 3 passes the matrix of its
  2D/FMV program in v8..v15 via `SET_VERTEX_DATA4F_M` – without this every vertex was transformed to the same point
  and the intro video was discarded as degenerate triangles. Plus `RECOMP_VSH_TRACE_EVERY=<n>` (stride of the VSH trace,
  default 20000 as before).

- `src/kernel/nv2a_pb_exec.c`: **rasteriser thread pool on POSIX too** (pthreads; previously `_WIN32` only, Linux rendered
  single-threaded) – SSX 3 title screen 4.4 → 15.4 fps; pool size as on Windows (`RECOMP_RASTER_THREADS`, default CPUs−4).
  Plus the executor's fps display uses wall-clock time (`GetTickCount64`) instead of `clock()` (= process CPU time on POSIX,
  which showed several times too few fps with multiple busy threads).

- `src/kernel/nv2a_pb_exec.c` (`backend_flush`): with register combiners in use, a back end only receives the stage-0
  texture when that stage's `SHADER_STAGE_PROGRAM` is not NONE (as the CPU path does in `rc_stage_fetch`) – otherwise
  the Vulkan back end multiplied SSX 3's title-screen sky by the left-over snowflake atlas and drew it black.

- `src/kernel/nv2a_backend.h`, `nv2a_pb_exec.c` (`backend_flush`, `backend_from_xf`): **back-end interface
  extension** `Nv2aBatchExt` for transformed batches: register-combiner state, all four texture stages, and per vertex
  the four texture-coordinate sets, specular colour and fog factor (what the CPU path feeds `nv2a_rc_eval`).
  `Nv2aBatch.ext` is NULL for screen-space batches; a back end that ignores it draws as before.
- `src/kernel/nv2a_pb_exec.c` (`raster_batch_program`): **post-transform vertex cache** per batch (direct mapped by
  index, off while CXT_WRITE is on): indexed meshes ran the vertex program once per index instead of once per vertex.
- `src/kernel/nv2a_vsh_interp.c`: **programs are decoded once** (fields extracted into `VshIns` when the program or
  start slot changes) instead of per source per vertex, and sources an op does not read are not fetched.
  New `nv2a_vsh_cxt_write_enabled()`. Together with the cache: SSX 3 race 23 → 55–60 fps on the Vulkan back end.
- `src/kernel/xbox_memory_layout.c` (`xbox_Nv2aRaiseVblank`, `xbox_Nv2aAckBusyBits`), `kernel_bridge.c`
  (`kernel_vblank_tick`): **the vblank stays pending until the title acknowledges it.** The ack thread held every
  interrupt status register at zero and often wiped PMC_INTR_0 bit 24 / PCRTC_INTR_0 bit 0 between the timer thread
  raising the vblank and the D3D ISR's DPC reading it; the DPC then skipped its vblank work and never set the event
  `D3DDevice_BlockUntilVerticalBlank` waits on. SSX 3 waits there once per `AvSetDisplayMode` step, so boot took
  0.7 s to over 70 s at random. Now the raise also sets a marker bit (31) in PCRTC_INTR_0 that D3D's ack (a write
  of exactly 1) removes; only then are the bits cleared (fallback after 100 ms). Boot: 0.5–0.6 s every run.
- `src/kernel/nv2a_pb_exec.c` (`backend_flush`): an `Nv2aTexture` carries a pitch only for linear formats. A
  swizzled/DXT stage keeps the pitch its stage last had (SSX 3's intro video: 640x480 linear), and the Vulkan back end
  took pitch != 0 for "linear" -- after the intro, the title screen sampled the corner of its textures (only the sky
  was left).

- `tools/recomp/translator.py`: declare the `_fa/_fb/_fas/_fbs` flag temporaries for functions containing
  `lock xadd` too (Amped 2: functions whose only flag setter is InterlockedIncrement did not compile).
- `tools/recomp/lifter.py` (`_FUNC_RESERVED_IDENT`): POSIX I/O, stdio, time, ctype, wchar and signal names. On POSIX the
  executable is linked with -rdynamic, so a generated function called `read`/`write`/`close` (Amped 2's CRT, FidDb names)
  interposed libc for the whole process and SDL's D-Bus connection crashed into guest code.
- `src/kernel/nv2a_overlay.c/.h` (new), `kernel_bridge.c` (`kernel_vblank_tick`), `src/video/fb_present_sdl.c`:
  **NV2A video overlay (PVIDEO)**. XDK titles show XMV movies through D3DDevice_UpdateOverlay, not through flips. The
  vblank latches a pending overlay buffer (frame copied out of guest memory, BUFFER bit cleared as the hardware does,
  STOP honoured); the SDL window composes it over the shown surface (YUY2/UYVY, scaling, colour key).
  `RECOMP_OVERLAY_DUMP=<prefix>` dumps every 30th frame. Not yet in the GDI window (fb_present.c).
- `kernel_bridge.c`: **event-driven DPC drain** – `KeInsertQueueDpc` wakes the timer thread (it used to run DPCs only
  after a fixed `Sleep(10)`); fixes starving DirectSound stream voices and movies paced by them (Amped 2: ~1/6 speed).
- `apu_core.c`/`apu_state.h`: SSL ticks queue (`ssl_backlog`) instead of merging into a pending ISTS bit;
  `RECOMP_APU_SSL_STATS=1` prints SSL ticks and stream list stats (`apu_vp.c`) every 5 s.
- `xbox_memory_layout.c`: `nv2a_ack_thread` backs off (`RECOMP_ACK_IDLE_US`, POSIX) when the pushbuffer is quiet and no
  vblank is pending.
- `nv2a_overlay.c`: frame change detection, convert once per new frame, fast unscaled compose; `fb_present_sdl.c`
  composes only on a new overlay frame or flip.
- `src/usb/ohci.c`, `usb_gamepad.c/.h` (`usb_gamepad_output`): **rumble** (from SSX3-Recomp). The emulated pad's OUT
  packets were counted and dropped. Output reports (`00 06 <left u16> <right u16>`) now go to `xbox_InputSetState()`,
  both on interrupt endpoint 2 and as the data stage of a HID SET_REPORT (`21 09`) on the control pipe.
- `nv2a_backend.h`, `nv2a_pb_exec.c`, `xbox_memory_layout.c`: optional `Nv2aBackend.idle`, called every NV2A poll round
  (`nv2a_pb_backend_idle`) – a back end presents overlay frames between flips there.
- `nv2a_pb_exec.c`: `RECOMP_FLIP_VSYNC=1` makes FLIP_STALL wait for the next emulated vblank (presentation interval 1).
- `kernel_bridge.c`: the vblank runs at exactly 60 Hz (16.67 ms average, resync after stalls) and the timer thread waits
  until it is due instead of checking every ~10 ms (vblanks used to land 16-20 ms apart).
- `nv2a_combiner.h`, `nv2a_pb_exec.c`: **dot-product texture shader modes** in the CPU path (DOT_PRODUCT, DOT_ZW,
  DOT_ST, DOT_STR_3D as 2D, DOT_STR_CUBE, DOT_RFLCT_SPEC(_CONST)); tracks SET_DOT_RGBMAPPING (0x1E74),
  SET_SHADER_OTHER_STAGE_INPUT (0x1E78) and SET_EYE_VECTOR (0x181C). Bump-env and DOT_RFLCT_DIFF still read zero.
- `nv2a_pb_exec.c` (`dma_resolve`): a page the title named through MmGetPhysicalAddress for a heap address resolves to
  RAM, not to the contiguous window page with the same number (as the APU already did).

- `nv2a_backend.h`, `nv2a_pb_exec.c`, `nv2a_vsh_interp.c/.h`: **vertex programs on a GPU back end**. Optional
  `Nv2aBackend.draw_program` with `Nv2aProgramBatch` (attribute spans, rebased triangle-list indices, decoded program,
  constants, program/constant generations, aa/uv scale, fog unit state); `backend_program_batch` hands over every
  program batch without CXT_WRITE (`RECOMP_VSH_CPU=1` keeps the CPU path); `nv2a_vsh_export` packs the decoded program
  (layout in the header). `backend_flush`'s state building is now `backend_state`, shared by both paths.
  `RECOMP_VXF_STATS=1` prints CPU transform statistics. Amped 2 race: 13-15 → ~55 fps.
- `nv2a_pb_scan.c`: logs a RunPushBuffer that returns outside its segment (rest dropped) or skips ring data.
- `kernel_hal.c`: **dispatch lock** – DISPATCH_LEVEL now excludes DPCs, as on the Xbox's one CPU. A thread holds a
  global lock from the moment its IRQL reaches DISPATCH_LEVEL until it drops below; a DPC (entered through
  `xbox_IrqlEnterInterrupt(2)`) takes it too, ISRs (device IRQL) do not. DirectSound's lock is a raise to
  DISPATCH_LEVEL: its stream DPC used to run between a game thread's SET_CURRENT_VOICE and the following volume / SSL
  writes, so they landed on the wrong voice (Amped 2: music mono, quiet or silent, no next track, static, a crash).
  A DPC waits at most `RECOMP_DISPATCH_LOCK_MS` (100) for a raised thread, then runs anyway with a warning and the
  `[IRQLHOLD]` holder list; `RECOMP_DISPATCH_LOCK=0` turns it off.
- `apu_dsp.c`: **stereo downmix of the speaker bins** – FL/FR + C and BL/BR at -3 dB to both sides, crosstalk bins
  6..9 as their speakers, LFE left out, the I3DL2 send (10) at -6 dB to both sides; FX sends 11..31 even/odd as
  before. The even/odd fold put LFE on the right only and the rears at full level (Amped 2's music: left 1.7x,
  right 2.7x, clipping). `RECOMP_APU_MIXDOWN=legacy` restores the even/odd fold.
- Audio diagnostics (`apu_vp.c`, `apu_dsp.c`, `apu_core.c`): under `RECOMP_APU_TRACE` a `[APU-VROUTE]` line per voice
  (list, envelope, pauses, all eight bin/volume pairs; multipass voices included) and `[APU-BINS]` (RMS per mixbin,
  samples the mixdown clamped) every second; `RECOMP_APU_TRACE_NO_MMIO=1` keeps those without the per-access register
  log; `RECOMP_APU_SSL_LOG=1` logs every stream list set/done/starve with a timestamp; `RECOMP_APU_VOICE_LOG=<voice>`
  logs every front-end method naming that voice (with the writer's guest stack for volume writes).
- `apu_vp.c`: **idle-voice traps round-robin** – one SE2FE_IDLE_VOICE trap per frame as before, but for the first
  idle voice *after* the one reported last instead of always the first in list order. DirectSound (XDK 5558) retires
  a buffer with several hardware voices through the trap of one of them and ignores the others; Amped 2's results
  screen left such a pair idle (v72 ignored, v73 the one it acts on), v72 trapped on every frame (~1500/s), no other
  idle voice was ever reported, and DirectSound's waits for stopped voices ran into their 0.5 s timeout one after
  another (the results screen at 5-40 fps). `RECOMP_APU_IDLE_LOG=1` prints the idle-voice traps per voice every 5 s.

Verified: Linux build + Windows cross build (MinGW) compile; own decoder test (8 cases, ucontext) green;
`tools.recomp.test_reserved_idents` green; SSX 3 on Linux reaches the same state as under Proton
(APU/AC'97/DSP doorbell/APU interrupts, intro videos are opened).
