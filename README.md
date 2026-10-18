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
| Game data in `game_files/` | ⬜ next step |
| Pipeline: parse, disasm, func_id, abi_analysis, recomp | ⬜ |
| Host, Vulkan renderer, input (ported from SSX3-Recomp), first build | ⬜ |
| Boot → logos → front end → riding | ⬜ |

Plan and open items: [docs/TODO.md](docs/TODO.md), running log: [docs/notes.md](docs/notes.md).

## Quick start (Linux)

```bash
git clone --recurse-submodules <this-repo>        # or locally: git submodule update --init --recursive
cd Amped2-Recomp

scripts/setup.sh                                   # Python venv + capstone, tool check
bash xboxrecomp/tools/linux/install_deps.sh        # SDL2, libepoxy, OpenSSL, ffmpeg (sudo); Vulkan driver + glslc needed later

scripts/extract_iso.sh "/path/to/Amped 2 (Xbox).iso"   # or an already extracted disc folder -> game_files/
scripts/run_pipeline.sh                            # parse, disasm, func_id, abi, recomp -> src/recomp/gen/
scripts/doctor.sh                                  # bring-up report
```

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
