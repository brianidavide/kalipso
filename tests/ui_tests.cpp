// SPDX-License-Identifier: AGPL-3.0-or-later
#include "window.h"
#include <QStandardPaths>
#include <QtTest>
#include <QSettings>
#include <QAccessible>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QDir>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QFormLayout>

class UiTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("kalipso-tests");
        QCoreApplication::setApplicationName("isolated-ui-tests");
    }
    void engineProfilesKeepTheirOwnModelsAndLanguages() {
        QSettings{}.clear();
        {
            MainWindow window;
            auto *engine=window.findChild<QComboBox*>("speechBackend");
            auto *language=window.findChild<QComboBox*>("speechLanguage");
            auto *model=window.findChild<QLineEdit*>("speechModel");
            QVERIFY(engine && language && model);
            engine->setCurrentIndex(engine->findData("moonshine"));
            model->setText("moonshine-model");language->setEditText("de-DE");
            engine->setCurrentIndex(engine->findData("vosk"));
            model->setText("vosk-model");language->setCurrentIndex(language->findData("it-IT"));
            engine->setCurrentIndex(engine->findData("moonshine"));
            QCOMPARE(model->text(),QString("moonshine-model"));
            QCOMPARE(language->currentData().toString(),QString("de-DE"));
            engine->setCurrentIndex(engine->findData("vosk"));
            QCOMPARE(model->text(),QString("vosk-model"));
            QCOMPARE(language->currentData().toString(),QString("it-IT"));
        }
        {
            MainWindow reopened;
            QCOMPARE(reopened.findChild<QComboBox*>("speechBackend")->currentData().toString(),QString("vosk"));
            QCOMPARE(reopened.findChild<QLineEdit*>("speechModel")->text(),QString("vosk-model"));
        }
        QSettings{}.clear();
    }
    void accessiblePalettesKeepReadTextAndSelectionLegible(){
        for(const auto &p:{Design::light,Design::dark}){
            const auto colors=Accessibility::palette(p);
            for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}){
                QVERIFY(Accessibility::contrast(colors.color(group,QPalette::Text),colors.color(group,QPalette::Base))>=4.5);
                QVERIFY(Accessibility::contrast(colors.color(group,QPalette::PlaceholderText),colors.color(group,QPalette::Base))>=4.5);
                QVERIFY(Accessibility::contrast(colors.color(group,QPalette::HighlightedText),colors.color(group,QPalette::Highlight))>=4.5);
            }
            Prompter reader;reader.setReaderTheme(QString(p.background)==Design::light.background?"light":"dark");reader.text->setPlainText("Read current next");reader.highlight(5,7);
            reader.show();QTest::qWait(30);QCOMPARE(reader.grab().toImage().pixelColor(3,3),QColor(p.background));
            const auto selections=reader.text->extraSelections();QCOMPARE(selections.size(),2);
            QVERIFY(Accessibility::contrast(selections[0].format.foreground().color(),QColor(p.surface))>=4.5);
            QVERIFY(Accessibility::contrast(selections[1].format.foreground().color(),selections[1].format.background().color())>=4.5);
        }
        QVERIFY(Accessibility::contrast(QColor("#777777"),Qt::white)<4.5);
    }
    void statusHasAnAccessibleRoleAndName(){
        StatusLabel status("Paused");auto *interface=QAccessible::queryAccessibleInterface(&status);QVERIFY(interface);
        QCOMPARE(interface->role(),QAccessible::StatusBar);status.setText("Listening");QCOMPARE(interface->text(QAccessible::Name),QString("Listening"));
    }
    void settingsFieldsHavePersistentAccessibleNames(){
        MainWindow window;
        for(const auto *name:{"speechBackend","speechLanguage","speechLibrary","speechModel","applicationTheme","readerTheme"}){
            auto *field=window.findChild<QWidget*>(name);QVERIFY(field);auto *interface=QAccessible::queryAccessibleInterface(field);QVERIFY(interface);QVERIFY2(!interface->text(QAccessible::Name).isEmpty(),name);
        }
    }
    void settingsScrollDoesNotChangeSelections(){
        QSettings{}.clear(); MainWindow window;window.show();
        auto *tabs=window.findChild<QTabWidget*>();tabs->setCurrentIndex(tabs->count()-1);
        auto wheel=[](QComboBox *combo){
            QWheelEvent event(QPointF(10,10),combo->mapToGlobal(QPoint(10,10)),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(combo,&event);
        };
        for(const auto *name:{"speechBackend","speechLanguage","applicationTheme","readerTheme","interfaceTextSize"}){
            auto *combo=window.findChild<QComboBox*>(name);QVERIFY(combo);combo->setFocus();
            const int before=combo->currentIndex();wheel(combo);QCOMPARE(combo->currentIndex(),before);
        }
        QComboBox unsafe;unsafe.addItems({"First","Second"});unsafe.show();unsafe.setFocus();wheel(&unsafe);
        QCOMPARE(unsafe.currentIndex(),1); // Counterexample: ordinary combos do change during scrolling.
    }
    void readerHidingStopsSpeechAndDoesNotResumeIt(){
        QSettings{}.clear(); MainWindow window;window.setDemoScript();window.showPrompter();
        auto *speech=window.findChild<SpeechService*>();QVERIFY(speech);
        QSignalSpy stopped(speech,&SpeechService::stopped);
        auto *reader=window.prompter();auto *button=reader->findChild<QPushButton*>("readerListeningButton");
        auto *status=reader->findChild<QLabel*>("readerListeningStatus");QVERIFY(button&&status);
        reader->setListeningState(true,"Listening");QCOMPARE(button->text(),QString("Pause listening"));
        reader->hide();QVERIFY(!speech->active());
        QCOMPARE(button->text(),QString("Start listening"));QCOMPARE(status->text(),QString("Microphone off"));
        window.showPrompter();QVERIFY(!speech->active());QCOMPARE(stopped.count(),0);
        speech->status("Microphone unavailable");QCOMPARE(status->text(),QString("Microphone unavailable"));
    }
    void firstReleaseNavigationAndReaderTextScaling(){
        QSettings{}.clear();MainWindow window;window.show();
        auto *tabs=window.findChild<QTabWidget*>();QCOMPARE(tabs->count(),2);
        QCOMPARE(tabs->tabText(0),QString("Script"));QCOMPARE(tabs->tabText(1),QString("Settings"));
        auto *button=window.prompter()->findChild<QPushButton*>("readerListeningButton");
        const double original=button->font().pointSizeF();
        const double scriptSize=window.prompter()->text->font().pointSizeF();
        window.findChild<QComboBox*>("interfaceTextSize")->setCurrentIndex(2);
        QCOMPARE(button->font().pointSizeF(),std::max(8.0,original)*2);
        QCOMPARE(window.prompter()->text->font().pointSizeF(),scriptSize);
        QSettings{}.clear();
    }
    void toolbarWrapsAtNarrowWidths(){
        QWidget host;auto *flow=new FlowLayout(&host);
        for(int i=0;i<4;++i)flow->addWidget(new AccessibleButton("Readable control"));
        host.resize(320,300);host.show();QTest::qWait(20);
        QVERIFY(flow->heightForWidth(320)>flow->heightForWidth(1000));
        for(int i=0;i<flow->count();++i){const auto rect=flow->itemAt(i)->geometry();QVERIFY(rect.left()>=0);QVERIFY(rect.right()<320);QVERIFY(rect.height()>=24);}
    }
    void interfaceTextCanGrowWithoutHorizontalScrolling(){
        QSettings{}.clear();MainWindow window;window.resize(640,760);window.show();
        auto *scale=window.findChild<QComboBox*>("interfaceTextSize");QVERIFY(scale);scale->setCurrentIndex(2);
        QTest::qWait(80);QCOMPARE(scale->currentData().toDouble(),2.0);
        QVERIFY(scale->font().pointSizeF()>=16);
        auto *scroll=qobject_cast<QScrollArea*>(window.centralWidget());QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
        auto *tabs=window.findChild<QTabWidget*>();QVERIFY(tabs);tabs->setCurrentIndex(tabs->count()-1);QTest::qWait(80);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
        window.resize(320,760);QTest::qWait(100);
        for(auto *area:window.findChildren<QScrollArea*>())if(area->isVisible())QCOMPARE(area->horizontalScrollBar()->maximum(),0);
        for(auto *form:tabs->currentWidget()->findChildren<QFormLayout*>())if(form->parentWidget()->isVisible()){
            const auto bounds=form->parentWidget()->contentsRect();
            for(int row=0;row<form->rowCount();++row)if(auto *item=form->itemAt(row,QFormLayout::LabelRole))
                if(auto *label=qobject_cast<QLabel*>(item->widget());label&&label->isVisible()){
                    QVERIFY(label->geometry().right()<=bounds.right());
                    QVERIFY(label->height()>=label->heightForWidth(label->width()));
                }
        }
        auto *settingsScroll=qobject_cast<QScrollArea*>(tabs->currentWidget());QVERIFY(settingsScroll);
        const int previousMinimum=settingsScroll->widget()->minimumWidth();
        settingsScroll->widget()->setMinimumWidth(900);QTest::qWait(30);
        QVERIFY(settingsScroll->horizontalScrollBar()->maximum()>0); // Detect the nested-overflow regression.
        settingsScroll->widget()->setMinimumWidth(previousMinimum);window.resize(640,760);QTest::qWait(80);
        if(const auto directory=qEnvironmentVariable("KALIPSO_A11Y_SHOTS");!directory.isEmpty()){
            QDir{}.mkpath(directory);window.grab().save(directory+"/qt-settings-200.png");
            if(auto *choose=window.findChild<QPushButton*>("chooseBackgroundWindow")){
                for(auto *parent=choose->parentWidget();parent;parent=parent->parentWidget())if(auto *area=qobject_cast<QScrollArea*>(parent)){area->ensureWidgetVisible(choose);QTest::qWait(20);}
                choose->setFocus();QTest::qWait(80);window.grab().save(directory+"/qt-window-colors-200.png");
                QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            }
            scale->setCurrentIndex(0);window.resize(1100,760);QTest::qWait(80);window.grab().save(directory+"/qt-settings.png");
            window.setDemoScript();window.showPrompter();window.prompter()->setReaderTheme("light");QTest::qWait(80);window.prompter()->grab().save(directory+"/qt-reader-light.png");
            window.prompter()->setReaderTheme("dark");QTest::qWait(80);window.prompter()->grab().save(directory+"/qt-reader-dark.png");
        }
    }
    void realScreenSamplingExcludesReaderAndStopsWhenDisabled(){
        if(QGuiApplication::platformName()!="xcb")QSKIP("Real X11 framebuffer required; offscreen tests cannot verify pixels behind windows.");
        QWidget background;background.setGeometry(100,100,400,400);background.setStyleSheet("background:#000000;");background.show();
        QWidget reader(nullptr,Qt::Window|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);reader.setGeometry(220,220,100,100);reader.setStyleSheet("background:#ffffff;");reader.show();reader.raise();QTest::qWait(100);
        Backdrop sampler(&reader);QSignalSpy colors(&sampler,&Backdrop::colorChanged);sampler.setEnabled(true);
        QTRY_VERIFY(!colors.isEmpty());QCOMPARE(colors.last()[0].value<QColor>(),QColor("#000000"));
        background.setStyleSheet("background:#ffffff;");const auto before=colors.size();QTRY_VERIFY_WITH_TIMEOUT(colors.size()>before,4000);QCOMPARE(colors.last()[0].value<QColor>(),QColor("#ffffff"));
        sampler.setEnabled(false);const auto stopped=colors.size();QTest::qWait(2200);QCOMPARE(colors.size(),stopped);
    }
    void realWaylandWindowSharingFollowsColorsAndStopsOnHide(){
        if(!qEnvironmentVariableIsSet("KALIPSO_WAYLAND_CAPTURE_TEST")||!QGuiApplication::platformName().startsWith("wayland"))
            QSKIP("Requires a real Wayland compositor, portal and explicit selection of the test source window.");
        QSettings{}.clear();
        QWidget source;source.setWindowTitle("Kalipso color test source");source.resize(800,600);source.setStyleSheet("background:#000000;");source.show();
        MainWindow window;window.setDemoScript();window.show();
        auto *sampler=window.prompter()->findChild<Backdrop*>();QVERIFY(sampler);
        auto *choose=window.findChild<QPushButton*>("chooseBackgroundWindow");QVERIFY(choose);
        QSignalSpy samples(sampler,&Backdrop::colorChanged);
        QTest::qWait(500);choose->click();
        QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty(),120000);
        QCOMPARE(samples.last()[0].value<QColor>(),QColor(Qt::black));
        QCOMPARE(window.prompter()->palette().color(QPalette::Window),QColor(Design::light.background));
        if(const auto directory=qEnvironmentVariable("KALIPSO_A11Y_SHOTS");!directory.isEmpty())window.prompter()->grab().save(directory+"/wayland-reader-light.png");
        source.setStyleSheet("background:#ffffff;");
        QTRY_COMPARE_WITH_TIMEOUT(samples.last()[0].value<QColor>(),QColor(Qt::white),12000);
        QTRY_COMPARE_WITH_TIMEOUT(window.prompter()->palette().color(QPalette::Window),QColor(Design::dark.background),12000);
        if(const auto directory=qEnvironmentVariable("KALIPSO_A11Y_SHOTS");!directory.isEmpty())window.prompter()->grab().save(directory+"/wayland-reader-dark.png");
        window.prompter()->hide();const auto count=samples.size();source.setStyleSheet("background:#000000;");QTest::qWait(3000);QCOMPARE(samples.size(),count);
        window.showPrompter();QTest::qWait(2500);QCOMPARE(samples.size(),count);
        window.findChild<QPlainTextEdit*>("scriptEditor")->document()->setModified(false);
        QSettings{}.clear();
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
