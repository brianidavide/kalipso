#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Captures the app's reader for the website scenes: a clean profile with one script, the reader
# opened with the current word at the start of a given passage, photographed at twice the size.
# Usage: capture-app.sh <kalipso binary> <out.png> <script file> <passage> <mode: manual|steady|voice> <theme: dark|light>
set -euo pipefail
app=$1 out=$2 script=$3 passage=$4 mode=$5 theme=$6
# The saved position is a character offset into the script.
at=$(python3 -c 'import sys; t=open(sys.argv[1],encoding="utf-8").read(); i=t.find(sys.argv[2]); sys.exit("passage not found") if i<0 else print(i)' "$script" "$passage")
id=5ce7e000000000000000000000000001
work=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$work"' EXIT
mkdir -p "$work/config/brianidavide" "$work/data/brianidavide/kalipso/scripts"
cp "$script" "$work/data/brianidavide/kalipso/scripts/$id.txt"
printf '[{"id":"%s","title":"Scene","modified":"2026-10-30T08:00:00Z","position":%d,"speed":150}]\n' "$id" "$at" \
    >"$work/data/brianidavide/kalipso/scripts/library.json"
printf '[appearance]\ntheme=light\n[reading]\nmode=%s\ntheme=%s\nspeed=150\nfontSize=30\ncountdown=0\n[library]\ncurrent=%s\n' \
    "$mode" "$theme" "$id" >"$work/config/brianidavide/kalipso.conf"
export HOME=$work XDG_CONFIG_HOME=$work/config XDG_DATA_HOME=$work/data XDG_CACHE_HOME=$work/cache
export QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 DISPLAY=:78
Xvfb :78 -screen 0 2400x1400x24 -nolisten tcp 2>/dev/null &
sleep 1
"$app" --prompter >/dev/null 2>&1 &
sleep 4
reader=$(xdotool search --onlyvisible --name 'Kalipso — reading' | tail -1)
xdotool mousemove 0 1399; sleep 0.5 # Outside the reader: no tooltip, and the controls stay while paused.
if [ "$mode" = steady ]; then
    # Reading: start scrolling and photograph before the controls fade (2.5 s without input).
    xdotool windowfocus --sync "$reader"
    xdotool key space
    sleep 1.2
fi
import -window "$reader" "$out"
