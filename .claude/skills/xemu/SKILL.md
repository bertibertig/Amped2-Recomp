---
name: xemu
description: Run Amped 2 in a separate xemu instance (Flatpak) as the real-Xbox reference - boot the PAL ISO, restore saved VM snapshots (intro movie, title, main menu, Millicent 1 level menu / ride) in about a second, press pad buttons, pause/resume, read guest memory and take 640x480 screenshots without taking the user's focus. Use whenever our recompiled build needs checking against real Xbox behaviour (boot order, movies, menus, rendering, colours, timing shape).
---

# xemu as the Amped 2 reference

Everything goes through `scripts/xemu.py` (our own tool, Linux, KDE Wayland desktop, xemu = Flatpak `app.xemu.xemu`).

```bash
scripts/xemu.py start --snapshot main_menu --paused   # boot + restore: ~5 s (4 s of it is the settle time)
scripts/xemu.py resume
scripts/xemu.py press start wait:3000 a down:400       # pad buttons
scripts/xemu.py cap --out shots/xemu/menu.png          # 640x480 PNG
scripts/xemu.py stop
```

The idea (a scripted, separate emulator instance with snapshots for fast reference shots) is taken from the SSX 3 port's
xemu skill, see "Credits" below. Script and text here are our own, written for Linux/Flatpak.

## Commands

| Command | What |
|---|---|
| `start [--snapshot TAG [--paused]] [--iso P] [--audio] [--size 960x720] [--port 4455]` | Boot **our own** instance. `--snapshot` restores a state once the pad is plugged in; `--paused` lets it run `--settle-ms` (4000) first, so the frame is fresh and xemu's "Connected 'Keyboard'" toast has gone. Default ISO = the `dvd_path` of the user's own xemu. |
| `stop` | QMP `quit`, then SIGTERM to processes that have **our** config on their command line. Never touches the user's xemu. Never use `flatpak kill` (would kill the user's instance too). |
| `status` | Our instance (run state, window, PIDs) and whether a user xemu runs. |
| `press STEP...` | `a`, `a:400` (hold ms), `lt+rt:300` (together), `wait:2000`; spaces or commas separate steps. `--hold` (120) / `--gap` (250) ms. |
| `cap [--out F] [--raw]` | Screenshot of our window, scaled to 640x480 (our build's frame size); `--raw` keeps the window size. Default file `shots/xemu/<timestamp>.png` (gitignored). Works while the window is covered, not while it is minimised. |
| `pause` / `resume` | QMP `stop` / `cont`. |
| `save TAG` / `load TAG [--paused]` / `delete TAG` / `snapshots` | VM snapshots (~0.2 s each way, 40–65 MB each, stored inside our HDD copy). `load --paused` settles 400 ms. |
| `hmp "..."` / `qmp '{...}'` | Raw monitor access, e.g. `hmp "info status"`. |
| `peek ADDR [N]` | N 32-bit words of guest **virtual** memory (`x/Nwx`), e.g. game globals for frame counters or state. |

Buttons (names as in our `scripts/press.sh`) and xemu's keyboard map they are sent as (Settings → Input → Mapping,
port 1 = Keyboard; these are xemu's defaults, confirmed by the user's screenshot of the mapping page):

| Button | Key | Button | Key | Button | Key |
|---|---|---|---|---|---|
| `a` | A | `up` | Up | `lup` / `lsup` | E |
| `b` | B | `down` | Down | `ldown` / `lsdown` | D |
| `x` | X | `left` | Left | `lleft` / `lsleft` | S |
| `y` | Y | `right` | Right | `lright` / `lsright` | F |
| `start` | Return | `white` | 1 | `rup` / `rsup` | I |
| `back` | Backspace | `black` | 2 | `rdown` / `rsdown` | K |
| `guide` | 5 | `lt` | W | `rleft` / `rsleft` | J |
| `lthumb` / `ls` | 3 | `rt` | O | `rright` / `rsright` | L |
| `rthumb` / `rs` | 4 | | | | |

Sticks are digital here (full throw while held); triggers are fully pressed or released.

## How our instance is set up (and why)

- **Files**: `~/.var/app/app.xemu.xemu/data/amped2-ref/` – generated `xemu.toml` (rewritten at every start), `xemu.log`,
  `state.json`, and copies (made once) of the user's `xbox_hdd.qcow2` and `eeprom.bin`. It lives there because the
  Flatpak only has the host read-only, except for its own data dir. BIOS/MCPX are read from the user's xemu config. The
  HDD copy carries the user's save from the time of copying (Sasha, rank #239, 5 % complete).
