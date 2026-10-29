// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tracker.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void multilingualAlignment() {
        QFile fixtures(QFINDTESTDATA("multilingual-tracker.json"));
        QVERIFY(fixtures.open(QIODevice::ReadOnly));
        for (const auto &value : QJsonDocument::fromJson(fixtures.readAll()).array()) {
            const auto data = value.toObject();
            const auto script = data["script"].toString();
            ScriptTracker tracker;
            tracker.setScript(script);
            QVERIFY2(tracker.follow(data["partial"].toString()), qPrintable(script));
            QCOMPARE(script.left(tracker.character() + tracker.length()), data["read"].toString());
            const auto before = tracker.character();
            tracker.setScript(QString::fromUtf8("🎤 ") + script);
            QCOMPARE(tracker.character(), before + 3);
        }
    }
    void italianWords() {
        const auto words = ScriptTracker::tokenize("È l'obiettivo: più qualità, 2026.");
        QCOMPARE(words.size(), 6);
        QCOMPARE(words[0].normalized, QString("è"));
        QCOMPARE(words[2].normalized, QString("obiettivo"));
        QCOMPARE(words[2].start, 4);
    }
    void followsPartialAndFinalWithoutDuplicateAdvance() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre quattro cinque sei");
        QVERIFY(tracker.follow("uno due"));
        QCOMPARE(tracker.position(), 1);
        QVERIFY(!tracker.follow("uno due"));
        QVERIFY(!tracker.follow("uno due", true));
        QVERIFY(tracker.follow("tre quattro"));
        QCOMPARE(tracker.position(), 3);
    }
    void resumesAfterPauseAndRejectsUnrelatedSpeech() {
        ScriptTracker tracker;
        tracker.setScript("Buongiorno a tutti oggi parliamo del nuovo progetto");
        QVERIFY(tracker.follow("buongiorno a tutti", true));
        QVERIFY(!tracker.follow("", true));
        QVERIFY(!tracker.follow("il gatto mangia il pesce", true));
        QCOMPARE(tracker.position(), 2);
        QVERIFY(tracker.follow("oggi parliamo del nuovo progetto", true));
        QCOMPARE(tracker.position(), 7);
    }
    void repeatsAndSkipsWithinBoundedWindow() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre quattro cinque sei sette otto nove dieci");
        tracker.follow("uno due tre quattro cinque", true);
        QVERIFY(tracker.follow("tre quattro", true));
        QCOMPARE(tracker.position(), 3);
        QVERIFY(tracker.follow("otto nove dieci", true));
        QCOMPARE(tracker.position(), 9);
    }
    void toleratesOneSubstitution() {
        ScriptTracker tracker;
        tracker.setScript("oggi presentiamo il nuovo progetto");
        QVERIFY(tracker.follow("oggi presentiamo un nuovo progetto", true));
        QCOMPARE(tracker.position(), 4);
    }
    void preservesAnchorWhenEditingBeforeIt() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre quattro");
        tracker.seek(2);
        tracker.setScript("introduzione uno due tre quattro");
        QCOMPARE(tracker.position(), 3);
        QCOMPARE(tracker.character(), 21);
        tracker.setScript("uno due tre quattro");
        QCOMPARE(tracker.position(), 2);
        tracker.setScript("");
        QCOMPARE(tracker.position(), -1);
    }
    void rejectsDistantFalseResynchronization() {
        ScriptTracker tracker;
        tracker.setScript(QString("parola ").repeated(120) + "obiettivo chiaro");
        QVERIFY(!tracker.follow("obiettivo chiaro", true));
        QCOMPARE(tracker.position(), -1);
    }
    void cuesAndHeadingsAreShownButNotSpoken() {
        const QString script = "# Opening\nHello [smile] everyone\n[look at camera]\nThanks [unclosed\n## Q&A ##";
        const auto words = ScriptTracker::tokenize(script);
        QStringList spoken;
        for (const auto &word : words)
            spoken << word.normalized;
        QCOMPARE(spoken, QStringList({"hello", "everyone", "thanks", "unclosed"}));
        QCOMPARE(ScriptTracker::cues(script).size(), 2);
        const auto sections = ScriptTracker::sections(script);
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections[0].title, QString("Opening"));
        QCOMPARE(sections[1].title, QString("Q&A"));
        QCOMPARE(sections[1].start, int(script.indexOf("## Q&A")));
        QVERIFY(ScriptTracker::headings("#hashtag is text").isEmpty());

        // Section jumps advance one section at a time (a jump leaves the position just before
        // the section's first word) and go back to the previous section.
        ScriptTracker sectioned;
        sectioned.setScript("# One\nalpha beta\n# Two\ngamma delta\n# Three\nepsilon");
        const auto jump = [&sectioned](int direction) {
            if (const int target = sectioned.sectionTarget(direction); target >= 0)
                sectioned.seekCharacter(target);
            return sectioned.position();
        };
        QCOMPARE(QList<int>({jump(1), jump(1), jump(1)}), QList<int>({1, 3, 3}));
        QCOMPARE(QList<int>({jump(-1), jump(-1), jump(-1)}), QList<int>({1, -1, -1}));
        sectioned.seek(2);
        QCOMPARE(jump(-1), -1);
        ScriptTracker plain;
        plain.setScript("no headings here");
        QCOMPARE(plain.sectionTarget(1), -1);

        // YAML front matter: shown, never spoken, never a section; its title names the script.
        const QString withMeta =
            "---\ntitle: \"Launch day\"\n# a YAML comment\nspeaker: [me]\n---\n# Opening\nHello everyone";
        QCOMPARE(ScriptTracker::frontMatter(withMeta).length, int(withMeta.indexOf("\n# Opening")));
        QStringList metaWords;
        for (const auto &word : ScriptTracker::tokenize(withMeta))
            metaWords << word.normalized;
        QCOMPARE(metaWords, QStringList({"hello", "everyone"}));
        QCOMPARE(ScriptTracker::sections(withMeta).size(), 1);
        QCOMPARE(ScriptTracker::title(withMeta), QString("Launch day"));
        QCOMPARE(ScriptTracker::title("---\nauthor: me\n...\n\n# Q&A\nText"), QString("Q&A"));
        QCOMPARE(ScriptTracker::frontMatter("---\nnot closed\nHello").length, 0);
        QCOMPARE(ScriptTracker::frontMatter("Intro\n---\nx\n---").length, 0);
    }
    void voiceSkipsCuesAndRecognizerTags() {
        ScriptTracker tracker;
        tracker.setScript("Welcome everyone [pause] today we launch");
        QVERIFY(tracker.follow("welcome everyone today", true));
        QCOMPARE(tracker.position(), 2);
        QVERIFY(!tracker.follow("[unk]", true)); // Vosk marks unknown words like this.
    }
    void recoversAfterLongSkipOrBacktrack() {
        QStringList filler;
        for (int i = 0; i < 150; ++i)
            filler << QString("parola%1").arg(i);
        ScriptTracker tracker;
        tracker.setScript("inizio del discorso " + filler.join(' ') + " la conclusione arriva adesso finalmente");
        QVERIFY(tracker.follow("inizio del discorso", true));
        QCOMPARE(tracker.position(), 2);
        // Far beyond the local window: four distinctive words in one place.
        QVERIFY(tracker.follow("la conclusione arriva adesso", true));
        QCOMPARE(tracker.position(), tracker.count() - 2);
        // And back again.
        QVERIFY(tracker.follow("inizio del discorso parola0", true));
        QCOMPARE(tracker.position(), 3);
    }
    void ambiguousLongJumpsAreRejected() {
        const QString chorus = "we will build it together ";
        ScriptTracker tracker;
        QStringList filler;
        for (int i = 0; i < 120; ++i)
            filler << QString("x%1").arg(i);
        tracker.setScript(chorus + filler.join(' ') + " " + chorus + filler.join(' ') + " " + chorus);
        QVERIFY(!tracker.follow("we will build it together", true) || tracker.position() < 10);
    }
    void reportsLostAfterRepeatedOffScriptSpeech() {
        ScriptTracker tracker;
        tracker.setScript("today we present the new project plan");
        QVERIFY(tracker.follow("today we present", true));
        QVERIFY(!tracker.lost());
        tracker.follow("by the way the weather is lovely", true);
        QVERIFY(!tracker.lost());
        tracker.follow("and my cat says hello to everyone", true);
        QVERIFY(tracker.lost());
        QVERIFY(tracker.follow("the new project", true));
        QVERIFY(!tracker.lost());
    }
    void seekCharacterPicksTheWordAtOrBefore() {
        ScriptTracker tracker;
        tracker.setScript("uno due tre");
        tracker.seekCharacter(5);
        QCOMPARE(tracker.position(), 1);
        tracker.seekCharacter(0);
        QCOMPARE(tracker.position(), 0);
        tracker.seekCharacter(-1);
        QCOMPARE(tracker.position(), -1);
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
