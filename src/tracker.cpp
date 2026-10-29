// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tracker.h"
#include <algorithm>
#include <limits>

namespace {
// Scripts commonly written without spaces (CJK, Thai, Khmer, Myanmar) are split
// into base-character units, matching the shell frontends. This is a matching
// fallback, not linguistic word segmentation.
bool denseScript(char32_t cp) {
    return (cp >= 0x3400 && cp <= 0x9fff) || (cp >= 0x20000 && cp <= 0x323af) || (cp >= 0x3040 && cp <= 0x30ff) ||
           (cp >= 0x31f0 && cp <= 0x31ff) || (cp >= 0xe00 && cp <= 0xeff) || (cp >= 0x1780 && cp <= 0x17ff) ||
           (cp >= 0x1000 && cp <= 0x109f) || (cp >= 0xaa60 && cp <= 0xaa7f);
}

QVector<Word> allWords(const QString &text) {
    QVector<Word> words;
    int start = -1, end = 0;
    bool dense = false;
    const auto flush = [&] {
        if (start < 0)
            return;
        words.push_back(
            {text.mid(start, end - start).normalized(QString::NormalizationForm_KC).toLower(), start, end - start});
        start = -1;
    };
    for (int i = 0; i < text.size();) {
        char32_t cp = text[i].unicode();
        int size = 1;
        if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(text[i], text[i + 1]);
            size = 2;
        }
        const auto category = QChar::category(cp);
        const bool mark = category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining ||
                          category == QChar::Mark_Enclosing;
        if (mark || cp == 0x200c || cp == 0x200d) {
            if (start >= 0)
                end = i + size;
        } else if (QChar::isLetterOrNumber(cp)) {
            const bool nextDense = denseScript(cp);
            if (start >= 0 && (dense || nextDense))
                flush();
            if (start < 0)
                start = i;
            end = i + size;
            dense = nextDense;
        } else {
            flush();
        }
        i += size;
    }
    flush();
    return words;
}

// Edit distance between the spoken words and script words [start, start+n),
// for every n up to m+2. Calls accept(n, edits) for each prefix length.
template <typename Accept>
void alignAt(const QVector<Word> &spoken, const QVector<Word> &words, int start, int last, QVector<int> &previous,
             QVector<int> &current, Accept accept) {
    const int m = int(spoken.size());
    for (int i = 0; i <= m; ++i)
        previous[i] = i;
    for (int n = 1; n <= m + 2 && start + n <= last; ++n) {
        current[0] = n;
        for (int i = 1; i <= m; ++i)
            current[i] = std::min({previous[i] + 1, current[i - 1] + 1,
                                   previous[i - 1] + (spoken[i - 1].normalized != words[start + n - 1].normalized)});
        accept(n, current[m]);
        previous.swap(current);
    }
}

int exactMatches(const QVector<Word> &spoken, const QVector<Word> &words, int start, int n) {
    int exact = 0;
    for (const auto &word : spoken)
        for (int j = start; j < start + n; ++j)
            if (word.normalized == words[j].normalized) {
                ++exact;
                break;
            }
    return exact;
}
} // namespace

QVector<TextRange> ScriptTracker::cues(const QString &text) {
    QVector<TextRange> ranges;
    int open = -1;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == u'[') {
            open = i; // No nesting: the innermost "[" starts the cue.
        } else if (text[i] == u'\n') {
            open = -1; // A cue ends on the line where it starts.
        } else if (text[i] == u']' && open >= 0) {
            ranges.push_back({open, i - open + 1});
            open = -1;
        }
    }
    return ranges;
}

