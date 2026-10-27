#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Package original source only; no server, models, recordings or binaries."""
from pathlib import Path
import hashlib
import zipfile

base = Path(__file__).resolve().parent
root = base.parent
out = root / 'artifacts/packages'
out.mkdir(parents=True, exist_ok=True)
assert (base / 'tracker.mjs').read_bytes() == (base / 'plasma/contents/ui/tracker.mjs').read_bytes()
assert (base / 'unicode-tokens.mjs').read_bytes() == (base / 'plasma/contents/ui/unicode-tokens.mjs').read_bytes()
packages = {
    'kalipso-plasma.plasmoid': [(base / 'plasma' / name, name) for name in [
        'metadata.json', 'contents/ui/main.qml', 'contents/ui/LocalEngine.qml', 'contents/ui/Tokens.js',
        'contents/ui/tracker.mjs', 'contents/ui/unicode-tokens.mjs', 'contents/ui/contrast.mjs']],
    'kalipso-gnome.zip': [(base / src, dst) for src, dst in [
        ('gnome/metadata.json', 'metadata.json'), ('gnome/extension.js', 'extension.js'),
        ('tracker.mjs', 'tracker.mjs'), ('contrast.mjs', 'contrast.mjs'), ('unicode-tokens.mjs', 'unicode-tokens.mjs'), ('gnome/local-engine.js', 'local-engine.js'), ('gnome/tokens.js', 'tokens.js'), ('gnome/stylesheet.css', 'stylesheet.css')]],
}
for name, files in packages.items():
    with zipfile.ZipFile(out / name, 'w', zipfile.ZIP_DEFLATED) as archive:
        for source, destination in files + [(root / 'LICENSE', 'LICENSE'), (root / 'LICENSES/Unicode-3.0.txt', 'Unicode-LICENSE.txt'), (root / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md'), (root / 'docs/local-engine.md', 'SETUP.md')]:
            assert source.suffix in {'.qml', '.mjs', '.js', '.css', '.json', '.txt', '.md'} or source.name == 'LICENSE'
            info = zipfile.ZipInfo(destination, (2026, 10, 26, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())
    print(hashlib.sha256((out / name).read_bytes()).hexdigest(), name)
