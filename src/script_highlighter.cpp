// SPDX-License-Identifier: AGPL-3.0-or-later
#include "script_highlighter.h"
#include "tracker.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>

// Front matter spans lines. It is measured from the first lines (at most 200), which
// stays cheap and also sees text replaced with setPlainText (no revision change).
int ScriptHighlighter::frontMatterEnd() {
    static const QRegularExpression open(QStringLiteral(R"(^---[ \t]*$)"));
    static const QRegularExpression close(QStringLiteral(R"(^(---|\.\.\.)[ \t]*$)"));
    int end = 0;
    auto block = document()->firstBlock();
    if (open.match(block.text()).hasMatch())
        for (int lines = 0; (block = block.next()).isValid() && lines < 200; ++lines)
            if (close.match(block.text()).hasMatch()) {
                end = block.position() + block.length() - 1;
                break;
            }
    if (end != frontMatterEnd_) {
        frontMatterEnd_ = end;
        QTimer::singleShot(0, this, &QSyntaxHighlighter::rehighlight); // Lines away from the edit change too.
    }
    return end;
}

void ScriptHighlighter::highlightBlock(const QString &text) {
    if (currentBlock().position() < frontMatterEnd()) {
        QTextCharFormat metadata;
        metadata.setForeground(cue_);
        setFormat(0, int(text.size()), metadata);
        return;
    }
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
    // List markers ("- ", "* ", "+ ", "1. "): dimmed like other Markdown markers.
    static const QRegularExpression list(QStringLiteral(R"(^\s*([-*+]|\d{1,3}[.)])(?=\s))"));
    if (const auto item = list.match(text); item.hasMatch()) {
        QTextCharFormat bullet = marker;
        bullet.setFontWeight(QFont::Bold);
        setFormat(item.capturedStart(1), item.capturedLength(1), bullet);
    }
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
    // ***bold italic***: applied last, so it replaces what the two rules above made of it.
    static const QRegularExpression both(QStringLiteral(R"(\*\*\*(?=\S)(.+?)(?<=\S)\*\*\*)"));
    for (auto it = both.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        QTextCharFormat strongItalic = format(match.capturedStart(1));
        strongItalic.setFontWeight(QFont::Bold);
        strongItalic.setFontItalic(true);
        setFormat(match.capturedStart(1), match.capturedLength(1), strongItalic);
        setFormat(match.capturedStart(), 3, marker);
        setFormat(match.capturedEnd() - 3, 3, marker);
    }
    QTextCharFormat cue;
    cue.setFontItalic(true);
    cue.setForeground(cue_);
    for (const auto &range : ScriptTracker::cues(text))
        setFormat(range.start, range.length, cue);
}
