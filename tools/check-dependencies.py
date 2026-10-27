#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Report newer stable build tools. Never rewrites pins or resolves releases at build time."""
import json
import re
import sys
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]


def fetch(url):
    request = Request(url, headers={'User-Agent': 'Kalipso-dependency-check', 'Accept': 'application/json'})
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def version(value):
    return tuple(int(part) for part in value.lstrip('v').split('.'))


def main():
    pinned = json.loads((ROOT / 'tools/dependency-versions.json').read_text())
    browser = json.loads((ROOT / 'tools/browser/package.json').read_text())['devDependencies']
    sources = {
        'aqt': 'https://pypi.org/pypi/aqtinstall/json',
        'qt': 'https://api.github.com/repos/qt/qtbase/tags?per_page=100',
        'node': 'https://nodejs.org/dist/index.json',
        'python': 'https://raw.githubusercontent.com/actions/python-versions/main/versions-manifest.json',
        'cmake': 'https://pypi.org/pypi/cmake/json',
        'playwright': 'https://registry.npmjs.org/playwright/latest',
        'axe-core': 'https://registry.npmjs.org/axe-core/latest',
    }
    report = []
    for name, current in (pinned | browser).items():
        try:
            data = fetch(sources[name])
            if name == 'qt':
                candidates = [row['name'].removeprefix('v') for row in data if re.fullmatch(r'v6\.\d+\.\d+', row['name'])]
            elif name == 'node':
                candidates = [row['version'].removeprefix('v') for row in data if re.fullmatch(r'v\d+\.\d+\.\d+', row['version'])]
            elif name == 'python':
                candidates = [row['version'] for row in data if row.get('stable') and re.fullmatch(r'\d+\.\d+\.\d+', row['version'])]
            else:
                candidates = [data['info']['version'] if name in ('cmake', 'aqt') else data['version']]
            latest = max(candidates, key=version)
            report.append({'name': name, 'pinned': current, 'latest': latest,
                           'update_available': version(latest) > version(current), 'source': sources[name]})
        except Exception as error:
            report.append({'name': name, 'pinned': current, 'error': str(error), 'source': sources[name]})
    print(json.dumps(report, indent=2))
    return int(any(row.get('update_available') or 'error' in row for row in report))


if __name__ == '__main__':
    sys.exit(main())
