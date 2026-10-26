#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
probe_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
probe_target="${XDG_DATA_HOME:-$HOME/.local/share}/gnome-shell/extensions/kalipso@brianidavide.github.io"
mkdir -p "$probe_target"
cp "$probe_dir/gnome/extension.js" "$probe_dir/gnome/local-engine.js" "$probe_dir/gnome/tokens.js" "$probe_dir/gnome/stylesheet.css" "$probe_dir/gnome/metadata.json" "$probe_dir/tracker.mjs" "$probe_dir/unicode-tokens.mjs" "$probe_target/"
printf 'Installed Kalipso to %s\n' "$probe_target"
