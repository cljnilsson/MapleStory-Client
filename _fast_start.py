#!/usr/bin/env python3
"""Reversible HeavenClient Linux fast-start patch + startup stage timings.

Run from MapleStory-Client root:
    python /path/to/maplestory_fast_start.py          # dry run
    python /path/to/maplestory_fast_start.py --apply  # backup + patch
"""
from __future__ import annotations

import argparse
import re
import shutil
from datetime import datetime
from pathlib import Path


def patch_configuration(s: str) -> tuple[str, str]:
    marker = 'bool start_shown = false;'
    new = 'bool start_shown = true;'
    if new in s:
        return s, 'logo already skipped'
    if s.count(marker) != 1:
        raise RuntimeError('Configuration.h: expected start_shown initializer not found')
    return s.replace(marker, new, 1), 'skip the Nexon/Wizet startup logo'


def patch_main(s: str) -> tuple[str, str]:
    mark = '[STARTUP]'
    if mark in s:
        return s, 'startup profiling already installed'
    start = s.find('Error init()')
    end = s.find('void update()', start)
    if start < 0 or end < 0:
        raise RuntimeError('MapleStory.cpp: init() / update() boundaries not found')
    fragment = s[start:end]
    header = re.search(r'Error\s+init\s*\(\s*\)\s*\{', fragment)
    if not header:
        raise RuntimeError('MapleStory.cpp: init() body not found')

    # Instrument each existing call without changing call order or error handling.
    begins = '''
        using StartupClock = std::chrono::steady_clock;
        const auto startup_begin = StartupClock::now();
        auto startup_last = startup_begin;
        auto startup_mark = [&](const char* label) {
            const auto now = StartupClock::now();
            const auto step_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startup_last).count();
            const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startup_begin).count();
            std::cerr << "[STARTUP] " << label << ": " << step_ms
                      << " ms (total " << total_ms << " ms)" << std::endl;
            startup_last = now;
        };
'''
    fragment = fragment[:header.end()] + begins + fragment[header.end():]

    calls = [
        ('Session::get().init()', 'session/connect'),
        ('NxFiles::init()', 'NX files'),
        ('Window::get().init()', 'window/OpenGL/fonts'),
        ('Music::init()', 'audio device'),
        ('Sound::init()', 'sound effects'),
    ]
    for call, label in calls:
        pattern = re.compile(r'(if\s*\(\s*Error\s+error\s*=\s*' + re.escape(call) + r'\s*\)\s*\n[ \t]*return\s+error\s*;)')
        matches = list(pattern.finditer(fragment))
        if len(matches) != 1:
            raise RuntimeError(f'MapleStory.cpp: expected exactly one init() call: {call}')
        fragment = pattern.sub(lambda m: m.group(1) + '\n        startup_mark("' + label + '");', fragment, count=1)

    direct = [
        ('Char::init();', 'character poses'),
        ('DamageNumber::init();', 'damage-number assets'),
        ('MapPortals::init();', 'portal assets'),
        ('Stage::get().init();', 'game stage'),
        ('UI::get().init();', 'login UI creation'),
    ]
    for call, label in direct:
        if fragment.count(call) != 1:
            raise RuntimeError(f'MapleStory.cpp: expected exactly one init() call: {call}')
        fragment = fragment.replace(call, call + '\n        startup_mark("' + label + '");', 1)

    if '#include <chrono>' not in s:
        s = '#include <chrono>\n' + s
        # Beginning of init() shifted by header text
        start += len('#include <chrono>\n')
        end += len('#include <chrono>\n')
    return s[:start] + fragment + s[end:], 'print startup timing for 10 initialization stages'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='backup and modify the sources')
    args = parser.parse_args()
    root = Path.cwd()
    files = [
        ('Configuration.h', patch_configuration),
        ('MapleStory.cpp', patch_main),
    ]
    changes: list[tuple[Path, str, str, str]] = []
    for relpath, fn in files:
        path = root / relpath
        if not path.exists():
            raise SystemExit(f'Missing {path}; run from the client repository root')
        old = path.read_text(encoding='utf-8-sig')
        try:
            new, result = fn(old)
        except RuntimeError as exc:
            raise SystemExit(f'Validation failed: {exc}. No files changed.')
        changes.append((path, old, new, result))

    print('HeavenClient fast-start patch (' + ('APPLY' if args.apply else 'DRY RUN') + ')')
    for path, old, new, result in changes:
        print(f'  {path.name}: {result}' + (' [unchanged]' if old == new else ''))
    if not args.apply:
        print('To apply: python /path/to/maplestory_fast_start.py --apply')
        return
    if all(old == new for _, old, new, _ in changes):
        print('Nothing to change.')
        return
    backup = root / ('fast-start-backup-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    backup.mkdir()
    for path, old, new, _ in changes:
        if old == new:
            continue
        shutil.copy2(path, backup / path.name)
        path.write_text(new, encoding='utf-8')
    print(f'Backups: {backup}')
    print('Rebuild and compare time to visible login screen.')


if __name__ == '__main__':
    main()
