# TODO

## Phase 0 – data and pipeline
- [ ] Copy the game data: `scripts/extract_iso.sh <image | folder>` (user)
- [ ] `scripts/setup.sh`, Linux deps
- [ ] `scripts/run_pipeline.sh parse` → fill in `docs/notes.md` (title ID, region, XDK version, sections, libraries)
- [ ] Full pipeline + `scripts/doctor.sh`; optional `scripts/ghidra.sh` for names

## Phase 1 – host and first build (port from reference/SSX3-Recomp)
- [ ] `CMakeLists.txt` (target `amped2_recomp`), `scripts/build.sh`, `build_win.sh`, `run.sh`, `run_win.sh`
- [ ] `src/main.c`: entry point / XBE path / game dir from `parse`; drop SSX 3's boot-video flags and fence address
- [ ] `src/recomp_manual.c` (starts empty), `config/seed_functions.json` (from runtime logs)
- [ ] Vulkan back end (`src/render/`, plus the Vulkan-Headers/volk submodules) and pad code (`src/input/`);
      rename `SSX3_*` switches to something game-neutral or `AMPED2_*`
- [ ] Consider moving the shared host/renderer/input into a common repo or submodule used by both ports

## Phase 2 – bring-up
- [ ] First start, seeds, KEVENTs, fence/vblank, DirectSound/AC'97 (see notes → "Carried over from SSX3-Recomp")
- [ ] Xbox Live / XNet: make the game run offline
- [ ] Logos, FMVs, front end, first run on the mountain

## Later
- [ ] Windows build (MinGW + Proton, then native), Steam Deck test, macOS
- [ ] 60 fps everywhere, then higher frame rates, resolution, AA, widescreen, HD textures
- [ ] Custom soundtrack support from a host folder
