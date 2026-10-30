// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
#include <QIcon>

// Monochrome SVG icons from :/icons, tinted to a palette colour so they keep
// the same contrast as the text next to them.
namespace Icons {
QIcon icon(const QString &name, const QColor &color, const QColor &disabled = {});
// How each icon moves on hover (see IconMotion): previous and next slide, play and pause grow,
// the settings gear turns by one tooth,
// new grows and close shrinks (a quarter turn would pass through the other sign), the reader goes out, edit tilts.
QString motion(const QString &name);
} // namespace Icons
