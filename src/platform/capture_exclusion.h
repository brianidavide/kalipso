// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
class QWidget;

// Asks the window system to leave a window out of screen captures and shares.
// Supported on Windows 10 version 2004 and later only. Capture tools that
// bypass the system's capture APIs can still record the window.
namespace CaptureExclusion {
bool supported();
// Returns true when the request was accepted by the window system.
bool apply(QWidget *window, bool exclude);
} // namespace CaptureExclusion
