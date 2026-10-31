#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Packs the companion download for people who use only the Plasma widget or the GNOME extension:
# the built kalipso-engine, an install script and the notices. Unpacked, one command installs it
# under ~/.local (see README.txt); the language is then chosen in the widget or extension.
# Usage: make-archive.sh <built kalipso-engine> <version> <output directory>
set -eu
engine=$1 version=$2 out=$3
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
name=kalipso-companion-v$version-linux-x86_64
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
dir=$work/$name
mkdir -p "$dir"
install -m 755 "$engine" "$dir/kalipso-engine"
strip "$dir/kalipso-engine" 2>/dev/null || true
install -m 644 "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$dir/"
install -m 644 "$root/packaging/companion/README.txt" "$dir/"
cat >"$dir/install" <<'SH'
#!/bin/sh
# Installs the Kalipso companion under ~/.local and registers it with the desktop. Options are
# passed to kalipso-engine --download: ./install --language it also downloads that language.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=$HOME/.local/bin
data=${XDG_DATA_HOME:-$HOME/.local/share}
mkdir -p "$bin" "$data/dbus-1/services" "$data/licenses/kalipso-engine"
cp "$here/kalipso-engine" "$bin/kalipso-engine.new" && chmod 755 "$bin/kalipso-engine.new"
mv -f "$bin/kalipso-engine.new" "$bin/kalipso-engine"
printf '[D-BUS Service]\nName=io.github.brianidavide.Kalipso.Engine\nExec="%s" --serve\n' "$bin/kalipso-engine" \
    >"$data/dbus-1/services/io.github.brianidavide.Kalipso.Engine.service"
cp "$here/LICENSE" "$data/licenses/kalipso-engine/LICENSE"
echo "Installed the companion in $bin."
if [ "$#" -gt 0 ]; then
    exec "$bin/kalipso-engine" --download "$@"
fi
echo "In the Kalipso widget or extension open Settings, then choose the language you speak."
echo "If it says the companion is missing, log out and back in."
SH
chmod 755 "$dir/install"
mkdir -p "$out"
tar -C "$work" -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
