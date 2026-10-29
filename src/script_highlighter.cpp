// SPDX-License-Identifier: AGPL-3.0-or-later
#include "script_highlighter.h"
#include "tracker.h"
#include <QRegularExpression>

void ScriptHighlighter::highlightBlock(const QString &text) {
    // Cues and headings never span lines, so each block is self-contained.
    if (!ScriptTracker::headings(text).isEmpty()) {
        QTextCharFormat heading;
        heading.setFontWeight(QFont::Bold);
        heading.setForeground(heading_);
        setFormat(0, int(text.size()), heading);
        return;
    }
    // Markdown emphasis: **bold** and *italic*, with the markers dimmed.
    static const QRegularExpression strong(QStringLiteral(R"(\*\*(?=\S)(.+?)(?<=\S)\*\*)"));
    static const QRegularExpression emphasis(QStringLiteral(R"((?<![*\w])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?![*\w]))"));
    QTextCharFormat marker;
    marker.setForeground(cue_);
    for (auto it = strong.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        QTextCharFormat bold = format(match.capturedStart(1));
        bold.setFontWeight(QFont::Bold);
        setFormat(match.capturedStart(1), match.capturedLength(1), bold);
        setFormat(match.capturedStart(), 2, marker);
        setFormat(match.capturedEnd() - 2, 2, marker);
    }
    for (auto it = emphasis.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        QTextCharFormat italic = format(match.capturedStart(1));
        italic.setFontItalic(true);
        setFormat(match.capturedStart(1), match.capturedLength(1), italic);
        setFormat(match.capturedStart(), 1, marker);
        setFormat(match.capturedEnd() - 1, 1, marker);
    }
    QTextCharFormat cue;
    cue.setFontItalic(true);
    cue.setForeground(cue_);
    for (const auto &range : ScriptTracker::cues(text))
        setFormat(range.start, range.length, cue);
}
