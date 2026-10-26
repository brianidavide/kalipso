#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Install one chosen native backend and companion. Downloads require --download."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from speech_assets import CATALOG,install_assets


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend',choices=['moonshine','vosk'],default='moonshine')
    parser.add_argument('--language',help='Model language, chosen explicitly; never inferred from desktop locale')
    parser.add_argument('--download',action='store_true',help='Download the pinned runtime and selected language model only')
    parser.add_argument('--list-models',action='store_true')
    parser.add_argument('--model',type=Path,help='Existing extracted model folder; alternative to --download')
    parser.add_argument('--library',help='Installed native library name or absolute library path')
    parser.add_argument('--architecture',type=int,choices=[2,3,4,5],default=2,help='Moonshine streaming architecture: tiny=2, base=3, small=4, medium=5')
    parser.add_argument('--prefix',type=Path,default=Path.home()/'.local')
    parser.add_argument('--cache',type=Path,default=Path.home()/'.cache/kalipso-downloads')
    args=parser.parse_args()
    if args.list_models:
        catalog=json.loads(CATALOG.read_text())
        for backend in ['moonshine','vosk']:print(backend+': '+', '.join(catalog[backend]['models']))
        print('Other compatible models can be supplied with --model/--library. Moonshine Italian is not in the catalog.')
        return
    if args.download and (not args.language or args.model or args.library or args.architecture!=2):
        parser.error('--download requires --language and uses the catalog Tiny model/runtime; omit --model, --library and alternate --architecture.')
    if not args.download and not args.model:parser.error('Choose --download --language CODE, or supply --model and optionally --library.')
    for program in ('cmake','c++','pw-record'):
        if not shutil.which(program):parser.error(f'Missing {program}. See docs/local-engine.md for distribution dependencies.')
    root=Path(__file__).resolve().parents[1];prefix=args.prefix.expanduser().resolve()
    build=root/'build-local-engine'
    def run(arguments):subprocess.run([str(arg) for arg in arguments],check=True)
    # Compile before any downloads, so missing Qt/toolchain fails immediately.
    run(['cmake','-S',root,'-B',build,'-DBUILD_TESTING=OFF','-DCMAKE_BUILD_TYPE=Release',f'-DCMAKE_INSTALL_PREFIX={prefix}'])
    run(['cmake','--build',build,'--target','kalipso-engine','--parallel','2'])
    if args.download:
        try:library,model=install_assets(args.backend,args.language,prefix,args.cache.expanduser().resolve())
        except (OSError,ValueError) as error:parser.error(str(error))
    else:
        model=args.model.expanduser().resolve();library=args.library or ('libmoonshine.so' if args.backend=='moonshine' else 'libvosk.so')
    configure=[build/'kalipso-engine','--configure','--backend',args.backend,'--library',library,'--model',model,'--architecture',args.architecture]
    if args.language:configure+=['--language',args.language]
    run(configure)
    run(['cmake','--install',build,'--component','Engine'])
    print(f'Installed {args.backend} companion to {prefix}. No microphone was opened.\nSelect {args.backend} in the widget/extension, then Start. Other backend profiles are preserved.\nRestart the desktop session if the D-Bus service is not discovered; see docs/local-engine.md.')

if __name__=='__main__':main()
