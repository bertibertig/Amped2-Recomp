#!/usr/bin/env python3
"""Drive a separate xemu instance (Flatpak app.xemu.xemu) as the Amped 2 reference.

  scripts/xemu.py start [--snapshot TAG [--paused]] [--iso PATH] [--audio] [--size 960x720]
  scripts/xemu.py press start wait:3000 a down:400 lt+rt   # buttons, see KEYS / --help
  scripts/xemu.py cap [--out shots/xemu/menu.png] [--raw]  # screenshot, 640x480 unless --raw
  scripts/xemu.py pause | resume | status | stop
  scripts/xemu.py save TAG | load TAG [--paused] | delete TAG | snapshots
  scripts/xemu.py hmp "info status" | qmp '{"execute":"query-status"}' | peek 0x80010000 [count]

How it works (details: .claude/skills/xemu/SKILL.md):
- Our own instance: own config, own copies of the user's HDD image and EEPROM in WORK, muted, QMP on
  127.0.0.1:PORT. The user's own xemu (and its files) are never touched.
- It runs as an X11 client (XWayland, --socket=x11), because only X11 lets us send keys to one window and read
  its pixels without focusing it. The Flatpak's GL is Mesa, so __GLX_VENDOR_LIBRARY_NAME=mesa is needed
  (otherwise "Couldn't find matching GLX visual" on the NVIDIA X server).
- Keys: xdotool keydown/keyup --window (XSendEvent) on xemu's keyboard pad map (port 1 = keyboard).
- Screenshots: ImageMagick `import -window` (works while covered, not while minimised).
"""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = "app.xemu.xemu"
USER_DATA = os.path.expanduser("~/.var/app/app.xemu.xemu/data/xemu/xemu")      # the user's xemu files (read only)
WORK = os.path.expanduser("~/.var/app/app.xemu.xemu/data/amped2-ref")          # ours; must be writable in the sandbox
STATE = os.path.join(WORK, "state.json")
CONFIG = os.path.join(WORK, "xemu.toml")
LOG = os.path.join(WORK, "xemu.log")
DEFAULT_PORT = 4455

