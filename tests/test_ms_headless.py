"""Screen-driven login behavior without starting a graphical client."""

import argparse
import contextlib
import io
from pathlib import Path
import sys
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


if __name__ == "__main__":
    unittest.main()
