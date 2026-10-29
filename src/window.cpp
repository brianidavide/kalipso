// SPDX-License-Identifier: AGPL-3.0-or-later
#include "window.h"
#include "design_tokens.h"
#include "import.h"
#include "platform/capture_exclusion.h"
#include "remote.h"
#include "script_highlighter.h"
#include "speech_assets.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
// Scrolling a settings page must not silently change a language or model.
class SettingsComboBox : public QComboBox {
public:
    SettingsComboBox() {
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setMinimumContentsLength(1);
    }

protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

// Shows a useful width of the script title but can still shrink on narrow windows.
class ScriptPicker : public SettingsComboBox {
public:
    QSize sizeHint() const override {
        auto size = SettingsComboBox::sizeHint();
        size.setWidth(std::max(size.width(), fontMetrics().averageCharWidth() * 28));
        return size;
    }
};

class SettingsSpinBox : public QSpinBox {
protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

// Puts labels above fields when the form becomes too narrow for both on one row.
class SettingsGroup : public QGroupBox {
public:
    explicit SettingsGroup(const QString &title) : QGroupBox(title) {}

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGroupBox::resizeEvent(event);
        adaptForm();
    }
    void changeEvent(QEvent *event) override {
        QGroupBox::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            adaptForm();
    }

private:
    void adaptForm() {
        auto *form = qobject_cast<QFormLayout *>(layout());
        if (!form)
            return;
        const auto margins = form->contentsMargins();
        const int available = std::max(1, contentsRect().width() - margins.left() - margins.right());
        const bool narrow = available < fontMetrics().horizontalAdvance(QStringLiteral("Application appearance")) + 350;
        form->setRowWrapPolicy(narrow ? QFormLayout::WrapAllRows : QFormLayout::WrapLongRows);
        for (int row = 0; row < form->rowCount(); ++row)
            if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
                if (auto *label = qobject_cast<QLabel *>(item->widget())) {
                    label->setWordWrap(narrow);
                    label->setMaximumWidth(narrow ? available : QWIDGETSIZE_MAX);
                }
    }
};

QLabel *note(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// A check box whose label wraps at narrow widths and large text sizes.
class WrappingCheckBox : public QWidget {
public:
    explicit WrappingCheckBox(const QString &text) : box(new QCheckBox) {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        box->setAccessibleName(text);
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        label->setBuddy(box);
        label->installEventFilter(this);
        layout->addWidget(box, 0, Qt::AlignTop);
        layout->addWidget(label, 1);
    }
    QCheckBox *box;

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonRelease && box->isEnabled())
            box->toggle();
        return QWidget::eventFilter(object, event);
    }
};

QFormLayout *formFor(QWidget *group) {
    auto *form = new QFormLayout(group);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    return form;
}

// Applies an edit as one contiguous replacement so undo and the reading
// anchor stay meaningful.
void replaceText(QTextDocument *document, const QString &value) {
    const auto old = document->toPlainText();
    if (old == value)
        return;
    int prefix = 0, suffix = 0;
    while (prefix < std::min(old.size(), value.size()) && old[prefix] == value[prefix])
        ++prefix;
    while (suffix < old.size() - prefix && suffix < value.size() - prefix &&
           old[old.size() - 1 - suffix] == value[value.size() - 1 - suffix])
        ++suffix;
    QTextCursor edit(document);
    edit.setPosition(prefix);
    edit.setPosition(int(old.size()) - suffix, QTextCursor::KeepAnchor);
    edit.beginEditBlock();
    edit.insertText(value.mid(prefix, value.size() - prefix - suffix));
    edit.endEditBlock();
}

QString duration(qint64 ms) {
    const qint64 seconds = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

const char *modeKey(ReadingMode mode) {
    return mode == ReadingMode::Manual ? "manual" : mode == ReadingMode::Steady ? "steady" : "voice";
}
ReadingMode modeFromKey(const QString &key) {
    return key == QLatin1String("manual")   ? ReadingMode::Manual
           : key == QLatin1String("steady") ? ReadingMode::Steady
                                            : ReadingMode::Voice;
}
} // namespace

