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
    printf '%s' 'password' | python ms_headless.py play --username account --password-stdin
    python ms_headless.py mark                # record current log byte offset
    python ms_headless.py wait 'Received Packet: 12' --after-offset 0 --timeout 30
    python ms_headless.py stop

State/logs live in ~/.local/state/maplestory-headless by default.
Run under the same Linux user as the game and Codex.
"""
from __future__ import annotations

import argparse
import collections
import getpass
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

def pick_session(argv: list[str]) -> str | None:
    """Extract --session NAME (anywhere in argv) so every command shares one parser path."""
    name = os.environ.get("MS_HEADLESS_SESSION")
    for i, arg in enumerate(list(argv)):
        if arg == "--session" and i + 1 < len(argv):
            name = argv[i + 1]
            del argv[i:i + 2]
            break
        if arg.startswith("--session="):
            name = arg.split("=", 1)[1]
            del argv[i]
            break
    if name in (None, "", "default"):
        return None
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", name):
        print("error: --session must match [A-Za-z0-9_-]{1,32}", file=sys.stderr)
        sys.exit(1)
    return name


SESSION = pick_session(sys.argv)
_BASE_DIR = Path(os.environ.get("MS_HEADLESS_DIR", "~/.local/state/maplestory-headless")).expanduser()
STATE_DIR = _BASE_DIR / "sessions" / SESSION if SESSION else _BASE_DIR
STATE_PATH = STATE_DIR / "session.json"
CLIENT_LOG = STATE_DIR / "client.log"
X_LOG = STATE_DIR / "xvfb.log"
AUTH = STATE_DIR / ".Xauthority"
HEADLESS_STATE = re.compile(r"^HEADLESS_STATE (login|world_select|channel_select|character_select|in_game)(?: count=(\d+) pic=(\d+))?$")
HEADLESS_ERROR = re.compile(r"^HEADLESS_ERROR login reason=(\d+)$")


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
        for stale in ("events.jsonl", "events.jsonl.1", "state.json"):
            (STATE_DIR / stale).unlink(missing_ok=True)
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
        env["MS_HEADLESS_DIR"] = str(STATE_DIR)
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
        print("Commands: status | logs | key | type | click | gm | play | wait | stop")
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


# The client reads raw key events and derives text from the Shift state, so
# `xdotool type` (which drops Shift for symbols) cannot enter characters like '!'.
# Send every ASCII character as an explicit key chord instead.
KEYSYMS = {
    " ": "space", "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "-": "minus", "_": "minus", "=": "equal", "+": "equal",
    "[": "bracketleft", "{": "bracketleft", "]": "bracketright", "}": "bracketright",
    "\\": "backslash", "|": "backslash", ";": "semicolon", ":": "semicolon",
    "'": "apostrophe", '"': "apostrophe", ",": "comma", "<": "comma", ".": "period",
    ">": "period", "/": "slash", "?": "slash", "`": "grave", "~": "grave",
}
SHIFTED = set('!@#$%^&*()_+{}|:"<>?~')


def key_chord(ch: str) -> str | None:
    if ch.isascii() and ch.isalpha():
        return f"Shift_L+{ch.lower()}" if ch.isupper() else ch
    if ch.isascii() and ch.isdigit():
        return ch
    if ch in KEYSYMS:
        return f"Shift_L+{KEYSYMS[ch]}" if ch in SHIFTED else KEYSYMS[ch]
    return None


def type_text(state: dict, value: str) -> None:
    if not value:
        die("Text is empty")
    chords = [key_chord(ch) for ch in value]
    if None in chords:
        die("Text contains characters that cannot be sent as key events (ASCII only)")
    run_xdotool(state, "key", "--delay", "45", *chords,
                timeout=max(15, int(len(value) * 0.15)))


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
    since = last_seq()
    focus_game(state)
    press(state, "Return")
    time.sleep(0.20)
    type_text(state, value)
    time.sleep(0.12)
    press(state, "Return")
    print("Submitted chat text via Enter -> type -> Enter (command content not logged)")
    if args.expect is not None:
        hit = wait_for(state, args.timeout, event_types=("chat", "notice"), text=args.expect,
                       since=since, what=f"reply containing {args.expect!r}")
        print(json.dumps(hit))


def cmd_act(args) -> None:
    """Submit a semantic action through the client's own packet code; result is an `act` event."""
    state = read_state()
    since = last_seq()
    act_id = f"a{int(time.time() * 1000)}"
    arg = " ".join(args.arg)
    if args.action == "move" and len(args.arg) == 2:
        args.timeout = max(args.timeout, int(args.arg[1]) / 1000 + 5)
    tmp = STATE_DIR / "act.tmp"
    tmp.write_text(f"{act_id} {args.action} {arg}\n")
    tmp.replace(STATE_DIR / "act.txt")
    hit = wait_for(state, args.timeout, event_types=("act",), text=act_id,
                   since=since, what=f"result of act {args.action}")
    print(json.dumps(hit))
    if not hit.get("ok"):
        sys.exit(1)


