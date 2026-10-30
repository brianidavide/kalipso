#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Set up voice following: the Kalipso companion, one speech runtime and one language model.

Three ways to get the companion:
  (default)        compile it from this source tree (needs CMake, a C++ compiler and Qt);
  --engine PATH    use a built companion, as shipped in the kalipso-companion download;
  --system         use the companion installed by a system package (.deb, Arch): only download and configure.
Downloads require --download; nothing opens the microphone.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from speech_assets import CATALOG, ROOT, install_assets

SERVICE = 'io.github.brianidavide.Kalipso.Engine'


def default_backend(language):
    """Moonshine when the catalog has the language (it follows speech more closely), otherwise Vosk."""
    catalog = json.loads(CATALOG.read_text())
    return 'moonshine' if not language or language in catalog['moonshine']['models'] else 'vosk'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--backend', choices=['moonshine', 'vosk'],
                        help='Speech runtime; by default Moonshine when it has the language, otherwise Vosk')
    parser.add_argument('--language', help='Model language, chosen explicitly; never inferred from desktop locale')
    parser.add_argument('--download', action='store_true', help='Download the pinned runtime and selected language model only')
    parser.add_argument('--list-models', action='store_true')
    parser.add_argument('--model', type=Path, help='Existing extracted model folder; alternative to --download')
    parser.add_argument('--library', help='Installed native library name or absolute library path')
    parser.add_argument('--architecture', type=int, choices=[2, 3, 4, 5], default=2,
                        help='Moonshine streaming architecture: tiny=2, base=3, small=4, medium=5')
    parser.add_argument('--engine', type=Path, help='A built kalipso-engine to install instead of compiling one')
    parser.add_argument('--system', action='store_true', help='Use the kalipso-engine installed by a system package')
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
    parser.add_argument('--cache', type=Path, default=Path.home() / '.cache/kalipso-downloads')
    args = parser.parse_args()
    if args.list_models:
        catalog = json.loads(CATALOG.read_text())
        for backend in ['moonshine', 'vosk']:
            print(backend + ': ' + ', '.join(catalog[backend]['models']))
        print('Other compatible models can be supplied with --model/--library. Moonshine Italian is not in the catalog.')
        return
    if args.backend is None:
        args.backend = default_backend(args.language)
    if args.download and (not args.language or args.model or args.library or args.architecture != 2):
        parser.error('--download requires --language and uses the catalog Tiny model/runtime; omit --model, --library and alternate --architecture.')
    if not args.download and not args.model:
        parser.error('Choose --download --language CODE, or supply --model and optionally --library.')
    if args.engine and args.system:
        parser.error('Choose --engine or --system, not both.')
    tools = ('cmake', 'c++', 'pw-record') if not (args.engine or args.system) else ('pw-record',)
    for program in tools:
        if not shutil.which(program):
            parser.error(f'Missing {program}. See docs/local-engine.md for distribution dependencies.')
    prefix = args.prefix.expanduser().resolve()

    def run(arguments):
        subprocess.run([str(arg) for arg in arguments], check=True)

    if args.system:
        engine = shutil.which('kalipso-engine')
        if not engine:
            parser.error('No kalipso-engine on this system. Install the Kalipso package first, or use --engine.')
    elif args.engine:
        engine = args.engine.expanduser().resolve()
        if not engine.is_file():
            parser.error(f'{engine} is not a file.')
    else:
        build = ROOT / 'build-local-engine'
        # Compile before any downloads, so missing Qt/toolchain fails immediately.
        run(['cmake', '-S', ROOT, '-B', build, '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}'])
        run(['cmake', '--build', build, '--target', 'kalipso-engine', '--parallel', '2'])
        engine = build / 'kalipso-engine'
    if args.download:
        try:
            library, model = install_assets(args.backend, args.language, prefix, args.cache.expanduser().resolve())
        except (OSError, ValueError) as error:
            parser.error(str(error))
    else:
        model = args.model.expanduser().resolve()
        library = args.library or ('libmoonshine.so' if args.backend == 'moonshine' else 'libvosk.so')
    configure = [engine, '--configure', '--backend', args.backend, '--library', library, '--model', model,
                 '--architecture', args.architecture]
    if args.language:
        configure += ['--language', args.language]
    run(configure)
    if args.engine:
        # The user's own copy, registered with the session bus like the system package's.
        installed = prefix / 'bin/kalipso-engine'
        installed.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(engine, installed)
        installed.chmod(0o755)
        services = prefix / 'share/dbus-1/services'
        services.mkdir(parents=True, exist_ok=True)
        (services / f'{SERVICE}.service').write_text(f'[D-BUS Service]\nName={SERVICE}\nExec="{installed}" --serve\n')
        license_file = ROOT / 'LICENSE'
        if license_file.is_file():
            (prefix / 'share/licenses/kalipso-engine').mkdir(parents=True, exist_ok=True)
            shutil.copy2(license_file, prefix / 'share/licenses/kalipso-engine/LICENSE')
    elif not args.system:
        run(['cmake', '--install', build, '--component', 'Engine'])
    where = 'the system companion' if args.system else f'the companion in {prefix}'
    print(f'Set up {args.backend} ({args.language or "own model"}) for {where}. No microphone was opened.\n'
          f'In the widget or extension choose Follow my voice, then {args.backend.capitalize()}, then Start listening.\n'
          'If it says the companion is missing, log out and back in so the desktop finds it.')


if __name__ == '__main__':
    main()
