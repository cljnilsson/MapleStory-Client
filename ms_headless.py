#!/usr/bin/env python3
"""Run the existing GLFW HeavenClient on an isolated virtual X display.

This is OFFSCREEN operation, not a renderer-free/protocol-only build.
Uses Python stdlib, plus Xvfb, xauth, xdotool, and a working HeavenClient.

Commands:
    python ms_headless.py start --project ~/Desktop/Projects/MS/MapleStory-Client
    python ms_headless.py status
    python ms_headless.py logs --lines 50
    python ms_headless.py key Return
    python ms_headless.py key Right --hold-ms 250
    python ms_headless.py type 'example text'
    python ms_headless.py type --stdin          # safer for credentials
    python ms_headless.py click 420 310
    python ms_headless.py gm '!example-command' # must already be in game; actual syntax depends on server
    python ms_headless.py mark                # record current log byte offset
    python ms_headless.py wait 'Received Packet: 12' --after-offset 0 --timeout 30
    python ms_headless.py stop

State/logs live in ~/.local/state/maplestory-headless by default.
Run under the same Linux user as the game and Codex.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import subprocess
import sys
import time

STATE_DIR = Path(os.environ.get("MS_HEADLESS_DIR", "~/.local/state/maplestory-headless")).expanduser()
STATE_PATH = STATE_DIR / "session.json"
CLIENT_LOG = STATE_DIR / "client.log"
X_LOG = STATE_DIR / "xvfb.log"
AUTH = STATE_DIR / ".Xauthority"


def die(message: str) -> None:
    raise SystemExit("Error: " + message)


def required(name: str) -> None:
    if shutil.which(name) is None:
        die(f"Missing {name}. Install with: sudo pacman -S --needed xorg-server-xvfb xorg-xauth xdotool mesa")


def proc_identity(pid: int) -> str | None:
    """Linux /proc starttime, used to avoid killing an unrelated reused PID."""
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
        fields = stat.split(") ", 1)[1].split()
        if fields[0] == "Z":
            return None
        return fields[19]  # Linux process stat field 22
    except (OSError, IndexError):
        return None


def alive(pid: int, start: str | None) -> bool:
    return bool(start and proc_identity(pid) == str(start))


def read_state(required_state: bool = True) -> dict | None:
    if not STATE_PATH.exists():
        if required_state:
            die("No active session. Run: python ms_headless.py start")
        return None
    try:
        return json.loads(STATE_PATH.read_text())
    except (OSError, ValueError):
        die(f"Invalid state file: {STATE_PATH}")


def client_alive(state: dict) -> bool:
    return alive(state["client_pid"], state.get("client_start"))


def virtual_alive(state: dict) -> bool:
    return alive(state["x_pid"], state.get("x_start"))


def env_for(state: dict) -> dict:
    env = os.environ.copy()
    env["DISPLAY"] = state["display"]
    env["XAUTHORITY"] = state["xauth"]
    return env


def run_xdotool(state: dict, *args: str, capture: bool = False, timeout: int = 15):
    return subprocess.run(["xdotool", *args], env=env_for(state), check=True,
                          text=True, capture_output=capture, timeout=timeout)


def choose_display() -> str:
    for n in range(110, 250):
        if not Path(f"/tmp/.X11-unix/X{n}").exists() and not Path(f"/tmp/.X{n}-lock").exists():
            return f":{n}"
    die("No free virtual X display in range :110 to :249")


def cmd_start(args) -> None:
    for prog in ("Xvfb", "xauth", "xdotool"):
        required(prog)
    project = Path(args.project).expanduser().resolve()
    build_dir = project / "build"
    binary = build_dir / "HeavenClient"
    if not binary.is_file():
        die(f"Missing executable {binary}. Build HeavenClient first.")
    if not os.access(binary, os.X_OK):
        die(f"Executable is not runnable: {binary}")
    prior = read_state(required_state=False)
    if prior and (client_alive(prior) or virtual_alive(prior)):
        die("An instance is already running. Use 'status' or 'stop' first.")

    old_umask = os.umask(0o077)
    try:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        STATE_DIR.chmod(0o700)
        display = choose_display()
        AUTH.touch(exist_ok=True, mode=0o600)
        AUTH.chmod(0o600)
        auth_cookie = secrets.token_hex(16)
        subprocess.run(["xauth", "-f", str(AUTH), "add", display, ".", auth_cookie],
                       check=True, capture_output=True, text=True)

        with X_LOG.open("w") as xout:
            xvfb = subprocess.Popen([
                "Xvfb", display, "-screen", "0", "1280x800x24", "+extension", "GLX",
                "-nolisten", "tcp", "-noreset", "-auth", str(AUTH)
            ], stdin=subprocess.DEVNULL, stdout=xout, stderr=subprocess.STDOUT,
               start_new_session=True)

        state = {
            "display": display,
            "xauth": str(AUTH),
            "x_pid": xvfb.pid,
            "x_start": proc_identity(xvfb.pid),
            "client_pid": 0,
            "client_start": None,
            "project": str(project),
            "started_at": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        }
        for _ in range(80):
            if xvfb.poll() is not None:
                die(f"Xvfb exited with {xvfb.returncode}. Examine {X_LOG}")
            if Path(f"/tmp/.X11-unix/X{display[1:]}").exists():
                try:
                    run_xdotool(state, "getdisplaygeometry", capture=True, timeout=2)
                    break
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                    pass
            time.sleep(0.1)
        else:
            xvfb.terminate()
            die(f"Xvfb did not initialize. Examine {X_LOG}")

        env = env_for(state)
        if not args.no_software_gl:
            env["LIBGL_ALWAYS_SOFTWARE"] = "1"
            env["GALLIUM_DRIVER"] = "llvmpipe"
        # Critical: relative assets/settings are resolved from build/.
        with CLIENT_LOG.open("w") as cout:
            client = subprocess.Popen([str(binary)], cwd=str(build_dir), env=env,
                                      stdin=subprocess.DEVNULL, stdout=cout,
                                      stderr=subprocess.STDOUT, start_new_session=True)
        state["client_pid"] = client.pid
        state["client_start"] = proc_identity(client.pid)
        STATE_PATH.write_text(json.dumps(state, indent=2) + "\n")
        STATE_PATH.chmod(0o600)
        time.sleep(0.3)
        if client.poll() is not None:
            xvfb.terminate()
            die(f"HeavenClient exited immediately ({client.returncode}). See {CLIENT_LOG}")
        print(f"Started offscreen HeavenClient (PID {client.pid}) on virtual display {display}.")
        print("No window appears on your desktop; OpenGL still runs on the virtual display.")
        print(f"Client log: {CLIENT_LOG}")
        print(f"X server log: {X_LOG}")
        print("Commands: status | logs | key | type | click | gm | wait | stop")
    finally:
        os.umask(old_umask)


def cmd_status(args) -> None:
    state = read_state()
    print(json.dumps({
        "client_running": client_alive(state),
        "xserver_running": virtual_alive(state),
        "display": state["display"],
        "client_pid": state["client_pid"],
        "started_at": state.get("started_at"),
        "client_log": str(CLIENT_LOG),
        "xserver_log": str(X_LOG),
    }, indent=2))
    if not client_alive(state):
        raise SystemExit(1)


def visible_window(state: dict, wait_seconds: int = 15) -> str:
    if not client_alive(state):
        die(f"HeavenClient is not running. Inspect {CLIENT_LOG}")
    until = time.monotonic() + wait_seconds
    while time.monotonic() < until:
        try:
            result = run_xdotool(state, "search", "--onlyvisible", "--name", "MapleStory",
                                 capture=True, timeout=2)
            ids = re.findall(r"^\d+$", result.stdout, re.M)
            if ids:
                return ids[-1]
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            pass
        if not client_alive(state):
            die(f"HeavenClient exited. Inspect {CLIENT_LOG}")
        time.sleep(0.25)
    die(f"Game window not detected on virtual display after {wait_seconds}s. Inspect {CLIENT_LOG} and {X_LOG}")


def focus_game(state: dict) -> str:
    window = visible_window(state)
    # windowfocus works without a desktop window manager; XTEST key sends to focus.
    run_xdotool(state, "windowfocus", window)
    return window


def press(state: dict, key: str, hold_ms: int = 0) -> None:
    if hold_ms:
        run_xdotool(state, "keydown", "--clearmodifiers", key)
        time.sleep(hold_ms / 1000)
        run_xdotool(state, "keyup", key)
    else:
        run_xdotool(state, "key", "--clearmodifiers", "--delay", "40", key)


def cmd_key(args) -> None:
    state = read_state()
    focus_game(state)
    press(state, args.key, args.hold_ms)
    print(f"Sent key {args.key} (hold {args.hold_ms}ms)")


def get_typed_text(args) -> str:
    if getattr(args, "stdin", False):
        return sys.stdin.read().rstrip("\r\n")
    if getattr(args, "text", None) is None:
        die("Supply text or use --stdin")
    return args.text


def type_text(state: dict, value: str) -> None:
    if not value:
        die("Text is empty")
    run_xdotool(state, "type", "--clearmodifiers", "--delay", "35", "--", value,
                timeout=max(15, int(len(value) * 0.12)))


def cmd_type(args) -> None:
    value = get_typed_text(args)
    state = read_state()
    focus_game(state)
    type_text(state, value)
    print(f"Typed {len(value)} characters (content not logged)")


def cmd_click(args) -> None:
    state = read_state()
    window = focus_game(state)
    run_xdotool(state, "mousemove", "--window", window, str(args.x), str(args.y))
    run_xdotool(state, "click", str(args.button))
    print(f"Clicked button {args.button} at client-relative ({args.x}, {args.y})")


def cmd_gm(args) -> None:
    value = get_typed_text(args)
    state = read_state()
    focus_game(state)
    press(state, "Return")
    time.sleep(0.20)
    type_text(state, value)
    time.sleep(0.12)
    press(state, "Return")
    print("Submitted chat text via Enter -> type -> Enter (command content not logged)")
    print("Only works if in-game chat is available and the server recognizes the command")


def cmd_logs(args) -> None:
    if not CLIENT_LOG.exists():
        die("No client log available; start the client first")
    with CLIENT_LOG.open(errors="replace") as inp:
        for line in collections.deque(inp, maxlen=args.lines):
            print(line, end="")


def cmd_mark(args) -> None:
    if not CLIENT_LOG.exists():
        die("No client log available; start the client first")
    # A byte offset allows Codex to assert only on events *after* an action.
    print(CLIENT_LOG.stat().st_size)


def cmd_wait(args) -> None:
    state = read_state()
    if not CLIENT_LOG.exists():
        die("Client log not available")
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        with CLIENT_LOG.open("rb") as inp:
            inp.seek(args.after_offset)
            if args.pattern in inp.read().decode("utf-8", "replace"):
                print(f"Matched in client log: {args.pattern!r}")
                return
        if not client_alive(state):
            die(f"Client exited while waiting for {args.pattern!r}")
        time.sleep(0.2)
    die(f"Timed out waiting for {args.pattern!r} after {args.timeout}s")


def graceful_stop(pid: int, identity: str | None, timeout: float = 3.0) -> None:
    if not alive(pid, identity):
        return
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    limit = time.monotonic() + timeout
    while time.monotonic() < limit and alive(pid, identity):
        time.sleep(0.1)
    if alive(pid, identity):
        os.kill(pid, signal.SIGKILL)


def cmd_stop(args) -> None:
    state = read_state()
    if client_alive(state):
        try:
            focus_game(state)
            press(state, "alt+F4")
            time.sleep(0.8)
        except (SystemExit, subprocess.CalledProcessError, subprocess.TimeoutExpired):
            pass
        graceful_stop(state["client_pid"], state.get("client_start"))
    graceful_stop(state["x_pid"], state.get("x_start"))
    STATE_PATH.unlink(missing_ok=True)
    print("Stopped client and virtual X server. Logs preserved in", STATE_DIR)


def main() -> None:
    parser = argparse.ArgumentParser(description="Offscreen CLI runner/controller for Linux HeavenClient")
    sub = parser.add_subparsers(dest="command", required=True)
    sp = sub.add_parser("start", help="Start virtual X display and HeavenClient")
    sp.add_argument("--project", default="~/Desktop/Projects/MS/MapleStory-Client",
                    help="Path containing build/HeavenClient")
    sp.add_argument("--no-software-gl", action="store_true", help="Do not force Mesa software GL")
    sp.set_defaults(func=cmd_start)
    sp = sub.add_parser("status", help="Print machine-readable process/display status")
    sp.set_defaults(func=cmd_status)
    sp = sub.add_parser("key", help="Send a key (xdotool key syntax, e.g. Return, Left, F1)")
    sp.add_argument("key")
    sp.add_argument("--hold-ms", type=int, default=0)
    sp.set_defaults(func=cmd_key)
    sp = sub.add_parser("type", help="Type text into focused game textfield")
    sp.add_argument("text", nargs="?")
    sp.add_argument("--stdin", action="store_true", help="Read text from stdin instead of argv")
    sp.set_defaults(func=cmd_type)
    sp = sub.add_parser("click", help="Click at game-window coordinates")
    sp.add_argument("x", type=int)
    sp.add_argument("y", type=int)
    sp.add_argument("--button", type=int, default=1)
    sp.set_defaults(func=cmd_click)
    sp = sub.add_parser("gm", help="Submit in-game chat text using Enter/type/Enter")
    sp.add_argument("text", nargs="?")
    sp.add_argument("--stdin", action="store_true", help="Read command from stdin")
    sp.set_defaults(func=cmd_gm)
    sp = sub.add_parser("logs", help="Print recent client output")
    sp.add_argument("--lines", type=int, default=80)
    sp.set_defaults(func=cmd_logs)
    sp = sub.add_parser("mark", help="Print current log byte offset for next wait --after-offset")
    sp.set_defaults(func=cmd_mark)
    sp = sub.add_parser("wait", help="Wait for an exact substring in client output; exit nonzero on timeout")
    sp.add_argument("pattern")
    sp.add_argument("--timeout", type=float, default=20.0)
    sp.add_argument("--after-offset", type=int, default=0, help="Only match log output after this byte offset")
    sp.set_defaults(func=cmd_wait)
    sp = sub.add_parser("stop", help="Stop client and virtual X server")
    sp.set_defaults(func=cmd_stop)
    args = parser.parse_args()
    if hasattr(args, "hold_ms") and args.hold_ms < 0:
        die("--hold-ms must be nonnegative")
    if hasattr(args, "after_offset") and args.after_offset < 0:
        die("--after-offset must be nonnegative")
    try:
        args.func(args)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        die(f"External command failed: {exc}")


if __name__ == "__main__":
    main()
