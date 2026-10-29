#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Copy the shared JavaScript modules from desktop/ into the Plasma package and
the website, which must contain their own copies. --check fails on any drift."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MODULES = ['tracker.mjs', 'contrast.mjs', 'unicode-tokens.mjs']
TARGETS = [ROOT / 'desktop/plasma/contents/ui', ROOT / 'site/assets']


def main():
    check = '--check' in sys.argv[1:]
    stale = []
    for name in MODULES:
        source = (ROOT / 'desktop' / name).read_bytes()
        for target in TARGETS:
            path = target / name
            if path.exists() and path.read_bytes() == source:
                continue
            stale.append(path.relative_to(ROOT))
            if not check:
                path.write_bytes(source)
    if check and stale:
        sys.exit('Shared modules differ from desktop/; run tools/sync-shared.py:\n' +
                 '\n'.join(f'  {path}' for path in stale))
    print('Shared modules are in sync.' if check else f'Updated {len(stale)} file(s).')


if __name__ == '__main__':
    main()