QVector<TextRange> ScriptTracker::headings(const QString &text) {
    QVector<TextRange> ranges;
    for (int lineStart = 0; lineStart <= text.size();) {
        int lineEnd = text.indexOf(u'\n', lineStart);
        if (lineEnd < 0)
            lineEnd = int(text.size());
        // Markdown ATX heading: up to three spaces, 1-6 "#", then a space or end of line.
        int i = lineStart;
        while (i < lineEnd && i - lineStart < 3 && text[i] == u' ')
            ++i;
        int hashes = 0;
        while (i + hashes < lineEnd && text[i + hashes] == u'#')
            ++hashes;
        if (hashes >= 1 && hashes <= 6 && (i + hashes == lineEnd || text[i + hashes].isSpace()))
            ranges.push_back({lineStart, lineEnd - lineStart});
        lineStart = lineEnd + 1;
    }
    return ranges;
}

QVector<Section> ScriptTracker::sections(const QString &text) {
    QVector<Section> result;
    for (const auto &range : headings(text)) {
        auto title = text.mid(range.start, range.length).trimmed();
        while (title.startsWith(u'#'))
            title.remove(0, 1);
        while (title.endsWith(u'#'))
            title.chop(1);
        title = title.trimmed();
        if (!title.isEmpty())
            result.push_back({title, range.start});
    }
    return result;
}

