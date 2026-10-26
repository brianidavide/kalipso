// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "tracker.h"
#include "approval.h"
#include "llm.h"
#include "speech.h"
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
signals:
    void step(int direction);
private:
    bool editing_ = false;
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
    ScriptTracker tracker_;
    ApprovalFlow flow_;
    LlmClient llm_;
    SpeechService speech_;
    Prompter *prompter_;
    QPlainTextEdit *script_, *transcript_, *question_, *answer_;
    QLineEdit *library_, *modelPath_, *microphone_, *meetingTarget_, *endpoint_, *model_, *apiKey_;
    QCheckBox *meeting_;
    QComboBox *screen_;
    QPushButton *listen_, *generate_, *insert_;
    QLabel *audioStatus_, *reviewStatus_, *position_, *liveTranscript_;
    QString scriptPath_;
    quint64 request_ = 0;
    bool waitForNewUtterance_ = false;
};
