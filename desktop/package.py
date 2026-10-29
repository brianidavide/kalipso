#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the Plasma widget and GNOME extension archives from source.

Archives contain only frontend source and notices: no companion binary,
speech model or recording. Output: artifacts/packages/. File timestamps are
fixed so identical sources produce identical archives.
"""
from pathlib import Path
import hashlib
import subprocess
import sys
import zipfile

BASE = Path(__file__).resolve().parent
ROOT = BASE.parent
OUT = ROOT / 'artifacts/packages'
SOURCE_SUFFIXES = {'.qml', '.mjs', '.js', '.css', '.json', '.xml'}
NOTICES = [(ROOT / 'LICENSE', 'LICENSE'), (ROOT / 'LICENSES/Unicode-3.0.txt', 'Unicode-LICENSE.txt'),
           (ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')]
GNOME_SHARED = ['tracker.mjs', 'contrast.mjs', 'unicode-tokens.mjs']


def plasma_files():
    package = BASE / 'plasma'
    return [(path, path.relative_to(package).as_posix()) for path in sorted(package.rglob('*'))
            if path.is_file() and path.suffix in SOURCE_SUFFIXES]


def gnome_files():
    extension = BASE / 'gnome'
    own = [(path, path.name) for path in sorted(extension.iterdir()) if path.suffix in SOURCE_SUFFIXES]
    return own + [(BASE / name, name) for name in GNOME_SHARED]


def write(name, files):
    with zipfile.ZipFile(OUT / name, 'w', zipfile.ZIP_DEFLATED) as archive:
        for source, destination in files + NOTICES:
            info = zipfile.ZipInfo(destination, (2026, 10, 26, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())
    print(hashlib.sha256((OUT / name).read_bytes()).hexdigest(), name)


def main():
    subprocess.run([sys.executable, str(ROOT / 'tools/sync-shared.py'), '--check'], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    write('kalipso-plasma.plasmoid', plasma_files())
    write('kalipso-gnome.zip', gnome_files())


if __name__ == '__main__':
    main()
