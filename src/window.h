// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "tracker.h"
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
    void setListeningState(bool active, const QString &message);
    QString colorSource() const{return backdrop_->source();}
    bool usesWindowPortal() const{return backdrop_->usesWindowPortal();}
    void chooseBackgroundWindow(){backdrop_->chooseWindow();}
    void stopWindowSharing(){backdrop_->stopWindowSharing();}
signals:
    void colorStatus(const QString &text);
    void step(int direction);
    void listeningRequested();
    void hidden();
protected:
    void hideEvent(QHideEvent *event) override;
private:
    StatusLabel *listeningStatus_;
    QPushButton *listeningButton_;
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
    SpeechService speech_;
    SystemSpeechClient systemCheck_;
    Prompter *prompter_;
    QPlainTextEdit *script_;
    QLineEdit *library_, *modelPath_, *microphone_;
    QComboBox *screen_;
    QComboBox *backend_, *language_, *architecture_;
    QLabel *systemStatus_;
    QPushButton *checkSystem_, *installSystem_;
    QString profile_;
    QPushButton *listen_;
    StatusLabel *audioStatus_;
    QLabel *position_;
    QString scriptPath_;
    bool installingSystem_ = false;
    bool waitForNewUtterance_ = false;
};