MainWindow::MainWindow(const QString &libraryDirectory)
    : library_(libraryDirectory), speech_(this), prompter_(new ReaderWindow) {
    setWindowTitle(tr("Kalipso"));
    resize(1100, 760);
    setMinimumSize(320, 240);
    Accessibility::apply(this, Design::light);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    auto *heading = new QLabel(tr("Your script. Ready to read."));
    heading->setWordWrap(true);
    auto headingFont = heading->font();
    headingFont.setPointSize(20);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    layout->addWidget(note(tr("Scripts are saved automatically on this device. Open the reader near your camera, "
                              "then follow your voice, scroll at a steady speed or step manually.")));

    auto *libraryBar = new QWidget;
    buildLibraryBar(libraryBar);
    layout->addWidget(libraryBar);

    auto *actions = new FlowLayout;
    auto *show = new AccessibleButton(tr("Open reader"));
    show->setDefault(true);
    show->setObjectName("primaryAction");
    run_ = new AccessibleButton(tr("Start listening"));
    run_->setObjectName("runAction");
    auto *readHere = new AccessibleButton(tr("Read from cursor"));
    readHere->setObjectName("readHere");
    for (auto *button : {show, static_cast<AccessibleButton *>(run_), readHere})
        actions->addWidget(button);
    layout->addLayout(actions);
    audioStatus_ = new StatusLabel(tr("Listening is off. No audio is being captured."));
    audioStatus_->setObjectName("audioStatus");
    layout->addWidget(audioStatus_);

    auto *tabs = new QTabWidget;
    script_ = new QPlainTextEdit;
    script_->setObjectName("scriptEditor");
    script_->setPlaceholderText(tr("Paste or write the script you want to read…\n\n"
                                   "[Bracketed cues] are shown but not read aloud. "
                                   "Lines starting with # become sections you can jump to."));
    script_->setAccessibleName(tr("Editable script"));
    auto editorFont = script_->font();
    editorFont.setPointSize(14);
    script_->setFont(editorFont);
    auto *editorHighlighter = new ScriptHighlighter(script_->document());
    editorHighlighter->setColors(QColor(Design::light.muted), QColor(Design::light.accent));
    tabs->addTab(script_, tr("Script"));
    tabs->addTab(makeSettings(), tr("Settings"));
    layout->addWidget(tabs, 1);
    position_ = note(tr("Ready to write."));
    position_->setObjectName("readingPosition");
    layout->addWidget(position_);

    auto *page = new QScrollArea;
    page->setWidgetResizable(true);
    page->setWidget(central);
    page->setFrameShape(QFrame::NoFrame);
    setCentralWidget(page);

    autosave_.setSingleShot(true);
    autosave_.setInterval(800);
    connect(&autosave_, &QTimer::timeout, this, &MainWindow::saveScript);
    steady_.setTimerType(Qt::PreciseTimer);
    connect(&steady_, &QTimer::timeout, this, &MainWindow::steadyTick);
    countdown_.setInterval(1000);
    connect(&countdown_, &QTimer::timeout, this, [this] {
        prompter_->showCountdown(--countdownLeft_);
        if (countdownLeft_ > 0)
            return;
        countdown_.stop();
        steady_.start();
        refreshRunning();
    });

    connect(show, &QPushButton::clicked, this, &MainWindow::showPrompter);
    connect(run_, &QPushButton::clicked, this, &MainWindow::toggleRunning);
    connect(readHere, &QPushButton::clicked, this, [this] {
        tracker_.setScript(script_->toPlainText());
        tracker_.seekCharacter(script_->textCursor().position());
        seekWord(tracker_.position());
    });

    connect(script_, &QPlainTextEdit::textChanged, this, [this] {
        const auto text = script_->toPlainText();
        if (text == scriptText_)
            return; // Formatting-only document change.
        scriptText_ = text;
        tracker_.setScript(text);
        if (speech_.active()) {
            waitForNewUtterance_ = true;
            speech_.setScript(text, tracker_.position());
        }
        if (prompter_->text->toPlainText() != text) {
            const QSignalBlocker block(prompter_->text);
            replaceText(prompter_->text->document(), text);
        }
        prompter_->setSections(ScriptTracker::sections(text));
        if (!loading_)
            autosave_.start();
        refreshReading();
    });
    connect(prompter_->text, &QTextEdit::textChanged, this, [this] {
        const auto text = prompter_->text->toPlainText();
        if (script_->toPlainText() != text)
            replaceText(script_->document(), text);
    });
    connect(prompter_, &ReaderWindow::step, this, [this](int delta) { seekWord(tracker_.position() + delta); });
    connect(prompter_, &ReaderWindow::resetRequested, this, [this] { seekWord(-1); });
    connect(prompter_, &ReaderWindow::jumpRequested, this, [this](int character) {
        tracker_.seekCharacter(character);
        seekWord(tracker_.position());
    });
    connect(prompter_, &ReaderWindow::toggleRequested, this, &MainWindow::toggleRunning);
    connect(prompter_, &ReaderWindow::speedRequested, this, &MainWindow::changeSpeed);
    connect(prompter_, &ReaderWindow::modeRequested, this, &MainWindow::setMode);
    connect(prompter_, &ReaderWindow::hidden, this, [this] {
        // Hiding the reader always releases the microphone; showing it never resumes.
        if (running()) {
            pauseRunning();
            audioStatus_->setText(tr("Paused. The reader is hidden and the microphone is off."));
        }
    });

    connect(&speech_, &SpeechService::status, this, [this](const QString &message) {
        speechStatus_ = message;
        audioStatus_->setText(message);
        refreshRunning();
    });
    connect(&speech_, &SpeechService::listening, this,
            [this] { session_.start(tracker_.position(), QDateTime::currentMSecsSinceEpoch()); });
    connect(&speech_, &SpeechService::stopped, this, [this] {
        finishSession();
        backend_->setEnabled(true);
        language_->setEnabled(true);
        checkSystem_->setEnabled(true);
        refreshRunning();
    });
    connect(&speech_, &SpeechService::transcript, this, [this](const QString &text, bool final) {
        if (waitForNewUtterance_) {
            if (final) {
                waitForNewUtterance_ = false;
                refreshReading();
            }
            return;
        }
        const bool wasLost = tracker_.lost();
        if (tracker_.follow(text, final)) {
            session_.advance(tracker_.position(), QDateTime::currentMSecsSinceEpoch());
            refreshReading();
        }
        if (final) {
            session_.transcript(text);
            speech_.setScript(script_->toPlainText(), tracker_.position());
        }
        if (tracker_.lost() != wasLost)
            refreshRunning();
    });

    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this] {
        saveScript();
        audioStatus_->setText(tr("Script saved."));
    });
    auto *showShortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(showShortcut, &QShortcut::activated, this, &MainWindow::showPrompter);

    setMode(modeFromKey(QSettings().value("reading/mode", "voice").toString()));
    prompter_->setSpeed(wordsPerMinute_);
    refreshLibrary();
    const auto saved = QSettings().value("library/current").toString();
    if (library_.contains(saved))
        openScript(saved);
    else if (!library_.entries().isEmpty())
        openScript(library_.entries().first().id);
    else
        newScript();
}

MainWindow::~MainWindow() {
    saveScript();
    saveSettings();
    speech_.stop();
    disconnect(&speech_, nullptr, this, nullptr);
    disconnect(prompter_, nullptr, this, nullptr);
    delete prompter_;
}

// --- Script library ---------------------------------------------------------

