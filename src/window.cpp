// SPDX-License-Identifier: AGPL-3.0-or-later
#include "window.h"
#include "design_tokens.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <QRegularExpression>

namespace {
QLabel *note(const QString &text) {
    auto *label=new QLabel(text); label->setWordWrap(true); label->setTextFormat(Qt::PlainText); return label;
}
QWidget *field(const QString &label,QWidget *input) {
    auto *box=new QWidget; auto *layout=new QVBoxLayout(box); layout->setContentsMargins(0,0,0,0);
    auto *title=new QLabel(label); title->setBuddy(input); layout->addWidget(title); layout->addWidget(input); return box;
}
void replaceText(QPlainTextEdit *editor, const QString &value) {
    const auto old=editor->toPlainText();
    if(old==value) return;
    int prefix=0, suffix=0;
    while(prefix<std::min(old.size(),value.size()) && old[prefix]==value[prefix]) ++prefix;
    while(suffix<old.size()-prefix && suffix<value.size()-prefix && old[old.size()-1-suffix]==value[value.size()-1-suffix]) ++suffix;
    QTextCursor edit(editor->document()); edit.setPosition(prefix);
    edit.setPosition(int(old.size())-suffix,QTextCursor::KeepAnchor);
    edit.beginEditBlock(); edit.insertText(value.mid(prefix,value.size()-prefix-suffix)); edit.endEditBlock();
}
}
Prompter::Prompter(QWidget *parent) : QWidget(parent,Qt::Window|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint) {
    setWindowTitle("Kalipso — reading"); setObjectName("readingWindow");
    resize(680,250); setMinimumSize(360,180);
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(16,10,16,12);
    auto *bar=new QHBoxLayout;
    auto *title=new QLabel(tr("READ AT YOUR PACE"));
    auto *previous=new QPushButton(tr("Previous")); auto *next=new QPushButton(tr("Next"));
    auto *edit=new QPushButton(tr("Edit")); edit->setCheckable(true);
    auto *hide=new QPushButton(tr("Hide"));
    bar->addWidget(title); bar->addStretch(); bar->addWidget(previous); bar->addWidget(next); bar->addWidget(edit); bar->addWidget(hide);
    layout->addLayout(bar);
    text=new QPlainTextEdit; text->setReadOnly(true); text->setFrameStyle(QFrame::NoFrame);
    text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    text->setAccessibleName(tr("Kalipso script")); text->setCenterOnScroll(true);
    layout->addWidget(text);
    auto *privacy=note(tr("Capture exclusion is not verified. Check what the other participant sees."));
    layout->addWidget(privacy);
    setStyleSheet(QString("QWidget#readingWindow { background:%1; color:%2; } QPlainTextEdit { background:%1; color:%2; selection-background-color:%3; } QLabel { color:%2; } QPushButton { padding:4px 8px; }").arg(Design::overlay,Design::overlayText,Design::action));
    setTextSize(28);
    connect(previous,&QPushButton::clicked,this,[this]{emit step(-1);});
    connect(next,&QPushButton::clicked,this,[this]{emit step(1);});
    connect(hide,&QPushButton::clicked,this,&QWidget::hide);
    connect(edit,&QPushButton::toggled,this,[this](bool enabled) { editing_=enabled; text->setReadOnly(!enabled); if(enabled) text->setFocus(); });
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),this); connect(escape,&QShortcut::activated,this,&QWidget::hide);
    auto *forward=new QShortcut(QKeySequence("Ctrl+Right"),this); connect(forward,&QShortcut::activated,this,[this]{emit step(1);});
    auto *back=new QShortcut(QKeySequence("Ctrl+Left"),this); connect(back,&QShortcut::activated,this,[this]{emit step(-1);});
}
void Prompter::setTextSize(int points) { auto font=text->font(); font.setPointSize(points); text->setFont(font); }
void Prompter::showAtCamera(QScreen *screen) {
    if (!screen) screen=QGuiApplication::primaryScreen();
    if (screen) {
        const auto geometry=screen->availableGeometry();
        resize(std::min(680,geometry.width()-32),250);
        winId(); windowHandle()->setScreen(screen);
        move(geometry.center().x()-width()/2,geometry.top()+16);
    }
    show(); raise();
}
void Prompter::highlight(int position,int length) {
    QTextCursor cursor(text->document()); cursor.setPosition(std::min(position,text->document()->characterCount()-1));
    cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,length);
    QTextEdit::ExtraSelection selection; selection.cursor=cursor;
    selection.format.setBackground(QColor(Design::reading)); selection.format.setForeground(QColor(Design::ink));
    text->setExtraSelections({selection});
    if (!editing_) { text->setTextCursor(cursor); text->centerCursor(); }
}
MainWindow::MainWindow() : flow_(this), llm_(this), speech_(this), prompter_(new Prompter) {
    setWindowTitle(tr("Kalipso")); resize(1100,760); setMinimumSize(700,540);
    setStyleSheet("QPushButton { padding:7px 12px; } QTabBar::tab { padding:8px 16px; } QPlainTextEdit { padding:8px; } QLineEdit { padding:6px; } QGroupBox { margin-top:12px; padding-top:12px; }");
    auto *central=new QWidget; auto *layout=new QVBoxLayout(central); layout->setContentsMargins(24,20,24,20); layout->setSpacing(14);
    auto *heading=new QLabel(tr("A little closer to the conversation.")); auto font=heading->font(); font.setPointSize(20); font.setBold(true); heading->setFont(font);
    layout->addWidget(heading);
    layout->addWidget(note(tr("Keep your script near the camera. Review every question and answer before it joins your words.")));
    auto *bar=new QHBoxLayout;
    auto *open=new QPushButton(tr("Open script…")); auto *save=new QPushButton(tr("Save script…"));
    auto *show=new QPushButton(tr("Show prompter")); show->setDefault(true);
    auto *readHere=new QPushButton(tr("Read from cursor"));
    listen_=new QPushButton(tr("Start listening"));
    bar->addWidget(open); bar->addWidget(save); bar->addWidget(readHere); bar->addStretch(); bar->addWidget(listen_); bar->addWidget(show); layout->addLayout(bar);
    audioStatus_=note(tr("Listening is off. No audio is being captured.")); layout->addWidget(audioStatus_);
    auto *tabs=new QTabWidget;
    script_=new QPlainTextEdit; script_->setObjectName("scriptEditor"); script_->setPlaceholderText(tr("Paste or write the script you want to read…")); script_->setAccessibleName(tr("Editable script"));
    auto editorFont=script_->font(); editorFont.setPointSize(14); script_->setFont(editorFont);
    tabs->addTab(script_,tr("Script"));
    auto *conversation=new QWidget; auto *conversationLayout=new QVBoxLayout(conversation);
    auto *splitter=new QSplitter;
    auto *left=new QWidget; auto *leftLayout=new QVBoxLayout(left);
    transcript_=new QPlainTextEdit; transcript_->setReadOnly(true); transcript_->document()->setMaximumBlockCount(150);
    transcript_->setPlaceholderText(tr("Meeting audio appears here after you enable it in Settings. You can also write a question directly."));
    leftLayout->addWidget(field(tr("Meeting transcript · kept in memory"),transcript_));
    liveTranscript_=note(QString{}); leftLayout->addWidget(liveTranscript_);
    auto *use=new QPushButton(tr("Use selected text as question")); auto *clear=new QPushButton(tr("Clear conversation"));
    leftLayout->addWidget(use); leftLayout->addWidget(clear);
    auto *right=new QWidget; auto *rightLayout=new QVBoxLayout(right);
    question_=new QPlainTextEdit; question_->setPlaceholderText(tr("What is the question you want to answer?")); question_->setMaximumHeight(120);
    rightLayout->addWidget(field(tr("1 · Check the question"),question_));
    generate_=new QPushButton(tr("Approve question and generate")); rightLayout->addWidget(generate_);
    answer_=new QPlainTextEdit; answer_->setPlaceholderText(tr("The proposed answer will appear here. Edit it before inserting."));
    rightLayout->addWidget(field(tr("2 · Review the answer"),answer_));
    insert_=new QPushButton(tr("Approve answer and append to script")); rightLayout->addWidget(insert_);
    reviewStatus_=note(tr("Nothing is sent until you approve the question.")); rightLayout->addWidget(reviewStatus_);
    splitter->addWidget(left); splitter->addWidget(right); splitter->setSizes({450,500}); conversationLayout->addWidget(splitter);
    tabs->addTab(conversation,tr("Conversation")); tabs->addTab(makeSettings(),tr("Settings")); layout->addWidget(tabs,1);
    position_=note(tr("Ready to write.")); position_->setObjectName("readingPosition"); readHere->setObjectName("readHere"); layout->addWidget(position_); setCentralWidget(central);
    connect(open,&QPushButton::clicked,this,&MainWindow::loadScript);
    connect(save,&QPushButton::clicked,this,[this]{ saveScript(); });
    connect(show,&QPushButton::clicked,this,&MainWindow::showPrompter);
    connect(listen_,&QPushButton::clicked,this,&MainWindow::toggleListening);
    connect(script_,&QPlainTextEdit::textChanged,this,[this] {
        const auto text=script_->toPlainText(); tracker_.setScript(text);
        if (speech_.active()) waitForNewUtterance_=true;
        if (prompter_->text->toPlainText()!=text) { QSignalBlocker block(prompter_->text); replaceText(prompter_->text,text); }
        refreshReading();
    });
    connect(prompter_->text,&QPlainTextEdit::textChanged,this,[this] {
        if (script_->toPlainText()!=prompter_->text->toPlainText()) replaceText(script_,prompter_->text->toPlainText());
        script_->document()->setModified(true);
    });
    connect(readHere,&QPushButton::clicked,this,[this] {
        const auto words=ScriptTracker::tokenize(script_->toPlainText());
        const int character=script_->textCursor().position();
        int index=-1; for(int i=0;i<words.size() && words[i].start<=character;++i) index=i;
        tracker_.seek(index); waitForNewUtterance_=speech_.active(); refreshReading();
    });
    connect(prompter_,&Prompter::step,this,[this](int delta){tracker_.seek(tracker_.position()+delta);waitForNewUtterance_=speech_.active();refreshReading();});
    connect(question_,&QPlainTextEdit::textChanged,this,[this]{flow_.setQuestion(question_->toPlainText());});
    connect(answer_,&QPlainTextEdit::textChanged,this,[this]{flow_.editAnswer(answer_->toPlainText());});
    connect(&flow_,&ApprovalFlow::changed,this,&MainWindow::refreshApproval);
    connect(&flow_,&ApprovalFlow::invalidated,&llm_,&LlmClient::cancel);
    connect(use,&QPushButton::clicked,this,[this] {
        auto text=transcript_->textCursor().selectedText().replace(QChar::ParagraphSeparator,'\n');
        if (text.isEmpty()) { reviewStatus_->setText(tr("Select the question in the transcript, or type it above.")); return; }
        question_->setPlainText(text); question_->setFocus();
    });
    connect(clear,&QPushButton::clicked,this,[this] {
        speech_.stop(); audioStatus_->setText(tr("Listening stopped and conversation cleared. Start listening to begin again."));
        llm_.cancel(); flow_.fail(request_); transcript_->clear(); question_->clear(); answer_->clear(); liveTranscript_->clear();
        reviewStatus_->setText(tr("Conversation cleared from this session."));
    });
    connect(generate_,&QPushButton::clicked,this,[this] {
        request_=flow_.approveQuestion(); if (!request_) return;
        reviewStatus_->setText(tr("Generating with %1. The script stays editable.").arg(model_->text()));
        llm_.generate(request_,QUrl(endpoint_->text().trimmed()),model_->text().trimmed(),apiKey_->text(),flow_.question(),transcript_->toPlainText());
    });
    connect(&llm_,&LlmClient::completed,this,[this](quint64 id,const QString &answer) {
        if(flow_.receiveAnswer(id,answer)) reviewStatus_->setText(tr("Review and edit this draft. It has not been added to the script."));
    });
    connect(&llm_,&LlmClient::failed,this,[this](quint64 id,const QString &message) {
        flow_.fail(id); if(id==request_) reviewStatus_->setText(message);
    });
    connect(insert_,&QPushButton::clicked,this,[this] {
        const auto answer=flow_.approveAnswer(); if(answer.isEmpty()) return;
        auto cursor=script_->textCursor(); cursor.movePosition(QTextCursor::End);
        cursor.beginEditBlock(); cursor.insertText((script_->toPlainText().isEmpty()?QString{}:QStringLiteral("\n\n"))+answer); cursor.endEditBlock();
        reviewStatus_->setText(tr("Approved answer appended. You can still edit it in Script."));
    });
    connect(&speech_,&SpeechService::status,audioStatus_,&QLabel::setText);
    connect(&speech_,&SpeechService::stopped,this,[this]{listen_->setText(tr("Start listening"));});
    connect(&speech_,&SpeechService::transcript,this,[this](int channel,const QString &text,bool final) {
        if(channel==0) {
            if(waitForNewUtterance_) { if(final) { waitForNewUtterance_=false; refreshReading(); } return; }
            if(tracker_.follow(text,final)) refreshReading();
        }
        else if(final) {
            liveTranscript_->clear();
            if(!text.isEmpty()) {
                transcript_->appendPlainText(text.left(4000));
                if (transcript_->toPlainText().size()>24000) transcript_->setPlainText(transcript_->toPlainText().right(20000));
                static const QRegularExpression questionStart("^(come|cosa|che|quale|quali|quanto|quanti|quando|perché|perche|puoi|potresti|how|what|which|when|why|can|could|would|do|does|is|are)\\b",QRegularExpression::CaseInsensitiveOption);
                if (question_->toPlainText().trimmed().isEmpty() && questionStart.match(text).hasMatch()) {
                    question_->setPlainText(text.left(4000));
                    reviewStatus_->setText(tr("Possible question from the last turn. Check and correct it before approving."));
                }
            }
        } else liveTranscript_->setText(text.left(500));
    });
    auto *saveShortcut=new QShortcut(QKeySequence::Save,this); connect(saveShortcut,&QShortcut::activated,this,[this]{saveScript();});
    auto *showShortcut=new QShortcut(QKeySequence("Ctrl+Return"),this); connect(showShortcut,&QShortcut::activated,this,&MainWindow::showPrompter);
    refreshApproval();
}
QWidget *MainWindow::makeSettings() {
    QSettings settings;
    auto *contents=new QWidget; auto *layout=new QVBoxLayout(contents);
    auto *reading=new QGroupBox(tr("Reading")); auto *readingForm=new QFormLayout(reading);
    screen_=new QComboBox; for(auto *screen:QGuiApplication::screens()) screen_->addItem(screen->name());
    auto *size=new QSpinBox; size->setRange(18,64); size->setValue(settings.value("reading/fontSize",28).toInt());
    prompter_->setTextSize(size->value());
    readingForm->addRow(tr("Monitor"),screen_); readingForm->addRow(tr("Text size"),size);
    connect(size,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){prompter_->setTextSize(value);QSettings{}.setValue("reading/fontSize",value);});
    layout->addWidget(reading);
    auto *audio=new QGroupBox(tr("Local speech recognition")); auto *audioForm=new QFormLayout(audio);
    library_=new QLineEdit(settings.value("speech/library","libvosk.so").toString());
    modelPath_=new QLineEdit(settings.value("speech/model").toString());
    microphone_=new QLineEdit(settings.value("speech/microphone").toString()); microphone_->setPlaceholderText(tr("Default microphone"));
    meetingTarget_=new QLineEdit(settings.value("speech/meetingTarget").toString()); meetingTarget_->setPlaceholderText(tr("PipeWire output node.name or object.serial"));
    meeting_=new QCheckBox(tr("Also transcribe meeting audio"));
    audioForm->addRow(tr("Vosk library"),library_); audioForm->addRow(tr("Model folder"),modelPath_);
    auto *browse=new QPushButton(tr("Choose model folder…")); audioForm->addRow(QString{},browse);
    connect(browse,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getExistingDirectory(this,tr("Choose speech model"));if(!path.isEmpty())modelPath_->setText(path);});
    audioForm->addRow(tr("Microphone node"),microphone_); audioForm->addRow(tr("Meeting output node"),meetingTarget_); audioForm->addRow(meeting_);
    audioForm->addRow(note(tr("Install Vosk and an extracted language model first. Use wpctl status / wpctl inspect to find audio nodes. Audio is processed locally and is not saved. Headphones prevent meeting audio from entering your microphone.")));
    layout->addWidget(audio);
    auto *ai=new QGroupBox(tr("Answer generation")); auto *aiForm=new QFormLayout(ai);
    endpoint_=new QLineEdit(settings.value("llm/endpoint","http://127.0.0.1:8080/v1/chat/completions").toString());
    model_=new QLineEdit(settings.value("llm/model").toString());
    apiKey_=new QLineEdit; apiKey_->setEchoMode(QLineEdit::Password); apiKey_->setPlaceholderText(tr("Kept in memory for this session only"));
    aiForm->addRow(tr("Chat completions URL"),endpoint_); aiForm->addRow(tr("Model name"),model_); aiForm->addRow(tr("API key (optional locally)"),apiKey_);
    aiForm->addRow(note(tr("Use a compatible local server or an HTTPS provider. Approving a question sends that question and up to 8,000 recent transcript characters to this endpoint. API keys and transcripts are never saved in settings.")));
    layout->addWidget(ai);
    layout->addWidget(note(tr("Screen sharing: install the optional KWin companion or configure a window rule to exclude this app from capture. Exclusion is not verified by the app and KDE has known thumbnail/Overview limitations. Test from a second participant before relying on it.")));
    layout->addStretch();
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(contents); return scroll;
}
MainWindow::~MainWindow() { saveSettings(); delete prompter_; }
void MainWindow::saveSettings() {
    QSettings s; s.setValue("speech/library",library_->text()); s.setValue("speech/model",modelPath_->text());
    s.setValue("speech/microphone",microphone_->text()); s.setValue("speech/meetingTarget",meetingTarget_->text());
    const QUrl url(endpoint_->text());
    if(LlmClient::validateEndpoint(url).isEmpty()) s.setValue("llm/endpoint",url.toString());
    s.setValue("llm/model",model_->text());
}
void MainWindow::showPrompter() {
    const auto screens=QGuiApplication::screens();
    prompter_->showAtCamera(screens.value(screen_->currentIndex(),QGuiApplication::primaryScreen())); refreshReading();
}
void MainWindow::refreshReading() {
    prompter_->highlight(tracker_.character(),tracker_.length());
    position_->setText(waitForNewUtterance_ ? tr("Voice following resumes after your next pause; the reading position is preserved.") : tr("Word %1 of %2 · Ctrl+Enter opens the prompter · Ctrl+Left/Right steps through words in the prompter").arg(tracker_.position()+1).arg(tracker_.count()));
}
void MainWindow::refreshApproval() {
    generate_->setEnabled(!flow_.question().trimmed().isEmpty() && flow_.stage()!=ApprovalFlow::Stage::Generating);
    insert_->setEnabled(flow_.stage()==ApprovalFlow::Stage::Answer && !flow_.answer().trimmed().isEmpty());
    answer_->setReadOnly(flow_.stage()!=ApprovalFlow::Stage::Answer);
    if(answer_->toPlainText()!=flow_.answer()) { QSignalBlocker block(answer_); answer_->setPlainText(flow_.answer()); }
}
void MainWindow::toggleListening() {
    if(speech_.active()) { speech_.stop(); audioStatus_->setText(tr("Listening is off. No audio is being captured.")); return; }
    saveSettings(); waitForNewUtterance_=false; speech_.start(library_->text(),modelPath_->text(),microphone_->text(),meetingTarget_->text(),meeting_->isChecked());
    if(speech_.active()) listen_->setText(tr("Stop listening"));
}
bool MainWindow::discardChanges() {
    if(!script_->document()->isModified()) return true;
    const auto answer=QMessageBox::question(this,tr("Save the script?"),tr("The script has unsaved changes."),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(answer==QMessageBox::Save) return saveScript();
    return answer==QMessageBox::Discard;
}
void MainWindow::loadScript() {
    if(!discardChanges()) return;
    const auto path=QFileDialog::getOpenFileName(this,tr("Open script"),{},tr("Text files (*.txt);;All files (*)")); if(path.isEmpty()) return;
    QFile file(path); if(!file.open(QIODevice::ReadOnly) || file.size()>2*1024*1024) { QMessageBox::warning(this,tr("Cannot open script"),tr("Choose a readable UTF-8 text file smaller than 2 MiB.")); return; }
    script_->setPlainText(QString::fromUtf8(file.readAll())); script_->document()->setModified(false); scriptPath_=path;
}
bool MainWindow::saveScript() {
    auto path=QFileDialog::getSaveFileName(this,tr("Save script"),scriptPath_,tr("Text files (*.txt)")); if(path.isEmpty()) return false;
    QSaveFile file(path); const auto data=script_->toPlainText().toUtf8();
    if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) { QMessageBox::warning(this,tr("Cannot save script"),tr("Check the destination and available disk space.")); return false; }
    scriptPath_=path; script_->document()->setModified(false); return true;
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if(!discardChanges()) { event->ignore(); return; }
    speech_.stop(); llm_.cancel(); prompter_->hide(); saveSettings(); event->accept();
}
void MainWindow::setDemoScript() {
    script_->setPlainText(tr("Thank you for joining today.\n\nLet us start with the question that matters most. What would make this project useful to the people who depend on it?\n\nWe can take this one step at a time. Listen carefully, check the question, and choose the words we want to share."));
    script_->document()->setModified(false); tracker_.seek(6); refreshReading();
}
