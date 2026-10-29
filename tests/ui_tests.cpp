// SPDX-License-Identifier: AGPL-3.0-or-later
#include "impress.h"
#include "remote.h"
#include "script_highlighter.h"
#include "speech.h"
#include "window.h"
#include <QAbstractButton>
#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>

namespace {
QPlainTextEdit *editor(MainWindow &window) { return window.findChild<QPlainTextEdit *>("scriptEditor"); }
QLabel *positionLabel(MainWindow &window) { return window.findChild<QLabel *>("readingPosition"); }
// The widgets that need the most width in a scroll area, so an overflow seen
// only on another machine (fonts, Qt version) can be fixed without guessing.
QString overflowReport(QScrollArea *area) {
    auto *content = area->widget();
    QList<QPair<int, QString>> needs;
    for (auto *child : content->findChildren<QWidget *>()) {
        if (!child->isVisible())
            continue;
        const int left = child->mapTo(content, QPoint(0, 0)).x();
        const int need = left + std::max(child->minimumSizeHint().width(), child->minimumWidth());
        QString text;
        if (auto *label = qobject_cast<QLabel *>(child))
            text = label->text().left(40);
        else if (auto *button = qobject_cast<QAbstractButton *>(child))
            text = button->text().left(40);
        needs.append({need, QString("%1#%2 '%3' x=%4 min=%5 hint=%6")
                                .arg(child->metaObject()->className(), child->objectName(), text)
                                .arg(left)
                                .arg(child->minimumSizeHint().width())
                                .arg(child->sizeHint().width())});
    }
    std::sort(needs.begin(), needs.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList lines{QString("%1 viewport=%2 content min=%3 width=%4")
                          .arg(area->objectName())
                          .arg(area->viewport()->width())
                          .arg(content->minimumSizeHint().width())
                          .arg(content->width())};
    for (int i = 0; i < std::min<qsizetype>(5, needs.size()); ++i)
        lines.append(QString("  needs %1: %2").arg(needs[i].first).arg(needs[i].second));
    return lines.join(u'\n');
}

QDialog *openSettings(MainWindow &window) {
    window.findChild<QPushButton *>("settingsButton")->click();
    return window.findChild<QDialog *>("settingsDialog");
}

void setCursor(QPlainTextEdit *edit, int position) {
    auto cursor = edit->textCursor();
    cursor.setPosition(position);
    edit->setTextCursor(cursor);
}

// Horizontal centre of the dark "ink" in an image with a light background.
double inkCentre(const QImage &image) {
    double sum = 0, count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 128) {
                sum += x;
                ++count;
            }
    return count ? sum / count / image.width() : 0.5;
}
} // namespace