void MainWindow::buildLibraryBar(QWidget *bar) {
    auto *layout = new FlowLayout(bar);
    scripts_ = new ScriptPicker;
    scripts_->setObjectName("scriptPicker");
    scripts_->setAccessibleName(tr("Script"));
    auto *add = new AccessibleButton(tr("New script"));
    add->setObjectName("newScript");
    auto *import = new AccessibleButton(tr("Import…"));
    import->setObjectName("importScript");
    import->setToolTip(tr("Text, Markdown, or speaker notes from PowerPoint (.pptx) and Impress (.odp)"));
    auto *exportButton = new AccessibleButton(tr("Export…"));
    auto *remove = new AccessibleButton(tr("Delete"));
    remove->setObjectName("deleteScript");
    for (auto *widget : QList<QWidget *>{scripts_, add, import, exportButton, remove})
        layout->addWidget(widget);
    connect(scripts_, &QComboBox::activated, this, [this] { openScript(scripts_->currentData().toString()); });
    connect(add, &QPushButton::clicked, this, &MainWindow::newScript);
    connect(import, &QPushButton::clicked, this, &MainWindow::importScript);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportScript);
    connect(remove, &QPushButton::clicked, this, &MainWindow::deleteScript);
}

void MainWindow::refreshLibrary() {
    const QSignalBlocker block(scripts_);
    scripts_->clear();
    for (const auto &entry : library_.entries())
        scripts_->addItem(entry.title, entry.id);
    scripts_->setCurrentIndex(std::max(0, scripts_->findData(currentId_)));
}

void MainWindow::openScript(const QString &id) {
    if (id == currentId_ && !id.isEmpty())
        return;
    saveScript();
    if (running())
        pauseRunning();
    // Read the saved position first: loading the text refreshes the reading state.
    const int character = library_.position(id);
    currentId_ = id;
    persist_ = true;
    QSettings().setValue("library/current", id);
    loading_ = true;
    script_->setPlainText(library_.load(id));
    loading_ = false;
    tracker_.setScript(script_->toPlainText());
    if (character >= 0)
        tracker_.seekCharacter(character);
    else
        tracker_.seek(-1);
    refreshLibrary();
    seekWord(tracker_.position());
}

void MainWindow::saveScript() {
    autosave_.stop();
    if (!persist_ || currentId_.isEmpty())
        return;
    const auto text = script_->toPlainText();
    if (text != library_.load(currentId_)) {
        if (!library_.save(currentId_, text)) {
            audioStatus_->setText(tr("Cannot save the script. Check the available disk space."));
            return;
        }
        const int index = scripts_->findData(currentId_);
        if (index >= 0)
            scripts_->setItemText(index, ScriptLibrary::titleFor(text));
    }
    library_.flush();
}

void MainWindow::newScript() {
    const auto id = library_.create();
    if (id.isEmpty()) {
        audioStatus_->setText(tr("Cannot create a script in %1.").arg(library_.directory()));
        return;
    }
    openScript(id);
    script_->setFocus();
}

void MainWindow::importScript() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Import script"), {}, ScriptImport::dialogFilter());
    if (path.isEmpty())
        return;
    const auto result = ScriptImport::read(path);
    if (!result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot import"), result.error);
        return;
    }
    const auto id = library_.create(result.text);
    if (id.isEmpty()) {
        audioStatus_->setText(tr("Cannot save the imported script."));
        return;
    }
    openScript(id);
    audioStatus_->setText(result.sections ? tr("Imported speaker notes from %n slide(s). Use Sections in the reader "
                                               "to jump between slides.",
                                               nullptr, result.sections)
                                          : tr("Script imported."));
}