QVector<Word> ScriptTracker::tokenize(const QString &text) {
    auto words = allWords(text);
    if (!text.contains(u'[') && !text.contains(u'#'))
        return words;
    auto silent = cues(text) + headings(text);
    if (silent.isEmpty())
        return words;
    std::sort(silent.begin(), silent.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
    QVector<Word> spoken;
    spoken.reserve(words.size());
    int range = 0;
    for (const auto &word : words) {
        while (range < silent.size() && silent[range].end() <= word.start)
            ++range;
        const bool hidden = range < silent.size() && silent[range].start <= word.start;
        if (!hidden)
            spoken.push_back(word);
    }
    return spoken;
}

void ScriptTracker::setScript(const QString &text) {
    if (script_ == text)
        return;
    // Map the reading anchor through a single contiguous editor change. Preserve
    // the position when text is inserted before it, including repeated paragraphs.
    int anchor = character(), prefix = 0;
    while (prefix < std::min(script_.size(), text.size()) && script_[prefix] == text[prefix])
        ++prefix;
    int suffix = 0;
    while (suffix < script_.size() - prefix && suffix < text.size() - prefix &&
           script_[script_.size() - 1 - suffix] == text[text.size() - 1 - suffix])
        ++suffix;
    if (anchor >= script_.size() - suffix)
        anchor += int(text.size() - script_.size());
    else if (anchor >= prefix)
        anchor = prefix;
    const bool started = position_ >= 0;
    script_ = text;
    words_ = tokenize(text);
    occurrences_.clear();
    for (int i = 0; i < words_.size(); ++i)
        occurrences_[words_[i].normalized].push_back(i);
    lastHypothesis_.clear();
    misses_ = 0;
    position_ = -1;
    if (started && !words_.empty()) {
        position_ = 0;
        for (int i = 0; i < words_.size() && words_[i].start <= anchor; ++i)
            position_ = i;
    }
}

void ScriptTracker::seek(int word) {
    position_ = std::clamp(word, -1, std::max(-1, int(words_.size()) - 1));
    lastHypothesis_.clear();
    misses_ = 0;
}

void ScriptTracker::seekCharacter(int character) {
    int index = -1;
    for (int i = 0; i < words_.size() && words_[i].start <= character; ++i)
        index = i;
    seek(index);
}

int ScriptTracker::sectionTarget(int direction) const {
    const auto list = sections(script_);
    if (list.isEmpty())
        return -1;
    const int next = position_ + 1 < words_.size() ? words_[position_ + 1].start : int(script_.size());
    if (direction > 0) {
        for (const auto &section : list)
            if (section.start > next)
                return section.start;
        return -1;
    }
    const auto before = std::count_if(list.begin(), list.end(), [next](const auto &s) { return s.start < next; });
    return list[std::max<qsizetype>(0, before - 2)].start;
}

int ScriptTracker::character() const { return position_ < 0 ? 0 : words_[position_].start; }
int ScriptTracker::length() const { return position_ < 0 ? 0 : words_[position_].length; }

// Bounded local sequence matching around the reading position: no language
// model and no whole-document scan for each partial transcript. Requires at
// least two exact words and at most one edit per three spoken words.
int ScriptTracker::localMatch(const QVector<Word> &spoken) const {
    const int m = int(spoken.size());
    const int first = std::max(0, position_ - 24), last = std::min(int(words_.size()), position_ + 81);
    QVector<int> previous(m + 1), current(m + 1);
    double best = std::numeric_limits<double>::max();
    int bestEnd = -1;
    for (int start = first; start < last; ++start)
        alignAt(spoken, words_, start, last, previous, current, [&](int n, int edits) {
            if (n < std::max(2, m - 2) || edits > m / 3 || exactMatches(spoken, words_, start, n) < 2)
                return;
            const int end = start + n - 1;
            const double score = edits * 100.0 + std::abs(end - position_) * 0.1;
            if (score < best) {
                best = score;
                bestEnd = end;
            }
        });
    return bestEnd;
}

// Recovers after a long skip or backtrack. Only a strong match that occurs in
// exactly one place in the script moves the reader.
int ScriptTracker::globalMatch(const QVector<Word> &spoken) const {
    const int m = int(spoken.size());
    const int minimumExact = std::max(4, m - m / 4);
    // A valid match contains all but at most m - minimumExact spoken words, so it
    // contains one of the (m - minimumExact + 1) rarest. Only alignments that
    // start shortly before one of their occurrences need to be checked.
    QVector<const QVector<int> *> rare;
    for (const auto &word : spoken)
        if (const auto found = occurrences_.constFind(word.normalized); found != occurrences_.cend())
            rare.push_back(&found.value());
    if (rare.size() < minimumExact)
        return -1;
    std::sort(rare.begin(), rare.end(), [](const auto *a, const auto *b) { return a->size() < b->size(); });
    rare.resize(m - minimumExact + 1 < rare.size() ? m - minimumExact + 1 : rare.size());
    QVector<int> starts;
    for (const auto *positions : rare)
        for (const int position : *positions)
            for (int start = std::max(0, position - m - 1); start <= position; ++start)
                starts.push_back(start);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    QVector<int> previous(m + 1), current(m + 1);
    int bestEdits = std::numeric_limits<int>::max();
    QVector<int> ends;
    for (const int start : starts)
        alignAt(spoken, words_, start, int(words_.size()), previous, current, [&](int n, int edits) {
            if (n < m - 1 || edits > m / 4 || edits > bestEdits ||
                exactMatches(spoken, words_, start, n) < minimumExact)
                return;
            if (edits < bestEdits) {
                bestEdits = edits;
                ends.clear();
            }
            ends.push_back(start + n - 1);
        });
    if (ends.isEmpty())
        return -1;
    // Overlapping alignments of the same passage are one location.
    const auto [low, high] = std::minmax_element(ends.begin(), ends.end());
    if (*high - *low > 2)
        return -1;
    return *high;
}

bool ScriptTracker::follow(const QString &hypothesis, bool final) {
    if (hypothesis.isEmpty() || hypothesis == lastHypothesis_) {
        if (final)
            lastHypothesis_.clear();
        return false;
    }
    lastHypothesis_ = final ? QString{} : hypothesis;
    // Recognizers may emit bracketed tokens such as "[unk]"; tokenize drops them.
    auto spoken = tokenize(hypothesis);
    if (spoken.isEmpty() || words_.isEmpty())
        return false;
    if (spoken.size() == 1) {
        const int next = position_ + 1;
        if (next < words_.size() && words_[next].normalized == spoken.front().normalized) {
            position_ = next;
            misses_ = 0;
            return true;
        }
        return false;
    }
    if (spoken.size() > 8)
        spoken = spoken.sliced(spoken.size() - 8);
    int end = localMatch(spoken);
    if (end < 0 && spoken.size() >= 4)
        end = globalMatch(spoken);
    if (end < 0) {
        if (final && spoken.size() >= 3)
            ++misses_;
        return false;
    }
    misses_ = 0;
    if (end == position_)
        return false;
    position_ = end;
    return true;
}
