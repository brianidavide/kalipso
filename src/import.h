// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>

// Turns files into script text. Presentations become one "# Slide N: Title"
// section per slide with speaker notes, so the reader can jump between slides.
namespace ScriptImport {
struct Result {
    QString text;
    QString error;
    int sections = 0;
};

Result read(const QString &path);
Result fromPptx(const QByteArray &archive);
Result fromOdp(const QByteArray &archive);
QString dialogFilter();
} // namespace ScriptImport
