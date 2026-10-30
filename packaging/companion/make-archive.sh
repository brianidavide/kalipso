#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Packs the companion download for people who use only the Plasma widget or the GNOME extension:
# the built kalipso-engine, the setup script with its model catalog, and the notices. Unpacked, one
# command sets up voice following under ~/.local (see README.txt).
# Usage: make-archive.sh <built kalipso-engine> <version> <output directory>
set -eu
engine=$1 version=$2 out=$3
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
name=kalipso-companion-v$version-linux-x86_64
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
dir=$work/$name
mkdir -p "$dir/tools/data" "$dir/third_party"
install -m 755 "$engine" "$dir/kalipso-engine"
strip "$dir/kalipso-engine" 2>/dev/null || true
install -m 644 "$root/tools/install-local-engine.py" "$root/tools/speech_assets.py" "$dir/tools/"
install -m 644 "$root/tools/data/speech-catalog.json" "$dir/tools/data/"
cp -R "$root/third_party/native-notices" "$dir/third_party/"
install -m 644 "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$dir/"
install -m 644 "$root/packaging/companion/README.txt" "$dir/"
cat >"$dir/install" <<'SH'
#!/bin/sh
# Sets up voice following for the Kalipso widget or extension. Example: ./install --language it
exec python3 "$(dirname -- "$0")/tools/install-local-engine.py" --engine "$(dirname -- "$0")/kalipso-engine" --download "$@"
SH
chmod 755 "$dir/install"
mkdir -p "$out"
tar -C "$work" -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
