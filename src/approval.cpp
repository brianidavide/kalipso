// SPDX-License-Identifier: AGPL-3.0-or-later
#include "approval.h"
void ApprovalFlow::setQuestion(const QString &text) {
    if (text == question_) return;
    ++version_; question_=text; answer_.clear(); stage_=Stage::Question;
    emit invalidated(); emit changed();
}
quint64 ApprovalFlow::approveQuestion() {
    if (question_.trimmed().isEmpty() || stage_ == Stage::Generating) return 0;
    ++version_; answer_.clear(); stage_=Stage::Generating; emit changed(); return version_;
}
bool ApprovalFlow::receiveAnswer(quint64 request, const QString &answer) {
    if (request != version_ || stage_ != Stage::Generating || answer.trimmed().isEmpty()) return false;
    answer_=answer; stage_=Stage::Answer; emit changed(); return true;
}
void ApprovalFlow::fail(quint64 request) {
    if (request != version_ || stage_ != Stage::Generating) return;
    stage_=Stage::Question; emit changed();
}
void ApprovalFlow::editAnswer(const QString &answer) {
    if (stage_ != Stage::Answer) return;
    answer_=answer; emit changed();
}
QString ApprovalFlow::approveAnswer() {
    if (stage_ != Stage::Answer || answer_.trimmed().isEmpty()) return {};
    stage_=Stage::Inserted; emit changed(); return answer_;
}
