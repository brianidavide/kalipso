#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
cd "$(dirname "$0")/../.."
mkdir -p artifacts/desktop-test
export XDG_RUNTIME_DIR=/tmp/kalipso-audio-probe
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export PULSE_SERVER=unix:$XDG_RUNTIME_DIR/pulse/native
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
pipewire > artifacts/desktop-test/pipewire.log 2>&1 & pw_pid=$!
trap 'kill "$pw_pid" 2>/dev/null || true' EXIT
for i in $(seq 1 50); do if pw-cli info 0 >/dev/null 2>&1; then break; fi; sleep .1; done
pw-cli info 0 >/dev/null
wireplumber > artifacts/desktop-test/wireplumber.log 2>&1 & wp_pid=$!
pipewire-pulse > artifacts/desktop-test/pulse.log 2>&1 & pulse_pid=$!
trap 'kill "$pw_pid" "$wp_pid" "$pulse_pid" 2>/dev/null || true' EXIT
for i in $(seq 1 50); do if pactl info >/dev/null 2>&1; then break; fi; sleep .1; done
sleep 1
pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=kalipso_test media.class=Audio/Sink object.linger=true audio.position=[ FL FR ] }'
sleep 2
pw-loopback --capture-props='node.target=kalipso_test stream.capture.sink=true' --playback-props='media.class=Audio/Source node.name=kalipso_virtual_mic node.description=KalipsoVirtualMicrophone' > artifacts/desktop-test/virtual-mic.log 2>&1 & loop_pid=$!
trap 'kill "$pw_pid" "$wp_pid" "$pulse_pid" "$loop_pid" 2>/dev/null || true' EXIT
sleep 1
pactl set-default-source kalipso_virtual_mic
printf '%s' "$DBUS_SESSION_BUS_ADDRESS" > artifacts/desktop-test/bus-address
pactl list short sources > artifacts/desktop-test/sources.txt
sleep 7200
