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
#include <QJsonArray>
#include <QTimer>

namespace {
QLabel *note(const QString &text) {
    auto *label=new QLabel(text); label->setWordWrap(true); label->setTextFormat(Qt::PlainText); return label;
}
QWidget *field(const QString &label,QWidget *input) {
    auto *box=new QWidget; auto *layout=new QVBoxLayout(box); layout->setContentsMargins(0,0,0,0);
    auto *title=new QLabel(label); title->setBuddy(input); input->setAccessibleName(label); layout->addWidget(title); layout->addWidget(input); return box;
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
    resize(680,250); setMinimumSize(320,180);
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(16,10,16,12);
    auto *bar=new FlowLayout;
    auto *title=new QLabel(tr("READ AT YOUR PACE"));title->setWordWrap(true);
    auto *previous=new AccessibleButton(tr("Previous")); auto *next=new AccessibleButton(tr("Next"));
    auto *edit=new AccessibleButton(tr("Edit")); edit->setCheckable(true);
    auto *hide=new AccessibleButton(tr("Hide"));
    bar->addWidget(title);  bar->addWidget(previous); bar->addWidget(next); bar->addWidget(edit); bar->addWidget(hide);
    layout->addLayout(bar);
    text=new QPlainTextEdit; text->setReadOnly(true); text->setFrameStyle(QFrame::NoFrame);
    text->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    text->setAccessibleName(tr("Kalipso script")); text->setCenterOnScroll(true);
    layout->addWidget(text);
    auto *privacy=note(tr("Capture exclusion is not verified. Check what the other participant sees."));
    layout->addWidget(privacy);
    backdrop_=new Backdrop(this);
    connect(backdrop_,&Backdrop::unavailable,this,&Prompter::colorStatus);
    connect(backdrop_,&Backdrop::colorChanged,this,[this](const QColor &sample){
        const auto luminance=Accessibility::luminance(sample);const QString next=luminance>0.35?"dark":luminance<0.15?"light":resolvedTheme_;
        if(next==resolvedTheme_||(paletteAge_.isValid()&&paletteAge_.elapsed()<2500))return;
        resolvedTheme_=next;paletteAge_.restart();colors_=next=="light"?Design::light:Design::dark;
        Accessibility::apply(this,colors_);text->setPalette(Accessibility::palette(colors_));highlight(position_,length_);
    });
    setReaderTheme("dark");
    setTextSize(28);
    connect(previous,&QPushButton::clicked,this,[this]{emit step(-1);});
    connect(next,&QPushButton::clicked,this,[this]{emit step(1);});
    connect(hide,&QPushButton::clicked,this,&QWidget::hide);
    connect(edit,&QPushButton::toggled,this,[this](bool enabled) { editing_=enabled; text->setReadOnly(!enabled); if(enabled) text->setFocus(); });
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),this); connect(escape,&QShortcut::activated,this,&QWidget::hide);
    auto *forward=new QShortcut(QKeySequence("Ctrl+Right"),this); connect(forward,&QShortcut::activated,this,[this]{emit step(1);});
    auto *back=new QShortcut(QKeySequence("Ctrl+Left"),this); connect(back,&QShortcut::activated,this,[this]{emit step(-1);});
}
void Prompter::setReaderTheme(const QString &theme){
    appearance_=theme;resolvedTheme_=theme=="light"?"light":"dark";paletteAge_.invalidate();
    colors_=resolvedTheme_=="light"?Design::light:Design::dark;
    Accessibility::apply(this,colors_);text->setPalette(Accessibility::palette(colors_));
    highlight(position_,length_);
    backdrop_->setEnabled(theme=="auto");
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
    position_=position;length_=length;
    QTextCursor cursor(text->document()); cursor.setPosition(std::min(position,text->document()->characterCount()-1));
    cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,length);
    QTextEdit::ExtraSelection selection; selection.cursor=cursor;
    selection.format.setBackground(QColor(colors_.selection)); selection.format.setForeground(QColor(colors_.selectionText));
    selection.format.setFontUnderline(true);
    QTextEdit::ExtraSelection read;read.cursor=QTextCursor(text->document());read.cursor.setPosition(0);read.cursor.setPosition(std::max(0,position),QTextCursor::KeepAnchor);read.format.setForeground(QColor(colors_.muted));
    text->setExtraSelections({read,selection});
    if (!editing_) { text->setTextCursor(cursor); text->centerCursor(); }
}
MainWindow::MainWindow() : flow_(this), llm_(this), speech_(this), prompter_(new Prompter) {
    setWindowTitle(tr("Kalipso")); resize(1100,760); setMinimumSize(320,240);
    Accessibility::apply(this,Design::light);
    auto *central=new QWidget; auto *layout=new QVBoxLayout(central); layout->setContentsMargins(24,20,24,20); layout->setSpacing(14);
    auto *heading=new QLabel(tr("A little closer to the conversation."));heading->setWordWrap(true); auto font=heading->font(); font.setPointSize(20); font.setBold(true); heading->setFont(font);
    layout->addWidget(heading);
    layout->addWidget(note(tr("Keep your script near the camera. Review every question and answer before it joins your words.")));
    auto *bar=new FlowLayout;
    auto *open=new AccessibleButton(tr("Open script…")); auto *save=new AccessibleButton(tr("Save script…"));
    auto *show=new AccessibleButton(tr("Show prompter")); show->setDefault(true);
    auto *readHere=new AccessibleButton(tr("Read from cursor"));
    listen_=new AccessibleButton(tr("Start listening"));
    bar->addWidget(open); bar->addWidget(save); bar->addWidget(readHere);  bar->addWidget(listen_); bar->addWidget(show); layout->addLayout(bar);
    audioStatus_=new StatusLabel(tr("Listening is off. No audio is being captured.")); layout->addWidget(audioStatus_);
    auto *tabs=new QTabWidget;
    script_=new QPlainTextEdit; script_->setObjectName("scriptEditor"); script_->setPlaceholderText(tr("Paste or write the script you want to read…")); script_->setAccessibleName(tr("Editable script"));
    auto editorFont=script_->font(); editorFont.setPointSize(14); script_->setFont(editorFont);
    tabs->addTab(script_,tr("Script"));
    auto *conversation=new QWidget; auto *conversationLayout=new QVBoxLayout(conversation);
    auto *splitter=new QSplitter(Qt::Vertical);
    auto *left=new QWidget; auto *leftLayout=new QVBoxLayout(left);
    transcript_=new QPlainTextEdit; transcript_->setReadOnly(true); transcript_->document()->setMaximumBlockCount(150);
    transcript_->setPlaceholderText(tr("Meeting audio appears here after you enable it in Settings. You can also write a question directly."));
    leftLayout->addWidget(field(tr("Meeting transcript · kept in memory"),transcript_));
    liveTranscript_=note(QString{}); leftLayout->addWidget(liveTranscript_);
    auto *use=new AccessibleButton(tr("Use selected text as question")); auto *clear=new AccessibleButton(tr("Clear conversation"));
    leftLayout->addWidget(use); leftLayout->addWidget(clear);
    auto *right=new QWidget; auto *rightLayout=new QVBoxLayout(right);
    question_=new QPlainTextEdit; question_->setPlaceholderText(tr("What is the question you want to answer?")); question_->setMinimumHeight(100);
    rightLayout->addWidget(field(tr("1 · Check the question"),question_));
    generate_=new AccessibleButton(tr("Approve question and generate")); rightLayout->addWidget(generate_);
    answer_=new QPlainTextEdit; answer_->setPlaceholderText(tr("The proposed answer will appear here. Edit it before inserting."));
    rightLayout->addWidget(field(tr("2 · Review the answer"),answer_));
    insert_=new AccessibleButton(tr("Approve answer and append to script")); rightLayout->addWidget(insert_);
    reviewStatus_=new StatusLabel(tr("Nothing is sent until you approve the question.")); rightLayout->addWidget(reviewStatus_);
    splitter->addWidget(left); splitter->addWidget(right); splitter->setSizes({450,500}); conversationLayout->addWidget(splitter);
    tabs->addTab(conversation,tr("Conversation")); tabs->addTab(makeSettings(),tr("Settings")); layout->addWidget(tabs,1);
    position_=note(tr("Ready to write.")); position_->setObjectName("readingPosition"); readHere->setObjectName("readHere"); layout->addWidget(position_); auto *pageScroll=new QScrollArea;pageScroll->setWidgetResizable(true);pageScroll->setWidget(central);pageScroll->setFrameShape(QFrame::NoFrame);setCentralWidget(pageScroll);
    connect(open,&QPushButton::clicked,this,&MainWindow::loadScript);
    connect(save,&QPushButton::clicked,this,[this]{ saveScript(); });
    connect(show,&QPushButton::clicked,this,&MainWindow::showPrompter);
    connect(listen_,&QPushButton::clicked,this,&MainWindow::toggleListening);
    connect(script_,&QPlainTextEdit::textChanged,this,[this] {
        const auto text=script_->toPlainText(); tracker_.setScript(text);
        if (speech_.active()) { waitForNewUtterance_=true; speech_.setScript(text,tracker_.position()); }
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
        tracker_.seek(index); speech_.setScript(script_->toPlainText(),tracker_.position()); waitForNewUtterance_=speech_.active(); refreshReading();
    });
    connect(prompter_,&Prompter::step,this,[this](int delta){tracker_.seek(tracker_.position()+delta);speech_.setScript(script_->toPlainText(),tracker_.position());waitForNewUtterance_=speech_.active();refreshReading();});
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
    connect(&speech_,&SpeechService::status,audioStatus_,&StatusLabel::setText);
    connect(&speech_,&SpeechService::stopped,this,[this]{listen_->setText(tr("Start listening"));backend_->setEnabled(true);language_->setEnabled(true);checkSystem_->setEnabled(true);});
    connect(&speech_,&SpeechService::transcript,this,[this](int channel,const QString &text,bool final) {
        if(channel==0) {
            if(waitForNewUtterance_) { if(final) { waitForNewUtterance_=false; refreshReading(); } return; }
            if(tracker_.follow(text,final)) refreshReading();
            if(final)speech_.setScript(script_->toPlainText(),tracker_.position());
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
    auto *reading=new QGroupBox(tr("Reading")); auto *readingForm=new QFormLayout(reading);readingForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    screen_=new QComboBox; for(auto *screen:QGuiApplication::screens()) screen_->addItem(screen->name());
    auto *size=new QSpinBox; size->setRange(18,64); size->setValue(settings.value("reading/fontSize",28).toInt());
    prompter_->setTextSize(size->value());
    readingForm->addRow(tr("Monitor"),screen_); readingForm->addRow(tr("Text size"),size);
    connect(size,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){prompter_->setTextSize(value);QSettings{}.setValue("reading/fontSize",value);});
    auto *appearance=new QComboBox;appearance->setObjectName("applicationTheme");appearance->addItem(tr("Light"),"light");appearance->addItem(tr("Dark"),"dark");
    const auto savedTheme=settings.value("appearance/theme","light").toString();appearance->setCurrentIndex(savedTheme=="dark"?1:0);Accessibility::apply(this,savedTheme=="dark"?Design::dark:Design::light);
    connect(appearance,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,appearance]{const auto name=appearance->currentData().toString();Accessibility::apply(this,name=="dark"?Design::dark:Design::light);QSettings{}.setValue("appearance/theme",name);});
    readingForm->addRow(tr("Application appearance"),appearance);
    auto *readerTheme=new QComboBox;readerTheme->setObjectName("readerTheme");readerTheme->addItem(tr("Dark"),"dark");readerTheme->addItem(tr("Light"),"light");readerTheme->addItem(tr("Automatic screen colors"),"auto");
    const auto savedReader=settings.value("reading/theme","dark").toString();readerTheme->setCurrentIndex(std::max(0,readerTheme->findData(savedReader)));prompter_->setReaderTheme(savedReader);
    connect(readerTheme,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,readerTheme]{const auto theme=readerTheme->currentData().toString();prompter_->setReaderTheme(theme);QSettings{}.setValue("reading/theme",theme);});readingForm->addRow(tr("Reader appearance"),readerTheme);
    auto *colorSource=new StatusLabel(prompter_->colorSource());readingForm->addRow(colorSource);connect(prompter_,&Prompter::colorStatus,colorSource,&StatusLabel::setText);
    if(prompter_->usesWindowPortal()){
        readingForm->addRow(note(tr("Follow the colors of a window you select, even while the reader covers it. Sharing stays active while the reader is visible. Images stay in memory and are never saved or sent. You can stop sharing at any time.")));
        auto *choose=new AccessibleButton(tr("Choose background window…"));choose->setObjectName("chooseBackgroundWindow");
        auto *stop=new AccessibleButton(tr("Stop window sharing"));stop->setObjectName("stopWindowSharing");
        readingForm->addRow(choose);readingForm->addRow(stop);
        connect(choose,&QPushButton::clicked,this,[this,readerTheme]{readerTheme->setCurrentIndex(readerTheme->findData("auto"));showPrompter();prompter_->chooseBackgroundWindow();});
        connect(stop,&QPushButton::clicked,prompter_,&Prompter::stopWindowSharing);
    }
    auto *uiScale=new QComboBox;uiScale->setObjectName("interfaceTextSize");uiScale->addItem("100%",1.0);uiScale->addItem("150%",1.5);uiScale->addItem("200%",2.0);
    readingForm->addRow(tr("Interface text size"),uiScale);
    connect(uiScale,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,uiScale]{
        const auto widgets=findChildren<QWidget*>();for(auto *widget:widgets)if(!widget->property("baseAccessibleFont").isValid())widget->setProperty("baseAccessibleFont",widget->font());
        const double scale=uiScale->currentData().toDouble();for(auto *widget:widgets){auto font=widget->property("baseAccessibleFont").value<QFont>();font.setPointSizeF(std::max(8.0,font.pointSizeF())*scale);widget->setFont(font);}QSettings{}.setValue("appearance/textScale",scale);
    });
    const double savedScale=settings.value("appearance/textScale",1.0).toDouble();if(savedScale!=1.0)QTimer::singleShot(0,this,[uiScale,savedScale]{const int index=uiScale->findData(savedScale);if(index>=0&&uiScale->currentIndex()==0)uiScale->setCurrentIndex(index);});
    layout->addWidget(reading);
    auto *audio=new QGroupBox(tr("Local speech recognition")); auto *audioForm=new QFormLayout(audio);audioForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    backend_=new QComboBox;backend_->setObjectName("speechBackend");
    if(SystemSpeechClient::platformSupported())backend_->addItem(tr("System · on-device"),"system");
    backend_->addItem("Moonshine","moonshine");backend_->addItem("Vosk","vosk");
    language_=new QComboBox;language_->setObjectName("speechLanguage");language_->setEditable(true);
    language_->addItem(tr("Choose language"),QString{});
    for(const auto &entry:QList<QPair<QString,QString>>{{"English (US)","en-US"},{"Italiano","it-IT"},{"Deutsch","de-DE"},{"Español","es-ES"},{"Français","fr-FR"},{"日本語","ja-JP"},{"中文","zh-CN"}})language_->addItem(entry.first,entry.second);
    library_=new QLineEdit;library_->setObjectName("speechLibrary");modelPath_=new QLineEdit;modelPath_->setObjectName("speechModel");
    architecture_=new QComboBox;architecture_->addItems({"Tiny Streaming","Base Streaming","Small Streaming","Medium Streaming"});
    microphone_=new QLineEdit(settings.value("speech/microphone").toString());microphone_->setPlaceholderText(tr("Default microphone"));
    meetingTarget_=new QLineEdit(settings.value("speech/meetingTarget").toString());meetingTarget_->setPlaceholderText(tr("PipeWire output node.name or object.serial"));
    meeting_=new QCheckBox(tr("Also transcribe meeting audio"));
    audioForm->addRow(tr("Speech engine"),backend_);audioForm->addRow(tr("Language"),language_);
    audioForm->addRow(tr("Native library"),library_);audioForm->addRow(tr("Model folder"),modelPath_);
    audioForm->addRow(tr("Moonshine model size"),architecture_);
    auto *browse=new AccessibleButton(tr("Choose model folder…"));audioForm->addRow(QString{},browse);
    connect(browse,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getExistingDirectory(this,tr("Choose speech model"));if(!path.isEmpty())modelPath_->setText(path);});
    auto *systemActions=new QWidget;auto *systemLayout=new FlowLayout(systemActions);systemLayout->setContentsMargins(0,0,0,0);
    checkSystem_=new AccessibleButton(tr("Check availability"));installSystem_=new AccessibleButton(tr("Install system language"));installSystem_->setEnabled(false);
    systemLayout->addWidget(checkSystem_);systemLayout->addWidget(installSystem_);
