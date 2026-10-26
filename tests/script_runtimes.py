#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Execute the shared Unicode fixtures in real QML and GJS, with negative controls.
Requires a laboratory with qmltestrunner (/usr/lib/qt6/bin) and gjs installed.
"""
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[1]
fixtures=json.loads((root/'tests/multilingual-tracker.json').read_text())
out=root/'artifacts/local-engine/script-runtimes';out.mkdir(parents=True,exist_ok=True)
tracker=(root/'desktop/tracker.mjs').as_uri()
body='''for(const f of cases){const t=new Tracking.Tracker(f.script);
if(!t.follow(f.partial))throw Error('No match: '+f.script);
const w=t.words[t.position];if(t.text.slice(0,w.start+w.length)!==f.read)throw Error('Wrong read span: '+f.script);
t.setText('🎤 '+f.script);if(t.words[t.position].start!==w.start+3)throw Error('Wrong edit anchor');}'''
env=dict(os.environ,QT_QPA_PLATFORM='offscreen')
results={}
for runtime in ('qml','gjs'):
    path=out/('tst_unicode.qml' if runtime=='qml' else 'unicode.js')
    def source(data):
        code='const cases='+json.dumps(data,ensure_ascii=False)+';'+body
        if runtime=='qml':return f'import QtQuick\nimport QtTest\nimport "{tracker}" as Tracking\nTestCase {{name:"UnicodeTracking";function test_allScripts(){{{code}}}}}'
        return f'import * as Tracking from "{tracker}";\n'+code+'\nprint("Unicode fixtures passed");'
    command=['/usr/lib/qt6/bin/qmltestrunner','-input',str(path)] if runtime=='qml' else ['gjs','-m',str(path)]
    path.write_text(source(fixtures));good=subprocess.run(command,env=env,text=True,capture_output=True)
    assert good.returncode==0,good.stdout+good.stderr
    bad=[dict(item) for item in fixtures];bad[0]['read']='deliberately incorrect'
    path.write_text(source(bad));negative=subprocess.run(command,env=env,text=True,capture_output=True)
    assert negative.returncode!=0,'Negative control unexpectedly passed'
    path.write_text(source(fixtures))
    results[runtime]={'fixtures':len(fixtures),'passed':True,'negativeControlFailedAsExpected':True}
print(json.dumps(results,indent=2))
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
