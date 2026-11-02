"""Screen-driven login behavior without starting a graphical client."""

import argparse
import contextlib
import fcntl
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ms_headless


class MemoryLog:
    def __init__(self, contents):
        self.contents = contents.encode()
        self.offset = 0

    def exists(self):
        return True

    def open(self, mode):
        assert mode == "rb"
        return self

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        pass

    def readlines(self):
        unread = self.contents[self.offset:]
        self.offset = len(self.contents)
        return unread.splitlines(keepends=True)

    def append(self, line):
        self.contents += line.encode()

    def __str__(self):
        return "memory-client.log"


class PlayTests(unittest.TestCase):
    def test_play_advances_only_after_screen_events(self):
        log = MemoryLog("HEADLESS_STATE login\n")
        steps = iter((
            "HEADLESS_STATE world_select\n",
            "HEADLESS_STATE channel_select\n",
            "HEADLESS_STATE character_select count=1 pic=2\n",
            "HEADLESS_STATE in_game\n",
        ))
        keys = []
        fields = []

        def press(_state, key):
            self.assertEqual(key, "Return")
            keys.append(key)
            log.append(next(steps))

        args = argparse.Namespace(username="account", password_stdin=True, timeout=2)
        with (patch.object(ms_headless, "CLIENT_LOG", log),
              patch.object(ms_headless, "read_state", return_value={}),
              patch.object(ms_headless, "client_alive", return_value=True),
              patch.object(ms_headless, "focus_game", return_value="window"),
              patch.object(ms_headless, "replace_login_field",
                           side_effect=lambda *params: fields.append(params[4:])),
              patch.object(ms_headless, "press", side_effect=press),
              patch.object(ms_headless.sys, "stdin", io.StringIO("password\n")),
              contextlib.redirect_stdout(io.StringIO()) as output):
            ms_headless.cmd_play(args)

        self.assertEqual(keys, ["Return"] * 4)
        self.assertEqual(fields, [("account",), ("password",)])
        self.assertIn('"screen": "in_game"', output.getvalue())

    def test_play_stops_at_pic_prompt(self):
        log = MemoryLog("HEADLESS_STATE character_select count=1 pic=1\n")
        args = argparse.Namespace(username=None, password_stdin=False, timeout=2)
        with (patch.object(ms_headless, "CLIENT_LOG", log),
              patch.object(ms_headless, "read_state", return_value={}),
              patch.object(ms_headless, "focus_game", return_value="window"),
              patch.object(ms_headless, "press") as press):
            with self.assertRaisesRegex(SystemExit, "requires a PIC"):
                ms_headless.cmd_play(args)
        press.assert_not_called()

    def test_login_rejection_stops_before_next_key(self):
        log = MemoryLog("HEADLESS_STATE login\n")
        args = argparse.Namespace(username=None, password_stdin=False, timeout=2)
        keys = []

        def press(_state, key):
            keys.append(key)
            log.append("HEADLESS_ERROR login reason=5\n")

        with (patch.object(ms_headless, "CLIENT_LOG", log),
              patch.object(ms_headless, "read_state", return_value={}),
              patch.object(ms_headless, "client_alive", return_value=True),
              patch.object(ms_headless, "focus_game", return_value="window"),
              patch.object(ms_headless, "replace_login_field"),
              patch.object(ms_headless, "press", side_effect=press),
              patch.object(ms_headless.sys.stdin, "isatty", return_value=True),
              patch.object(ms_headless.getpass, "getpass", return_value="password")):
            with self.assertRaisesRegex(SystemExit, "Login rejected"):
                ms_headless.cmd_play(args)
        self.assertEqual(keys, ["Return"])


class GMOutcomeTests(unittest.TestCase):
    def test_gm_waits_for_observed_state_after_expected_reply(self):
        args = argparse.Namespace(
            text="!map", stdin=False, expect="Moved", timeout=8,
            wait_state="map_id==100000000", completion_timeout=3)
        waits = []
        with (patch.object(ms_headless, "read_state", return_value={}),
              patch.object(ms_headless, "last_seq", return_value=4),
              patch.object(ms_headless, "focus_game"),
              patch.object(ms_headless, "press"),
              patch.object(ms_headless, "type_text"),
              patch.object(ms_headless.time, "sleep"),
              patch.object(ms_headless, "wait_for", side_effect=lambda *a, **kw:
                           waits.append((a, kw)) or {"ok": True}),
              contextlib.redirect_stdout(io.StringIO())):
            ms_headless.cmd_gm(args)

        self.assertEqual(waits[0][1]["what"], "reply containing 'Moved'")
        self.assertEqual(waits[1][1]["what"], "state after GM command")
        self.assertEqual(waits[1][0][1], 3)
        self.assertEqual(waits[1][1]["state_expr"], "map_id==100000000")


class ActionTransportTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.state_dir = Path(self.tempdir.name)
        self.patches = (
            patch.object(ms_headless, "STATE_DIR", self.state_dir),
            patch.object(ms_headless, "last_seq", return_value=12),
        )
        for item in self.patches:
            item.start()

    def tearDown(self):
        for item in reversed(self.patches):
            item.stop()
        self.tempdir.cleanup()

    def test_action_separates_dispatch_from_observed_completion(self):
        observed = iter((
            {"type": "act", "id": "request", "ok": True},
            {"type": "state", "map_id": 100000000},
        ))
        waits = []

        def wait_for(*_args, **kwargs):
            waits.append(kwargs)
            result = next(observed)
            if kwargs.get("state_expr"):
                return result
            request_id = ms_headless.STATE_DIR.joinpath("act.txt").read_text().split()[0]
            result["text"] = request_id
            return result

        with patch.object(ms_headless, "wait_for", side_effect=wait_for):
            result = ms_headless.dispatch_action(
                {}, "jump", [], 2, "map_id==100000000", 5)

        self.assertTrue(result["dispatch"]["ok"])
        self.assertEqual(result["completion"]["map_id"], 100000000)
        self.assertEqual(len(waits), 2)
        self.assertEqual(waits[0]["what"], "result of act jump")
        self.assertEqual(waits[1]["what"], "state after act jump")
        self.assertFalse((self.state_dir / "act.txt").exists())

    def test_timed_out_dispatch_writes_cancellation_for_same_request(self):
        request_ids = []

        def timeout(*_args, **_kwargs):
            request_ids.append((self.state_dir / "act.txt").read_text().split()[0])
            raise TimeoutError("timed out")

        with patch.object(ms_headless, "wait_for", side_effect=timeout):
            with self.assertRaises(TimeoutError):
                ms_headless.dispatch_action({}, "move", ["right", "100"], 1)

        self.assertEqual(len(request_ids), 1)
        self.assertEqual((self.state_dir / "cancel.txt").read_text().strip(), request_ids[0])
        self.assertFalse((self.state_dir / "act.txt").exists())

    def test_overlapping_action_is_rejected_while_mailbox_is_locked(self):
        lock_path = self.state_dir / "act.lock"
        with lock_path.open("a") as owner:
            fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
            args = argparse.Namespace(
                action="jump", arg=[], timeout=2, wait_state=None,
                completion_timeout=None)
            with (patch.object(ms_headless, "read_state") as read_state,
                  contextlib.redirect_stderr(io.StringIO()) as error):
                with self.assertRaises(SystemExit) as raised:
                    ms_headless.cmd_act(args)

        self.assertIn("another semantic action is still in progress",
                      str(raised.exception))
        self.assertEqual(error.getvalue(), "")
        read_state.assert_not_called()

    def test_batch_stops_at_first_rejected_action(self):
        args = argparse.Namespace(timeout=2)
        results = iter((
            {"dispatch": {"ok": True}, "completion": "not_requested"},
            {"dispatch": {"ok": False}, "completion": "not_dispatched"},
        ))
        calls = []

        def dispatch(_state, action, argv, *_args):
            calls.append((action, argv))
            return next(results)

        with (patch.object(ms_headless, "read_state", return_value={}),
              patch.object(ms_headless, "dispatch_action", side_effect=dispatch),
              patch.object(ms_headless.sys, "stdin", io.StringIO(
                  '[["jump"],["window","stats","open"],["attack"]]')),
              contextlib.redirect_stdout(io.StringIO()) as output):
            with self.assertRaises(SystemExit) as raised:
                ms_headless.cmd_batch(args)

        self.assertEqual(raised.exception.code, 1)
        self.assertEqual(calls, [("jump", []), ("window", ["stats", "open"])])
        self.assertIn('"stopped_at": 1', output.getvalue())


if __name__ == "__main__":
    unittest.main()
