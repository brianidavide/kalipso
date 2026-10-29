#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run the shared tracker in the real QML (Plasma) and GJS (GNOME) engines.

Node passing desktop/tracker.test.mjs does not prove that QML's V4 or GNOME's
SpiderMonkey accept the same module. Each engine runs the multilingual fixtures
plus cue, heading and long-jump checks, then a deliberately wrong expectation
that must fail. Usage: script_runtimes.py QMLTESTRUNNER GJS
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = json.loads((ROOT / 'tests/multilingual-tracker.json').read_text())
BODY = '''
for (const f of cases) {
    const t = new Tracking.Tracker(f.script);
    if (!t.follow(f.partial)) throw Error('No match: ' + f.script);
    const w = t.words[t.position];
    if (t.text.slice(0, w.start + w.length) !== f.read) throw Error('Wrong read span: ' + f.script);
    t.setText('🎤 ' + f.script);
    if (t.words[t.position].start !== w.start + 3) throw Error('Wrong edit anchor');
}
const cued = Tracking.tokenize('# Opening\\nHello [smile] everyone').map(w => w.word).join(' ');
if (cued !== 'hello everyone') throw Error('Cues or headings were spoken: ' + cued);
if (Tracking.sections('# A\\nx\\n## B').map(s => s.title).join() !== 'A,B') throw Error('Wrong sections');
const filler = Array.from({length: 150}, (_, i) => 'w' + i).join(' ');
const jumpy = new Tracking.Tracker('one two three ' + filler + ' the final words arrive now');
jumpy.follow('one two three', true);
if (!jumpy.follow('the final words arrive', true) || jumpy.lost) throw Error('No long-jump recovery');
if (!new Tracking.Tracker('a [b] c').readingMarkup('#777777', '#ffffff').includes('<i>[b]</i>')) throw Error('No cue style');
'''


def source(runtime, tracker_uri, cases):
    code = 'const cases = ' + json.dumps(cases, ensure_ascii=False) + ';' + BODY
    if runtime == 'qml':
        return (f'import QtQuick\nimport QtTest\nimport "{tracker_uri}" as Tracking\n'
                f'TestCase {{ name: "Tracking"; function test_tracker() {{ {code} }} }}\n')
    return f'import * as Tracking from "{tracker_uri}";\n{code}\nprint("tracker passed");\n'


def main():
    qmltestrunner, gjs = sys.argv[1:3]
    environment = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    with tempfile.TemporaryDirectory(prefix='kalipso-runtimes-') as directory:
        for runtime, tracker in (('qml', ROOT / 'desktop/plasma/contents/ui/tracker.mjs'),
                                 ('gjs', ROOT / 'desktop/tracker.mjs')):
            path = Path(directory) / ('tst_tracker.qml' if runtime == 'qml' else 'tracker.js')
            command = [qmltestrunner, '-input', str(path)] if runtime == 'qml' else [gjs, '-m', str(path)]
            path.write_text(source(runtime, tracker.as_uri(), FIXTURES))
            good = subprocess.run(command, env=environment, text=True, capture_output=True)
            assert good.returncode == 0, f'{runtime}: {good.stdout}{good.stderr}'
            wrong = [dict(item) for item in FIXTURES]
            wrong[0]['read'] = 'deliberately incorrect'
            path.write_text(source(runtime, tracker.as_uri(), wrong))
            negative = subprocess.run(command, env=environment, text=True, capture_output=True)
            assert negative.returncode != 0, f'{runtime}: negative control unexpectedly passed'
            print(f'{runtime}: {len(FIXTURES)} fixtures and tracker checks pass; negative control fails as expected')


if __name__ == '__main__':
    main()
