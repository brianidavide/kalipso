#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Captures the Plasma widget for the website scenes: the built package, with the scene's script,
# reading position (the start of a passage), colours and mode set as defaults in a temporary copy,
# shown by plasmawindowed at twice the size. The repository is not changed.
# Usage: capture-plasma.sh <out.png> <script file> <passage> <dark|light> <manual|steady|voice> [width height]
set -euo pipefail
out=$(realpath -m "$1") script=$2 passage=$3 appearance=$4 mode=$5 width=${6:-620} height=${7:-420}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$root/desktop/package.py" >/dev/null
python3 - "$root/artifacts/packages/kalipso-plasma.plasmoid" "$work/package" "$script" "$passage" "$appearance" "$mode" <<'PY'
import re, sys, zipfile
from pathlib import Path
from xml.sax.saxutils import escape
archive, target, script, passage, appearance, mode = sys.argv[1:]
zipfile.ZipFile(archive).extractall(target)
config = Path(target, 'contents/config/main.xml')
xml = config.read_text(encoding='utf-8')
def default(name, value):
    global xml
    xml, count = re.subn(rf'(<entry name="{name}"[^>]*>\s*<default>).*?(</default>)',
                         lambda m: m.group(1) + escape(value) + m.group(2), xml, flags=re.S)
    assert count == 1, name
text = Path(script).read_text(encoding='utf-8').rstrip('\n')
assert passage in text, 'passage not found'
default('script', text)
default('position', str(text.index(passage)))  # A character offset, like the widget saves it.
default('appearance', appearance)
default('mode', mode)
config.write_text(xml, encoding='utf-8')
PY
cat >"$work/run.sh" <<EOF
export HOME=$work XDG_CONFIG_HOME=$work/config XDG_DATA_HOME=$work/data XDG_CACHE_HOME=$work/cache
export QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software QT_SCALE_FACTOR=2 DISPLAY=:79
Xvfb :79 -screen 0 2400x1600x24 -nolisten tcp 2>/dev/null &
sleep 1
plasmawindowed "$work/package" >/dev/null 2>&1 &
sleep 7
win=\$(xdotool search --onlyvisible --name "." | head -1)
xdotool windowsize "\$win" $((width * 2)) $((height * 2))
xdotool mousemove 0 1599
sleep 1.5
import -window "\$win" "$out"
kill \$(jobs -p) 2>/dev/null
EOF
dbus-run-session -- bash "$work/run.sh" >/dev/null 2>&1
test -s "$out"
