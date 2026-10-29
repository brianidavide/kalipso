// SPDX-License-Identifier: AGPL-3.0-or-later
#include "capture_exclusion.h"
#include <AppKit/AppKit.h>
#include <QWidget>

namespace CaptureExclusion {
bool supported() { return true; }

bool apply(QWidget *window, bool exclude) {
    auto *view = reinterpret_cast<NSView *>(window->winId());
    if (!view || !view.window)
        return false;
    view.window.sharingType = exclude ? NSWindowSharingNone : NSWindowSharingReadOnly;
    return true;
}
} // namespace CaptureExclusion
