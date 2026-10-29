// SPDX-License-Identifier: AGPL-3.0-or-later
#include "capture_exclusion.h"
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

#ifndef Q_OS_MACOS
namespace CaptureExclusion {
bool supported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false; // X11 has no exclusion; on KDE Wayland use the KWin script.
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
#endif
