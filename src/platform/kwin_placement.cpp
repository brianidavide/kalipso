// SPDX-License-Identifier: AGPL-3.0-or-later
#include "kwin_placement.h"
#ifdef KALIPSO_PORTAL_COLORS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#endif

namespace {
// Not the id of the installable script (packaging/kwin, "kalipso-companion"): KWin unloads a running
// script named like an installed one that is not enabled in System Settings.
const char *const kScript = "kalipso-reader";
bool loadedHere = false;
} // namespace

bool KWinPlacement::load() {
#ifdef KALIPSO_PORTAL_COLORS
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return false;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->isServiceRegistered(QStringLiteral("org.kde.KWin")))
        return false;
    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
    for (const auto *name : {"kalipso-companion", kScript}) {
        const QDBusReply<bool> running = scripting.call("isScriptLoaded", QString::fromLatin1(name));
        if (!running.isValid())
            return false;
        if (running.value()) // Enabled in System Settings, or loaded by an earlier Kalipso.
            return true;
    }
    // KWin reads the script from a file: a copy of the one built into Kalipso, in the user's
    // private runtime directory.
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QFile source(":/kwin/main.js");
    QFile copy(directory + "/kalipso-kwin.js");
    if (directory.isEmpty() || !source.open(QIODevice::ReadOnly) || !copy.open(QIODevice::WriteOnly) ||
        copy.write(source.readAll()) < 0)
        return false;
    copy.close();
    const QDBusReply<int> id = scripting.call("loadScript", copy.fileName(), QString::fromLatin1(kScript));
    if (!id.isValid() || id.value() < 0)
        return false;
    // start() runs the loaded scripts that are not running yet. The object path of the new script
    // is not reliable: KWin reuses numbers after a script is unloaded.
    if (!scripting.call("start").errorName().isEmpty()) {
        scripting.call("unloadScript", QString::fromLatin1(kScript));
        return false;
    }
    loadedHere = true;
    return true;
#else
    return false;
#endif
}

void KWinPlacement::unload() {
#ifdef KALIPSO_PORTAL_COLORS
    if (!loadedHere)
        return;
    loadedHere = false;
    QDBusInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus())
        .call("unloadScript", QString::fromLatin1(kScript));
#endif
}
