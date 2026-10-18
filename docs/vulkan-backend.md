# Vulkan render back end – design

Taken over from SSX3-Recomp (same code, `AMPED2_*` names); the SSX 3 history below is kept because it explains the
choices. Amped 2 additions are in the section "Amped 2" at the end.

Goal: replace the pushbuffer executor's CPU rasteriser with the GPU (target 60 fps; later higher resolution, AA,
HD textures, high frame rates). Vulkan rather than OpenGL: the user's choice (2026-10-18). The CPU rasteriser stays
available as the reference (`AMPED2_RENDERER=cpu`) for comparison dumps; the Vulkan path is the one being maintained.

## Integration
- The runtime calls a registered back end through `xboxrecomp/src/kernel/nv2a_backend.h`: `clear`, `draw`, `flip`, all on
  the NV2A poll thread, one at a time. The executor still decodes the pushbuffer, runs vertex programs on the CPU
  (`nv2a_vsh_interp.c`), clips at the near plane and hands over triangles in surface pixels.
- Our back end lives in **`src/render/`** (own code, no submodule change) and is registered in `src/main.c` when
  `AMPED2_RENDERER=vulkan` (the default in `scripts/run.sh`). The window and its event loop run on the main thread from
  process start (`amped2_vk_host_main`); the guest runs on its own thread; rendering happens on the NV2A thread.
- Dependencies as pinned submodules: `third_party/Vulkan-Headers` (v1.4.363), `third_party/volk`
  (vulkan-sdk-1.4.363.0; loads `libvulkan.so`/`vulkan-1.dll` at run time → no import library or SDK, MinGW included).
- Shaders: GLSL in `src/render/shaders/`, compiled to SPIR-V C arrays at build time with `glslc -mfmt=c`.

## Rendering
- One offscreen colour image per guest surface (`color_va`) at full surface size (AA factor included), one
  depth/stencil image per `zeta_va` (a scratch one when the title has none). Dynamic rendering (Vulkan 1.3).
- `flip`: blit the surface the title last rendered into to the swapchain (linear filtering = AA downsample), present.
- State (blend/depth/stencil/cull/colour mask) as **dynamic state** (1.3 core + `VK_EXT_extended_dynamic_state3`), so no
  pipeline per NV2A state combination. Alpha test in the shader.
- Positions: surface pixels → NDC in the vertex shader (push constants: surface size); `w = 1/rhw` for perspective-
  correct interpolation; z normalised from the Z24 range.
- Textures: `nv2a_backend_decode_texture` → BGRA8, cached by (address, format, size); a hash of the raw guest bytes is
  checked once per frame and the texture re-decoded only when it changed (FMV frames are rewritten in place).
  Render-to-texture: a texture that points at a surface the GPU rendered samples that GPU image.

## Register combiners (milestone 3)
- Runtime patch: `Nv2aBatch.ext` (`Nv2aBatchExt`) carries, for transformed batches, the combiner state, all four
  texture stages and per vertex four texture-coordinate sets, specular and fog factor. Screen-space batches have
  none (the CPU path does not run them through the combiners either) and draw stage-0 texel × diffuse.
- `nv2a.frag` is an **uber-shader**: a line-by-line port of the CPU path (`rc_stage_fetch` + `nv2a_rc_eval`, after
  xemu's psh.c). Combiner state goes into a 528-byte std140 UBO in the frame's ring (reused while unchanged),
  bound through the push descriptor next to the four stage samplers (bindings 0-3, UBO 4). One precompiled SPIR-V,
  one pipeline; the ext vertex data is a second vertex binding with dynamic stride (0 + one zeroed element without
  ext).
- Not yet: cube maps (face 0 sampled as 2D), bump-env/dot-product stage modes (read 0, as on the CPU path),
  palettised textures.
- Texture change detection hashes exactly level 0's bytes (DXT block size, bpp per format): an upper bound
  (w*h*4) took in per-frame data behind DXT textures and re-decoded ~20 textures every race frame.

## Milestones
1. ✅ Vulkan init, window, swapchain; `clear` + `flip`. Debug dump as BMP (`AMPED2_VK_DUMP`).
2. ✅ Triangles with vertex colour + texture stage 0, blend/depth/stencil/alpha test. 60 fps title/menu.
3. ✅ Interface extension + combiner uber-shader + multitexturing. Race 55–60 fps (with the vertex cache and
   pre-decoded vertex programs in the runtime patch).
4. Render-to-texture checks, AA/resolution, per-triangle culling, frames in flight, Windows (Win32 surface instead of SDL).

## Amped 2
- **Video overlay**: movies (XMV) are shown through the NV2A overlay, not drawn (see `docs/notes.md`). The runtime
  latches the overlay (`nv2a_overlay.c`); the back end keeps a 640x480 BGRA overlay image, uploads it only when the
  overlay has a new frame, and blits it over the surface at every present (`overlay_record`).
- **Presents without flips**: the back-end interface got an optional `idle` callback (runtime patch), called every
  round of the NV2A poll loop. `vkb_idle` presents when the overlay has a frame not yet shown – during a movie the game
  never flips. `present(0)` is the flip path without counting, logging or dumping.
- **Frame pacing**: Amped 2 does not wait for the vblank itself; it relies on the flip stalling until the vblank
  (presentation interval 1). The executor ended FLIP_STALL at once, so on the GPU the title flipped ~300 times a second.
  `RECOMP_FLIP_VSYNC=1` (default in `scripts/run.sh`) makes FLIP_STALL wait for the next emulated vblank.
- Checking presented frames without a screen: the Mesa screenshot layer,
  `VK_INSTANCE_LAYERS=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG="frames=200-6-150,output_dir=<dir>"`
  (PNG per frame, overlay included – `AMPED2_VK_DUMP` dumps the guest surface only).
- Not yet: colour-keyed overlays in the Vulkan path (the CPU window honours the key); validation layers are not
  installed on this machine (`vulkan-validation-layers`).