class UiTests : public QObject {
    Q_OBJECT
    QTemporaryDir scripts_;

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("kalipso-tests");
        QCoreApplication::setApplicationName("isolated-ui-tests");
        QVERIFY(scripts_.isValid());
    }
    void init() {
        QSettings().clear();
        QSettings().setValue("reading/countdown", 0);
        QDir(scripts_.path()).removeRecursively();
        QDir().mkpath(scripts_.path());
    }

    void engineProfilesKeepTheirOwnModelsAndLanguages() {
        {
            MainWindow window(scripts_.path());
            auto *engine = window.findChild<QComboBox *>("speechBackend");
            auto *language = window.findChild<QComboBox *>("speechLanguage");
            auto *model = window.findChild<QLineEdit *>("speechModel");
            QVERIFY(engine && language && model);
            engine->setCurrentIndex(engine->findData("moonshine"));
            model->setText("moonshine-model");
            language->setEditText("de-DE");
            engine->setCurrentIndex(engine->findData("vosk"));
            model->setText("vosk-model");
            language->setCurrentIndex(language->findData("it-IT"));
            engine->setCurrentIndex(engine->findData("moonshine"));
            QCOMPARE(model->text(), QString("moonshine-model"));
            QCOMPARE(language->currentData().toString(), QString("de-DE"));
            engine->setCurrentIndex(engine->findData("vosk"));
            QCOMPARE(model->text(), QString("vosk-model"));
            QCOMPARE(language->currentData().toString(), QString("it-IT"));
        }
        MainWindow reopened(scripts_.path());
        QCOMPARE(reopened.findChild<QComboBox *>("speechBackend")->currentData().toString(), QString("vosk"));
        QCOMPARE(reopened.findChild<QLineEdit *>("speechModel")->text(), QString("vosk-model"));
    }

    void scriptsAndReadingPositionSurviveRestart() {
        QString id;
        {
            MainWindow window(scripts_.path());
            editor(window)->setPlainText("uno due tre quattro");
            setCursor(editor(window), 8);
            window.findChild<QPushButton *>("readHere")->click();
            QVERIFY(positionLabel(window)->text().startsWith("Word 3 of 4"));
            id = window.currentScript();
        }
        MainWindow reopened(scripts_.path());
        QCOMPARE(reopened.currentScript(), id);
        QCOMPARE(editor(reopened)->toPlainText(), QString("uno due tre quattro"));
        QVERIFY(positionLabel(reopened)->text().startsWith("Word 3 of 4"));
    }

    void libraryCreatesSwitchesAndDeletesScripts() {
        MainWindow window(scripts_.path());
        auto *picker = window.findChild<QComboBox *>("scriptPicker");
        editor(window)->setPlainText("# First talk\nHello");
        const auto first = window.currentScript();
        window.findChild<QPushButton *>("newScript")->click();
        QVERIFY(window.currentScript() != first);
        QVERIFY(editor(window)->toPlainText().isEmpty());
        editor(window)->setPlainText("Second talk");
        QCOMPARE(picker->count(), 2);
        picker->setCurrentIndex(picker->findData(first));
        emit picker->activated(picker->currentIndex());
        QCOMPARE(window.currentScript(), first);
        QCOMPARE(editor(window)->toPlainText(), QString("# First talk\nHello"));
        QCOMPARE(picker->currentText(), QString("First talk"));
    }

    void accessiblePalettesKeepReadTextAndSelectionLegible() {
        for (const auto &p : {Design::light, Design::dark}) {
            const auto colors = Accessibility::palette(p);
            for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                QVERIFY(Accessibility::contrast(colors.color(group, QPalette::Text),
                                                colors.color(group, QPalette::Base)) >= 4.5);
                QVERIFY(Accessibility::contrast(colors.color(group, QPalette::PlaceholderText),
                                                colors.color(group, QPalette::Base)) >= 4.5);
                QVERIFY(Accessibility::contrast(colors.color(group, QPalette::HighlightedText),
                                                colors.color(group, QPalette::Highlight)) >= 4.5);
            }
            ReaderWindow reader;
            reader.setReaderTheme(QString(p.background) == Design::light.background ? "light" : "dark");
            reader.text->setPlainText("Read current next");
            reader.highlight(5, 7);
            reader.show();
            QTest::qWait(30);
            QCOMPARE(reader.grab().toImage().pixelColor(3, 3), QColor(p.background));
            const auto selections = reader.text->extraSelections();
            QCOMPARE(selections.size(), 2);
            QVERIFY(Accessibility::contrast(selections[0].format.foreground().color(), QColor(p.surface)) >= 4.5);
            // The current word is accent text on a soft tint, underlined.
            QVERIFY(Accessibility::contrast(selections[1].format.foreground().color(),
                                            selections[1].format.background().color()) >= 4.5);
            QVERIFY(selections[1].format.fontUnderline());
        }
        QVERIFY(Accessibility::contrast(QColor("#777777"), Qt::white) < 4.5);
    }

    void statusHasAnAccessibleRoleAndName() {
        StatusLabel status("Paused");
        auto *interface = QAccessible::queryAccessibleInterface(&status);
        QVERIFY(interface);
        QCOMPARE(interface->role(), QAccessible::StatusBar);
        status.setText("Listening");
        QCOMPARE(interface->text(QAccessible::Name), QString("Listening"));
    }

    void settingsFieldsHaveAccessibleNames() {
        MainWindow window(scripts_.path());
        for (const auto *name : {"speechBackend", "speechLanguage", "speechLibrary", "speechModel", "applicationTheme",
                                 "readerTheme", "readerMirror", "readingLine", "installedSpeech", "scriptPicker"}) {
            auto *field = window.findChild<QWidget *>(name);
            QVERIFY2(field, name);
            auto *interface = QAccessible::queryAccessibleInterface(field);
            QVERIFY(interface);
            QVERIFY2(!interface->text(QAccessible::Name).isEmpty(), name);
        }
    }

    void settingsScrollDoesNotChangeSelections() {
        MainWindow window(scripts_.path());
        window.show();
        auto *settings = openSettings(window);
        QVERIFY(settings && settings->isVisible());
        const auto wheel = [](QComboBox *combo) {
            QWheelEvent event(QPointF(10, 10), combo->mapToGlobal(QPoint(10, 10)), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(combo, &event);
        };
        for (auto *combo : settings->findChildren<QComboBox *>()) {
            if (!combo->isVisible() || combo->count() < 2)
                continue;
            combo->setFocus();
            const int before = combo->currentIndex();
            wheel(combo);
            QCOMPARE(combo->currentIndex(), before);
        }
        QComboBox unsafe;
        unsafe.addItems({"First", "Second"});
        unsafe.show();
        unsafe.setFocus();
        wheel(&unsafe);
        QCOMPARE(unsafe.currentIndex(), 1); // Counterexample: ordinary combos change while scrolling.
    }

    void hidingTheReaderPausesAndShowingDoesNotResume() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three four five six seven eight");
        window.showPrompter();
        auto *reader = window.prompter();
        auto *mode = reader->findChild<QToolButton *>("readerMode");
        for (auto *action : mode->menu()->actions())
            if (action->text() == "Steady speed")
                action->trigger();
        QCOMPARE(mode->text(), QString("Steady speed"));
        auto *primary = reader->findChild<QPushButton *>("readerListeningButton");
        auto *status = reader->findChild<QLabel *>("readerListeningStatus");
        primary->click();
        QCOMPARE(primary->text(), QString("Pause scrolling"));
        reader->hide();
        QCOMPARE(primary->text(), QString("Start scrolling"));
        QVERIFY(status->text().startsWith("Paused"));
        window.showPrompter();
        QTest::qWait(600);
        QCOMPARE(primary->text(), QString("Start scrolling"));
    }

    void steadyScrollingAdvancesAndStopsAtTheEnd() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three");
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Steady);
        for (int i = 0; i < 30; ++i)
            emit reader->speedRequested(10); // Clamped at 400 words per minute.
        emit reader->toggleRequested();
        QTRY_VERIFY_WITH_TIMEOUT(positionLabel(window)->text().startsWith("Word 3 of 3"), 3000);
        QTRY_COMPARE(reader->findChild<QLabel *>("readerListeningStatus")->text(), QString("End of script"));
    }

    void readerControlsFadeWhileReadingAndReturn() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText(QString("word ").repeated(200));
        window.showPrompter();
        auto *reader = window.prompter();
        emit reader->modeRequested(ReadingMode::Steady);
        auto *status = reader->findChild<QLabel *>("readerListeningStatus");
        emit reader->toggleRequested();
        QVERIFY(status->text().startsWith("Scrolling · 2 min left"));
        QVERIFY(reader->controlsVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!reader->controlsVisible(), 4000);
        QTest::keyClick(reader->text, Qt::Key_Plus); // Any key brings the controls back.
        QVERIFY(reader->controlsVisible());
        emit reader->toggleRequested(); // Paused: controls stay.
        QTest::qWait(3000);
        QVERIFY(reader->controlsVisible());
        QVERIFY(status->text().startsWith("Paused · "));
    }

    void readerFontAndLineSpacingFollowSettingsAndEdits() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText(QString("word ").repeated(300));
        QVERIFY(positionLabel(window)->text().endsWith(" · about 3 min")); // 300 words at 140 words/min.
        window.showPrompter();
        auto *reader = window.prompter();
        openSettings(window);
        auto *spacing = window.findChild<QComboBox *>("readerLineSpacing");
        auto *font = window.findChild<QComboBox *>("readerFont");
        QVERIFY(spacing && font);
        const auto lineHeight = [reader] { return reader->text->document()->lastBlock().blockFormat().lineHeight(); };
        const int before = reader->text->cursorRect(QTextCursor(reader->text->document()->lastBlock())).height();
        spacing->setCurrentIndex(spacing->findData(150));
        emit spacing->activated(spacing->currentIndex());
        QCOMPARE(lineHeight(), 150.0);
        QCOMPARE(QSettings().value("reading/lineSpacing").toInt(), 150);
        // The script keeps the spacing through edits that add paragraphs.
        editor(window)->setPlainText(QString("word ").repeated(300) + "\n\nA new paragraph");
        QCOMPARE(lineHeight(), 150.0);
        QVERIFY(reader->text->toPlainText().endsWith("A new paragraph"));
        QVERIFY(before > 0);
        // A chosen font reaches the reader; "Interface font" goes back to the default.
        QVERIFY(font->count() > 1);
        font->setCurrentIndex(1);
        emit font->activated(1);
        QCOMPARE(reader->text->font().family(), font->itemData(1).toString());
        font->setCurrentIndex(0);
        emit font->activated(0);
        QCOMPARE(reader->text->font().family(), QApplication::font().family());
    }

    void eachScriptKeepsItsSpeedAndTheRemoteJumpsBetweenSections() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# One\nalpha beta\n# Two\ngamma delta\n# Three\nepsilon");
        auto *remote = window.findChild<RemoteControl *>();
        QVERIFY(remote);
        // A jump leaves the position just before the section's first word.
        const auto expect = [&window](const char *command, const char *label) {
            emit window.findChild<RemoteControl *>()->command(command);
            return positionLabel(window)->text().startsWith(label);
        };
        QVERIFY(expect("section-next", "Word 2 of 5"));     // Before "gamma"
        QVERIFY(expect("section-next", "Word 4 of 5"));     // Before "epsilon"
        QVERIFY(expect("section-next", "Word 4 of 5"));     // No further section
        QVERIFY(expect("section-previous", "Word 2 of 5")); // Back to Two
        QVERIFY(expect("section-previous", "Word 0 of 5")); // Back to One
        QVERIFY(expect("section-previous", "Word 0 of 5")); // Stays on the first

        // Each script keeps its own steady speed when switching between them.
        auto *picker = window.findChild<QComboBox *>("scriptPicker");
        auto *speed = window.prompter()->findChild<QLabel *>("readerSpeed");
        const auto first = picker->currentData().toString();
        emit window.prompter()->speedRequested(30);
        const auto firstSpeed = speed->text();
        window.findChild<QPushButton *>("newScript")->click();
        const auto second = picker->currentData().toString();
        QVERIFY(second != first);
        emit window.prompter()->speedRequested(-60);
        const auto secondSpeed = speed->text();
        QVERIFY(secondSpeed != firstSpeed);
        picker->setCurrentIndex(picker->findData(first));
        emit picker->activated(picker->currentIndex());
        QCOMPARE(speed->text(), firstSpeed);
        picker->setCurrentIndex(picker->findData(second));
        emit picker->activated(picker->currentIndex());
        QCOMPARE(speed->text(), secondSpeed);
    }

    void sessionReportCanBeSavedAfterAVoiceSession() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# Intro\nalpha beta gamma delta epsilon zeta eta\n# Main\niota kappa");
        auto *action = window.findChild<QAction *>("saveSessionReport");
        QVERIFY(action && !action->isEnabled());
        auto *speech = window.findChild<SpeechService *>();
        QVERIFY(speech);
        emit speech->listening();
        emit speech->transcript("alpha beta gamma delta epsilon zeta", true);
        emit speech->stopped();
        QVERIFY(action->isEnabled());
        QTemporaryDir out;
        const auto path = out.filePath("report.md");
        QVERIFY(window.saveSessionReport(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto report = QString::fromUtf8(file.readAll());
        QVERIFY2(report.startsWith("# Session report: Intro\n"), qPrintable(report));
        QVERIFY2(report.contains("- Words read: 6"), qPrintable(report));
        QVERIFY2(report.contains("| Intro |"), qPrintable(report));
    }

    void customKeysCanReplaceBuiltInKeys() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# One\nalpha beta gamma\n# Two\ndelta");
        window.showPrompter();
        auto *reader = window.prompter();
        openSettings(window);
        const auto edit = [&window](const char *id) { return window.findChild<QKeySequenceEdit *>(id); };
        const auto assign = [](QKeySequenceEdit *edit, QKeySequence sequence) {
            edit->setKeySequence(sequence);
            emit edit->editingFinished();
        };
        auto *speed = reader->findChild<QLabel *>("readerSpeed");
        const auto before = speed->text();
        // The up arrow can change speed instead of stepping back.
        assign(edit("key-faster"), QKeySequence(Qt::Key_Up));
        QTest::keyClick(reader->text, Qt::Key_Up);
        QVERIFY(speed->text() != before);
        QVERIFY(positionLabel(window)->text().startsWith("Word 0 of 4"));
        // A pedal key for the next section.
        assign(edit("key-section-next"), QKeySequence(Qt::Key_B));
        QTest::keyClick(reader->text, Qt::Key_B);
        QVERIFY2(positionLabel(window)->text().startsWith("Word 3 of 4"), qPrintable(positionLabel(window)->text()));
        // A key belongs to one action: giving it to another moves it.
        assign(edit("key-toggle"), QKeySequence(Qt::Key_B));
        QVERIFY(edit("key-section-next")->keySequence().isEmpty());
        QCOMPARE(QSettings().value("keys/toggle").toString(), QString("B"));
        // Only one key per action; built-in keys that were not reassigned still work.
        assign(edit("key-reset"), QKeySequence(Qt::Key_R, Qt::Key_S));
        QCOMPARE(edit("key-reset")->keySequence(), QKeySequence(Qt::Key_R));
        QTest::keyClick(reader->text, Qt::Key_Left);
        QVERIFY(positionLabel(window)->text().startsWith("Word 2 of 4"));
        // Clearing a key gives the built-in one back.
        edit("key-faster")->clear();
        QTest::keyClick(reader->text, Qt::Key_Up);
        QVERIFY(positionLabel(window)->text().startsWith("Word 1 of 4"));
        QSettings().remove("keys");
    }

    void impressSlidesMoveTheReaderToTheirNotes() {
        auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            python = QStandardPaths::findExecutable("python");
        if (python.isEmpty())
            QSKIP("Needs Python to stand in for LibreOffice");
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# Slide 1: Welcome\n\nThank everyone for coming.\nMention the  two  spaces.\n\n"
                                     "# Slide 3: Results\n\nRevenue grew by twelve percent.\nPause for questions.\n");
        auto *impress = window.findChild<ImpressFollower *>();
        auto *status = window.findChild<QLabel *>("audioStatus");
        QVERIFY(impress && status);
        // The same JSON lines impress_follow.py prints for a real slideshow.
        impress->startHelper(python, {"-c", R"(import json, time
for event in [{"state": "connecting"}, {"state": "waiting"},
              {"state": "slide", "slide": 3, "notes": "Revenue grew by twelve percent.\nPause for questions."}]:
    print(json.dumps(event), flush=True)
time.sleep(0.4)
for event in [{"state": "slide", "slide": 2, "notes": ""},
              {"state": "slide", "slide": 4, "notes": "Not in this script"}, {"state": "closed"}]:
    print(json.dumps(event), flush=True)
    time.sleep(0.2)
)"});
        QTRY_VERIFY_WITH_TIMEOUT(positionLabel(window)->text().startsWith("Word 8 of 16"), 5000); // Before "Revenue".
        QCOMPARE(status->text(), QString("Following Impress · slide 3"));
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Impress · slide 2 has no notes."), 5000);
        QVERIFY(positionLabel(window)->text().startsWith("Word 8 of 16")); // Stays put.
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Impress · the notes of slide 4 are not in this script."),
                                  5000);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("The presentation was closed. Stopped following Impress."),
                                  5000);
        QTRY_VERIFY_WITH_TIMEOUT(!impress->active(), 5000);
        QCOMPARE(window.findChild<QAction *>("presentWithImpress")->text(), QString("Present with Impress…"));
    }

    // Opt-in (KALIPSO_IMPRESS_E2E=1, with LibreOffice, xdotool and an X11 display): opens the
    // fixture in real Impress and presses F5 and the right arrow there, as a presenter would.
    void realImpressSlideshowIsFollowed() {
        if (qEnvironmentVariable("KALIPSO_IMPRESS_E2E") != "1")
            QSKIP("Set KALIPSO_IMPRESS_E2E=1 to drive a real LibreOffice Impress");
        const auto xdotool = QStandardPaths::findExecutable("xdotool");
        if (xdotool.isEmpty() || ImpressFollower::findOffice().soffice.isEmpty())
            QSKIP("Needs LibreOffice and xdotool");
        QTemporaryDir work;
        const auto path = work.filePath("talk notes.odp");
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/notes.odp"), path));
        MainWindow window(scripts_.path());
        QVERIFY(window.importFile(path));
        auto *impress = window.findChild<ImpressFollower *>();
        auto *status = window.findChild<QLabel *>("audioStatus");
        QVERIFY(impress->start(path));
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Connected to Impress"), 90000);
        const auto press = [&xdotool](const QString &key) {
            QProcess::execute(xdotool, {"search", "--sync", "--name", "talk notes", "windowactivate", "--sync"});
            QProcess::execute(xdotool, {"key", key});
        };
        press("F5");
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Following Impress · slide 1"), 30000);
        QVERIFY(positionLabel(window)->text().startsWith("Word 0 of"));
        // A click advances the full-screen slideshow (keyboard focus is unreliable without a window manager).
        const auto advance = [&xdotool] {
            QTest::qWait(2000); // The slideshow reports a slide before its window takes input.
            QProcess::execute(xdotool, {"mousemove", "300", "300", "click", "1"});
        };
        advance();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Impress · slide 2 has no notes."), 30000);
        advance();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Following Impress · slide 3"), 30000);
        QVERIFY2(positionLabel(window)->text().startsWith("Word 8 of"), qPrintable(positionLabel(window)->text()));
        impress->stop();
        QProcess::execute(xdotool, {"key", "Escape"});
        QProcess::execute("pkill", {"-f", "--", "--accept=pipe,name=kalipso-"}); // The LibreOffice started here.
    }

    void formattingWrapsTheSelectionInMarkdown() {
        MainWindow window(scripts_.path());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window)); // Shortcuts need the active window.
        auto *text = editor(window);
        text->setPlainText("Say hello world now");
        text->setFocus();
        auto cursor = text->textCursor();
        cursor.setPosition(3);
        cursor.setPosition(10, QTextCursor::KeepAnchor); // " hello " with spaces at both edges.
        text->setTextCursor(cursor);
        QTest::keySequence(text, QKeySequence::Bold);
        QCOMPARE(text->toPlainText(), QString("Say **hello** world now"));
        QCOMPARE(text->textCursor().selectedText(), QString("hello")); // Still selected for more formatting.
        window.findChild<QAction *>("formatItalic")->trigger();
        QCOMPARE(text->toPlainText(), QString("Say ***hello*** world now"));
        // An empty selection gets an empty pair with the caret inside.
        cursor.movePosition(QTextCursor::End);
        text->setTextCursor(cursor);
        window.findChild<QAction *>("formatCue")->trigger();
        QTest::keyClicks(text, "smile");
        QCOMPARE(text->toPlainText(), QString("Say ***hello*** world now[smile]"));
        // The heading toggles on the current line.
        QTest::keySequence(text, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
        QCOMPARE(text->toPlainText(), QString("# Say ***hello*** world now[smile]"));
        window.findChild<QAction *>("formatHeading")->trigger();
        QCOMPARE(text->toPlainText(), QString("Say ***hello*** world now[smile]"));
        text->document()->undo(); // One undo step per formatting action.
        QCOMPARE(text->toPlainText(), QString("# Say ***hello*** world now[smile]"));
    }

    void highlighterShowsMarkdownMetadataListsAndEmphasis() {
        QTextDocument document;
        document.documentLayout(); // As in an editor: without a layout, no change signals reach the highlighter.
        ScriptHighlighter highlighter(&document);
        const QColor cue(0x11, 0x22, 0x33), heading(0x44, 0x55, 0x66);
        highlighter.setColors(cue, heading);
        document.setPlainText("---\ntitle: Launch\n---\n- item\n***both*** **bold** *it*\n# Head");
        QTest::qWait(20); // The front matter rehighlight is queued.
        const auto formatAt = [&document](int blockNumber, int position) {
            const auto block = document.findBlockByNumber(blockNumber);
            for (const auto &range : block.layout()->formats())
                if (position >= range.start && position < range.start + range.length)
                    return range.format;
            return QTextCharFormat();
        };
        QCOMPARE(formatAt(1, 0).foreground().color(), cue);                                 // Front matter is dimmed.
        QCOMPARE(formatAt(3, 0).foreground().color(), cue);                                 // "-" list marker.
        QVERIFY(!formatAt(3, 2).hasProperty(QTextFormat::ForegroundBrush));                 // The item text is not.
        QVERIFY(formatAt(4, 3).fontWeight() == QFont::Bold && formatAt(4, 3).fontItalic()); // ***both***
        QCOMPARE(formatAt(4, 9).foreground().color(), cue);                                 // Closing *** markers.
        QVERIFY(formatAt(4, 13).fontWeight() == QFont::Bold && !formatAt(4, 13).fontItalic());
        QVERIFY(formatAt(4, 21).fontItalic());
        QCOMPARE(formatAt(5, 2).foreground().color(), heading);
        // Without the closing "---", the first line is plain text again.
        document.setPlainText("---\ntitle: Launch\n- item");
        QTest::qWait(20);
        QVERIFY(!formatAt(1, 0).hasProperty(QTextFormat::ForegroundBrush));
    }

    void readerKeysStepTheScript() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("one two three four");
        window.showPrompter();
        auto *view = window.prompter()->text;
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_PageDown);
        QVERIFY(positionLabel(window)->text().startsWith("Word 2 of 4"));
        QTest::keyClick(view, Qt::Key_Left);
        QVERIFY(positionLabel(window)->text().startsWith("Word 1 of 4"));
        QTest::keyClick(view, Qt::Key_Home);
        QVERIFY(positionLabel(window)->text().startsWith("Word 0 of 4"));
    }

    void readerControlsKeepKeyboardFocusOnTheScript() {
        // Clicking a control must not let a pedal's Space or arrows activate it or type.
        ReaderWindow reader;
        for (auto *button : reader.findChildren<QAbstractButton *>())
            QCOMPARE(button->focusPolicy(), Qt::TabFocus);
    }

    void sectionsJumpAndCuesStayOutOfTheWordCount() {
        MainWindow window(scripts_.path());
        editor(window)->setPlainText("# Opening\nHello everyone [smile]\n\n# Results\nRevenue grew");
        QVERIFY(positionLabel(window)->text().contains("of 4"));
        auto *sections = window.prompter()->findChild<QToolButton *>("readerSections");
        QVERIFY(!sections->isHidden());
        const auto actions = sections->menu()->actions();
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions[1]->text(), QString("Results"));
        actions[1]->trigger();
        // The last word before the heading is read; "Revenue" is next.
        QVERIFY(positionLabel(window)->text().startsWith("Word 2 of 4"));
    }

    void mirroringFlipsTheReaderText() {
        ReaderWindow reader;
        reader.setReaderTheme("light");
        reader.text->setPlainText("Mirror");
        reader.resize(600, 300);
        reader.show();
        QTest::qWait(30);
        const auto normal = reader.text->viewport()->grab().toImage();
        reader.setMirror(Mirror::Horizontal);
        const auto mirrored = reader.text->viewport()->grab().toImage();
        QVERIFY(inkCentre(normal) < 0.4);
        QVERIFY(inkCentre(mirrored) > 0.6);
        reader.setMirror(Mirror::None);
        QVERIFY(inkCentre(reader.text->viewport()->grab().toImage()) < 0.4);
    }

    void navigationAndReaderTextScaling() {
        MainWindow window(scripts_.path());
        window.show();
        // Settings is its own window; the main window is the script.
        QVERIFY(!window.findChild<QTabWidget *>());
        auto *settings = openSettings(window);
        QVERIFY(settings && settings->isVisible());
        QCOMPARE(window.findChild<QPushButton *>("primaryAction")->text(), QString("Open reader"));
        auto *button = window.prompter()->findChild<QPushButton *>("readerListeningButton");
        const double original = button->font().pointSizeF();
        const double scriptSize = window.prompter()->text->font().pointSizeF();
        window.findChild<QComboBox *>("interfaceTextSize")->setCurrentIndex(2);
        QCOMPARE(button->font().pointSizeF(), std::max(8.0, original) * 2);
        QCOMPARE(window.prompter()->text->font().pointSizeF(), scriptSize);
    }

    void toolbarWrapsAtNarrowWidths() {
        QWidget host;
        auto *flow = new FlowLayout(&host);
        for (int i = 0; i < 4; ++i)
            flow->addWidget(new AccessibleButton("Readable control"));
        host.resize(320, 300);
        host.show();
        QTest::qWait(20);
        QVERIFY(flow->heightForWidth(320) > flow->heightForWidth(1000));
        for (int i = 0; i < flow->count(); ++i) {
            const auto rect = flow->itemAt(i)->geometry();
            QVERIFY(rect.left() >= 0);
            QVERIFY(rect.right() < 320);
            QVERIFY(rect.height() >= 24);
        }
    }

    void interfaceTextCanGrowWithoutHorizontalScrolling() {
        MainWindow window(scripts_.path());
        window.resize(640, 760);
        window.show();
        auto *settings = openSettings(window);
        auto *scale = window.findChild<QComboBox *>("interfaceTextSize");
        scale->setCurrentIndex(2);
        QTest::qWait(80);
        QVERIFY(scale->font().pointSizeF() >= 16);
        auto *scroll = qobject_cast<QScrollArea *>(window.centralWidget());
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        for (const int width : {640, 320}) {
            window.resize(width, 760);
            settings->resize(width, 700);
            QTest::qWait(100);
            for (auto *area : window.findChildren<QScrollArea *>())
                if (area->isVisible())
                    QVERIFY2(area->horizontalScrollBar()->maximum() == 0,
                             qPrintable(QString("width %1: ").arg(width) + overflowReport(area)));
            // At 320 px every text in the settings can wrap: a single-line label as wide as the
            // window overflows with wider fonts (Linux CI) even when it fits here.
            if (width == 320)
                for (auto *label : settings->findChildren<QLabel *>())
                    if (label->isVisible() && !label->text().isEmpty())
                        QVERIFY2(label->wordWrap(), qPrintable(label->text()));
            // Every visible control fits inside its window.
            for (QWidget *top : {static_cast<QWidget *>(&window), static_cast<QWidget *>(settings)})
                for (auto *control : top->findChildren<QAbstractButton *>())
                    if (control->isVisible() && control->window() == top) {
                        const auto right = control->mapTo(top, QPoint(control->width(), 0)).x();
                        QVERIFY2(right <= top->width(), qPrintable(control->text()));
                    }
        }
        for (auto *form : settings->findChildren<QFormLayout *>()) {
            if (!form->parentWidget()->isVisible())
                continue;
            const auto bounds = form->parentWidget()->contentsRect();
            for (int row = 0; row < form->rowCount(); ++row)
                if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
                    if (auto *label = qobject_cast<QLabel *>(item->widget()); label && label->isVisible()) {
                        QVERIFY(label->geometry().right() <= bounds.right());
                        QVERIFY(label->height() >= label->heightForWidth(label->width()));
                    }
        }
        auto *settingsScroll = settings->findChild<QScrollArea *>();
        QVERIFY(settingsScroll);
        const int minimum = settingsScroll->widget()->minimumWidth();
        settingsScroll->widget()->setMinimumWidth(900);
        QTest::qWait(30);
        QVERIFY(settingsScroll->horizontalScrollBar()->maximum() > 0); // Detects the nested-overflow regression.
        settingsScroll->widget()->setMinimumWidth(minimum);
        QTRY_COMPARE(settingsScroll->horizontalScrollBar()->maximum(), 0); // And back, before any screenshot.
        if (const auto directory = qEnvironmentVariable("KALIPSO_A11Y_SHOTS"); !directory.isEmpty()) {
            QDir().mkpath(directory);
            window.grab().save(directory + "/qt-editor-320-200.png");
            settings->grab().save(directory + "/qt-settings-320-200.png");
            scale->setCurrentIndex(0);
            window.resize(1100, 760);
            settings->resize(620, 700);
            window.setDemoScript();
            QTest::qWait(80);
            window.grab().save(directory + "/qt-editor.png");
            settings->grab().save(directory + "/qt-settings.png");
            settings->findChild<QScrollArea *>()->ensureWidgetVisible(window.findChild<QWidget *>("key-reset"));
            QTest::qWait(30);
            settings->grab().save(directory + "/qt-settings-keys.png");
            window.showPrompter();
            for (const auto *theme : {"light", "dark"}) {
                window.prompter()->setReaderTheme(theme);
                QTest::qWait(80);
                window.prompter()->grab().save(directory + QString("/qt-reader-%1.png").arg(theme));
            }
            window.prompter()->setMode(ReadingMode::Steady);
            window.prompter()->resize(320, 300);
            QTest::qWait(80);
            window.prompter()->grab().save(directory + "/qt-reader-320.png");
            window.findChild<QComboBox *>("applicationTheme")->setCurrentIndex(1);
            window.resize(1100, 760);
            QTest::qWait(80);
            window.grab().save(directory + "/qt-editor-dark.png");
            settings->grab().save(directory + "/qt-settings-dark.png");
        }
    }

    void editorWrapsLongLinesInsideTheColumn() {
        MainWindow window(scripts_.path());
        window.resize(1100, 760);
        window.show();
        editor(window)->setPlainText(QString("A long line of words ").repeated(40));
        for (const int width : {1100, 700, 1400}) {
            window.resize(width, 760);
            QTest::qWait(50);
            QCOMPARE(editor(window)->horizontalScrollBar()->maximum(), 0);
        }
    }

    void readerEditsPreserveReadingPositionAndUndo() {
        MainWindow window(scripts_.path());
        auto *script = editor(window);
        script->setPlainText("uno due tre quattro");
        setCursor(script, 8);
        window.findChild<QPushButton *>("readHere")->click();
        QVERIFY(positionLabel(window)->text().startsWith("Word 3 of 4"));
        auto *reader = window.prompter()->text;
        QTextCursor insert(reader->document());
        insert.setPosition(0);
        insert.insertText("prima ");
        QCOMPARE(script->toPlainText(), QString("prima uno due tre quattro"));
        QVERIFY(positionLabel(window)->text().startsWith("Word 4 of 5"));
        script->undo();
        QCOMPARE(script->toPlainText(), QString("uno due tre quattro"));
        QCOMPARE(reader->toPlainText(), script->toPlainText());
        QVERIFY(positionLabel(window)->text().startsWith("Word 3 of 4"));
    }

    void editingDoesNotMoveReadingAnchorToEditorCaret() {
        MainWindow window(scripts_.path());
        auto *script = editor(window);
        script->setPlainText("uno due tre quattro");
        setCursor(script, 8);
        window.findChild<QPushButton *>("readHere")->click();
        setCursor(script, 0);
        QVERIFY(positionLabel(window)->text().startsWith("Word 3 of 4"));
        script->insertPlainText("prima ");
        QVERIFY(positionLabel(window)->text().startsWith("Word 4 of 5"));
    }

    void demoScriptNeverTouchesTheLibrary() {
        MainWindow window(scripts_.path());
        const auto id = window.currentScript();
        editor(window)->setPlainText("My real script");
        window.setDemoScript();
        QTest::qWait(1000); // Longer than the autosave delay.
        QCOMPARE(ScriptLibrary(scripts_.path()).load(id), QString("My real script"));
    }

    void realScreenSamplingExcludesReaderAndStopsWhenDisabled() {
        if (QGuiApplication::platformName() != "xcb")
            QSKIP("Real X11 framebuffer required; offscreen tests cannot verify pixels behind windows.");
        QWidget background;
        background.setGeometry(100, 100, 400, 400);
        background.setStyleSheet("background:#000000;");
        background.show();
        QWidget reader(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        reader.setGeometry(220, 220, 100, 100);
        reader.setStyleSheet("background:#ffffff;");
        reader.show();
        reader.raise();
        QTest::qWait(100);
        Backdrop sampler(&reader);
        QSignalSpy colors(&sampler, &Backdrop::colorChanged);
        sampler.setEnabled(true);
        QTRY_VERIFY(!colors.isEmpty());
        QCOMPARE(colors.last()[0].value<QColor>(), QColor("#000000"));
        background.setStyleSheet("background:#ffffff;");
        const auto before = colors.size();
        QTRY_VERIFY_WITH_TIMEOUT(colors.size() > before, 4000);
        QCOMPARE(colors.last()[0].value<QColor>(), QColor("#ffffff"));
        sampler.setEnabled(false);
        const auto stopped = colors.size();
        QTest::qWait(2200);
        QCOMPARE(colors.size(), stopped);
    }

    void realWaylandWindowSharingFollowsColorsAndStopsOnHide() {
        if (!qEnvironmentVariableIsSet("KALIPSO_WAYLAND_CAPTURE_TEST") ||
            !QGuiApplication::platformName().startsWith("wayland"))
            QSKIP("Requires a real Wayland compositor, portal and explicit selection of the test source window.");
        QWidget source;
        source.setWindowTitle("Kalipso color test source");
        source.resize(800, 600);
        source.setStyleSheet("background:#000000;");
        source.show();
        MainWindow window(scripts_.path());
        window.setDemoScript();
        window.show();
        auto *sampler = window.prompter()->findChild<Backdrop *>();
        QVERIFY(sampler);
        auto *choose = window.findChild<QPushButton *>("chooseBackgroundWindow");
        QVERIFY(choose);
        QSignalSpy samples(sampler, &Backdrop::colorChanged);
        QTest::qWait(500);
        choose->click();
        QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty(), 120000);
        QCOMPARE(samples.last()[0].value<QColor>(), QColor(Qt::black));
        QCOMPARE(window.prompter()->palette().color(QPalette::Window), QColor(Design::light.background));
        source.setStyleSheet("background:#ffffff;");
        QTRY_COMPARE_WITH_TIMEOUT(samples.last()[0].value<QColor>(), QColor(Qt::white), 12000);
        QTRY_COMPARE_WITH_TIMEOUT(window.prompter()->palette().color(QPalette::Window), QColor(Design::dark.background),
                                  12000);
        window.prompter()->hide();
        const auto count = samples.size();
        source.setStyleSheet("background:#000000;");
        QTest::qWait(3000);
        QCOMPARE(samples.size(), count);
        window.showPrompter();
        QTest::qWait(2500);
        QCOMPARE(samples.size(), count);
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