void MainWindow::exportScript() {
    auto path = QFileDialog::getSaveFileName(this, tr("Export script"), ScriptLibrary::titleFor(script_->toPlainText()),
                                             tr("Text (*.txt);;Markdown (*.md)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const auto data = script_->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        QMessageBox::warning(this, tr("Cannot export"), tr("Check the destination and available disk space."));
    else
        audioStatus_->setText(tr("Exported to %1.").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::deleteScript() {
    if (!persist_ || currentId_.isEmpty())
        return;
    const auto title = ScriptLibrary::titleFor(script_->toPlainText());
    if (!script_->toPlainText().trimmed().isEmpty() &&
        QMessageBox::question(this, tr("Delete script?"), tr("Delete “%1” from this device?").arg(title),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const auto removed = currentId_;
    autosave_.stop();
    currentId_.clear();
    library_.remove(removed);
    const auto remaining = library_.entries();
    if (remaining.isEmpty())
        newScript();
    else
        openScript(remaining.first().id);
    audioStatus_->setText(tr("Deleted “%1”.").arg(title));
}

void MainWindow::setDemoScript() {
    // Demonstrations and screenshots never touch the user's library.
    saveScript();
    persist_ = false;
    loading_ = true;
    script_->setPlainText(tr("# Welcome\n\nThank you for joining today. [Smile]\n\n"
                             "Let us start with the question that matters most. What would make this project useful "
                             "to the people who depend on it?\n\n# Next steps\n\nWe can take this one step at a time. "
                             "[Pause] Listen carefully, check the question, and choose the words we want to share."));
    loading_ = false;
    {
        const QSignalBlocker block(scripts_);
        scripts_->clear();
        scripts_->addItem(tr("Sample script (not saved)"));
    }
    tracker_.setScript(script_->toPlainText());
    tracker_.seek(6);
    refreshReading();
}

// --- Reading ----------------------------------------------------------------

void MainWindow::startSteadyScrolling() {
    pauseRunning();
    mode_ = ReadingMode::Steady;
    prompter_->setMode(mode_);
    steady_.setInterval(60000 / wordsPerMinute_);
    steady_.start();
    refreshRunning();
}

void MainWindow::showPrompter() {
    const auto name = screen_->currentData().toString();
    QScreen *target = QGuiApplication::primaryScreen();
    for (auto *screen : QGuiApplication::screens())
        if (screen->name() == name)
            target = screen;
    prompter_->showOn(target);
    refreshReading();
}

void MainWindow::seekWord(int word) {
    tracker_.seek(word);
    speech_.setScript(script_->toPlainText(), tracker_.position());
    waitForNewUtterance_ = speech_.active();
    refreshReading();
}

void MainWindow::refreshReading() {
    prompter_->highlight(tracker_.character(), tracker_.length());
    if (persist_ && !currentId_.isEmpty())
        library_.setPosition(currentId_, tracker_.position() >= 0 ? tracker_.character() : -1);
    if (remote_ && remote_->active())
        remote_->setState({{"mode", modeKey(mode_)},
                           {"running", running()},
                           {"position", tracker_.position() + 1},
                           {"total", tracker_.count()},
                           {"speed", wordsPerMinute_}});
    if (waitForNewUtterance_) {
        position_->setText(tr("Voice following resumes after your next pause; the reading position is preserved."));
        return;
    }
    position_->setText(tr("Word %1 of %2 · Ctrl+Enter opens the reader · in the reader, arrow keys step and Space "
                          "starts or pauses")
                           .arg(tracker_.position() + 1)
                           .arg(tracker_.count()));
}

void MainWindow::setMode(ReadingMode mode) {
    if (running())
        pauseRunning();
    mode_ = mode;
    QSettings().setValue("reading/mode", modeKey(mode));
    prompter_->setMode(mode);
    refreshRunning();
}

bool MainWindow::running() const {
    if (mode_ == ReadingMode::Voice)
        return speech_.active();
    return steady_.isActive() || countdown_.isActive();
}

void MainWindow::toggleRunning() {
    if (running())
        pauseRunning();
    else
        startRunning();
}

void MainWindow::startRunning() {
    if (mode_ == ReadingMode::Manual)
        return;
    if (mode_ == ReadingMode::Voice)
        return startListening();
    if (tracker_.count() == 0)
        return;
    if (tracker_.position() >= tracker_.count() - 1)
        seekWord(-1);
    steady_.setInterval(60000 / wordsPerMinute_);
    countdownLeft_ = QSettings().value("reading/countdown", 3).toInt();
    if (countdownLeft_ > 0) {
        prompter_->showCountdown(countdownLeft_);
        countdown_.start();
    } else {
        steady_.start();
    }
    refreshRunning();
}

void MainWindow::pauseRunning() {
    countdown_.stop();
    countdownLeft_ = 0;
    prompter_->showCountdown(0);
    steady_.stop();
    if (speech_.active()) {
        speech_.stop();
        audioStatus_->setText(tr("Listening is off. No audio is being captured."));
    }
    refreshRunning();
}

void MainWindow::steadyTick() {
    if (tracker_.position() >= tracker_.count() - 1) {
        pauseRunning();
        prompter_->setStatus(tr("End of script"));
        return;
    }
    tracker_.seek(tracker_.position() + 1);
    refreshReading();
}

void MainWindow::changeSpeed(int delta) {
    wordsPerMinute_ = std::clamp(wordsPerMinute_ + delta, 40, 400);
    QSettings().setValue("reading/speed", wordsPerMinute_);
    prompter_->setSpeed(wordsPerMinute_);
    if (steady_.isActive())
        steady_.setInterval(60000 / wordsPerMinute_);
    refreshReading();
}

void MainWindow::refreshRunning() {
    const bool active = running();
    QString status;
    switch (mode_) {
    case ReadingMode::Manual:
        status = tr("Manual · microphone off");
        run_->setText(tr("Start listening"));
        break;
    case ReadingMode::Steady:
        status = countdown_.isActive() ? tr("Starting…")
                 : active              ? tr("Scrolling · microphone off")
                                       : tr("Paused · microphone off");
        run_->setText(active ? tr("Pause scrolling") : tr("Start scrolling"));
        break;
    case ReadingMode::Voice:
        status = !active                   ? tr("Microphone off")
                 : tracker_.lost()         ? tr("Off script · Kalipso catches up when you return to the text")
                 : speechStatus_.isEmpty() ? tr("Listening")
                                           : speechStatus_;
        run_->setText(active ? tr("Pause listening") : tr("Start listening"));
        break;
    }
    run_->setVisible(mode_ != ReadingMode::Manual);
    prompter_->setRunning(active, status);
    refreshReading();
}

void MainWindow::startListening() {
    saveSettings();
    systemCheck_.stop();
    installingSystem_ = false;
    waitForNewUtterance_ = false;
    speechStatus_.clear();
    speech_.setScript(script_->toPlainText(), tracker_.position());
    SpeechRequest request;
    request.backend = backend_->currentData().toString();
    request.library = speechLibrary_->text();
    request.model = modelPath_->text();
    request.microphone = request.backend == QLatin1String("system") ? QString() : microphone_->text();
    request.language = speechLanguage();
    request.architecture = architecture_->currentIndex() + 2;
    speech_.start(request);
    if (speech_.active()) {
        checkSystem_->setEnabled(false);
        installSystem_->setEnabled(false);
        backend_->setEnabled(false);
        language_->setEnabled(false);
    }
    refreshRunning();
}

void MainWindow::finishSession() {
    if (!session_.active())
        return;
    const auto summary = session_.finish(QDateTime::currentMSecsSinceEpoch());
    if (summary.words < 5)
        return;
    QString text = tr("Last session: %1 · %n word(s)", nullptr, summary.words).arg(duration(summary.durationMs));
    if (summary.wordsPerMinute > 0)
        text += " · " + tr("%1 words/min").arg(qRound(summary.wordsPerMinute));
    text += " · " + tr("%n pause(s) over 3 s", nullptr, summary.longPauses);
    if (summary.longPauses)
        text += " " + tr("(longest %1)").arg(duration(summary.longestPauseMs));
    if (summary.fillers)
        text += " · " + tr("%n filler word(s) recognized", nullptr, summary.fillers);
    audioStatus_->setText(text);
}

// --- Settings ---------------------------------------------------------------

QWidget *MainWindow::makeSettings() {
    auto *contents = new QWidget;
    auto *layout = new QVBoxLayout(contents);
    layout->addWidget(makeReadingSettings());
    layout->addWidget(makeSpeechSettings());
    layout->addWidget(makeRemoteSettings());
    for (auto *form : contents->findChildren<QFormLayout *>())
        for (int row = 0; row < form->rowCount(); ++row) {
            auto *labelItem = form->itemAt(row, QFormLayout::LabelRole);
            auto *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            if (!labelItem || !fieldItem)
                continue;
            auto *label = qobject_cast<QLabel *>(labelItem->widget());
            auto *input = fieldItem->widget();
            if (!label || !input)
                continue;
            label->setWordWrap(true);
            label->setMinimumWidth(0);
            label->setBuddy(input);
            input->setAccessibleName(label->text());
        }
    layout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(contents);
    scroll->setMinimumSize(0, 0);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    return scroll;
}

QWidget *MainWindow::makeReadingSettings() {
    QSettings settings;
    auto *group = new SettingsGroup(tr("Reading"));
    auto *form = formFor(group);

    screen_ = new SettingsComboBox;
    screen_->setObjectName("readerScreen");
    refreshScreens();
    connect(qApp, &QGuiApplication::screenAdded, this, &MainWindow::refreshScreens);
    connect(qApp, &QGuiApplication::screenRemoved, this, &MainWindow::refreshScreens);
    connect(screen_, &QComboBox::activated, this,
            [this] { QSettings().setValue("reading/screen", screen_->currentData().toString()); });
    form->addRow(tr("Monitor"), screen_);

    auto *size = new SettingsSpinBox;
    size->setObjectName("readerTextSize");
    size->setRange(18, 96);
    size->setValue(settings.value("reading/fontSize", 28).toInt());
    prompter_->setTextSize(size->value());
    connect(size, &QSpinBox::valueChanged, this, [this](int value) {
        prompter_->setTextSize(value);
        QSettings().setValue("reading/fontSize", value);
    });
    form->addRow(tr("Text size"), size);

    auto *line = new SettingsComboBox;
    line->setObjectName("readingLine");
    line->addItem(tr("Top"), 0.12);
    line->addItem(tr("Upper third"), 0.3);
    line->addItem(tr("Center"), 0.5);
    line->setCurrentIndex(std::max(0, line->findData(settings.value("reading/line", 0.3).toDouble())));
    prompter_->setReadingLine(line->currentData().toDouble());
    connect(line, &QComboBox::currentIndexChanged, this, [this, line] {
        prompter_->setReadingLine(line->currentData().toDouble());
        QSettings().setValue("reading/line", line->currentData());
    });
    form->addRow(tr("Current line position"), line);

    auto *mirror = new SettingsComboBox;
    mirror->setObjectName("readerMirror");
    mirror->addItem(tr("Off"), int(Mirror::None));
    mirror->addItem(tr("Horizontal (beam-splitter glass)"), int(Mirror::Horizontal));
    mirror->addItem(tr("Vertical"), int(Mirror::Vertical));
    mirror->addItem(tr("Horizontal and vertical"), int(Mirror::Both));
    mirror->setCurrentIndex(std::max(0, mirror->findData(settings.value("reading/mirror", 0).toInt())));
    prompter_->setMirror(Mirror(mirror->currentData().toInt()));
    connect(mirror, &QComboBox::currentIndexChanged, this, [this, mirror] {
        prompter_->setMirror(Mirror(mirror->currentData().toInt()));
        QSettings().setValue("reading/mirror", mirror->currentData());
    });
    form->addRow(tr("Mirror text"), mirror);

    wordsPerMinute_ = std::clamp(settings.value("reading/speed", 140).toInt(), 40, 400);
    auto *countdown = new SettingsComboBox;
    countdown->setObjectName("readerCountdown");
    countdown->addItem(tr("Off"), 0);
    countdown->addItem(tr("3 seconds"), 3);
    countdown->addItem(tr("5 seconds"), 5);
    countdown->setCurrentIndex(std::max(0, countdown->findData(settings.value("reading/countdown", 3).toInt())));
    connect(countdown, &QComboBox::currentIndexChanged, this,
            [countdown] { QSettings().setValue("reading/countdown", countdown->currentData()); });
    form->addRow(tr("Countdown before steady scrolling"), countdown);

    auto *appearance = new SettingsComboBox;
    appearance->setObjectName("applicationTheme");
    appearance->addItem(tr("Light"), "light");
    appearance->addItem(tr("Dark"), "dark");
    const auto savedTheme = settings.value("appearance/theme", "light").toString();
    appearance->setCurrentIndex(savedTheme == "dark" ? 1 : 0);
    Accessibility::apply(this, savedTheme == "dark" ? Design::dark : Design::light);
    connect(appearance, &QComboBox::currentIndexChanged, this, [this, appearance] {
        const auto name = appearance->currentData().toString();
        Accessibility::apply(this, name == "dark" ? Design::dark : Design::light);
        QSettings().setValue("appearance/theme", name);
    });
    form->addRow(tr("Application appearance"), appearance);

    auto *readerTheme = new SettingsComboBox;
    readerTheme->setObjectName("readerTheme");
    readerTheme->addItem(tr("Dark"), "dark");
    readerTheme->addItem(tr("Light"), "light");
    readerTheme->addItem(tr("Automatic screen colors"), "auto");
    const auto savedReader = settings.value("reading/theme", "dark").toString();
    readerTheme->setCurrentIndex(std::max(0, readerTheme->findData(savedReader)));
    prompter_->setReaderTheme(savedReader);
    connect(readerTheme, &QComboBox::currentIndexChanged, this, [this, readerTheme] {
        const auto theme = readerTheme->currentData().toString();
        prompter_->setReaderTheme(theme);
        QSettings().setValue("reading/theme", theme);
    });
    form->addRow(tr("Reader appearance"), readerTheme);
    auto *colorSource = new StatusLabel(prompter_->colorSource());
    form->addRow(colorSource);
    connect(prompter_, &ReaderWindow::colorStatus, colorSource, &StatusLabel::setText);
    if (prompter_->usesWindowPortal()) {
        form->addRow(note(tr("Follow the colors of a window you select, even while the reader covers it. Sharing "
                             "stays active while the reader is visible. Images stay in memory and are never saved "
                             "or sent. You can stop sharing at any time.")));
        auto *choose = new AccessibleButton(tr("Choose background window…"));
        choose->setObjectName("chooseBackgroundWindow");
        auto *stop = new AccessibleButton(tr("Stop window sharing"));
        stop->setObjectName("stopWindowSharing");
        form->addRow(choose);
        form->addRow(stop);
        connect(choose, &QPushButton::clicked, this, [this, readerTheme] {
            readerTheme->setCurrentIndex(readerTheme->findData("auto"));
            showPrompter();
            prompter_->chooseBackgroundWindow();
        });
        connect(stop, &QPushButton::clicked, prompter_, &ReaderWindow::stopWindowSharing);
    }

    auto *scale = new SettingsComboBox;
    scale->setObjectName("interfaceTextSize");
    scale->addItem("100%", 1.0);
    scale->addItem("150%", 1.5);
    scale->addItem("200%", 2.0);
    form->addRow(tr("Interface text size"), scale);
    connect(scale, &QComboBox::currentIndexChanged, this, [this, scale] {
        const double value = scale->currentData().toDouble();
        applyInterfaceScale(value);
        QSettings().setValue("appearance/textScale", value);
    });
    const double savedScale = settings.value("appearance/textScale", 1.0).toDouble();
    if (savedScale != 1.0)
        QTimer::singleShot(0, this, [scale, savedScale] {
            const int index = scale->findData(savedScale);
            if (index >= 0 && scale->currentIndex() == 0)
                scale->setCurrentIndex(index);
        });

    auto *language = new SettingsComboBox;
    language->setObjectName("interfaceLanguage");
    language->addItem(tr("System language"), QString());
    language->addItem("English", "en");
    language->addItem("Italiano", "it");
    language->setCurrentIndex(std::max(0, language->findData(settings.value("appearance/language").toString())));
    connect(language, &QComboBox::currentIndexChanged, this, [this, language] {
        QSettings().setValue("appearance/language", language->currentData());
        audioStatus_->setText(tr("The interface language changes the next time Kalipso starts."));
    });
    form->addRow(tr("Interface language"), language);

    auto *hiddenRow = new WrappingCheckBox(tr("Hide the reader from screen sharing and recordings"));
    auto *hidden = hiddenRow->box;
    hidden->setObjectName("excludeFromCapture");
    if (CaptureExclusion::supported()) {
        hidden->setChecked(settings.value("reading/excludeFromCapture", true).toBool());
        prompter_->setCaptureExcluded(hidden->isChecked());
        connect(hidden, &QCheckBox::toggled, this, [this](bool checked) {
            prompter_->setCaptureExcluded(checked);
            QSettings().setValue("reading/excludeFromCapture", checked);
        });
        form->addRow(hiddenRow);
        form->addRow(note(tr("Screen sharing apps that use the system capture service leave the reader out. "
                             "Check the view from another participant before relying on it.")));
    } else {
        hiddenRow->deleteLater();
        form->addRow(note(tr("Screen sharing may include the reader. On KDE Plasma, the optional Kalipso KWin "
                             "script asks KWin to leave it out. Check the view from another participant.")));
    }
    return group;
}

void MainWindow::refreshScreens() {
    const auto saved = QSettings().value("reading/screen").toString();
    const QSignalBlocker block(screen_);
    screen_->clear();
    for (auto *screen : QGuiApplication::screens()) {
        auto label = screen->name();
        if (!screen->model().isEmpty())
            label = screen->model() + " (" + screen->name() + ")";
        if (screen == QGuiApplication::primaryScreen())
            label = tr("%1 · primary").arg(label);
        screen_->addItem(label, screen->name());
    }
    const int index = screen_->findData(saved);
    screen_->setCurrentIndex(index >= 0 ? index
                                        : std::max(0, screen_->findData(QGuiApplication::primaryScreen()
                                                                            ? QGuiApplication::primaryScreen()->name()
                                                                            : QString())));
}

void MainWindow::applyInterfaceScale(double scale) {
    // Scales controls in both windows; the script's own size is set separately.
    auto widgets = findChildren<QWidget *>();
    for (auto *child : prompter_->findChildren<QWidget *>())
        if (child != prompter_->text && !prompter_->text->isAncestorOf(child))
            widgets.append(child);
    for (auto *widget : widgets) {
        if (!widget->property("baseAccessibleFont").isValid())
            widget->setProperty("baseAccessibleFont", widget->font());
        auto font = widget->property("baseAccessibleFont").value<QFont>();
        font.setPointSizeF(std::max(8.0, font.pointSizeF()) * scale);
        widget->setFont(font);
    }
}

QWidget *MainWindow::makeSpeechSettings() {
    QSettings settings;
    auto *group = new SettingsGroup(tr("Speech"));
    auto *form = formFor(group);
    backend_ = new SettingsComboBox;
    backend_->setObjectName("speechBackend");
    if (SystemSpeechClient::platformSupported())
        backend_->addItem(tr("System · on-device"), "system");
    backend_->addItem("Moonshine", "moonshine");
    backend_->addItem("Vosk", "vosk");
    language_ = new SettingsComboBox;
    language_->setObjectName("speechLanguage");
    language_->setEditable(true);
    language_->addItem(tr("Choose…"), QString());
    const QList<QPair<QString, QString>> languages{
        {"English (US)", "en-US"}, {"Italiano", "it-IT"}, {"Deutsch", "de-DE"}, {"Español", "es-ES"},
        {"Français", "fr-FR"},     {"日本語", "ja-JP"},   {"中文", "zh-CN"}};
    for (const auto &[label, code] : languages)
        language_->addItem(label, code);
    installed_ = new SettingsComboBox;
    installed_->setObjectName("installedSpeech");
    speechLibrary_ = new QLineEdit;
    speechLibrary_->setObjectName("speechLibrary");
    modelPath_ = new QLineEdit;
    modelPath_->setObjectName("speechModel");
    architecture_ = new SettingsComboBox;
    architecture_->addItems({tr("Tiny"), tr("Base"), tr("Small"), tr("Medium")});
    microphone_ = new QLineEdit(settings.value("speech/microphone").toString());
    microphone_->setPlaceholderText(tr("Default microphone"));

    form->addRow(tr("Speech engine"), backend_);
    form->addRow(tr("Installed language model"), installed_);
    // Library and model paths are only needed for models installed by hand.
    customSpeech_ = new QWidget;
    auto *custom = new QFormLayout(customSpeech_);
    custom->setContentsMargins(0, 0, 0, 0);
    custom->setRowWrapPolicy(QFormLayout::WrapAllRows);
    custom->addRow(tr("Native library"), speechLibrary_);
    custom->addRow(tr("Model folder"), modelPath_);
    auto *browse = new AccessibleButton(tr("Choose model folder…"));
    custom->addRow(browse);
    form->addRow(customSpeech_);
    form->addRow(tr("Language"), language_);
    form->addRow(tr("Moonshine model size"), architecture_);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Choose speech model"));
        if (!path.isEmpty())
            modelPath_->setText(path);
    });

    auto *systemActions = new QWidget;
    auto *systemLayout = new FlowLayout(systemActions);
    checkSystem_ = new AccessibleButton(tr("Check availability"));
    installSystem_ = new AccessibleButton(tr("Install system language"));
    installSystem_->setEnabled(false);
    systemLayout->addWidget(checkSystem_);
    systemLayout->addWidget(installSystem_);
#ifndef Q_OS_MACOS
    installSystem_->hide();
#endif
    systemStatus_ = note(tr("Choose a language and check its availability. No microphone is opened by this check."));
    systemStatus_->setObjectName("systemSpeechStatus");
    form->addRow(systemActions);
    form->addRow(systemStatus_);
    form->addRow(tr("Microphone"), microphone_);
    form->addRow(note(tr("Recognition stays on this device. Moonshine and Vosk need a native runtime and a language "
                         "model: install them with tools/install-local-engine.py and they appear above. System "
                         "speech uses the default microphone. Kalipso never switches to a cloud service.")));

    const auto updateFields = [this, form, systemActions] {
        const auto backend = backend_->currentData().toString();
        const bool system = backend == QLatin1String("system");
        const bool custom = installed_->currentData().toString().isEmpty();
        form->setRowVisible(installed_, !system);
        customSpeech_->setVisible(!system && custom);
        form->setRowVisible(microphone_, !system);
        form->setRowVisible(architecture_, backend == QLatin1String("moonshine") && custom);
        systemActions->setVisible(system);
        systemStatus_->setVisible(system);
    };
    const auto initial = settings
                             .value("speech/backend", settings.contains("speech/model")         ? "vosk"
                                                      : SystemSpeechClient::platformSupported() ? "system"
                                                                                                : "moonshine")
                             .toString();
    backend_->setCurrentIndex(std::max(0, backend_->findData(initial)));
    profile_ = backend_->currentData().toString();
    loadSpeechProfile();
    refreshInstalledSpeech();
    updateFields();

    connect(backend_, &QComboBox::currentIndexChanged, this, [this, updateFields] {
        systemCheck_.stop();
        installingSystem_ = false;
        saveSpeechProfile();
        profile_ = backend_->currentData().toString();
        loadSpeechProfile();
        refreshInstalledSpeech();
        updateFields();
        systemStatus_->setText(tr("Choose a language and check its availability."));
        checkSystem_->setEnabled(true);
        installSystem_->setEnabled(false);
    });
    connect(installed_, &QComboBox::activated, this, [this, updateFields] {
        const auto model = installed_->currentData().toString();
        for (const auto &item : installedSpeech())
            if (item.model == model) {
                speechLibrary_->setText(item.library);
                modelPath_->setText(item.model);
                architecture_->setCurrentIndex(0); // The catalog installs Tiny streaming models.
                if (!item.language.isEmpty()) {
                    const QSignalBlocker block(language_);
                    language_->setEditText(item.language);
                }
            }
        saveSpeechProfile();
        updateFields();
    });
    connect(language_, &QComboBox::currentTextChanged, this, [this] {
        systemCheck_.stop();
        installingSystem_ = false;
        installSystem_->setEnabled(false);
        checkSystem_->setEnabled(true);
        systemStatus_->setText(tr("Check availability for the selected language."));
    });
    connect(checkSystem_, &QPushButton::clicked, this, [this] {
        systemStatus_->setText(tr("Checking installed system speech…"));
        systemCheck_.check(speechLanguage());
    });
    connect(installSystem_, &QPushButton::clicked, this, [this] {
        installingSystem_ = true;
        installSystem_->setEnabled(false);
        checkSystem_->setEnabled(false);
        systemCheck_.install(speechLanguage());
    });
    connect(&systemCheck_, &SystemSpeechClient::capabilities, this, [this](const QJsonObject &result) {
        systemStatus_->setText(result.value("message").toString());
        installSystem_->setEnabled(result.value("supported").toBool() && !result.value("installed").toBool());
        for (const auto &locale : result.value("locales").toArray())
            if (language_->findData(locale.toString()) < 0)
                language_->addItem(locale.toString(), locale.toString());
    });
    connect(&systemCheck_, &SystemSpeechClient::message, systemStatus_, &QLabel::setText);
    connect(&systemCheck_, &SystemSpeechClient::failed, this, [this](const QString &message) {
        installingSystem_ = false;
        systemStatus_->setText(message);
        checkSystem_->setEnabled(true);
    });
    connect(&systemCheck_, &SystemSpeechClient::completed, this, [this] {
        checkSystem_->setEnabled(true);
        if (installingSystem_) {
            installingSystem_ = false;
            systemCheck_.check(speechLanguage());
        }
    });
    return group;
}

void MainWindow::refreshInstalledSpeech() {
    const QSignalBlocker block(installed_);
    installed_->clear();
    const auto backend = backend_->currentData().toString();
    for (const auto &item : installedSpeech())
        if (item.backend == backend)
            installed_->addItem(item.label(), item.model);
    installed_->addItem(tr("Other model installed by hand…"), QString());
    // A configured profile selects its model; a fresh profile uses the first install.
    int index = installed_->findData(modelPath_->text());
    if (index < 0 && modelPath_->text().isEmpty() && installed_->count() > 1) {
        index = 0;
        for (const auto &item : installedSpeech())
            if (item.model == installed_->itemData(0).toString()) {
                speechLibrary_->setText(item.library);
                modelPath_->setText(item.model);
                if (language_->currentText().isEmpty() || language_->currentIndex() == 0)
                    language_->setEditText(item.language);
            }
    }
    installed_->setCurrentIndex(index >= 0 ? index : installed_->count() - 1);
}

QWidget *MainWindow::makeRemoteSettings() {
    auto *group = new SettingsGroup(tr("Remote control"));
    auto *form = formFor(group);
    form->addRow(note(tr("Foot pedals and presentation clickers work while the reader is focused: arrow keys and "
                         "Page Up/Down step, Space starts or pauses, + and − change the steady speed, Home returns "
                         "to the start.")));
    auto *enableRow = new WrappingCheckBox(tr("Allow a phone on this network to control the reader"));
    auto *enable = enableRow->box;
    enable->setObjectName("remoteControl");
    auto *link = new QLabel;
    link->setObjectName("remoteLink");
    link->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    link->setWordWrap(true);
    auto *code = new QLabel;
    code->setObjectName("remoteCode");
    code->setAccessibleName(tr("QR code for the remote control link"));
    form->addRow(enableRow);
    form->addRow(link);
    form->addRow(code);
    form->addRow(note(tr("Anyone on this network who has the link can start, pause and step the reader; the script "
                         "text is never shared. A new link is created each time you turn this on, and it stops when "
                         "Kalipso closes.")));
    link->hide();
    code->hide();
    remote_ = new RemoteControl(this);
    connect(remote_, &RemoteControl::command, this, [this](const QString &name) {
        if (name == QLatin1String("toggle"))
            toggleRunning();
        else if (name == QLatin1String("next"))
            seekWord(tracker_.position() + 1);
        else if (name == QLatin1String("previous"))
            seekWord(tracker_.position() - 1);
        else if (name == QLatin1String("faster"))
            changeSpeed(10);
        else if (name == QLatin1String("slower"))
            changeSpeed(-10);
        else if (name == QLatin1String("reset"))
            seekWord(-1);
    });
    connect(enable, &QCheckBox::toggled, this, [this, link, code, enable](bool on) {
        if (on && !remote_->start()) {
            const QSignalBlocker block(enable);
            enable->setChecked(false);
            link->setText(tr("Remote control could not start on this network."));
            link->show();
            return;
        }
        if (!on)
            remote_->stop();
        link->setVisible(on);
        code->setVisible(on);
        if (on) {
            link->setText(tr("Open on your phone: %1").arg(remote_->url()));
            code->setPixmap(QPixmap::fromImage(RemoteControl::qrCode(remote_->url())));
            refreshReading();
        }
    });
    return group;
}

QString MainWindow::speechLanguage() const {
    if (language_->currentIndex() >= 0 && language_->currentText() == language_->itemText(language_->currentIndex()))
        return language_->currentData().toString();
    return language_->currentText().trimmed();
}

void MainWindow::saveSpeechProfile() {
    if (profile_.isEmpty())
        return;
    QSettings settings;
    const auto prefix = "profiles/" + profile_ + "/";
    settings.setValue(prefix + "library", speechLibrary_->text());
    settings.setValue(prefix + "model", modelPath_->text());
    settings.setValue(prefix + "language", speechLanguage());
    settings.setValue(prefix + "architecture", architecture_->currentIndex() + 2);
}

void MainWindow::loadSpeechProfile() {
    QSettings settings;
    const auto prefix = "profiles/" + profile_ + "/";
    QString defaultLibrary = profile_ == QLatin1String("moonshine") ? "libmoonshine" : "libvosk";
#ifdef Q_OS_WIN
    defaultLibrary = (profile_ == QLatin1String("moonshine") ? QString("moonshine") : QString("vosk")) + ".dll";
#elif defined(Q_OS_MACOS)
    defaultLibrary += ".dylib";
#else
    defaultLibrary += ".so";
#endif
    const QVariant legacyLibrary =
        profile_ == QLatin1String("vosk") ? settings.value("speech/library", defaultLibrary) : QVariant(defaultLibrary);
    speechLibrary_->setText(settings.value(prefix + "library", legacyLibrary).toString());
    modelPath_->setText(
        settings
            .value(prefix + "model", profile_ == QLatin1String("vosk") ? settings.value("speech/model") : QVariant())
            .toString());
    const auto language = settings.value(prefix + "language").toString();
    const QSignalBlocker block(language_);
    const int index = language_->findData(language);
    if (index >= 0)
        language_->setCurrentIndex(index);
    else
        language_->setEditText(language);
    architecture_->setCurrentIndex(std::clamp(settings.value(prefix + "architecture", 2).toInt() - 2, 0, 3));
}

void MainWindow::saveSettings() {
    QSettings settings;
    saveSpeechProfile();
    settings.setValue("speech/backend", backend_->currentData().toString());
    settings.setValue("speech/microphone", microphone_->text());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveScript();
    pauseRunning();
    prompter_->hide();
    saveSettings();
    event->accept();
}
