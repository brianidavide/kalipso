// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "library.h"
#include "reader.h"
#include "session_stats.h"
#include "speech.h"
#include "tracker.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <functional>

class QAbstractButton;
class QCheckBox;
class QDialog;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class RemoteControl;
class SpeechDownload;
struct InstalledSpeech;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // An empty libraryDirectory uses the application data folder.
    explicit MainWindow(const QString &libraryDirectory = {});
    // Writes the last voice session's report (More → Save session report…).
    bool saveSessionReport(const QString &path);
    // Imports a text, document or presentation file as a new script and opens it.
    bool importFile(const QString &path);
    ~MainWindow() override;

    void showPrompter();
    void setDemoScript();
    // Starts steady scrolling immediately, without changing the saved mode.
    void startSteadyScrolling();
    ReaderWindow *prompter() const { return prompter_; }
    QString currentScript() const { return currentId_; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Script library
    void buildLibraryBar(QWidget *bar);
    void refreshLibrary();
    void openScript(const QString &id);
    bool saveScript(); // False when the current script could not be written.
    void newScript();
    void importScript();
    void followSlide(int number, const QString &notes);
    void exportScript();
    void wrapSelection(const QString &before, const QString &after);
    void toggleHeading();
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
    QString statusText() const;
    void steadyTick();
    void changeSpeed(int delta);
    void jumpSection(int direction);
    bool voiceCommand(const QString &utterance);
    void startListening();
    // Opens Settings; with a field, scrolled to it and focused.
    void showSettings(QWidget *field = nullptr);
    void finishSession();

    // Settings
    QWidget *makeSettings();
    QWidget *makeReadingSettings();
    QWidget *makeSpeechSettings();
    QWidget *makeRemoteSettings();
    QWidget *makeKeySettings();
    void applyTheme();
    void refreshScreens();
    void refreshVoiceLanguages();
    // Listens with this model from now on; announce says so under the language list.
    void useInstalledSpeech(const InstalledSpeech &speech, bool announce = true);
    void downloadLanguage(const QString &code);
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
    SessionStats::Summary lastSession_; // The last voice session, for "Save session report…".
    QString lastSessionScript_;
    QDateTime lastSessionAt_;
    QAction *reportAction_ = nullptr;
    ReaderWindow *prompter_;
    RemoteControl *remote_ = nullptr;
    class ImpressFollower *impress_ = nullptr;
    QAction *impressAction_ = nullptr;
    QList<QAction *> formatActions_;
    bool voiceCommands_ = false;
    ReadingMode mode_ = ReadingMode::Voice;
    QTimer steady_, countdown_;
    int countdownLeft_ = 0;
    int wordsPerMinute_ = 140;
    QString speechStatus_;
    QString speechError_; // Why listening did not start or stopped; shown in the reader until the next try.
    bool waitForNewUtterance_ = false;

    QComboBox *scripts_;
    QPlainTextEdit *script_;
    QDialog *settings_ = nullptr;
    class ScriptHighlighter *editorHighlighter_ = nullptr;
    QList<QPair<QAbstractButton *, QString>> icons_; // Buttons whose icon follows the theme.
    StatusLabel *audioStatus_;
    QLabel *position_;

    QComboBox *screen_, *voiceLanguage_, *backend_, *language_, *architecture_;
    QWidget *customSpeech_;
    SpeechDownload *download_;
    QProgressBar *downloadProgress_;
    StatusLabel *downloadStatus_;
    QPushButton *downloadAction_;
    QString downloadCode_;
    QElapsedTimer downloadShown_;
    std::function<void()> updateSpeechFields_;
    QLineEdit *speechLibrary_, *modelPath_, *microphone_;
    QLabel *systemStatus_;
    QPushButton *checkSystem_, *installSystem_;
    QString profile_;
    bool installingSystem_ = false;
};
