// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
#include <QIcon>

// Monochrome SVG icons from :/icons, tinted to a palette colour so they keep
// the same contrast as the text next to them.
namespace Icons {
QIcon icon(const QString &name, const QColor &color, const QColor &disabled = {});
}