def cmd_bundle(args) -> None:
    """Collect metadata, state, recent events, logs and an optional screenshot (no credentials)."""
    out = Path(args.out).expanduser() if args.out else STATE_DIR / f"bundle-{int(time.time())}"
    out.mkdir(parents=True, exist_ok=True)
    meta = {"session": SESSION or "default", "time": time.strftime("%Y-%m-%dT%H:%M:%S")}
    saved = read_state(required_state=False)
    if saved:
        meta.update({k: saved[k] for k in ("display", "client_pid") if k in saved})
        meta["client_alive"] = client_alive(saved)
    (out / "meta.json").write_text(json.dumps(meta, indent=2))
    state_json = STATE_DIR / "state.json"
    if state_json.is_file():
        shutil.copy(state_json, out / "state.json")
    (out / "events.jsonl").write_text("".join(json.dumps(e) + "\n" for e in read_events()[-args.events:]))
    if CLIENT_LOG.is_file():
        with CLIENT_LOG.open(errors="replace") as inp:
            (out / "client.log").write_text("".join(collections.deque(inp, maxlen=args.lines)))
    shot = None
    if args.screenshot and saved and virtual_alive(saved):
        tool = shutil.which("import")
        if tool:
            r = subprocess.run([tool, "-window", "root", str(out / "screenshot.png")],
                               env=env_for(saved), capture_output=True, timeout=20)
            shot = "screenshot.png" if r.returncode == 0 else None
    print(json.dumps({"bundle": str(out), "screenshot": shot}))


def cmd_logs(args) -> None:
    if not CLIENT_LOG.exists():
        die("No client log available; start the client first")
    with CLIENT_LOG.open(errors="replace") as inp:
        for line in collections.deque(inp, maxlen=args.lines):
            print(line, end="")


def cmd_state(args) -> None:
    path = STATE_DIR / "state.json"
    if not path.is_file():
        die("No state.json yet. Start the client (it writes state on the first update).")
    try:
        data = json.loads(path.read_text())
    except json.JSONDecodeError:
        die("state.json is mid-write; retry")
    if args.field:
        for part in args.field.split("."):
            try:
                data = data[int(part)] if isinstance(data, list) else data[part]
            except (KeyError, IndexError, ValueError, TypeError):
                data = None
                break
    print(json.dumps(data, indent=None if args.field else 2))


def read_events() -> list[dict]:
    events = []
    for path in (STATE_DIR / "events.jsonl.1", STATE_DIR / "events.jsonl"):
        if path.is_file():
            for line in path.read_text(errors="replace").splitlines():
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError:
                    pass  # partial trailing line
    return events


def print_events(args, event_types: tuple[str, ...] | None) -> None:
    events = read_events()
    if events and args.since >= 0 and events[0]["seq"] > args.since + 1:
        print(f"WARNING: events up to seq {events[0]['seq'] - 1} expired", file=sys.stderr)
    for event in events:
        if event["seq"] > args.since and (event_types is None or event["type"] in event_types):
            print(json.dumps(event))


