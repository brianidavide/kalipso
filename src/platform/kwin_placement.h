// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// On Plasma with Wayland, applications cannot place their windows or keep them above others.
// Kalipso's KWin script does it for the reader: at the top of the screen, under the camera, above
// other windows and, where KWin supports it, left out of screen captures. The script is built into
// Kalipso and loaded into KWin while Kalipso runs; nothing is installed or enabled permanently.
// Elsewhere (X11, other desktops, macOS, Windows) the reader places itself and this does nothing.
namespace KWinPlacement {
// Loads the script under KWin on Wayland. Returns true when the script runs, also when it was
// already enabled in System Settings.
bool load();
// Unloads the script if load() loaded it.
void unload();
} // namespace KWinPlacement
