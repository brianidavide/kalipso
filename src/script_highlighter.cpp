// SPDX-License-Identifier: AGPL-3.0-or-later
#include "script_highlighter.h"
#include "tracker.h"

void ScriptHighlighter::highlightBlock(const QString &text) {
    // Cues and headings never span lines, so each block is self-contained.
    if (!ScriptTracker::headings(text).isEmpty()) {
        QTextCharFormat heading;
        heading.setFontWeight(QFont::Bold);
        heading.setForeground(heading_);
        setFormat(0, int(text.size()), heading);
        return;
    }
    QTextCharFormat cue;
    cue.setFontItalic(true);
    cue.setForeground(cue_);
    for (const auto &range : ScriptTracker::cues(text))
        setFormat(range.start, range.length, cue);
}