def cmd_events(args) -> None:
    print_events(args, (args.type,) if args.type else None)


def cmd_chat(args) -> None:
    print_events(args, ("chat", "notice"))


def cmd_mark(args) -> None:
    if not CLIENT_LOG.exists():
        die("No client log available; start the client first")
    # A byte offset allows Codex to assert only on events *after* an action.
    print(CLIENT_LOG.stat().st_size)


EXIT_GONE = 2
EXIT_TIMEOUT = 3
STATE_EXPR = re.compile(r"^\s*([A-Za-z0-9_.]+)\s*(==|!=|>=|<=|>|<|~)?\s*(.*?)\s*$")


def load_state_json() -> dict | None:
    try:
        return json.loads((STATE_DIR / "state.json").read_text())
    except (OSError, json.JSONDecodeError):
        return None


def lookup(data, path: str):
    for part in path.split("."):
        try:
            data = data[int(part)] if isinstance(data, list) else data[part]
        except (KeyError, IndexError, ValueError, TypeError):
            return None
    return data


def state_matches(expr: str, data: dict | None) -> bool:
    """`path` (present and truthy), or `path OP value` with OP in == != >= <= > < ~ (contains)."""
    if data is None:
        return False
    m = STATE_EXPR.match(expr)
    if not m:
        die(f"Bad --state expression: {expr!r}")
    path, op, want = m.groups()
    got = lookup(data, path)
    if op is None:
        return bool(got)
    if got is None:
        return False
    if op == "~":
        return want in str(got)
    try:
        want_v = type(got)(want) if not isinstance(got, bool) else want.lower() == "true"
    except ValueError:
        return False
    return {"==": got == want_v, "!=": got != want_v, ">=": got >= want_v,
            "<=": got <= want_v, ">": got > want_v, "<": got < want_v}[op]


def last_seq() -> int:
    events = read_events()
    return events[-1]["seq"] if events else 0


def event_matches(event: dict, types: tuple[str, ...] | None, text: str | None) -> bool:
    if types and event["type"] not in types:
        return False
    return text is None or text in str(event.get("text", json.dumps(event)))


def wait_for(state: dict, timeout: float, *, state_expr: str | None = None,
             event_types: tuple[str, ...] | None = None, text: str | None = None,
             since: int = 0, what: str = "condition") -> dict:
    """Return the match; exit 3 on timeout (printing last state/event), 2 if the client is gone."""
    deadline = time.monotonic() + timeout
    while True:
        if state_expr is not None and state_matches(state_expr, load_state_json()):
            return {"state": state_expr}
        if event_types is not None or text is not None:
            for event in read_events():
                if event["seq"] > since and event_matches(event, event_types, text):
                    return event
        if not client_alive(state):
            print(f"Client exited while waiting for {what}", file=sys.stderr)
            raise SystemExit(EXIT_GONE)
        if time.monotonic() >= deadline:
            events = read_events()
            print(json.dumps({"timeout": what, "last_state": load_state_json(),
                              "last_event": events[-1] if events else None}), file=sys.stderr)
            raise SystemExit(EXIT_TIMEOUT)
        time.sleep(0.2)


def cmd_wait(args) -> None:
    state = read_state()
    if args.pattern is None and args.state is None and args.event is None and args.match is None:
        die("Supply a log pattern, --state, --event, or --match")
    if args.pattern is not None:
        if not CLIENT_LOG.exists():
            die("Client log not available")
        deadline = time.monotonic() + args.timeout
        while True:
            with CLIENT_LOG.open("rb") as inp:
                inp.seek(args.after_offset)
                if args.pattern in inp.read().decode("utf-8", "replace"):
                    print(f"Matched in client log: {args.pattern!r}")
                    return
            if not client_alive(state):
                print(f"Client exited while waiting for {args.pattern!r}", file=sys.stderr)
                raise SystemExit(EXIT_GONE)
            if time.monotonic() >= deadline:
                print(f"Timed out waiting for {args.pattern!r} after {args.timeout}s", file=sys.stderr)
                raise SystemExit(EXIT_TIMEOUT)
            time.sleep(0.2)
    since = args.since if args.since is not None else last_seq()
    types = (args.event,) if args.event else None
    hit = wait_for(state, args.timeout, state_expr=args.state, event_types=types,
                   text=args.match, since=since,
                   what=" / ".join(filter(None, [args.state, args.event, args.match])))
    print(json.dumps(hit))


