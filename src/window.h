// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "tracker.h"
#include "approval.h"
#include "llm.h"
#include "speech.h"
#include "accessibility.h"
#include "backdrop.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>

class Prompter : public QWidget {
    Q_OBJECT
public:
    explicit Prompter(QWidget *parent=nullptr);
    QPlainTextEdit *text;
    void showAtCamera(QScreen *screen);
    void highlight(int position,int length);
    void setTextSize(int points);
    void setReaderTheme(const QString &theme);
    QString colorSource() const{return backdrop_->source();}
signals:
    void colorStatus(const QString &text);
public:
signals:
    void step(int direction);
private:
    bool editing_ = false;
    Design::Palette colors_=Design::dark;
    Backdrop *backdrop_;
    QString appearance_="dark",resolvedTheme_="dark";
    QElapsedTimer paletteAge_;
    int position_=0,length_=0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow();
    ~MainWindow() override;
    void showPrompter();
    void setDemoScript();
    Prompter *prompter() const { return prompter_; }
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refreshReading();
    void refreshApproval();
    void toggleListening();
    void loadScript();
    bool saveScript();
    bool discardChanges();
    void saveSettings();
    QWidget *makeSettings();
    void loadSpeechProfile();
    void saveSpeechProfile();
    QString speechLanguage() const;
    ScriptTracker tracker_;
    ApprovalFlow flow_;
    LlmClient llm_;
    SpeechService speech_;
    SystemSpeechClient systemCheck_;
    Prompter *prompter_;
    QPlainTextEdit *script_, *transcript_, *question_, *answer_;
    QLineEdit *library_, *modelPath_, *microphone_, *meetingTarget_, *endpoint_, *model_, *apiKey_;
    QCheckBox *meeting_;
    QComboBox *screen_;
    QComboBox *backend_, *language_, *architecture_;
    QLabel *systemStatus_;
    QPushButton *checkSystem_, *installSystem_;
    QString profile_;
    QPushButton *listen_, *generate_, *insert_;
    StatusLabel *audioStatus_, *reviewStatus_;
    QLabel *position_, *liveTranscript_;
    QString scriptPath_;
    quint64 request_ = 0;
    bool installingSystem_ = false;
    bool waitForNewUtterance_ = false;
};