- **X11, not Wayland**: started with `--socket=x11 --env=SDL_VIDEODRIVER=x11`. Only under X11 (XWayland) can we post
  keys to one window (`xdotool keydown/keyup --window`, XSendEvent – focus never changes) and read its pixels
  (`import -window`). xemu's SDL accepts the synthetic key events.
- **GL**: the Flatpak only gets NVIDIA's GL when the matching `org.freedesktop.Platform.GL.nvidia-<host driver>`
  runtime is installed. Without it xemu runs on Mesa **llvmpipe** (software, slower than real hardware) and, under
  X11, needs `__GLX_VENDOR_LIBRARY_NAME=mesa` (else "Couldn't find matching GLX visual"). The script checks this and
  prints the `flatpak install` line when it is missing (2026-10-18: host 615.78.08, only 615.71.09 installed).
- **Input isolation**: `[input] auto_bind = false` and `background_input_capture = false`. Without that, xemu
  auto-binds the user's real gamepad to a free port – the user's pad would then drive our instance, and every
  snapshot would carry an extra USB hub, so it no longer loads without that pad ("Unknown section … usb-hub").
- Muted (`volume_limit = 0`, `--audio` to hear it), no menubar, `fit = 'stretch'` in a 4:3 window: the window shows
  only the guest frame, so the 640x480 capture is the whole frame with nothing cropped.
- QMP on `127.0.0.1:4455`; there is no `screendump` in xemu, hence `import`.
- Snapshots are restored with `loadvm` over the monitor, never with `-loadvm` on the command line: that restores before
  xemu has plugged in the keyboard pad (with its hub), and the load fails.

Rules:
- Our instance and the user's own xemu can run at the same time. The user's xemu is a Wayland window: our tools cannot
  see it or send it anything, and that is intended. Never edit the user's xemu files (`~/.var/app/app.xemu.xemu/data/xemu/xemu/`).
- `stop` when done.
- Snapshots belong to our HDD copy + this ISO + this xemu version. Deleting `amped2-ref/xbox_hdd.qcow2` deletes all of
  them. A newer xemu Flatpak may refuse old snapshots – then recreate them (below).

## Snapshots (Amped 2 PAL "Europe, Australia", En/Fr/De, our HDD copy)

| Tag | State |
|---|---|
| `intro_movie` | Cold boot, ~40 s in: intro movie (after the MGS and XSN Sports logo movies) |
| `title` | Title screen "PRESS START TO BEGIN" (Y = Xbox demos) |
| `main_menu` | Main Menu, Career highlighted, profile Sasha |
| `millicent1_menu` | Career → Ride! → Millicent 1, challenge list (Evergreen highlighted), 3D level behind it |
| `millicent1_ride` | Millicent 1, Evergreen session: riding, HUD visible |

Check what exists with `scripts/xemu.py snapshots`.

Recreating them from a cold boot (`start` without a snapshot, ~2.5 min):
1. Timeline under llvmpipe: MGS logo ~10 s, XSN Sports ~20 s, intro movie ~30 s → `save intro_movie`.
2. `press start wait:3000` → legal/ESRB screen; `start wait:3000` → title → `save title`.
3. `start wait:3000` → Main Menu → `save main_menu`. (`b` goes back one menu.)
4. `start` (Career) → `start` (Ride!) → `start` (Millicent 1 / Breckenridge, Event 1) → "LOADING" with the controls
   picture → ~20 s → challenge list → `save millicent1_menu`.
5. `a` (Evergreen) → ~12 s → "Media Challenge" card → `a` → "Challenges" board → `a`, `wait:3000`, `pause` →
   `save millicent1_ride`.

`start` and `a` both select in the menus; `b` goes back / quits the session.

## Comparing with our build

Our side: `scripts/run.sh` (keyboard pad via `scripts/press.sh`, frame dumps with `RECOMP_FB_DUMP=<prefix>`,
movie overlay frames with `RECOMP_OVERLAY_DUMP=<prefix>`). Both sides are 640x480. Compare menus, layout, colours,
lighting and the order of screens; timing and animation never match to the frame, and xemu on llvmpipe runs below real
speed, so compare the shape of timing (e.g. a halving), not absolute numbers.

## Credits

The approach – a separately configured xemu instance driven over QMP, with VM snapshots of game states, input that
does not take focus, and focus-free screenshots – follows the xemu skill of the independent SSX 3 port by koenhendriks (SSX3-recomp):
https://github.com/koenhendriks/SSX3-recomp/blob/main/.claude/skills/xemu/SKILL.md (local copy:
`reference/SSX3-recomp-koen/.claude/skills/xemu/SKILL.md`). That repo is GPL-2.0-or-later, so only the idea was taken;
the tool (`scripts/xemu.py`) and this text were written from scratch (Windows/PowerShell there, Linux/Flatpak/X11 here).
