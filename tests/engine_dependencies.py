#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Missing dependencies must be reported before any audio capture starts."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='kalipso-migration-test-') as directory:
    env = dict(os.environ, XDG_CONFIG_HOME=directory)
    legacy = Path(directory)/'Teleprompter/engine.conf'
    legacy.parent.mkdir()
    legacy.write_text('[speech]\nbackend=vosk\nmicrophone=my-input\n\n[profiles]\nvosk\\model=/existing/model\nvosk\\library=/existing/libvosk.so\nvosk\\language=it\n')
    result = subprocess.run([binary, '--list-backends'], env=env, text=True, capture_output=True, check=True)
    settings = json.loads(result.stdout)
    assert settings['selected'] == 'vosk'
    assert settings['backends'][1]['model'] == '/existing/model'
    assert settings['backends'][1]['language'] == 'it'
    migrated = Path(directory)/'Kalipso/engine.conf'
    assert 'my-input' in migrated.read_text()
    before = migrated.read_bytes()
    legacy.write_text('[speech]\nbackend=moonshine\n')
    result = subprocess.run([binary, '--list-backends'], env=env, text=True, capture_output=True, check=True)
    assert json.loads(result.stdout)['selected'] == 'vosk'
    assert migrated.read_bytes() == before
    print('PASS: legacy profiles imported once without overwriting Kalipso settings')

with tempfile.TemporaryDirectory(prefix='kalipso-engine-test-') as directory:
    env = dict(os.environ, XDG_CONFIG_HOME=directory, XDG_DATA_HOME=directory+'/data')
    check = subprocess.run([binary, '--configure', '--backend', 'vosk', '--library', directory+'/missing.so', '--model', directory+'/missing-model'], env=env, text=True, capture_output=True)
    assert check.returncode == 1, check
    dependencies = json.loads(check.stdout)
    assert not dependencies['ready'] and not dependencies['modelLoadTested']
    assert any('Vosk library' in item for item in dependencies['missing'])
    assert any('model is missing' in item for item in dependencies['missing'])
    child = subprocess.Popen([binary, '--serve'], env=env)
    def call(method,*arguments):
        result = subprocess.run(['gdbus','call','--session','--dest','io.github.brianidavide.Kalipso.Engine','--object-path','/io/github/brianidavide/Kalipso/Engine','--method','io.github.brianidavide.Kalipso.Engine.'+method,*arguments], env=env, text=True, capture_output=True)
        if result.returncode:raise RuntimeError(result.stderr)
        return result.stdout.strip()
    try:
        for attempt in range(30):
            try:
                actual = json.loads(ast.literal_eval(call('GetDependencies'))[0]);break
            except RuntimeError:time.sleep(.1)
        else:raise AssertionError('D-Bus service did not start')
        assert not actual['ready']
        assert call('Start') == '(false,)'
        state = json.loads(ast.literal_eval(call('GetState'))[0])
        assert state['status'] == 'missing-dependencies' and not state['active']
        assert 'Vosk library' in state['message'] or 'PipeWire' in state['message']
        choices=json.loads(ast.literal_eval(call('GetBackends'))[0])
        assert choices['selected']=='vosk'
        assert [b['id'] for b in choices['backends']]==['moonshine','vosk']
        assert call('SelectBackend','moonshine')=='(true,)'
        moon=json.loads(ast.literal_eval(call('GetDependencies'))[0])
        assert moon['backend']=='moonshine' and not moon['ready'] and not moon['model']
        assert call('Start')=='(false,)'
        try:call('SelectBackend','not-an-engine')
        except RuntimeError as error:assert 'InvalidArgs' in str(error)
        else:raise AssertionError('Unsupported backend accepted')
        assert json.loads(ast.literal_eval(call('GetBackends'))[0])['selected']=='moonshine'
        assert call('SelectBackend','vosk')=='(true,)'
        restored=json.loads(ast.literal_eval(call('GetDependencies'))[0])
        assert restored['model']==directory+'/missing-model'
        assert restored['library']==directory+'/missing.so'
        # Languages: an installed model is used at once, without downloading.
        data = Path(directory)/'data/kalipso'
        (data/'runtimes/vosk-0.3.45/vosk').mkdir(parents=True)
        (data/'runtimes/vosk-0.3.45/vosk/libvosk.so').write_bytes(b'')
        model = data/'models/vosk/vosk-model-small-it-0.22'
        for name in ['am/final.mdl', 'conf/mfcc.conf']:
            (model/name).parent.mkdir(parents=True, exist_ok=True)
            (model/name).write_bytes(b'x')
        languages = json.loads(ast.literal_eval(call('GetLanguages'))[0])
        found = {item['code']: item for item in languages['languages']}
        assert sorted(found) == ['de', 'en', 'es', 'it', 'ja', 'zh'], found
        assert found['it']['ready'] and found['it']['backend'] == 'vosk' and found['it']['name'] == 'Italiano'
        assert not found['en']['ready'] and found['en']['backend'] == 'moonshine' and found['en']['bytes'] > 0
        assert languages['install']['stage'] == 'idle' and not languages['install']['active']
        assert call('InstallLanguage', 'it') == '(true,)'
        chosen = json.loads(ast.literal_eval(call('GetDependencies'))[0])
        assert chosen['backend'] == 'vosk' and chosen['model'] == str(model) and chosen['language'] == 'it', chosen
        install = json.loads(ast.literal_eval(call('GetLanguages'))[0])['install']
        assert install['stage'] == 'done' and install['language'] == 'it' and not install['active'], install
        try:call('InstallLanguage', 'xx')
        except RuntimeError as error:assert 'InvalidArgs' in str(error)
        else:raise AssertionError('Unknown language accepted')
        call('CancelInstall')
        assert not json.loads(ast.literal_eval(call('GetLanguages'))[0])['install']['active']
        call('Stop')
        assert not json.loads(ast.literal_eval(call('GetState'))[0])['active']
        print('PASS: backend selection, profile isolation, invalid selection rejected, missing dependencies, no capture')
        print('PASS: catalog languages listed with their state; an installed language is used without downloading')
    finally:
        child.terminate();child.wait(timeout=10)
