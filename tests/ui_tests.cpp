// SPDX-License-Identifier: AGPL-3.0-or-later
#include "window.h"
#include <QStandardPaths>
#include <QtTest>

class UiTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("kalipso-tests");
        QCoreApplication::setApplicationName("isolated-ui-tests");
    }
    void overlayEditsPreserveReadingPositionAndUndo() {
        MainWindow window;
        auto *script=window.findChild<QPlainTextEdit*>("scriptEditor");
        auto *read=window.findChild<QPushButton*>("readHere");
        auto *position=window.findChild<QLabel*>("readingPosition");
        QVERIFY(script && read && position);
        script->setPlainText("uno due tre quattro");
        auto cursor=script->textCursor(); cursor.setPosition(8); script->setTextCursor(cursor); read->click();
        QVERIFY(position->text().startsWith("Word 3 of 4"));
        auto *overlay=window.prompter()->text;
        QTextCursor insert(overlay->document()); insert.setPosition(0); insert.insertText("prima ");
        QCOMPARE(script->toPlainText(),QString("prima uno due tre quattro"));
        QVERIFY(position->text().startsWith("Word 4 of 5"));
        script->undo();
        QCOMPARE(script->toPlainText(),QString("uno due tre quattro"));
        QCOMPARE(overlay->toPlainText(),script->toPlainText());
        QVERIFY(position->text().startsWith("Word 3 of 4"));
        script->document()->setModified(false);
    }
    void editingDoesNotMoveReadingAnchorToEditorCaret() {
        MainWindow window;
        auto *script=window.findChild<QPlainTextEdit*>("scriptEditor");
        script->setPlainText("uno due tre quattro");
        auto cursor=script->textCursor(); cursor.setPosition(8); script->setTextCursor(cursor);
        window.findChild<QPushButton*>("readHere")->click();
        cursor.setPosition(0); script->setTextCursor(cursor);
        QVERIFY(window.findChild<QLabel*>("readingPosition")->text().startsWith("Word 3 of 4"));
        script->insertPlainText("prima ");
        QVERIFY(window.findChild<QLabel*>("readingPosition")->text().startsWith("Word 4 of 5"));
        script->document()->setModified(false);
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
