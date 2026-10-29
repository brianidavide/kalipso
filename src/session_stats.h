// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "tracker.h"
#include <QSet>
#include <QString>
#include <algorithm>

// Delivery summary for one voice-following session: pace, pauses and filler
// words. Computed in memory from reading positions and recognized text.
class SessionStats {
public:
    struct Summary {
        qint64 durationMs = 0;
        int words = 0;
        double wordsPerMinute = 0;
        int longPauses = 0;
        qint64 longestPauseMs = 0;
        int fillers = 0;
    };
    static constexpr qint64 kLongPauseMs = 3000;

    bool active() const { return active_; }

    void start(int position, qint64 now) {
        active_ = true;
        summary_ = {};
        startPosition_ = furthest_ = position;
        startedAt_ = lastAdvance_ = now;
    }

    void advance(int position, qint64 now) {
        if (!active_ || position <= furthest_)
            return;
        const qint64 gap = now - lastAdvance_;
        if (gap >= kLongPauseMs) {
            ++summary_.longPauses;
            summary_.longestPauseMs = std::max(summary_.longestPauseMs, gap);
        }
        furthest_ = position;
        lastAdvance_ = now;
    }

    // Continues measuring from a position reached by a jump, not by reading.
    void rebase(int position, qint64 now) {
        if (!active_)
            return;
        summary_.words += std::max(0, furthest_ - startPosition_);
        startPosition_ = furthest_ = position;
        lastAdvance_ = now;
    }

    // Counts hesitation sounds. Many recognizers omit them, so this is a lower bound.
    void transcript(const QString &finalText) {
        static const QSet<QString> fillers{"um", "umm", "uh", "uhm", "er", "erm", "ehm", "eh", "ah", "hmm", "mm"};
        if (!active_)
            return;
        for (const auto &word : ScriptTracker::tokenize(finalText))
            summary_.fillers += fillers.contains(word.normalized);
    }

    Summary finish(qint64 now) {
        active_ = false;
        summary_.durationMs = std::max<qint64>(0, now - startedAt_);
        summary_.words += std::max(0, furthest_ - startPosition_);
        const double minutes = summary_.durationMs / 60000.0;
        summary_.wordsPerMinute = minutes > 0.05 ? summary_.words / minutes : 0;
        return summary_;
    }

private:
    bool active_ = false;
    int startPosition_ = -1, furthest_ = -1;
    qint64 startedAt_ = 0, lastAdvance_ = 0;
    Summary summary_;
};
