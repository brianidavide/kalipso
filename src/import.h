// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>

// Turns files into script text. Presentations become one "# Slide N: Title"
// section per slide with speaker notes, so the reader can jump between slides.
// Documents (.docx, .odt) become Markdown: headings are sections, list items
// start with "-", bold and italic are kept as emphasis.
namespace ScriptImport {
struct Result {
    QString text;
    QString error;
    int sections = 0;
};

Result read(const QString &path);
Result fromPptx(const QByteArray &archive);
Result fromOdp(const QByteArray &archive);
Result fromDocx(const QByteArray &archive);
Result fromOdt(const QByteArray &archive);
QString dialogFilter();
} // namespace ScriptImport
