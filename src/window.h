// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "library.h"
#include "reader.h"
#include "session_stats.h"
#include "speech.h"
#include "tracker.h"
#include <QMainWindow>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class RemoteControl;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // An empty libraryDirectory uses the application data folder.
    explicit MainWindow(const QString &libraryDirectory = {});
    ~MainWindow() override;

    void showPrompter();
    void setDemoScript();
    ReaderWindow *prompter() const { return prompter_; }
    QString currentScript() const { return currentId_; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Script library
    void buildLibraryBar(QWidget *bar);
    void refreshLibrary();
    void openScript(const QString &id);
    void saveScript();
    void newScript();
    void importScript();
    void exportScript();
    void deleteScript();

    // Reading
    void refreshReading();
    void seekWord(int word);
    void setMode(ReadingMode mode);
    void toggleRunning();
    void startRunning();
    void pauseRunning();
    bool running() const;
    void refreshRunning();
    void steadyTick();
    void changeSpeed(int delta);
    void startListening();
    void finishSession();

    // Settings
    QWidget *makeSettings();
    QWidget *makeReadingSettings();
    QWidget *makeSpeechSettings();
    QWidget *makeRemoteSettings();
    void refreshScreens();
    void refreshInstalledSpeech();
    void loadSpeechProfile();
    void saveSpeechProfile();
    void saveSettings();
    void applyInterfaceScale(double scale);
    QString speechLanguage() const;

    ScriptLibrary library_;
    QString currentId_;
    QString scriptText_;
    bool persist_ = true;
    bool loading_ = false;
    QTimer autosave_;

    ScriptTracker tracker_;
    SpeechService speech_;
    SystemSpeechClient systemCheck_;
    SessionStats session_;
    ReaderWindow *prompter_;
    RemoteControl *remote_ = nullptr;
    ReadingMode mode_ = ReadingMode::Voice;
    QTimer steady_, countdown_;
    int countdownLeft_ = 0;
    int wordsPerMinute_ = 140;
    QString speechStatus_;
    bool waitForNewUtterance_ = false;

    QComboBox *scripts_;
    QPlainTextEdit *script_;
    QPushButton *run_;
    StatusLabel *audioStatus_;
    QLabel *position_;

    QComboBox *screen_, *installed_, *backend_, *language_, *architecture_;
    QWidget *customSpeech_;
    QLineEdit *speechLibrary_, *modelPath_, *microphone_;
    QLabel *systemStatus_;
    QPushButton *checkSystem_, *installSystem_;
    QString profile_;
    bool installingSystem_ = false;
};
