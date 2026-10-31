#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the Kalipso companion from this source tree and set up voice following.

The companion is installed under --prefix (default ~/.local) with its D-Bus service. With --language it
then downloads that language's speech model, as Settings does in Kalipso; --model (with --backend and
optionally --library) uses a model installed by hand instead. Nothing opens the microphone.
Installed packages and the companion download do not need this: choose the language in Settings, or
run kalipso-voice-setup --language CODE.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'tools/data/speech-catalog.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--language', help='Language to download, for example it or en')
    parser.add_argument('--backend', choices=['moonshine', 'vosk'],
                        help='Speech engine; by default the one that follows the language best')
    parser.add_argument('--list-models', action='store_true', help='Show the languages Kalipso can download')
    parser.add_argument('--model', type=Path, help='A model folder installed by hand, instead of downloading')
    parser.add_argument('--library', help='The native library for --model, as a name or a path')
    parser.add_argument('--architecture', type=int, choices=[2, 3, 4, 5],
                        help='Moonshine streaming architecture of --model: tiny=2, base=3, small=4, medium=5')
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
    args = parser.parse_args()
    if args.list_models:
        catalog = json.loads(CATALOG.read_text())
        for backend in ['moonshine', 'vosk']:
            print(backend + ': ' + ', '.join(catalog[backend]['models']))
        return
    if not args.language and not args.model:
        parser.error('Choose --language CODE, or --model with a folder installed by hand.')
    if args.model and not args.backend:
        parser.error('--model needs --backend moonshine or vosk.')
    for program in ('cmake', 'c++', 'pw-record'):
        if not shutil.which(program):
            parser.error(f'Missing {program}. See docs/local-engine.md for distribution dependencies.')
    prefix = args.prefix.expanduser().resolve()

    def run(arguments):
        subprocess.run([str(arg) for arg in arguments], check=True)

    build = ROOT / 'build-local-engine'
    run(['cmake', '-S', ROOT, '-B', build, '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=Release',
         f'-DCMAKE_INSTALL_PREFIX={prefix}'])
    run(['cmake', '--build', build, '--target', 'kalipso-engine', '--parallel', '2'])
    run(['cmake', '--install', build, '--component', 'Engine'])
    engine = prefix / 'bin/kalipso-engine'
    if args.model:
        configure = [engine, '--configure', '--backend', args.backend, '--model', args.model.expanduser().resolve()]
        for option, value in [('--library', args.library), ('--language', args.language),
                              ('--architecture', args.architecture)]:
            if value:
                configure += [option, value]
        run(configure)
    else:
        run([engine, '--download', '--language', args.language] + (['--backend', args.backend] if args.backend else []))
    print(f'The companion is in {prefix / "bin"}. If the widget or extension says it is missing, log out and back in.')


if __name__ == '__main__':
    main()