# xemu's default keyboard map for the pad on port 1 (Settings > Input > Mapping) -> X keysyms.
# Button names follow scripts/press.sh (our build), with a few aliases.
KEYS = {
    "a": "a", "b": "b", "x": "x", "y": "y",
    "back": "BackSpace", "guide": "5", "start": "Return",
    "lthumb": "3", "rthumb": "4", "white": "1", "black": "2",
    "up": "Up", "down": "Down", "left": "Left", "right": "Right",
    "lup": "e", "lleft": "s", "lright": "f", "ldown": "d", "lt": "w",
    "rup": "i", "rleft": "j", "rright": "l", "rdown": "k", "rt": "o",
}
ALIASES = {"ls": "lthumb", "rs": "rthumb", "select": "back",
           "lsup": "lup", "lsdown": "ldown", "lsleft": "lleft", "lsright": "lright",
           "rsup": "rup", "rsdown": "rdown", "rsleft": "rleft", "rsright": "rright"}


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def load_state():
    try:
        with open(STATE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def our_pids():
    """Host PIDs of our instance: every process whose command line names our config file."""
    out = subprocess.run(["pgrep", "-f", "--", f"-config_path {CONFIG}"],
                         capture_output=True, text=True).stdout
    return [int(p) for p in out.split() if int(p) != os.getpid()]


def find_iso():
    """The Amped 2 image the user's own xemu has loaded (dvd_path), unless --iso is given."""
    try:
        with open(os.path.join(USER_DATA, "xemu.toml")) as f:
            m = re.search(r"^dvd_path\s*=\s*'([^']*)'", f.read(), re.M)
            return m.group(1) if m else None
    except OSError:
        return None


def user_files():
    with open(os.path.join(USER_DATA, "xemu.toml")) as f:
        txt = f.read()

    def get(key):
        m = re.search(rf"^{key}\s*=\s*'([^']*)'", txt, re.M)
        if not m:
            die(f"{key} missing in {USER_DATA}/xemu.toml – set up xemu once (BIOS, MCPX, HDD) first")
        return m.group(1)
    return get("bootrom_path"), get("flashrom_path"), get("eeprom_path"), get("hdd_path")


class QMP:
    def __init__(self, port, wait_s=0):
        deadline = time.time() + wait_s                     # start: QMP listens a moment after the window opens
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
                self.f = self.sock.makefile("rw")
                self.f.readline()                           # greeting
                self.cmd("qmp_capabilities")
                return
            except OSError:
                if time.time() > deadline:
                    die(f"No QMP on 127.0.0.1:{port} – is our instance running? (scripts/xemu.py start)")
                time.sleep(0.2)

    def cmd(self, execute, **args):
        msg = {"execute": execute}
        if args:
            msg["arguments"] = args
        return self.raw(msg)

    def raw(self, msg):
        self.f.write(json.dumps(msg) + "\n")
        self.f.flush()
        while True:
            line = self.f.readline()
            if not line:
                die("QMP connection closed")
            r = json.loads(line)
            if "event" not in r:
                if "error" in r:
                    die(f"QMP error: {r['error'].get('desc', r['error'])}")
                return r.get("return")

    def hmp(self, line):
        out = self.cmd("human-monitor-command", **{"command-line": line})
        return out.replace("\r", "")


def qmp():
    st = load_state()
    return QMP(st["port"] if st else DEFAULT_PORT)


def xemu_windows():
    out = subprocess.run(["xdotool", "search", "--name", r"^xemu \| v"], capture_output=True, text=True).stdout
    return [int(w) for w in out.split()]


def window():
    st = load_state()
    if not st or not our_pids():
        die("Our xemu instance is not running (scripts/xemu.py start)")
    return st["window"]


def glx_env():
    """The Flatpak only has NVIDIA GL when its GL.nvidia runtime matches the host driver exactly; without it the GL
    is Mesa (llvmpipe), and Mesa needs its own GLX vendor on the NVIDIA X server."""
    try:
        with open("/proc/driver/nvidia/version") as f:
            ver = re.search(r"\s(\d+\.\d+(?:\.\d+)?)\s", f.readline()).group(1)
    except (OSError, AttributeError):
        return []                                           # no NVIDIA driver: Mesa is the native GL anyway
    rt = "org.freedesktop.Platform.GL.nvidia-" + ver.replace(".", "-")
    if subprocess.run(["flatpak", "info", rt], capture_output=True).returncode == 0:
        return []
    print(f"note: {rt} not installed – xemu renders with llvmpipe (slow). Fix: flatpak install flathub {rt}")
    return ["--env=__GLX_VENDOR_LIBRARY_NAME=mesa"]


def write_config(iso, audio, size):
    bootrom, flashrom, eeprom, hdd = user_files()
    w, h = size
    with open(CONFIG, "w") as f:
        f.write(f"""# Generated by scripts/xemu.py at every start – edits are lost.
[general]
show_welcome = false
skip_boot_anim = true

[input]
auto_bind = false                 # the user's real pads must never reach our instance
background_input_capture = false

[input.bindings]
port1_driver = 'usb-xbox-gamepad'
port1 = 'keyboard'

[audio]
volume_limit = {1.0 if audio else 0.0}

[display.ui]
show_menubar = false
fit = 'stretch'

[display.window]
last_width = {w}
last_height = {h}

[sys]
mem_limit = '128'

[sys.files]
bootrom_path = '{bootrom}'
flashrom_path = '{flashrom}'
eeprom_path = '{os.path.join(WORK, "eeprom.bin")}'
hdd_path = '{os.path.join(WORK, "xbox_hdd.qcow2")}'
dvd_path = '{iso}'
""")
    return eeprom, hdd


def cmd_start(a):
    if our_pids():
        die("Our instance is already running (scripts/xemu.py stop first, or use load)")
    for tool in ("flatpak", "xdotool", "import"):
        if not shutil.which(tool):
            die(f"{tool} not found (xdotool, imagemagick and the xemu Flatpak are needed)")
    iso = a.iso or find_iso()
    if not iso or not os.path.exists(iso):
        die("No ISO: pass --iso <Amped 2 xiso>, or load it once in your own xemu (dvd_path)")
    size = tuple(int(v) for v in a.size.lower().split("x"))
    os.makedirs(WORK, exist_ok=True)
    eeprom, hdd = write_config(iso, a.audio, size)
    # Copies of the user's HDD/EEPROM, made once: our snapshots live inside our HDD copy.
    for src, dst in ((hdd, "xbox_hdd.qcow2"), (eeprom, "eeprom.bin")):
        dst = os.path.join(WORK, dst)
        if not os.path.exists(dst):
            subprocess.run(["cp", "--reflink=auto", src, dst], check=True)
            print(f"copied {src} -> {dst}")
    before = set(xemu_windows())
    cmd = ["flatpak", "run", "--socket=x11", "--env=SDL_VIDEODRIVER=x11"] + glx_env() + ["--command=xemu", APP,
           "-config_path", CONFIG, "-qmp", f"tcp:127.0.0.1:{a.port},server,nowait"]
    with open(LOG, "w") as log:
        subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                         start_new_session=True)
    win = None
    for _ in range(150):                                    # up to 15 s
        time.sleep(0.1)
        if not our_pids():
            break                                           # exited early (e.g. a snapshot that does not fit)
        new = [w for w in xemu_windows() if w not in before]
        if new:
            win = new[0]
            break
    if not win:
        subprocess.run(["tail", "-5", LOG])
        die(f"xemu window did not appear (log: {LOG})")
    with open(STATE, "w") as f:
        json.dump({"window": win, "port": a.port, "iso": iso}, f)
    subprocess.run(["xdotool", "windowsize", str(win), str(size[0]), str(size[1])])
    q = QMP(a.port, wait_s=20)
    if a.snapshot:
        restore(q, a.snapshot, a.paused, a.settle_ms, wait_s=15)
    print(f"started: window {win}, QMP 127.0.0.1:{a.port}, ISO {os.path.basename(iso)}"
          + (f", snapshot {a.snapshot}" if a.snapshot else "") + (" (paused)" if a.paused else ""))


