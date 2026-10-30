#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Rebuilds the website's "in real use" scenes: real captures of the app, the Plasma widget and the
# GNOME extension (artifacts/scenes/), placed in simulated desktops (scenes.html) and rendered to
# site/assets/media/scene-*.webp. Run again whenever the interface shown changes.
# Needs: a build of the app, Xvfb, xdotool, ImageMagick, plasmawindowed, gnome-shell, ffmpeg with
# libwebp, Python with Pillow, and Playwright (PLAYWRIGHT=/path/to/playwright/index.mjs).
# Usage: tools/scenes/build.sh [path/to/kalipso]   (default: build/kalipso)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
app=${1:-$root/build/kalipso}
out=$root/artifacts/scenes
mkdir -p "$out"
cd "$root"
"$here/capture-app.sh" "$app" "$out/app-call.png" "$here/scripts/call.txt" "Before we start" steady dark
"$here/capture-app.sh" "$app" "$out/app-recording.png" "$here/scripts/recording.txt" "Today I will" steady light
"$here/capture-plasma.sh" "$out/plasma-slides.png" "$here/scripts/slides.txt" "forty percent" dark manual 620 360
"$here/capture-gnome.sh" "$out/gnome-lesson.png" "$here/scripts/lesson.txt" "where the light goes" dark manual
node "$here/render.mjs"
