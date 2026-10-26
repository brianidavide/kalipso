// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QObject>

class ApprovalFlow : public QObject {
    Q_OBJECT
public:
    enum class Stage { Question, Generating, Answer, Inserted };
    explicit ApprovalFlow(QObject *parent=nullptr) : QObject(parent) {}
    void setQuestion(const QString &text);
    quint64 approveQuestion();
    bool receiveAnswer(quint64 request, const QString &answer);
    void fail(quint64 request);
    void editAnswer(const QString &answer);
    QString approveAnswer();
    QString question() const { return question_; }
    QString answer() const { return answer_; }
    Stage stage() const { return stage_; }
signals:
    void changed();
    void invalidated();
private:
    QString question_, answer_;
    quint64 version_ = 0;
    Stage stage_ = Stage::Question;
};