def cmd_stop(a):
    pids = our_pids()
    if not pids:
        print("not running")
        return
    st = load_state()
    try:
        s = socket.create_connection(("127.0.0.1", st["port"] if st else DEFAULT_PORT), timeout=3)
        f = s.makefile("rw")
        f.readline()
        for m in ({"execute": "qmp_capabilities"}, {"execute": "quit"}):
            f.write(json.dumps(m) + "\n")
            f.flush()
    except OSError:
        pass
    for _ in range(50):
        if not our_pids():
            break
        time.sleep(0.1)
    for p in our_pids():                                    # only processes with our config on the command line
        try:
            os.kill(p, 15)
        except OSError:
            pass
    print("stopped")


def cmd_status(a):
    pids = our_pids()
    st = load_state()
    if pids and st:
        q = QMP(st["port"])
        print(f"ours: running={q.cmd('query-status')['status']} window={st['window']} port={st['port']} "
              f"pids={pids} iso={os.path.basename(st['iso'])}")
    else:
        print("ours: not running")
    other = subprocess.run(["pgrep", "-af", "xemu"], capture_output=True, text=True).stdout.splitlines()
    other = [l for l in other if "/xemu" in l and CONFIG not in l and "xemu.py" not in l]
    print("user xemu: " + ("running" if other else "not running"))


def parse_steps(tokens):
    """'a', 'a:400' (hold ms), 'lt+rt:300' (together), 'wait:2000'; commas also separate steps."""
    steps = []
    for tok in ",".join(tokens).split(","):
        tok = tok.strip().lower()
        if not tok:
            continue
        name, _, ms = tok.partition(":")
        if name == "wait":
            steps.append(("wait", int(ms or 500)))
            continue
        keys = []
        for b in name.split("+"):
            b = ALIASES.get(b, b)
            if b not in KEYS:
                die(f"unknown button '{b}' – known: {' '.join(KEYS)} (aliases: {' '.join(ALIASES)})")
            keys.append(KEYS[b])
        steps.append(("press", keys, int(ms) if ms else None))
    return steps


def cmd_press(a):
    win = str(window())
    steps = parse_steps(a.steps)
    for i, s in enumerate(steps):
        if s[0] == "wait":
            time.sleep(s[1] / 1000)
            continue
        _, keys, hold = s
        subprocess.run(["xdotool", "keydown", "--window", win] + keys, check=True)
        time.sleep((hold or a.hold) / 1000)
        subprocess.run(["xdotool", "keyup", "--window", win] + keys, check=True)
        if i + 1 < len(steps) and steps[i + 1][0] != "wait":
            time.sleep(a.gap / 1000)


