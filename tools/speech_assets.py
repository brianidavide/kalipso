# SPDX-License-Identifier: AGPL-3.0-or-later
"""Explicit, hash-verified native runtime/model installation; standard library only."""
import hashlib
import json
import platform
import shutil
import stat
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CATALOG=ROOT/'tools/data/speech-catalog.json'

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def fetch(url,digest,cache):
    if not url.startswith('https://'):raise ValueError('Only HTTPS model sources are allowed')
    cache.mkdir(parents=True,exist_ok=True);target=cache/digest
    if target.exists() and sha(target)==digest:return target
    temporary=cache/(digest+'.part')
    print('Downloading '+url,flush=True)
    try:
        with urllib.request.urlopen(url,timeout=120) as source,temporary.open('wb') as output:
            if not source.geturl().startswith('https://'):raise ValueError('Insecure download redirect')
            shutil.copyfileobj(source,output)
        if sha(temporary)!=digest:raise ValueError('Download checksum mismatch: '+url)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    return target

def extract(archive,destination,allowed):
    """Validate every member before extracting a permitted subset. Reject links/traversal."""
    with zipfile.ZipFile(archive) as package:
        entries=package.infolist()
        for entry in entries:
            path=Path(entry.filename)
            if path.is_absolute() or '..' in path.parts or stat.S_ISLNK(entry.external_attr>>16):
                raise ValueError('Unsafe archive member: '+entry.filename)
        for entry in entries:
            if entry.is_dir() or not allowed(entry.filename):continue
            target=destination/entry.filename;target.parent.mkdir(parents=True,exist_ok=True)
            with package.open(entry) as source,target.open('wb') as output:shutil.copyfileobj(source,output)

def install_assets(backend,language,prefix,cache):
    if platform.system()!='Linux' or platform.machine() not in ('x86_64','AMD64'):
        raise ValueError('Automatic native downloads currently support Linux x86_64. Use --library and --model on other platforms.')
    catalog=json.loads(CATALOG.read_text());runtime=catalog[backend]
    if language not in runtime['models']:
        raise ValueError(f'No verified download for {backend}/{language}. Available: {", ".join(runtime["models"])}. Use --model for another compatible model; Moonshine Italian is not available in this catalog.')
    model=runtime['models'][language]
    wheel=fetch(runtime['url'],runtime['sha256'],cache)
    base=prefix/'share/kalipso';runtime_dir=base/'runtimes'/(backend+'-'+runtime['version'])
    runtime_dir.parent.mkdir(parents=True,exist_ok=True)
    if not runtime_dir.exists():
        with tempfile.TemporaryDirectory(dir=runtime_dir.parent,prefix='.install-') as staging:
            stage=Path(staging)/'runtime';stage.mkdir()
            extract(wheel,stage,lambda name:name in runtime['nativeFiles'])
            for name in runtime['nativeFiles']:
                if not (stage/name).is_file():raise ValueError('Native package is missing '+name)
            notices=ROOT/'third_party/native-notices'/backend
            if notices.exists():shutil.copytree(notices,stage/'notices')
            (stage/'source.json').write_text(json.dumps({k:v for k,v in runtime.items() if k!='models'},indent=2)+'\n')
            stage.rename(runtime_dir)
    # Detect changed/partial installed libraries instead of trusting only a directory name.
    with zipfile.ZipFile(wheel) as package:
        for name in runtime['nativeFiles']:
            if not (runtime_dir/name).is_file() or sha(runtime_dir/name)!=hashlib.sha256(package.read(name)).hexdigest():
                raise ValueError(f'Installed runtime differs from the pinned package: {runtime_dir}. Move that directory aside and retry.')
    model_dir=base/'models'/backend/model['name'];model_dir.parent.mkdir(parents=True,exist_ok=True)
    if backend=='moonshine':
        model_dir.mkdir(exist_ok=True)
        for file in model['files']:
            name=file['name']
            if Path(name).name!=name:raise ValueError('Invalid model filename')
            target=model_dir/name
            if target.exists() and sha(target)==file['sha256']:continue
            downloaded=fetch(model['source']+'/'+name,file['sha256'],cache)
            temp=model_dir/(name+'.part');shutil.copyfile(downloaded,temp);temp.replace(target)
    else:
        archive=fetch(model['url'],model['sha256'],cache)
        if not model_dir.exists():
            with tempfile.TemporaryDirectory(dir=model_dir.parent,prefix='.install-') as staging:
                extract(archive,Path(staging),lambda name:name.startswith(model['name']+'/'))
                (Path(staging)/model['name']).rename(model_dir)
        with zipfile.ZipFile(archive) as package:
            for entry in package.infolist():
                if entry.is_dir():continue
                target=model_dir.parent/entry.filename
                if not target.is_file() or sha(target)!=hashlib.sha256(package.read(entry)).hexdigest():
                    raise ValueError(f'Installed model differs from the pinned archive: {model_dir}. Move that directory aside and retry.')
    (model_dir/'kalipso-source.json').write_text(json.dumps({**model,'language':language},indent=2)+'\n')
    return runtime_dir/runtime['library'],model_dir
