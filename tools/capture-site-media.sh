#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Captures the website's product media from the Linux app: the editor (also the video's poster) and a
# 15-second walkthrough: open the reader, start, a three-second countdown,
# steady scrolling while the controls fade, pause (the time left shows), hide. A virtual X display without a window manager, a clean profile set to steady
# speed, the --demo sample script; no microphone is opened and nothing is composited.
# Needs Xvfb, xdotool, ImageMagick (import), ffmpeg with libx264 and libvpx-vp9, and cwebp or ffmpeg's libwebp.
# Usage: tools/capture-site-media.sh [path/to/kalipso]   (default: build/kalipso)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
app=${1:-$root/build/kalipso}
out=$root/site/assets/media
work=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$work"' EXIT

mkdir -p "$work/config/brianidavide"
printf '[appearance]\ntheme=light\n[reading]\nmode=steady\nspeed=150\ncountdown=3\n' \
    >"$work/config/brianidavide/kalipso.conf"
export HOME=$work XDG_CONFIG_HOME=$work/config XDG_DATA_HOME=$work/data XDG_CACHE_HOME=$work/cache
export QT_QPA_PLATFORM=xcb DISPLAY=:77

Xvfb :77 -screen 0 1100x760x24 -nolisten tcp &
sleep 1
"$app" --demo --quit-after 30000 >/dev/null 2>&1 &
sleep 4
xdotool mousemove 1099 759 # Out of the way; the recording leaves the pointer out anyway.
import -window root "$work/editor.png"

ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -video_size 1100x760 -framerate 30 -i :77 -t 15 \
    -c:v libx264rgb -qp 0 -preset ultrafast "$work/raw.mkv" &
recorder=$!
# Without a window manager nothing gives keyboard focus: give it to the window that should get the keys.
focus() { xdotool windowfocus --sync "$(xdotool search --onlyvisible --name "$1" | tail -1)"; }
sleep 1
focus '^Kalipso$'
xdotool key ctrl+Return # Open the reader.
sleep 1.2
focus 'Kalipso — reading'
xdotool key space # Start: a three-second countdown, then steady scrolling; the controls fade.
sleep 9
xdotool key space # Pause: the controls come back with the time left.
sleep 2.2
xdotool key Escape # Hide the reader.
wait $recorder

ffmpeg -loglevel error -y -i "$work/editor.png" -c:v libwebp -lossless 1 "$out/native-editor.webp"
ffmpeg -loglevel error -y -i "$work/raw.mkv" -c:v libx264 -preset veryslow -crf 30 -pix_fmt yuv420p \
    -movflags +faststart -an "$out/native-walkthrough.mp4"
ffmpeg -loglevel error -y -i "$work/raw.mkv" -c:v libvpx-vp9 -crf 42 -b:v 0 -row-mt 1 -pix_fmt yuv420p \
    -an "$out/native-walkthrough.webm"
ls -l "$out"/native-*