def cmd_cap(a):
    win = str(window())
    out = a.out or os.path.join(ROOT, "shots", "xemu", time.strftime("%Y%m%d-%H%M%S") + ".png")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    cmd = ["import", "-silent", "-window", win]
    if not a.raw:
        cmd += ["-resize", "640x480!"]                      # xemu stretches to the window; the guest frame is 4:3
    subprocess.run(cmd + [out], check=True)
    print(out)


def cmd_save(a):
    print(qmp().hmp(f"savevm {a.tag}").strip() or f"saved {a.tag}")


def restore(q, tag, paused, settle_ms, wait_s=0):
    """loadvm over the monitor. Not -loadvm on the command line: that restores before xemu hot-plugs the keyboard
    pad (with its USB hub), and the snapshot then does not match ("Unknown section ... usb-hub"). Right after a
    start the pad may still be missing, so retry for wait_s."""
    deadline = time.time() + wait_s
    while True:
        out = q.hmp(f"loadvm {tag}").strip()
        if not out:
            break
        if time.time() > deadline:
            die(f"loadvm {tag}: {out}")
        time.sleep(0.5)
    if q.cmd("query-status")["status"] != "running":
        q.cmd("cont")
    if paused:
        time.sleep(settle_ms / 1000)                        # let a fresh frame replace the restored picture
        q.cmd("stop")
    print(f"loaded {tag}" + (" (paused)" if paused else ""))


def cmd_load(a):
    restore(qmp(), a.tag, a.paused, a.settle_ms)


def cmd_delete(a):
    print(qmp().hmp(f"delvm {a.tag}").strip() or f"deleted {a.tag}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start")
    s.add_argument("--snapshot")
    s.add_argument("--paused", action="store_true")
    s.add_argument("--iso")
    s.add_argument("--audio", action="store_true", help="unmute (default: muted)")
    s.add_argument("--size", default="960x720", help="window size, keep 4:3 (default 960x720)")
    s.add_argument("--port", type=int, default=DEFAULT_PORT)
    s.add_argument("--settle-ms", type=int, default=4000,
                   help="run time before --paused freezes; long enough for the 'Connected Keyboard' toast to go")
    s.set_defaults(fn=cmd_start)
    sub.add_parser("stop").set_defaults(fn=cmd_stop)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    s = sub.add_parser("press", help="buttons: " + " ".join(KEYS))
    s.add_argument("steps", nargs="+")
    s.add_argument("--hold", type=int, default=120, help="default hold ms")
    s.add_argument("--gap", type=int, default=250, help="ms between presses")
    s.set_defaults(fn=cmd_press)
    s = sub.add_parser("cap")
    s.add_argument("--out")
    s.add_argument("--raw", action="store_true", help="keep the window size instead of 640x480")
    s.set_defaults(fn=cmd_cap)
    sub.add_parser("pause").set_defaults(fn=lambda a: qmp().cmd("stop"))
    sub.add_parser("resume").set_defaults(fn=lambda a: qmp().cmd("cont"))
    for name, fn in (("save", cmd_save), ("delete", cmd_delete)):
        s = sub.add_parser(name)
        s.add_argument("tag")
        s.set_defaults(fn=fn)
    s = sub.add_parser("load")
    s.add_argument("tag")
    s.add_argument("--paused", action="store_true")
    s.add_argument("--settle-ms", type=int, default=400)
    s.set_defaults(fn=cmd_load)
    sub.add_parser("snapshots").set_defaults(fn=lambda a: print(qmp().hmp("info snapshots"), end=""))
    s = sub.add_parser("hmp")
    s.add_argument("line")
    s.set_defaults(fn=lambda a: print(qmp().hmp(a.line), end=""))
    s = sub.add_parser("qmp")
    s.add_argument("json")
    s.set_defaults(fn=lambda a: print(json.dumps(qmp().raw(json.loads(a.json)), indent=1)))
    s = sub.add_parser("peek", help="read guest virtual memory (32-bit words)")
    s.add_argument("addr")
    s.add_argument("count", nargs="?", type=int, default=1)
    s.set_defaults(fn=lambda a: print(qmp().hmp(f"x/{a.count}wx {a.addr}"), end=""))
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
