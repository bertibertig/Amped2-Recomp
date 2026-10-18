# Amped2-Recomp – Instructions for Claude Code

Goal: statically recompile **Amped 2 (original Xbox, 2003, Microsoft Game Studios / Indie Built)** to C with
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) and get it running as a native PC port (x86-64).
No emulator: the game code is translated to C once; the xboxrecomp runtime and our own code replace kernel/GPU/audio/input.

This is the second port after **SSX3-Recomp** (same author, same toolkit version, same approach). Everything that was
learned there is in `docs/notes.md` → "Carried over from SSX3-Recomp" – read it before debugging anything, most of the
bring-up problems (missing XDK sections, seeds, KEVENTs, vblank, AC'97/APU, MMIO on Linux, input init) are generic.

## Why this approach
Xbox code is already x86 (no MIPS/PowerPC translation). Amped 2 is an Xbox exclusive, so there is no other-platform
decomp to lean on; there is also no known port of this game – our own SSX3 port is the main template.

## Repo layout
```
xboxrecomp/        Git submodule (sp00nznet/xboxrecomp v0.14.0, the same commit as SSX3-Recomp). Do NOT edit here;
                   runtime changes live in patches/xboxrecomp/ and are applied by the scripts.
patches/xboxrecomp/  Runtime patch taken over from SSX3-Recomp (Linux MMIO decoder, case-insensitive paths, vblank,
                   pushbuffer executor fixes, Vulkan back-end interface) – game-agnostic.
config/            Our own config, e.g. seed_functions.json (entry points the static analysis does not see) – created
                   during bring-up, the pipeline picks it up automatically once it exists
game_files/        Extracted copy of your own disc (gitignored): default.xbe + data
reference/         Local read-only copies (gitignored): SSX3-Recomp (ours), SSX3-recomp-koen, SSX-Tricky-PC, OpenTricky
scripts/           setup.sh, extract_iso.sh, run_pipeline.sh, doctor.sh, ghidra.sh (+ ghidra/), fetch_references.sh
src/recomp/gen/    GENERATED code (gitignored, from your own XBE) – never commit, never edit by hand
docs/notes.md      Running work log (addresses, findings, decisions) – keep adding to it
docs/TODO.md       Plan / open items
```
Not there yet (comes after `parse`, see docs/TODO.md): `src/main.c`, `src/recomp_manual.c`, `CMakeLists.txt`,
`scripts/build.sh`/`run.sh`, `src/render/` (Vulkan), `src/input/`. They will be ported from `reference/SSX3-Recomp`
(our own code) with the SSX3-specific parts (addresses, boot-video flags, `SSX3_*` switches) taken out.

## Workflow (keep this order)
1. `scripts/setup.sh` – venv + capstone; Linux build deps: `bash xboxrecomp/tools/linux/install_deps.sh` (sudo)
2. `scripts/extract_iso.sh <Amped2.iso | extracted disc folder>` → `game_files/default.xbe` + data
3. `scripts/run_pipeline.sh parse` – XBE header: **record title ID, region, XDK version, entry point, base address,
   sections, libraries, kernel imports in docs/notes.md**. Check the XDK library list for XONLINE/XNET (Xbox Live),
   WMA/soundtrack libraries, and which sections the disasm step will sweep (printed as "XDK sections").
4. `scripts/run_pipeline.sh` (everything) – disasm → names → func_id → **abi_analysis (never skip!)** → recomp (`--split 250`).
5. `scripts/doctor.sh` – what is still unresolved (unresolved call targets, stubs, unimplemented instructions).
6. Port host/build from SSX3-Recomp, build → the first start **will crash**. That is normal: read stderr, evaluate the
   ICALL trace, add seeds (`tools.seed_from_log` → `config/seed_functions.json`) and overrides, then repeat.
Guides: `xboxrecomp/docs/GETTING_STARTED.md`, `xboxrecomp/docs/pipeline/06-debugging.md`,
`xboxrecomp/docs/technical/indirect-calls.md`, `xboxrecomp/docs/technical/pushbuffer-executor.md`, `lessons-learned.md`.

## Platform note
- **Linux first** (target: Steam Deck), Windows second (MinGW cross build + Proton, later native), macOS later.
- Linux: own POSIX host (sigaction MMIO faults, SDL2 window) and the Vulkan back end from SSX3-Recomp. The toolkit's
  OpenGL path (`d3d8_gl.c`) is not used.
- The pipeline (steps 2–5) runs anywhere (Python + capstone).

## References (read first, before reinventing anything)
- `reference/SSX3-Recomp/` – **our own** SSX 3 port: host (`src/main.c`), Vulkan renderer (`src/render/`), pad code
  (`src/input/`), CMake, scripts, and the work log `docs/notes.md`. Our code, so it may be reused here directly.
- `reference/SSX3-recomp-koen/` – independent SSX 3 port, Windows-first, detailed bring-up log in its `CLAUDE.md`
  (generic toolkit issues: guest-CPU fairness, vblank/flip traps, streamer races …). GPL-2.0-or-later: use findings,
  never copy code or text unless the user decided on a GPL-compatible license.
- `reference/SSX-Tricky-PC/`, `reference/OpenTricky/` – xboxrecomp ports of SSX Tricky: NV2A→D3D11 renderer,
  combiners/vertex programs as shaders, fix-up passes. OpenTricky is GPL-3.0-only (findings only).
- None of them is the same engine as Amped 2 (EA vs. Indie Built/Microsoft): game-side addresses do **not** carry over,
  only the toolkit/XDK-level mechanisms. XDK library code (D3D8, DSOUND, XAPI) is similar across titles of the same
  XDK version – compare the XDK version from `parse` with SSX 3's (5558).
- [xemu](https://xemu.app) as the behaviour reference: skill `.claude/skills/xemu/SKILL.md` + `scripts/xemu.py` (own
  Flatpak instance, snapshots, focus-free keys and screenshots). Modelled on the xemu skill of `reference/SSX3-recomp-koen`
  (idea only, own code/text).

## Rules
- **The repo language is English**: docs, code comments, script output, commit messages, `docs/notes.md` entries.
- **Do not commit game data, XBE, ISO or generated code** (all in .gitignore). Only our own sources and tools.
- The GitHub remote (when created) must be **private**, like SSX3-Recomp.
- Never edit generated code in `src/recomp/gen/` by hand – fixes go in as an override, seed or pipeline patch.
- Changes to `xboxrecomp/` (submodule) only as patches in `patches/xboxrecomp/` (re-export after editing, see its README);
  ask first, prefer our own code. Fixes that also help SSX3-Recomp should be ported back there.
- Small, verifiable steps: after every change regenerate/build/run, and record the result in `docs/notes.md`.
- If numbers/addresses are unclear: prove them from `xbe_parser` output or the disassembly, never guess.
- Test the **default flow** (logos/intro played, skipped with a key press) as well as any skip switches.
- Memory use during compilation is high: `--split 250`, reduce `JOBS=` when RAM is low.
