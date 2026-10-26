#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Download the exact benchmark models, verify hashes and write relocated models.json.

No model code is executed. Uses the model definitions in benchmarks/asr/models.json.
"""
import argparse
import hashlib
import json
import shutil
import urllib.request
import zipfile
from pathlib import Path


def sha(path):
    with path.open('rb') as handle:return hashlib.file_digest(handle,'sha256').hexdigest()


def download(url,path,expected=None):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or (expected and sha(path)!=expected):
        temporary=path.with_suffix(path.suffix+'.part')
        with urllib.request.urlopen(url,timeout=120) as source,temporary.open('wb') as output:
            shutil.copyfileobj(source,output)
        if expected and sha(temporary)!=expected:raise ValueError(f'Hash mismatch: {url}')
        temporary.replace(path)
    if expected and sha(path)!=expected:raise ValueError(f'Hash mismatch: {path}')


def directory_sha(directory):
    entries=[]
    for path in sorted(p for p in directory.rglob('*') if p.is_file()):
        entries.append(f'{path.relative_to(directory).as_posix()}\0{sha(path)}\n')
    return hashlib.sha256(''.join(entries).encode()).hexdigest()


def prepare(manifest,root):
    models=json.loads(manifest.read_text())['models'];root.mkdir(parents=True,exist_ok=True)
    for model in models:
        if model['engine']=='vosk':
            target=root/'models/vosk'/model['model']
            if not target.is_dir() or directory_sha(target)!=model['sha256']:
                archive=root/'downloads'/(model['model']+'.zip')
                download('https://alphacephei.com/vosk/models/'+archive.name,archive,model.get('archive_sha256'))
                with zipfile.ZipFile(archive) as package:
                    for entry in package.infolist():
                        path=Path(entry.filename)
                        if path.is_absolute() or '..' in path.parts or path.parts[0]!=model['model']:
                            raise ValueError(f'Unsafe archive member: {entry.filename}')
                    package.extractall(target.parent)
            if directory_sha(target)!=model['sha256']:raise ValueError(f'Directory hash mismatch: {target}')
        elif model['engine']=='whisper.cpp':
            target=root/'models/whisper'/(model['model']+'.bin')
            revision=model['version'].split('@')[1]
            download(model['source_url'].replace('/resolve/main/',f'/resolve/{revision}/'),target,model['sha256'])
        elif model['engine']=='moonshine-voice':
            target=root/'models/moonshine'/model['model']/model['version']
            for entry in model['files']:
                if Path(entry['name']).name!=entry['name']:raise ValueError('Expected a flat model file')
                download(model['source_url']+'/'+entry['name'],target/entry['name'],entry['sha256'])
        else:raise ValueError(model['engine'])
        model['path']=str(target.resolve());print(f'Verified {model["model"]}',flush=True)
    (root/'models.json').write_text(json.dumps({'models':models},indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args();prepare(args.manifest,args.destination)
