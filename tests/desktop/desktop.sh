#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
export DBUS_SESSION_BUS_ADDRESS=$(cat artifacts/desktop-test/bus-address)
export DBUS_SYSTEM_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS
export XDG_RUNTIME_DIR=/tmp/kalipso-audio-probe
export PULSE_SERVER=unix:$XDG_RUNTIME_DIR/pulse/native
if [ "$1" = gnome ]; then
 export XDG_DATA_HOME="$PWD/artifacts/desktop-test/gnome-data"
 export KALIPSO_TEST_API=1
 export GSETTINGS_BACKEND=keyfile XDG_CONFIG_HOME=$PWD/artifacts/desktop-test/gnome-config GNOME_SHELL_SESSION_MODE=user LIBGL_ALWAYS_SOFTWARE=1
 bash desktop/install-gnome.sh
 gsettings set org.gnome.shell enabled-extensions "['kalipso@brianidavide.github.io']"
 exec xvfb-run -a -s '-screen 0 1440x1000x24' bash -c 'printf "%s" "$DISPLAY" > artifacts/desktop-test/gnome-display; printf "%s" "$XAUTHORITY" > artifacts/desktop-test/gnome-xauthority; printf "%s" "$$" > artifacts/desktop-test/gnome-pid; exec gnome-shell --nested --wayland --no-x11 --wayland-display=kalipso-gnome --sm-disable'
else
 export QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software
 exec xvfb-run -a -s '-screen 0 1440x1000x24' bash -c 'printf "%s" "$DISPLAY" > artifacts/desktop-test/plasma-display; printf "%s" "$XAUTHORITY" > artifacts/desktop-test/plasma-xauthority; printf "%s" "$$" > artifacts/desktop-test/plasma-pid; exec plasmawindowed "$PWD/desktop/plasma"'
fi
