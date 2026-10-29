#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the GNOME extension archive and installs it for the current user.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$root/desktop/package.py" >/dev/null
gnome-extensions install --force "$root/artifacts/packages/kalipso-gnome.zip"
echo 'Installed. Log out and back in, then run: gnome-extensions enable kalipso@brianidavide.github.io'