#ifndef Q_OS_MACOS
    installSystem_->hide();
#endif
    systemStatus_=note(tr("Choose a language and check its availability. No microphone is opened by this check."));
    systemStatus_->setObjectName("systemSpeechStatus");audioForm->addRow(systemActions);audioForm->addRow(systemStatus_);
    audioForm->addRow(tr("Microphone"),microphone_);audioForm->addRow(tr("Meeting output node"),meetingTarget_);audioForm->addRow(meeting_);
    const auto updateFields=[this,audioForm,browse,systemActions]{
        const bool system=backend_->currentData().toString()=="system";
        for(auto *widget:QList<QWidget*>{library_,modelPath_,microphone_}){widget->setVisible(!system);if(auto *label=audioForm->labelForField(widget))label->setVisible(!system);}
        const bool moon=backend_->currentData().toString()=="moonshine";
        architecture_->setVisible(moon);audioForm->labelForField(architecture_)->setVisible(moon);
        browse->setVisible(!system);systemActions->setVisible(system);systemStatus_->setVisible(system);
        meeting_->setEnabled(!system);if(system)meeting_->setChecked(false);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        meeting_->hide();meetingTarget_->hide();audioForm->labelForField(meetingTarget_)->hide();
#endif
    };
    QString initial=settings.value("speech/backend",settings.contains("speech/model")?"vosk":SystemSpeechClient::platformSupported()?"system":"moonshine").toString();
    int selection=backend_->findData(initial);backend_->setCurrentIndex(selection<0?0:selection);
    profile_=backend_->currentData().toString();loadSpeechProfile();updateFields();
    connect(backend_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,updateFields]{
        systemCheck_.stop();installingSystem_=false;saveSpeechProfile();profile_=backend_->currentData().toString();loadSpeechProfile();updateFields();
        systemStatus_->setText(tr("Choose a language and check its availability."));checkSystem_->setEnabled(true);installSystem_->setEnabled(false);
    });
    connect(language_,&QComboBox::currentTextChanged,this,[this]{systemCheck_.stop();installingSystem_=false;installSystem_->setEnabled(false);checkSystem_->setEnabled(true);systemStatus_->setText(tr("Check availability for the selected language."));});
    connect(checkSystem_,&QPushButton::clicked,this,[this]{systemStatus_->setText(tr("Checking installed system speech…"));systemCheck_.check(speechLanguage());});
    connect(installSystem_,&QPushButton::clicked,this,[this]{installingSystem_=true;installSystem_->setEnabled(false);checkSystem_->setEnabled(false);systemCheck_.install(speechLanguage());});
    connect(&systemCheck_,&SystemSpeechClient::capabilities,this,[this](const QJsonObject &result){
        systemStatus_->setText(result.value("message").toString());installSystem_->setEnabled(result.value("supported").toBool()&&!result.value("installed").toBool());
        const auto locales=result.value("locales").toArray();
        for(const auto &locale:locales)if(language_->findData(locale.toString())<0)language_->addItem(locale.toString(),locale.toString());
    });
    connect(&systemCheck_,&SystemSpeechClient::message,systemStatus_,&QLabel::setText);
    connect(&systemCheck_,&SystemSpeechClient::failed,this,[this](const QString &message){installingSystem_=false;systemStatus_->setText(message);checkSystem_->setEnabled(true);});
    connect(&systemCheck_,&SystemSpeechClient::completed,this,[this]{checkSystem_->setEnabled(true);if(installingSystem_){installingSystem_=false;systemCheck_.check(speechLanguage());}});
    audioForm->addRow(note(tr("Recognition stays on this device. Moonshine and Vosk require a matching native library and extracted model. System speech uses the default microphone; language availability depends on the operating system. Changing engines never switches to a cloud service.")));
    layout->addWidget(audio);
    auto *ai=new QGroupBox(tr("Answer generation")); auto *aiForm=new QFormLayout(ai);aiForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    endpoint_=new QLineEdit(settings.value("llm/endpoint","http://127.0.0.1:8080/v1/chat/completions").toString());
    model_=new QLineEdit(settings.value("llm/model").toString());
    apiKey_=new QLineEdit; apiKey_->setEchoMode(QLineEdit::Password); apiKey_->setPlaceholderText(tr("Kept in memory for this session only"));
    aiForm->addRow(tr("Chat completions URL"),endpoint_); aiForm->addRow(tr("Model name"),model_); aiForm->addRow(tr("API key (optional locally)"),apiKey_);
    aiForm->addRow(note(tr("Use a compatible local server or an HTTPS provider. Approving a question sends that question and up to 8,000 recent transcript characters to this endpoint. API keys and transcripts are never saved in settings.")));
    layout->addWidget(ai);
    layout->addWidget(note(tr("Screen sharing: install the optional KWin companion or configure a window rule to exclude this app from capture. Exclusion is not verified by the app and KDE has known thumbnail/Overview limitations. Test from a second participant before relying on it.")));
    for(auto *form:contents->findChildren<QFormLayout*>())for(int row=0;row<form->rowCount();++row){
        auto *labelItem=form->itemAt(row,QFormLayout::LabelRole);auto *fieldItem=form->itemAt(row,QFormLayout::FieldRole);
        if(labelItem&&fieldItem)if(auto *label=qobject_cast<QLabel*>(labelItem->widget()))if(auto *input=fieldItem->widget()){label->setBuddy(input);input->setAccessibleName(label->text());}
    }
    layout->addStretch();
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(contents); return scroll;
}
MainWindow::~MainWindow() { saveSettings(); delete prompter_; }
QString MainWindow::speechLanguage() const {
    if(language_->currentIndex()>=0&&language_->currentText()==language_->itemText(language_->currentIndex()))return language_->currentData().toString();
    return language_->currentText().trimmed();
}
void MainWindow::saveSpeechProfile(){
    if(profile_.isEmpty())return;
    QSettings s;const auto prefix="profiles/"+profile_+"/";
    s.setValue(prefix+"library",library_->text());s.setValue(prefix+"model",modelPath_->text());
    s.setValue(prefix+"language",speechLanguage());s.setValue(prefix+"architecture",architecture_->currentIndex()+2);
}
void MainWindow::loadSpeechProfile(){
    QSettings s;const auto prefix="profiles/"+profile_+"/";
    QString defaultLibrary=profile_=="moonshine"?"libmoonshine":"libvosk";
#ifdef Q_OS_WIN
    defaultLibrary=(profile_=="moonshine"?QString("moonshine"):QString("vosk"))+".dll";
#elif defined(Q_OS_MACOS)
    defaultLibrary+=".dylib";
#else
    defaultLibrary+=".so";
#endif
    library_->setText(s.value(prefix+"library",profile_=="vosk"?s.value("speech/library",defaultLibrary):QVariant(defaultLibrary)).toString());
    modelPath_->setText(s.value(prefix+"model",profile_=="vosk"?s.value("speech/model"):QVariant{}).toString());
    const auto language=s.value(prefix+"language").toString();
    const QSignalBlocker block(language_);const int index=language_->findData(language);
    if(index>=0)language_->setCurrentIndex(index);else language_->setEditText(language);
    architecture_->setCurrentIndex(std::clamp(s.value(prefix+"architecture",2).toInt()-2,0,3));
}
void MainWindow::saveSettings() {
    QSettings s; saveSpeechProfile();s.setValue("speech/backend",backend_->currentData().toString());
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
    saveSettings();systemCheck_.stop();installingSystem_=false;waitForNewUtterance_=false;
    speech_.setScript(script_->toPlainText(),tracker_.position());
    const auto backend=backend_->currentData().toString();
    speech_.start(library_->text(),modelPath_->text(),backend=="system"?QString{}:microphone_->text(),meetingTarget_->text(),meeting_->isChecked(),backend,speechLanguage(),architecture_->currentIndex()+2);
    if(speech_.active()){checkSystem_->setEnabled(false);installSystem_->setEnabled(false);listen_->setText(tr("Stop listening"));backend_->setEnabled(false);language_->setEnabled(false);}
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
