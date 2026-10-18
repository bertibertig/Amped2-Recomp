# reference/

Local, shallow read-only copies of other projects (not part of this repo, via `.gitignore`).
Create/update with `scripts/fetch_references.sh`.

| Folder | Project | Use |
|---|---|---|
| SSX3-Recomp | bertibertig/SSX3-Recomp (ours, private; falls back to a symlink to ~/Coding/SSX3-Recomp) | Template: host, Vulkan back end, input, CMake, scripts, work log |
| SSX3-recomp-koen | koenhendriks/SSX3-recomp | Independent SSX 3 port; bring-up log with generic toolkit issues |
| SSX-Tricky-PC | MatiasRiveraC/SSX-Tricky-PC | xboxrecomp port of SSX Tricky: D3D11 renderer, fix-up passes, RE notes |
| OpenTricky | GiZcesi/OpenTricky | Continuation with improved renderer and launcher |

None of them is the Amped 2 engine: game addresses do not carry over, toolkit/XDK mechanisms do.
Licenses: SSX3-recomp-koen GPL-2.0-or-later, OpenTricky GPL-3.0-only (findings only, no code or text);
xboxrecomp MIT; xemu parts LGPL-2.1+.
