// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tracker.h"
#include "approval.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void multilingualAlignment() {
        QFile fixtures(QFINDTESTDATA("multilingual-tracker.json"));QVERIFY(fixtures.open(QIODevice::ReadOnly));
        for(const auto &value:QJsonDocument::fromJson(fixtures.readAll()).array()) {
            const auto data=value.toObject();const auto script=data["script"].toString();
            ScriptTracker tracker;tracker.setScript(script);
            QVERIFY2(tracker.follow(data["partial"].toString()),qPrintable(script));
            QCOMPARE(script.left(tracker.character()+tracker.length()),data["read"].toString());
            const auto before=tracker.character();tracker.setScript(QString::fromUtf8("🎤 ")+script);
            QCOMPARE(tracker.character(),before+3);
        }
    }
    void italianWords() {
        const auto words=ScriptTracker::tokenize("È l'obiettivo: più qualità, 2026.");
        QCOMPARE(words.size(),6); QCOMPARE(words[0].normalized,QString("è"));
        QCOMPARE(words[2].normalized,QString("obiettivo")); QCOMPARE(words[2].start,4);
    }
    void followsPartialAndFinalWithoutDuplicateAdvance() {
        ScriptTracker tracker; tracker.setScript("uno due tre quattro cinque sei");
        QVERIFY(tracker.follow("uno due")); QCOMPARE(tracker.position(),1);
        QVERIFY(!tracker.follow("uno due")); QVERIFY(!tracker.follow("uno due",true));
        QVERIFY(tracker.follow("tre quattro")); QCOMPARE(tracker.position(),3);
    }
    void resumesAfterPauseAndRejectsUnrelatedSpeech() {
        ScriptTracker tracker; tracker.setScript("Buongiorno a tutti oggi parliamo del nuovo progetto");
        QVERIFY(tracker.follow("buongiorno a tutti",true));
        QVERIFY(!tracker.follow("",true)); QVERIFY(!tracker.follow("il gatto mangia il pesce",true));
        QCOMPARE(tracker.position(),2);
        QVERIFY(tracker.follow("oggi parliamo del nuovo progetto",true)); QCOMPARE(tracker.position(),7);
    }
    void repeatsAndSkipsWithinBoundedWindow() {
        ScriptTracker tracker; tracker.setScript("uno due tre quattro cinque sei sette otto nove dieci");
        tracker.follow("uno due tre quattro cinque",true);
        QVERIFY(tracker.follow("tre quattro",true)); QCOMPARE(tracker.position(),3);
        QVERIFY(tracker.follow("otto nove dieci",true)); QCOMPARE(tracker.position(),9);
    }
    void toleratesOneSubstitution() {
        ScriptTracker tracker; tracker.setScript("oggi presentiamo il nuovo progetto");
        QVERIFY(tracker.follow("oggi presentiamo un nuovo progetto",true)); QCOMPARE(tracker.position(),4);
    }
    void preservesAnchorWhenEditingBeforeIt() {
        ScriptTracker tracker; tracker.setScript("uno due tre quattro"); tracker.seek(2);
        tracker.setScript("introduzione uno due tre quattro"); QCOMPARE(tracker.position(),3); QCOMPARE(tracker.character(),21);
        tracker.setScript("uno due tre quattro"); QCOMPARE(tracker.position(),2);
        tracker.setScript(""); QCOMPARE(tracker.position(),-1);
    }
    void rejectsDistantFalseResynchronization() {
        ScriptTracker tracker; tracker.setScript(QString("parola ").repeated(120)+"obiettivo chiaro");
        QVERIFY(!tracker.follow("obiettivo chiaro",true)); QCOMPARE(tracker.position(),-1);
    }
    void approvalsAreRequired() {
        ApprovalFlow flow; QVERIFY(flow.approveAnswer().isEmpty()); QCOMPARE(flow.approveQuestion(),0ULL);
        flow.setQuestion("Qual è il prossimo passo?"); const auto request=flow.approveQuestion(); QVERIFY(request!=0);
        QCOMPARE(flow.approveQuestion(),0ULL); QVERIFY(flow.approveAnswer().isEmpty());
        QVERIFY(flow.receiveAnswer(request,"Verifichiamo il prototipo."));
        flow.editAnswer("Proviamolo insieme."); QCOMPARE(flow.approveAnswer(),QString("Proviamolo insieme."));
        QVERIFY(flow.approveAnswer().isEmpty());
    }
    void changingQuestionRejectsStaleResponse() {
        ApprovalFlow flow; flow.setQuestion("prima domanda"); const auto old=flow.approveQuestion();
        flow.setQuestion("domanda corretta"); QVERIFY(!flow.receiveAnswer(old,"risposta vecchia"));
        const auto current=flow.approveQuestion(); flow.fail(old);
        QCOMPARE(flow.stage(),ApprovalFlow::Stage::Generating);
        QVERIFY(flow.receiveAnswer(current,"risposta corretta"));
        flow.setQuestion("terza domanda"); QVERIFY(flow.approveAnswer().isEmpty());
    }
    void blankAnswerCannotBeInserted() {
        ApprovalFlow flow; flow.setQuestion("domanda"); auto id=flow.approveQuestion();
        QVERIFY(!flow.receiveAnswer(id,"  ")); flow.fail(id);
        id=flow.approveQuestion(); QVERIFY(flow.receiveAnswer(id,"bozza"));
        flow.editAnswer(" "); QVERIFY(flow.approveAnswer().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
