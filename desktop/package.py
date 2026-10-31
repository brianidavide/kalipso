#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the Plasma widget, GNOME extension and KWin script archives from source.

Archives contain only frontend source and notices: no companion binary,
speech model or recording. Output: artifacts/packages/. File timestamps are
fixed so identical sources produce identical archives.
"""
from pathlib import Path
import ast
import hashlib
import struct
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
PLASMA_DOMAIN = 'plasma_applet_io.github.brianidavide.kalipso'
GNOME_DOMAIN = 'kalipso'


def compile_po(path):
    """Compile a .po file (plurals included, no contexts) to .mo bytes."""
    messages = {}
    entry, field = {}, None
    fuzzy = False  # "#, fuzzy" translations are guesses from msgmerge: never ship them.

    def finish():
        if 'msgid' not in entry or 'msgstr' not in entry and 'msgstr[0]' not in entry:
            return
        if fuzzy and entry['msgid']:
            return
        if 'msgid_plural' in entry:  # .mo plural: "singular\0plural" -> "form0\0form1..."
            forms = [entry[f'msgstr[{n}]'] for n in range(10) if f'msgstr[{n}]' in entry]
            if any(forms):
                messages[entry['msgid'] + '\0' + entry['msgid_plural']] = '\0'.join(forms)
        elif entry['msgstr'] or not entry['msgid']:
            messages[entry['msgid']] = entry['msgstr']

    for line in path.read_text(encoding='utf-8').splitlines() + ['']:
        line = line.strip()
        if line.startswith('#,') and 'fuzzy' in line:
            finish()
            entry, field, fuzzy = {}, None, True
            continue
        if line.startswith('msgid '):
            if field is not None:
                finish()
                fuzzy = False
            entry, field = {}, 'msgid'
        elif line.startswith(('msgid_plural ', 'msgstr ', 'msgstr[')):
            field = line.split(' ', 1)[0]
        elif not line.startswith('"'):
            continue
        entry[field] = entry.get(field, '') + ast.literal_eval(line[line.index('"'):])
    finish()
    keys = sorted(messages)
    ids = [key.encode() for key in keys]
    strs = [messages[key].encode() for key in keys]
    header_size = 7 * 4
    table = header_size + 16 * len(keys)
    ids_data = b''.join(item + b'\0' for item in ids)
    offsets, position = [], table
    for item in ids:
        offsets.append((len(item), position))
        position += len(item) + 1
    str_offsets = []
    for item in strs:
        str_offsets.append((len(item), position))
        position += len(item) + 1
    output = struct.pack('<7I', 0x950412de, 0, len(keys), header_size, header_size + 8 * len(keys), 0, 0)
    output += b''.join(struct.pack('<2I', *pair) for pair in offsets)
    output += b''.join(struct.pack('<2I', *pair) for pair in str_offsets)
    return output + ids_data + b''.join(item + b'\0' for item in strs)


def translations(prefix, domain):
    return [(path, f'{prefix}{path.stem}/LC_MESSAGES/{domain}.mo', compile_po(path))
            for path in sorted((BASE / 'po').glob('*.po'))]


def plasma_files():
    package = BASE / 'plasma'
    # The widget draws its text in the app's typeface, embedded like the app (src/fonts, OFL).
    fonts = [(path, 'contents/fonts/' + path.name) for path in sorted((ROOT / 'src/fonts').iterdir())
             if path.suffix in {'.ttf', '.txt'}]
    # Kalipso's icon, shown in a panel: the widget may be installed without the app and its icon.
    icon = [(ROOT / 'packaging/io.github.brianidavide.kalipso.svg', 'contents/images/kalipso.svg')]
    return [(path, path.relative_to(package).as_posix()) for path in sorted(package.rglob('*'))
            if path.is_file() and path.suffix in SOURCE_SUFFIXES] + fonts + icon


def gnome_files():
    extension = BASE / 'gnome'
    own = [(path, path.name) for path in sorted(extension.iterdir()) if path.suffix in SOURCE_SUFFIXES]
    return own + [(BASE / name, name) for name in GNOME_SHARED]


def kwin_files():
    package = ROOT / 'packaging/kwin'
    return [(path, path.relative_to(package).as_posix()) for path in sorted(package.rglob('*'))
            if path.is_file() and path.suffix in SOURCE_SUFFIXES]


def write(name, files, generated=()):
    with zipfile.ZipFile(OUT / name, 'w', zipfile.ZIP_DEFLATED) as archive:
        entries = [(destination, source.read_bytes()) for source, destination in files + NOTICES]
        entries += [(destination, data) for _, destination, data in generated]
        for destination, data in entries:
            info = zipfile.ZipInfo(destination, (2026, 10, 26, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(hashlib.sha256((OUT / name).read_bytes()).hexdigest(), name)


def main():
    subprocess.run([sys.executable, str(ROOT / 'tools/sync-shared.py'), '--check'], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    write('kalipso-plasma.plasmoid', plasma_files(), translations('contents/locale/', PLASMA_DOMAIN))
    write('kalipso-gnome.zip', gnome_files(), translations('locale/', GNOME_DOMAIN))
    write('kalipso-kwin.kwinscript', kwin_files())


if __name__ == '__main__':
    main()
