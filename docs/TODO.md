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
- [ ] Movies play at ~1/6 speed: XMV audio clock / APU stream voices (see notes)
- [ ] Seed the XPP callbacks 0x0032E3A4, 0x0032EDAB, 0x003306DF
- [x] Xbox Live / XNet: XNetGetTitleXnAddr answers "no network" (more XNet calls may need the same)
- [x] Splash screen (CPU rasteriser, 12 threads)
- [ ] Logos/intro at full speed, front end, first run on the mountain
- [ ] Overlay in the Vulkan back end too (nv2a_overlay_compose_xrgb)

## Later
- [ ] Windows build (MinGW + Proton, then native), Steam Deck test, macOS
- [ ] 60 fps everywhere, then higher frame rates, resolution, AA, widescreen, HD textures
- [ ] Custom soundtrack support from a host folder
