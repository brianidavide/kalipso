#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Preserve Qt's offline license catalog and installed package metadata in CI bundles."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from html.parser import HTMLParser
from pathlib import Path
import shutil
import subprocess
from urllib.parse import urljoin, urlparse
from urllib.request import urlopen

class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = set()
    def handle_starttag(self, tag, attrs):
        if tag == 'a':
            self.links.update(value for key, value in attrs if key == 'href' and value)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('version')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    destination = args.output
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy(root / 'LICENSE', destination / 'Kalipso-AGPL-3.0.txt')
    shutil.copy(root / 'THIRD_PARTY_NOTICES.md', destination)
    shutil.copytree(root / 'LICENSES', destination / 'license-texts', dirs_exist_ok=True)
    qmake = shutil.which('qmake') or shutil.which('qmake6')
    if not qmake:
        raise RuntimeError('Qt qmake is not available')
    prefix = Path(subprocess.check_output([qmake, '-query', 'QT_INSTALL_PREFIX'], text=True).strip())
    for directory in ('sbom', 'LICENSES'):
        if (prefix / directory).is_dir():
            shutil.copytree(prefix / directory, destination / ('qt-' + directory), dirs_exist_ok=True)
    series = '.'.join(args.version.split('.')[:2])
    base = f'https://doc.qt.io/qt-{series}/'
    def fetch(name):
        try:
            with urlopen(base + name, timeout=60) as response:
                content = response.read().decode('utf-8')
        except Exception as error:
            raise RuntimeError(f'Cannot retrieve {base + name}: {error}') from error
        (destination / name).write_text(content, encoding='utf-8')
        return content
    index = fetch('licenses-used-in-qt.html')
    links = Links(); links.feed(index)
    pages = {'licensing.html', 'lgpl.html', 'gpl.html', 'fdl.html'}
    for link in links.links:
        url = urlparse(urljoin(base, link))
        if url.netloc == 'doc.qt.io' and url.path.startswith(urlparse(base).path):
            name = Path(url.path).name
            if '-attribution-' in name and name.endswith('.html'):
                pages.add(name)
    if not any('-attribution-' in name for name in pages):
        raise RuntimeError('Qt license catalog has no component notices')
    with ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(fetch, sorted(pages)))
    (destination / 'README.txt').write_text(
        f'Kalipso uses dynamically linked Qt {args.version}. Qt retains its own licensing terms.\n'
        f'Offline license catalog: licenses-used-in-qt.html (including linked component notices).\n'
        f'The catalog covers additional Qt modules; only licenses of shipped components apply.\n'
        f'Original catalog: {base}licenses-used-in-qt.html\n'
        f'Qt source: https://download.qt.io/archive/qt/{series}/{args.version}/submodules/\n'
        'Kalipso source: https://github.com/brianidavide/kalipso\n'
        'Qt libraries remain replaceable in the bundle; rebuilding instructions are in the source repository.\n', encoding='utf-8')
    print(f'Packaged Qt {args.version} license catalog and {len(pages)} notice pages')

if __name__ == '__main__':
    main()
