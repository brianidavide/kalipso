// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QHash>
#include <QString>
#include <QVector>

struct Word {
    QString normalized;
    int start;
    int length;
};

// A span of script text: a [direction cue] or a "# Section" heading line.
struct TextRange {
    int start;
    int length;
    int end() const { return start + length; }
};

struct Section {
    QString title;
    int start; // Character offset of the heading line.
};

// Follows a speaker through a script. Words inside [bracketed cues] and
// "# heading" lines are shown to the reader but never matched against speech.
class ScriptTracker {
public:
    static QVector<TextRange> cues(const QString &text);
    static QVector<TextRange> headings(const QString &text);
    static QVector<Section> sections(const QString &text);
    // Speakable words, in order. Excludes cues and headings.
    static QVector<Word> tokenize(const QString &text);

    void setScript(const QString &text);
    // Advances to the last script word matched by a speech hypothesis.
    // Returns true when the reading position moved.
    bool follow(const QString &hypothesis, bool final = false);
    void seek(int word);
    // Moves to the first word at or after a character offset.
    void seekCharacter(int character);
    // Start of the section to jump to, or -1 without one. Forward: the next heading after
    // the word about to be read. Backward: the section before the current one (at most the
    // first). Measured from the next word because a jump leaves the position just before it.
    int sectionTarget(int direction) const;

    int position() const { return position_; }
    int character() const;
    int length() const;
    int count() const { return int(words_.size()); }
    const QVector<Word> &words() const { return words_; }
    // Several consecutive utterances did not match anywhere in the script.
    bool lost() const { return misses_ >= 2; }

private:
    int localMatch(const QVector<Word> &spoken) const;
    int globalMatch(const QVector<Word> &spoken) const;

    QString script_, lastHypothesis_;
    QVector<Word> words_;
    QHash<QString, QVector<int>> occurrences_; // Word -> indexes, for whole-script recovery.
    int position_ = -1;
    int misses_ = 0;
};