def read_screen_events(log) -> tuple[tuple[str, int | None, int | None] | None, str | None]:
    """Consume new client-reported screen events, retaining the latest state."""
    current = None
    error = None
    for raw in log.readlines():
        line = raw.decode("utf-8", "replace").strip()
        match = HEADLESS_STATE.fullmatch(line)
        if match:
            current = (match[1], int(match[2]) if match[2] else None,
                       int(match[3]) if match[3] else None)
        elif HEADLESS_ERROR.fullmatch(line):
            error = line
    return current, error


def wait_for_screen(log, state: dict, deadline: float, previous: str | None = None):
    while True:
        screen, error = read_screen_events(log)
        if error:
            die(f"Login rejected ({error}). Inspect {CLIENT_LOG}")
        if screen and screen[0] != previous:
            return screen
        if not client_alive(state):
            die(f"HeavenClient exited. Inspect {CLIENT_LOG}")
        if time.monotonic() >= deadline:
            die(f"Timed out waiting for a screen change from {previous or 'startup'}; "
                f"rebuild the client with HEADLESS_STATE support and inspect {CLIENT_LOG}")
        time.sleep(0.05)


def replace_login_field(state: dict, window: str, x: int, y: int, value: str) -> None:
    run_xdotool(state, "mousemove", "--window", window, str(x), str(y))
    run_xdotool(state, "click", "1")
    # This client's Textfield does not implement Ctrl+A; its login fields are
    # limited to 12 characters, so clear from the end with BackSpace instead.
    press(state, "End")
    for _ in range(12):
        press(state, "BackSpace")
    type_text(state, value)


