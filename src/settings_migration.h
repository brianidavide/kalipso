// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QSettings>

inline void importLegacySettings(const QString &organization, const QString &application) {
    QSettings current;
    if (!current.allKeys().isEmpty())
        return;
    const QSettings legacy(organization, application);
    for (const auto &key : legacy.allKeys())
        current.setValue(key, legacy.value(key));
    current.sync();
}
