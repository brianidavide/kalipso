// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "tracker.h"
#include <QStringList>
#include <algorithm>

// Phrases the Windows recognizer may hear: the script around the reading position, plus any
// extra phrases (voice commands). Windows accepts at most 256 per update.
inline QStringList scriptPhrases(const QString &script, int position, const QStringList &extra = {}) {
    const auto words = ScriptTracker::tokenize(script);
    QStringList phrases;
    const int first = std::max(0, position - 2);
    const int last = std::min(int(words.size()), std::max(0, position + 1) + 24);
    for (int i = first; i < last; ++i) {
        QString phrase;
        for (int j = i; j < std::min(last, i + 8); ++j) {
            if (!phrase.isEmpty())
                phrase += ' ';
            phrase += script.mid(words[j].start, words[j].length);
            if (phrase.size() <= 512)
                phrases.append(phrase);
        }
    }
    phrases.removeDuplicates();
    phrases = phrases.mid(0, 256 - int(extra.size()));
    for (const auto &phrase : extra)
        if (!phrases.contains(phrase))
            phrases.append(phrase);
    return phrases;
}
