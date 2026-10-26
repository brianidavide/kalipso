// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QString>
#include <QVector>

struct Word { QString normalized; int start; int length; };
class ScriptTracker {
public:
    static QVector<Word> tokenize(const QString &text);
    void setScript(const QString &text);
    bool follow(const QString &hypothesis, bool final = false);
    void seek(int word);
    int position() const { return position_; }
    int character() const;
    int length() const;
    int count() const { return int(words_.size()); }
private:
    QString script_, lastHypothesis_;
    QVector<Word> words_;
    int position_ = -1;
};
