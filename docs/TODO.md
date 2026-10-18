# TODO

## Phase 0 – data and pipeline
- [x] Copy the game data: `scripts/extract_iso.sh <image | folder>` (user)
- [x] `scripts/setup.sh` (Linux deps still to check)
- [x] `scripts/run_pipeline.sh parse` → fill in `docs/notes.md` (title ID, region, XDK version, sections, libraries)
- [x] Full pipeline + `scripts/doctor.sh`; `scripts/ghidra.sh` names applied (190)

## Phase 1 – host and first build (port from reference/SSX3-Recomp)
- [x] `CMakeLists.txt` (target `amped2_recomp`), `scripts/build.sh`, `build_win.sh`, `run.sh`, `run_win.sh`
- [x] `src/main.c`: entry point / XBE path / game dir from `parse`; drop SSX 3's boot-video flags and fence address
- [x] `src/recomp_manual.c` (media check, XNet offline), `config/seed_functions.json` (3 seeds)
- [ ] Vulkan back end (`src/render/`, plus the Vulkan-Headers/volk submodules) and pad code (`src/input/`);
      rename `SSX3_*` switches to something game-neutral or `AMPED2_*`
- [ ] Consider moving the shared host/renderer/input into a common repo or submodule used by both ports

## Phase 2 – bring-up
- [ ] Check the real unimplemented instructions (cpuid in sub_0026A2F0, cli/sti, pushfd/popfd in XNET) – see notes
- [ ] Find out what `Boom.xbe` is and whether default.xbe launches it (XLaunchNewImage)
- [x] First start, seeds, KEVENTs, fence/vblank, DirectSound/AC'97, heap reclaim
- [x] NV2A video overlay (movies) in the runtime + SDL window
- [x] Movies play at full speed (DPC latency fixed in the runtime)
- [ ] 5.1 multichannel audio: XMV/DirectSound set up 6 mix bins; the APU output is mixed to stereo for now
- [x] Seed the XPP callbacks 0x0032E3A4, 0x0032EDAB, 0x003306DF
- [ ] Re-run the pipeline for seed 0x0032FC8B (XPP callback seen in the intro run)
- [x] Xbox Live / XNet: XNetGetTitleXnAddr answers "no network" (more XNet calls may need the same)
- [x] Splash screen (CPU rasteriser, 12 threads)
- [x] Logo/intro skip (`AMPED2_SKIP_LOGOS`, `AMPED2_SKIP_INTRO`), live input (`scripts/press.sh`)
- [x] Legal/ESRB screen, title screen
- [ ] Main menu → first run on the mountain (needs Start presses; xemu snapshots exist as reference)
- [ ] Overlay in the Vulkan back end too (nv2a_overlay_compose_xrgb)

## Next
- [ ] Vulkan back end from SSX3-Recomp (front end is rasteriser-bound at ~6 fps)

## Input
- [x] Keyboard as pad 1 (runtime map) + Amped 2 keys (`src/input/amped2_keys.c`: Space = A, U = Y, O = B,
      Escape = Start, Tab = Back); checked in the menus and on the pad state
- [x] Game controllers (`src/input/host_pad.c` = SSX3-Recomp's pad code: XInput/SDL, hotplug) + rumble (runtime patch)
- [ ] Riding with keyboard and pad (needs the Millicent 1 crash fixed): steering, Space-hold jump, grabs, U/O tricks,
      Tab reset; rumble on a real pad (Game Options → Vibration is on by default)
- [ ] Rebinding from a config file (`s_bind` in host_pad.c, the key tables), deadzones; the game's own Button Layout
      option exists (B to slide on rails or the other layout)
- [ ] Steam Deck: built-in controls through Steam Input

## Later
- [ ] Windows build (MinGW + Proton, then native), Steam Deck test, macOS
- [ ] 60 fps everywhere, then higher frame rates, resolution, AA, widescreen, HD textures
- [ ] Custom soundtrack support from a host folder
