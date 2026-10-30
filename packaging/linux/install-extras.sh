#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# For distribution packages: installs what goes with the app beyond `cmake --install` — the Plasma
# widget, the GNOME Shell extension and the KWin script, unpacked where each desktop looks for add-ons
# installed system-wide.
# Usage: install-extras.sh <source tree> <staging prefix, e.g. $pkgdir/usr>
set -eu
src=$1 prefix=$2
python3 "$src/desktop/package.py" >/dev/null
unpack() {
    mkdir -p "$2"
    python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$1" "$2"
}
packages=$src/artifacts/packages
unpack "$packages/kalipso-plasma.plasmoid" "$prefix/share/plasma/plasmoids/io.github.brianidavide.kalipso"
unpack "$packages/kalipso-gnome.zip" "$prefix/share/gnome-shell/extensions/kalipso@brianidavide.github.io"
unpack "$packages/kalipso-kwin.kwinscript" "$prefix/share/kwin/scripts/kalipso-companion"
