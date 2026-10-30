#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Captures the GNOME Shell extension for the website scenes: the built extension in a nested GNOME
# Shell (at normal size: the nested shell does not scale reliably), its saved state set to the scene's script, position (the start of a
# passage), mode and colours. The reader is cut out of the screen with transparent rounded corners.
# Usage: capture-gnome.sh <out.png> <script file> <passage> <dark|light> <manual|steady|voice>
set -euo pipefail
out=$(realpath -m "$1") script=$2 passage=$3 appearance=$4 mode=$5
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$root/desktop/package.py" >/dev/null
extension=$work/data/gnome-shell/extensions/kalipso@brianidavide.github.io
mkdir -p "$extension" "$work/data/kalipso" "$work/run"
chmod 700 "$work/run"
python3 - "$root/artifacts/packages/kalipso-gnome.zip" "$extension" "$script" "$passage" "$appearance" "$mode" \
    "$work/data/kalipso/gnome-extension.json" <<'PY'
import json, sys, zipfile
from pathlib import Path
archive, target, script, passage, appearance, mode, state = sys.argv[1:]
zipfile.ZipFile(archive).extractall(target)
text = Path(script).read_text(encoding='utf-8').rstrip('\n')
assert passage in text, 'passage not found'
Path(state).write_text(json.dumps({'script': text, 'position': text.index(passage), 'mode': mode,
                                   'appearance': appearance}), encoding='utf-8')
PY
cat >"$work/run.sh" <<EOF
export HOME=$work XDG_DATA_HOME=$work/data XDG_CONFIG_HOME=$work/config XDG_CACHE_HOME=$work/cache
export XDG_RUNTIME_DIR=$work/run DBUS_SYSTEM_BUS_ADDRESS=\$DBUS_SESSION_BUS_ADDRESS DISPLAY=:80
export GSETTINGS_BACKEND=keyfile LIBGL_ALWAYS_SOFTWARE=1 GNOME_SHELL_SESSION_MODE=user
gsettings set org.gnome.shell enabled-extensions "['kalipso@brianidavide.github.io']"
gsettings set org.gnome.shell welcome-dialog-last-shown-version '999'
Xvfb :80 -screen 0 1440x900x24 -nolisten tcp 2>/dev/null &
sleep 1
MUTTER_DEBUG_DUMMY_MODE_SPECS=1440x900 gnome-shell --nested --wayland --no-x11 --wayland-display=kalipso-scene \
    --sm-disable >/dev/null 2>&1 &
sleep 16
import -window root "$work/screen.png"
kill \$(jobs -p) 2>/dev/null
EOF
dbus-run-session -- bash "$work/run.sh" >/dev/null 2>&1
# The reader is the box drawn with the theme's border colour, centred below the top bar.
python3 - "$work/screen.png" "$out" "$root/design/tokens.json" "$appearance" <<'PY'
import json, sys
from PIL import Image, ImageDraw
screen, out, tokens, appearance = sys.argv[1:]
image = Image.open(screen).convert('RGB')
palette = json.load(open(tokens))['palette'][appearance]
border = tuple(int(palette['border'][i:i + 2], 16) for i in (1, 3, 5))
near = lambda p: all(abs(a - b) <= 6 for a, b in zip(p, border))
width, height = image.size
middle = width // 2
rows = [y for y in range(height) if near(image.getpixel((middle, y)))]
top, bottom = rows[0], rows[-1]
row = (top + bottom) // 2
columns = [x for x in range(width) if near(image.getpixel((x, row)))]
left, right = columns[0], columns[-1]
reader = image.crop((left, top, right + 1, bottom + 1)).convert('RGBA')
mask = Image.new('L', reader.size, 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, reader.size[0] - 1, reader.size[1] - 1), radius=round(18 * reader.size[0] / 640), fill=255)  # The reader is 640 px wide at 1x.
reader.putalpha(mask)
reader.save(out)
print(reader.size)
PY
