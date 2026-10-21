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
- [x] **Music bug** (user report, 2026-10-20): music stopped, came back quiet/crackling, no next track, static, crash –
      DirectSound's stream DPC raced game threads at DISPATCH_LEVEL (runtime dispatch lock); stereo downmix of the
      speaker bins fixed the left/right imbalance and clipping (see notes 2026-10-21)
- [x] **Results-screen lag** (user report, 2026-10-21): DirectSound waited 0.5 s per stopped voice – the runtime
      always reported the same idle voice (v72) and never the one DirectSound retires the buffer through (v73);
      idle-voice traps are round-robin now (see notes 2026-10-21)
- [ ] LB/White (next music track) crash on the slope, reported once (2026-10-20, maybe a pre-v0.2.0 build) – not
      reproduced with White every 16 s through a ride
- [ ] 5.1 multichannel audio: XMV/DirectSound set up 6 mix bins; the APU output is mixed down to stereo (ITU-style
      downmix of bins 0..9 now); real 5.1 needs a 6-channel host device and XC_AUDIO surround
- [ ] GP/EP DSP: effect sends (I3DL2 bin 10, FX sends 11..31) are added dry; the title's DSP image (yetidsp.bin)
      would run reverb/effects on them
- [x] Seed the XPP callbacks 0x0032E3A4, 0x0032EDAB, 0x003306DF
- [x] Re-run the pipeline for seed 0x0032FC8B (XPP callback seen in the intro run)
- [x] Xbox Live / XNet: XNetGetTitleXnAddr answers "no network" (more XNet calls may need the same)
- [x] Splash screen (CPU rasteriser, 12 threads)
- [x] Logo/intro skip (`AMPED2_SKIP_LOGOS`, `AMPED2_SKIP_INTRO`), live input (`scripts/press.sh`)
- [x] Legal/ESRB screen, title screen
- [x] Main menu, career menu, level select (Vulkan, 60 fps)
- [x] Dot-product texture shader modes (CPU path): rider pants/shoes/head lit and textured
- [x] Dot-product modes + real cube maps in the Vulkan shader/back end (rider matches the CPU path)
- [x] Rider upper body, gloves, head and hair: RunPushBuffer JUMPs followed, D3D's software-method patch done by the executor (matches xemu, 60 fps)
- [ ] Software methods 0x08/0x09 (PGRAPH register writes from D3D's GPU interrupt handler) are logged, not modelled
- [x] Rider in a ride (Millicent 1) checked against xemu: complete (hair, hands, upper body), HUD and goals board shown
- [x] Riding at ~55 fps: texture re-validation fixed, vertex ring grows, vertex programs on the GPU (`nv2a_vsh.vert`)
- [ ] Rider shadow: D16 depth texture (format 0x30) as a shadow map – sample the depth surface with a compare sampler
- [ ] 60 fps steady (menus and race sit at ~55: vsync pacing of FLIP_STALL / the vblank wait)
- [ ] Per-title save root: `~/.local/share/xboxrecomp/Cache` (z:) is shared with SSX3-Recomp (RECOMP_SAVE_DIR per port?)
- [x] First run on the mountain (Millicent 1): entering the Evergreen session crashed – seeds 0x000A5A70, 0x0024F240
- [x] Overlay in the Vulkan back end (idle presents between flips)
- [ ] Colour-keyed overlays in the Vulkan path

## Next
- [x] Vulkan back end from SSX3-Recomp (60 fps in the menus, frame pacing via RECOMP_FLIP_VSYNC)

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
- [x] Windows package (MinGW): boots, menus and a ride under Wine 11 / GE-Proton with `RECOMP_WIN_PLACEHOLDER=1`
      (set by `run.bat`), software renderer only (~2-6 fps); shipped with v0.1.0 as experimental
- [ ] Windows: Vulkan back end (Win32 surface) – the software renderer is far too slow to play
- [ ] Windows under Wine/Proton: movies stay black (video overlay in the D3D11/CPU window path) and no music
      (sound effects play) – recheck with the dispatch lock (the Linux music dropout was a DPC race, which a
      ~2-6 fps software-rendered run hits all the more)
- [ ] Windows: test on real Windows 10/11 – `run.bat` and the new `run.ps1` (v0.2.0; `run.ps1` only checked with
      PowerShell 7 on Linux against a stand-in binary)
- [ ] Windows: `--settings` TUI does not show – amped2_recomp.exe is a GUI-subsystem program, so it has no console
      for the settings screen (AttachConsole/AllocConsole when `--settings` is given, or a console build); run.bat /
      run.ps1 also redirect its output to run.log. The settings are Vulkan-only anyway until the Windows Vulkan port
- [ ] Windows under Wine/Proton: check by ear whether the music plays now (v0.2.0 test: the APU's WAV tap is not
      silent, but that is not what XAudio2 plays)
- [ ] Software renderer: stray triangles stretched out from trees/objects (seen on Windows; check
      `AMPED2_RENDERER=cpu` on Linux)
- [ ] Steam Deck test, macOS
- [ ] Custom soundtrack support from a host folder

## Graphics enhancements
All options live in a categorised settings TUI (Resolution Scaling, Aspect Ratio, Framerate, Display, …) whose
choices are saved to a config file and picked up on the next start.

First batch (done 2026-10-20, `amped2_recomp --settings` / `scripts/run.sh --settings`, src/settings/):
- [x] Settings TUI + persistent config file (categories per enhancement, selections remembered)
- [x] Resolution scaling: internal resolution 1x-5x (640x480 → 3200x2400 at 4:3)
- [x] Aspect ratio: 4:3, 16:9, 16:10, 21:9 – Hor+ in rides, HUD kept 4:3 in the middle, menus and movies 4:3
- [x] Framerate: real unlock (the game runs on QueryPerformanceCounter time), limit = display refresh / fixed / none
- [x] Window mode: windowed, fullscreen, borderless fullscreen + display selection, Alt+Enter / F11 in game
- [ ] Ride frame rate is CPU-bound at ~85-115 fps (menus 200-800): the guest spins in D3D (`sub_0031EA90`, 22 %)
      waiting for the NV2A thread – pushbuffer decode (`nv2a_pb_exec_method` 11 %, `note` 4 %), texture
      re-validation hashing (`hash_guest_texture` 7 %, ~1 GB/s). Needed for 144-270 fps on the slope.
- [ ] Ride/menu detection for the aspect ratio is a heuristic (draws into an off-screen target = ride, in_ride() in
      vk_backend.c); a game-state flag would be exact. Splash screen at boot counts as a ride for ~30 flips
- [ ] Hor+: objects the game culls against its 4:3 frustum may pop in at the far sides (not seen in Millicent 1)
- [ ] Alt+Enter / F11 toggle not exercised by a test yet (needs window focus)

Backlog (chosen 2026-10-20):
- Anti-aliasing and filtering
  - [ ] MSAA / SSAA (2x-8x on the Vulkan render targets, or supersampling on top of the resolution scale)
  - [ ] Post-process AA (FXAA / SMAA final pass, also catches alpha-tested foliage and fences)
  - [ ] Anisotropic filtering override (2x-16x)
  - [ ] Sharpening (AMD CAS) / FSR 1 upscaling (Steam Deck)
- Textures and detail
  - [ ] HD texture packs: dump and replace textures by hash from a host folder
  - [ ] High-resolution rider shadow map with PCF (depends on the D16 shadow-map item in Phase 2)
  - [ ] LOD bias / model LOD / draw and fog distance (needs game-side research)
  - [ ] Texture filter override (force linear/trilinear, keep-original option)
- Display and widescreen
  - [ ] HUD/UI correction for wide aspect ratios (keep 4:3 proportions, edge-anchored or pillarboxed)
  - [ ] FOV slider (on top of the Hor+ aspect-ratio change)
  - [ ] VSync on/off/adaptive, frame limiter options, FPS / frame-time overlay
  - [ ] Output scaling modes: fit, stretch, integer; nearest vs linear for the final blit
- In-game settings
  - [ ] In-game overlay with the TUI's options, styled after Amped 2's own menus, pad-navigable (Steam Deck Game
        Mode has no terminal)
- Post-processing and extras
  - [ ] Brightness / gamma / saturation; optional HDR output (scRGB / HDR10)
  - [ ] CRT / scanline filter
  - [ ] Toggle or tune the game's own screen effects (blur, glow, overlays – needs research)
  - [ ] Full-resolution screenshot hotkey, later a photo mode