def cmd_play(args) -> None:
    """Advance from the login screen to an active field using screen events."""
    state = read_state()
    if not CLIENT_LOG.exists():
        die("Client log not available")
    deadline = time.monotonic() + args.timeout
    with CLIENT_LOG.open("rb") as log:
        screen, _ = read_screen_events(log)
        if not screen:
            screen = wait_for_screen(log, state, deadline)
        window = focus_game(state)
        while screen[0] != "in_game":
            name, count, pic = screen
            if name == "login":
                if args.password_stdin:
                    password = sys.stdin.read().rstrip("\r\n")
                elif sys.stdin.isatty():
                    password = getpass.getpass("Password: ")
                else:
                    die("At login screen; pipe a password with --password-stdin")
                if not password:
                    die("Password from stdin is empty")
                if args.username:
                    replace_login_field(state, window, 500, 250, args.username)
                replace_login_field(state, window, 500, 280, password)
            elif name == "character_select":
                if count == 0:
                    die("No characters available; create one before using play")
                if pic != 2:
                    die(f"Character selection requires a PIC (mode {pic}); use the manual CLI controls")
            press(state, "Return")
            screen = wait_for_screen(log, state, deadline, previous=name)
        print(json.dumps({"screen": "in_game", "client_running": True,
                          "client_log": str(CLIENT_LOG)}))


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
    parser = argparse.ArgumentParser(
        description="Offscreen CLI runner/controller for Linux HeavenClient. "
                    "Global option: --session NAME (or MS_HEADLESS_SESSION) selects an independent "
                    "client with its own display, state dir and logs; omitted = default session.")
    sub = parser.add_subparsers(dest="command", required=True)
    sp = sub.add_parser("start", help="Start virtual X display and HeavenClient")
    sp.add_argument("--project", default=str(Path(__file__).resolve().parent),
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
    sp.add_argument("--expect", help="Wait for a chat/notice event containing this text (exit 3 on timeout)")
    sp.add_argument("--timeout", type=float, default=10.0, help="Seconds to wait with --expect")
    sp.set_defaults(func=cmd_gm)
    sp = sub.add_parser("act", help="Semantic action: npc-talk <oid>, npc-choose next|yes|no|prev|close|<n>, use-item <slot>, equip <slot>, unequip <slot>, move left|right|up|down <ms>, jump, portal, attack")
    sp.add_argument("action", choices=["npc-talk", "npc-choose", "use-item", "equip", "unequip", "move", "jump", "portal", "attack", "party-create", "party-leave", "party-invite", "party-accept"])
    sp.add_argument("arg", nargs="*")
    sp.add_argument("--timeout", type=float, default=5.0)
    sp.set_defaults(func=cmd_act)
    sp = sub.add_parser("bundle", help="Write a diagnostic bundle directory (meta, state, events, log tail, optional screenshot)")
    sp.add_argument("--out", help="Output directory (default: <session dir>/bundle-<time>)")
    sp.add_argument("--events", type=int, default=100)
    sp.add_argument("--lines", type=int, default=200)
    sp.add_argument("--screenshot", action="store_true", help="Needs ImageMagick `import`")
    sp.set_defaults(func=cmd_bundle)

    sp = sub.add_parser("logs", help="Print recent client output")
    sp.add_argument("--lines", type=int, default=80)
    sp.set_defaults(func=cmd_logs)
    sp = sub.add_parser("state", help="Print the client's state.json snapshot")
    sp.add_argument("--field", help="Dotted path, e.g. player.hp or mobs.0.oid")
    sp.set_defaults(func=cmd_state)

    sp = sub.add_parser("events", help="Print client events (JSONL) with seq > --since")
    sp.add_argument("--since", type=int, default=0)
    sp.add_argument("--type", help="screen|map|spawn|despawn|chat|error")
    sp.set_defaults(func=cmd_events)

    sp = sub.add_parser("chat", help="Print chat and server-notice events (GM replies arrive as notices)")
    sp.add_argument("--since", type=int, default=0)
    sp.set_defaults(func=cmd_chat)

    sp = sub.add_parser("mark", help="Print current log byte offset for next wait --after-offset")
    sp.set_defaults(func=cmd_mark)
    sp = sub.add_parser("wait", help="Wait for a log substring, --state expr, or --event; exit 0 matched, 3 timeout, 2 client gone")
    sp.add_argument("pattern", nargs="?", help="Exact substring in client output (legacy mode)")
    sp.add_argument("--state", help="e.g. map==100000000, player.hp>=10, screen==in_game, mobs.0.oid")
    sp.add_argument("--event", help="Event type: screen|map|spawn|despawn|chat|notice|error")
    sp.add_argument("--match", help="Substring of the event text (any event type if --event is omitted)")
    sp.add_argument("--since", type=int, help="Only events after this seq (default: only new events)")
    sp.add_argument("--timeout", type=float, default=20.0)
    sp.add_argument("--after-offset", type=int, default=0, help="Only match log output after this byte offset")
    sp.set_defaults(func=cmd_wait)
    sp = sub.add_parser("play", aliases=["enter-game"],
                        help="Enter the game from login using client screen events")
    sp.add_argument("--username", help="Account name (omit when the client saved it)")
    sp.add_argument("--password-stdin", action="store_true", help="Read password from stdin")
    sp.add_argument("--timeout", type=float, default=90.0, help="Overall transition timeout in seconds")
    sp.set_defaults(func=cmd_play)
    sp = sub.add_parser("stop", help="Stop client and virtual X server")
    sp.set_defaults(func=cmd_stop)
    args = parser.parse_args()
    if hasattr(args, "hold_ms") and args.hold_ms < 0:
        die("--hold-ms must be nonnegative")
    if hasattr(args, "after_offset") and args.after_offset < 0:
        die("--after-offset must be nonnegative")
    if args.command in ("play", "enter-game") and args.timeout <= 0:
        die("--timeout must be positive")
    try:
        args.func(args)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        die(f"External command failed: {exc}")


if __name__ == "__main__":
    main()
