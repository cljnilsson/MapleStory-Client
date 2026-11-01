#!/usr/bin/env python3
"""Run a JSON flow of headless-client steps.

    python flow.py flows/login_to_game.json [--param user=admin] [--keep]

Flow file: {"name", "params": {k: default}, "steps": [...]}. Each step has an optional
"session" (default "default") and one of:
    {"start": true}                         start the client
    {"play": true, "username": "${user}", "password": "${password}"}
    {"act": "party-create", "args": ["x"], "timeout": 5}
    {"gm": "!command", "expect": "text"}
    {"wait": {"state": "map==1", "event": "chat", "match": "hi"}, "timeout": 20}
    {"assert": "player.hp>0"}               state expression, checked immediately
    {"stop": true}
"${name}" is replaced from params. Passwords come from params or the environment variable
named by "password_env"; they are sent on stdin and never printed or bundled.
On failure the step, expectation and observed state are printed, a bundle is written for each
session used, and every started session is stopped (unless --keep).
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

CLI = Path(__file__).resolve().parent / "ms_headless.py"
ENV = {**os.environ, "ALSOFT_DRIVERS": "null"}


def sub(value, params):
    if isinstance(value, str):
        return re.sub(r"\$\{(\w+)\}", lambda m: str(params[m.group(1)]), value)
    if isinstance(value, list):
        return [sub(v, params) for v in value]
    if isinstance(value, dict):
        return {k: sub(v, params) for k, v in value.items()}
    return value


def cli(session, *args, stdin=None, timeout=300):
    cmd = [sys.executable, str(CLI)] + (["--session", session] if session != "default" else []) + list(args)
    return subprocess.run(cmd, input=stdin, env=ENV, text=True, capture_output=True, timeout=timeout)


def observed(session):
    r = cli(session, "state")
    return r.stdout.strip() if r.returncode == 0 else f"(no state: {r.stdout.strip() or r.stderr.strip()})"


def run_step(step, session, params):
    """Return (ok, expected, detail)."""
    t = str(step.get("timeout", 20))
    if step.get("start"):
        r = cli(session, "start")
        return r.returncode == 0, "client starts", r.stdout + r.stderr
    if step.get("play"):
        pw = step.get("password")
        if pw is None and step.get("password_env"):
            pw = os.environ.get(step["password_env"], "")
        r = cli(session, "play", "--username", step["username"], "--password-stdin",
                "--timeout", str(step.get("timeout", 90)), stdin=pw or "")
        return r.returncode == 0, f"{step['username']} reaches in_game", (r.stdout + r.stderr).replace(pw or "\0", "***")
    if "act" in step:
        r = cli(session, "act", step["act"], *map(str, step.get("args", [])), "--timeout", t)
        return r.returncode == 0, f"act {step['act']} ok", r.stdout + r.stderr
    if "gm" in step:
        extra = ["--expect", step["expect"], "--timeout", t] if "expect" in step else []
        r = cli(session, "gm", step["gm"], *extra)
        return r.returncode == 0, f"gm {step['gm']} -> {step.get('expect', 'sent')}", r.stdout + r.stderr
    if "wait" in step:
        w = step["wait"]
        a = ["wait", "--timeout", t]
        for key in ("state", "event", "match"):
            if key in w:
                a += [f"--{key}", w[key]]
        r = cli(session, *a)
        return r.returncode == 0, f"wait {json.dumps(w)}", r.stdout + r.stderr
    if "assert" in step:
        r = cli(session, "wait", "--state", step["assert"], "--timeout", str(step.get("timeout", 2)))
        return r.returncode == 0, f"state {step['assert']}", r.stdout + r.stderr
    if step.get("stop"):
        r = cli(session, "stop")
        return r.returncode == 0, "client stops", r.stdout + r.stderr
    return False, "known step kind", f"unrecognised step: {json.dumps(step)}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("flow")
    ap.add_argument("--param", action="append", default=[], metavar="K=V")
    ap.add_argument("--keep", action="store_true", help="leave sessions running after the flow")
    ap.add_argument("--bundle-dir", default=None, help="parent dir for failure bundles")
    args = ap.parse_args()
    flow = json.loads(Path(args.flow).read_text())
    params = dict(flow.get("params", {}))
    params.update(p.split("=", 1) for p in args.param)
    used, started = [], []
    for i, raw in enumerate(flow["steps"], 1):
        step = sub(raw, params)
        session = step.get("session", "default")
        if session not in used:
            used.append(session)
        ok, expected, detail = run_step(step, session, params)
        label = f"step {i} [{session}] {step.get('name', expected)}"
        if step.get("start") and ok:
            started.append(session)
        if ok:
            print(f"PASS {label}")
            continue
        print(f"FAIL {label}\n  expected: {expected}\n  output:   {detail.strip()}\n  observed: {observed(session)}")
        for s in used:
            extra = ["--out", str(Path(args.bundle_dir) / f"{s}")] if args.bundle_dir else []
            r = cli(s, "bundle", *extra)
            print(f"  bundle[{s}]: {r.stdout.strip() or r.stderr.strip()}")
        if not args.keep:
            for s in started:
                cli(s, "stop")
        sys.exit(1)
    if not args.keep:
        for s in started:
            if not any(st.get("stop") and st.get("session", "default") == s for st in flow["steps"]):
                cli(s, "stop")
    print(f"flow {flow.get('name', args.flow)} passed ({len(flow['steps'])} steps)")


if __name__ == "__main__":
    main()
