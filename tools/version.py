#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Kalipso's version, kept the same in every place that states it.

Versions go up one step at a time, 0.1, 0.2 ... 0.9, 1.0, 1.1 ... 1.9, 2.0: ten steps between public
releases. Every step is tagged and built (a draft GitHub release, visible to maintainers only); only
the first version (0.1) and every X.0 are published.

  version.py check [vX.Y]     every file states the same version (and the tag's, if given)
  version.py public vX.Y      exit 0 if that tag is published, 1 if it stays a draft
  version.py next             print the version after the current one
  version.py set X.Y [--stable YYYY-MM-DD]   write a version everywhere; --stable records the release date
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE = ROOT / 'CMakeLists.txt'
METAINFO = ROOT / 'packaging/io.github.brianidavide.kalipso.metainfo.xml'
JSON_FILES = {ROOT / 'desktop/plasma/metadata.json': ('KPlugin', 'Version'),
              ROOT / 'packaging/kwin/metadata.json': ('KPlugin', 'Version'),
              ROOT / 'desktop/gnome/metadata.json': (None, 'version-name')}
FIRST = '0.1'
PATTERN = re.compile(r'^(0|[1-9][0-9]*)\.[0-9]$')  # One digit after the point: ten steps per major.


def parse(version):
    if not PATTERN.match(version):
        sys.exit(f'{version!r} is not a Kalipso version (X.Y, with Y from 0 to 9)')
    major, step = version.split('.')
    return int(major), int(step)


def read():
    found = {}
    found[CMAKE.name] = re.search(r'project\(Kalipso VERSION ([0-9.]+)', CMAKE.read_text()).group(1)
    found[METAINFO.name] = re.search(r'<release version="([^"]+)"', METAINFO.read_text()).group(1)
    for path, (section, key) in JSON_FILES.items():
        data = json.loads(path.read_text())
        found[str(path.relative_to(ROOT))] = (data[section] if section else data)[key]
    return found


def current():
    versions = set(read().values())
    if len(versions) != 1:
        sys.exit('Versions differ:\n' + '\n'.join(f'  {name}: {value}' for name, value in read().items()))
    return versions.pop()


def is_public(version):
    major, step = parse(version)
    return version == FIRST or step == 0


def following(version):
    major, step = parse(version)
    return f'{major + 1}.0' if step == 9 else f'{major}.{step + 1}'


def write(version, stable_date=None):
    parse(version)
    text = CMAKE.read_text()
    CMAKE.write_text(re.sub(r'(project\(Kalipso VERSION )[0-9.]+', rf'\g<1>{version}', text, count=1))
    for path, (section, key) in JSON_FILES.items():
        raw = path.read_text()
        old = (json.loads(raw)[section] if section else json.loads(raw))[key]
        path.write_text(raw.replace(f'"{key}": "{old}"', f'"{key}": "{version}"', 1))
    # The newest release goes first in AppStream; a development entry is replaced, not stacked.
    metainfo = METAINFO.read_text()
    kind = 'stable' if stable_date else 'development'
    date = stable_date or re.search(r'<release version="[^"]+" date="([^"]+)"', metainfo).group(1)
    entry = f'<release version="{version}" date="{date}" type="{kind}">'
    if re.search(r'<release version="[^"]+" date="[^"]+" type="development">', metainfo):
        metainfo = re.sub(r'<release version="[^"]+" date="[^"]+" type="development">', entry, metainfo, count=1)
    else:
        metainfo = metainfo.replace('<releases>\n    <release ', f'<releases>\n    {entry}\n      <description><p>Kalipso {version}.</p></description>\n    </release>\n    <release ', 1)
    METAINFO.write_text(metainfo)


def main(args):
    if not args or args[0] not in {'check', 'public', 'next', 'set'}:
        sys.exit(__doc__)
    command = args[0]
    if command == 'check':
        version = current()
        parse(version)
        if len(args) > 1 and args[1] != f'v{version}':
            sys.exit(f'Tag {args[1]} does not match the version {version}')
        print(f'Version {version} everywhere' + (' (published when tagged)' if is_public(version) else ' (draft when tagged)'))
    elif command == 'public':
        sys.exit(0 if is_public(args[1].removeprefix('v')) else 1)
    elif command == 'next':
        print(following(current()))
    elif command == 'set':
        stable = args[args.index('--stable') + 1] if '--stable' in args else None
        write(args[1], stable)
        print(f'Version {current()}')


if __name__ == '__main__':
    main(sys.argv[1:])
