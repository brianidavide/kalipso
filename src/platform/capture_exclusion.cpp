// SPDX-License-Identifier: AGPL-3.0-or-later
#include "capture_exclusion.h"
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace CaptureExclusion {
bool supported() {
#ifdef Q_OS_WIN
    return true;
#else
    // macOS: Apple documents NSWindowSharingNone as a legacy value that must not
    // be used to hide content, and ScreenCaptureKit-based apps capture it anyway.
    // X11 has no exclusion; on KDE Plasma 6.6+ KWin offers "Hide from Screencast".
    return false;
#endif
}

bool apply(QWidget *window, bool exclude) {
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (SetWindowDisplayAffinity(handle, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE))
        return true;
    // Windows before 10 version 2004 only supports blacking the window out.
    return exclude && SetWindowDisplayAffinity(handle, WDA_MONITOR);
#else
    Q_UNUSED(window);
    Q_UNUSED(exclude);
    return false;
#endif
}
} // namespace CaptureExclusion
