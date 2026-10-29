// SPDX-License-Identifier: AGPL-3.0-or-later
#include "reader.h"
#include "platform/capture_exclusion.h"
#include "script_highlighter.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSizeGrip>
#include <QTextBlock>
#include <QTextFrame>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace {
bool horizontal(Mirror mirror) { return mirror == Mirror::Horizontal || mirror == Mirror::Both; }
bool vertical(Mirror mirror) { return mirror == Mirror::Vertical || mirror == Mirror::Both; }
bool wayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }

// The reader has no title bar: dragging its name moves the window. Works on
// Wayland too, where applications cannot position their own windows.
class DragHandle : public QLabel {
public:
    explicit DragHandle(const QString &text) : QLabel(text) {
        setCursor(Qt::SizeAllCursor);
        setToolTip(QObject::tr("Drag to move the reader"));
        setWordWrap(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        QLabel::mousePressEvent(event);
    }
};
} // namespace

ReaderView::ReaderView(QWidget *parent)
    : QTextEdit(parent), highlighter_(new ScriptHighlighter(document())),
      scroll_(new QPropertyAnimation(verticalScrollBar(), "value", this)) {
    setObjectName("readingScript");
    setReadOnly(true);
    setAcceptRichText(false);
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAccessibleName(tr("Kalipso script"));
    // Edits are mirrored into the editor, which owns the undo history.
    document()->setUndoRedoEnabled(false);
    scroll_->setDuration(220);
    scroll_->setEasingCurve(QEasingCurve::OutCubic);
}

void ReaderView::setMirror(Mirror mirror) {
    mirror_ = mirror;
    scrollToCharacter(anchor_, false);
    viewport()->update();
}

void ReaderView::setReadingLine(double fraction) {
    readingLine_ = std::clamp(fraction, 0.0, 0.9);
    QResizeEvent event(size(), size());
    resizeEvent(&event);
}

void ReaderView::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    // Leave room after the last line so it can reach the reading line.
    const QSignalBlocker block(this);
    auto format = document()->rootFrame()->frameFormat();
    const double bottom = viewport()->height() * (1.0 - readingLine_);
    if (std::abs(format.bottomMargin() - bottom) > 1) {
        format.setBottomMargin(bottom);
        document()->rootFrame()->setFrameFormat(format);
    }
    scrollToCharacter(anchor_, false);
}

void ReaderView::scrollToCharacter(int character, bool animate) {
    anchor_ = character;
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(character, 0, std::max(0, document()->characterCount() - 1)));
    const QRect line = cursorRect(cursor);
    const int center = line.center().y() + verticalScrollBar()->value();
    const double fraction = vertical(mirror_) ? 1.0 - readingLine_ : readingLine_;
    const int target = std::clamp(center - int(viewport()->height() * fraction), verticalScrollBar()->minimum(),
                                  verticalScrollBar()->maximum());
    scroll_->stop();
    if (!animate || std::abs(target - verticalScrollBar()->value()) > viewport()->height()) {
        verticalScrollBar()->setValue(target);
        return;
    }
    scroll_->setStartValue(verticalScrollBar()->value());
    scroll_->setEndValue(target);
    scroll_->start();
}

QPoint ReaderView::unmirrored(QPoint point) const {
    const auto area = viewport()->rect();
    if (horizontal(mirror_))
        point.setX(area.width() - 1 - point.x());
    if (vertical(mirror_))
        point.setY(area.height() - 1 - point.y());
    return point;
}

void ReaderView::paintEvent(QPaintEvent *event) {
    if (mirror_ == Mirror::None)
        return QTextEdit::paintEvent(event);
    // Draw the document through a flipped painter; the viewport background is
    // already filled with the reader palette.
    QPainter painter(viewport());
    const auto area = viewport()->rect();
    painter.translate(horizontal(mirror_) ? area.width() : 0, vertical(mirror_) ? area.height() : 0);
    painter.scale(horizontal(mirror_) ? -1 : 1, vertical(mirror_) ? -1 : 1);
    const int dx = horizontalScrollBar()->value(), dy = verticalScrollBar()->value();
    painter.translate(-dx, -dy);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    // Style sheets restyle the widget's own paint path only; use the reader colors.
    context.palette.setColor(QPalette::Text, textColor_);
    context.clip = QRectF(dx, dy, area.width(), area.height());
    for (const auto &selection : extraSelections())
        context.selections.append({selection.cursor, selection.format});
    document()->documentLayout()->draw(&painter, context);
}

void ReaderView::mouseReleaseEvent(QMouseEvent *event) {
    QTextEdit::mouseReleaseEvent(event);
    if (isReadOnly() && event->button() == Qt::LeftButton && !textCursor().hasSelection())
        emit characterClicked(cursorForPosition(unmirrored(event->position().toPoint())).position());
}

ReaderWindow::ReaderWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    // Fixed title: the KWin script and the macOS color sampler identify the reader by it.
    setWindowTitle(QStringLiteral("Kalipso — reading"));
    setAccessibleName(tr("Kalipso reader"));
    setObjectName("readingWindow");
    resize(680, 260);
    setMinimumSize(320, 180);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 12);
    auto *bar = new FlowLayout;
    auto *title = new DragHandle(tr("Kalipso"));
    sections_ = new QToolButton;
    sections_->setText(tr("Sections"));
    sections_->setObjectName("readerSections");
    sections_->setAccessibleName(tr("Jump to section"));
    sections_->setPopupMode(QToolButton::InstantPopup);
    sections_->setMenu(new QMenu(sections_));
    sections_->hide();
    auto *previous = new AccessibleButton(tr("Previous"));
    auto *next = new AccessibleButton(tr("Next"));
    edit_ = new AccessibleButton(tr("Edit"));
    edit_->setCheckable(true);
    auto *hide = new AccessibleButton(tr("Hide"));
    for (auto *widget : QList<QWidget *>{title, sections_, previous, next, edit_, hide})
        bar->addWidget(widget);
    layout->addLayout(bar);

    text = new ReaderView;
    text->setPlaceholderText(tr("Add your script in the editor to begin."));
    text->installEventFilter(this);
    layout->addWidget(text, 1);
    countdown_ = new QLabel(text);
    countdown_->setObjectName("readerCountdown");
    countdown_->setAlignment(Qt::AlignCenter);
    countdown_->setAttribute(Qt::WA_TransparentForMouseEvents);
    countdown_->hide();

    auto *controls = new FlowLayout;
    primary_ = new AccessibleButton(tr("Start listening"));
    primary_->setObjectName("readerListeningButton");
    mode_ = new QComboBox;
    mode_->setObjectName("readerMode");
    mode_->setAccessibleName(tr("Reading mode"));
    mode_->addItem(tr("Manual"), int(ReadingMode::Manual));
    mode_->addItem(tr("Steady speed"), int(ReadingMode::Steady));
    mode_->addItem(tr("Follow my voice"), int(ReadingMode::Voice));
    speedBox_ = new QWidget;
    auto *speedLayout = new QHBoxLayout(speedBox_);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    auto *slower = new AccessibleButton(tr("Slower"));
    auto *faster = new AccessibleButton(tr("Faster"));
    speed_ = new QLabel;
    speed_->setObjectName("readerSpeed");
    speedLayout->addWidget(slower);
    speedLayout->addWidget(speed_);
    speedLayout->addWidget(faster);
    status_ = new StatusLabel(tr("Microphone off"));
    status_->setObjectName("readerListeningStatus");
    for (auto *widget : QList<QWidget *>{primary_, mode_, speedBox_, status_})
        controls->addWidget(widget);
    layout->addLayout(controls);
    auto *grip = new QSizeGrip(this);
    grip->setObjectName("readerResize");
    grip->setToolTip(tr("Drag to resize the reader"));

    connect(previous, &QPushButton::clicked, this, [this] { emit step(-1); });
    connect(next, &QPushButton::clicked, this, [this] { emit step(1); });
    connect(hide, &QPushButton::clicked, this, &QWidget::hide);
    connect(primary_, &QPushButton::clicked, this, &ReaderWindow::toggleRequested);
    connect(slower, &QPushButton::clicked, this, [this] { emit speedRequested(-10); });
    connect(faster, &QPushButton::clicked, this, [this] { emit speedRequested(10); });
    connect(mode_, &QComboBox::activated, this,
            [this] { emit modeRequested(ReadingMode(mode_->currentData().toInt())); });
    connect(text, &ReaderView::characterClicked, this, &ReaderWindow::jumpRequested);
    connect(edit_, &QPushButton::toggled, this, [this](bool enabled) {
        editing_ = enabled;
        text->setReadOnly(!enabled);
        // Scroll bars only while editing; reading follows the current line.
        text->setVerticalScrollBarPolicy(enabled ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        text->style()->unpolish(text); // The style sheet depends on readOnly.
        text->style()->polish(text);
        edit_->setText(enabled ? tr("Done") : tr("Edit"));
        if (enabled)
            text->setFocus();
        else
            highlight(position_, length_);
    });

    backdrop_ = new Backdrop(this);
    connect(backdrop_, &Backdrop::unavailable, this, &ReaderWindow::colorStatus);
    connect(backdrop_, &Backdrop::colorChanged, this, [this](const QColor &sample) {
        // Hysteresis: switch only on a clear change and at most every 2.5 s.
        const auto luminance = Accessibility::luminance(sample);
        const QString next = luminance > 0.35 ? "dark" : luminance < 0.15 ? "light" : resolvedTheme_;
        if (next == resolvedTheme_ || (paletteAge_.isValid() && paletteAge_.elapsed() < 2500))
            return;
        resolvedTheme_ = next;
        paletteAge_.restart();
        applyPalette(next == "light" ? Design::light : Design::dark);
    });
    setReaderTheme("dark");
    setTextSize(28);
    setMode(ReadingMode::Voice);
    setCaptureExcluded(false);
}

void ReaderWindow::applyPalette(const Design::Palette &palette) {
    colors_ = palette;
    Accessibility::apply(this, palette);
    text->setPalette(Accessibility::palette(palette));
    text->setTextColor(QColor(palette.text));
    text->highlighter()->setColors(QColor(palette.muted), QColor(palette.accent));
    countdown_->setStyleSheet(
        QString("color:%1; background:%2; border-radius:12px;").arg(palette.text, palette.surface));
    highlight(position_, length_);
}

void ReaderWindow::setReaderTheme(const QString &theme) {
    resolvedTheme_ = theme == "light" ? "light" : "dark";
    paletteAge_.invalidate();
    applyPalette(resolvedTheme_ == "light" ? Design::light : Design::dark);
    backdrop_->setEnabled(theme == "auto");
}

void ReaderWindow::setTextSize(int points) {
    auto font = text->font();
    font.setPointSize(points);
    text->setFont(font);
    QFont large = countdown_->font();
    large.setPointSize(points * 3);
    large.setBold(true);
    countdown_->setFont(large);
}

void ReaderWindow::setMode(ReadingMode mode) {
    readingMode_ = mode;
    mode_->setCurrentIndex(mode_->findData(int(mode)));
    speedBox_->setVisible(mode == ReadingMode::Steady);
    updatePrimary();
}

void ReaderWindow::setRunning(bool running, const QString &status) {
    running_ = running;
    updatePrimary();
    setStatus(status);
}

void ReaderWindow::setStatus(const QString &status) { status_->setText(status); }

void ReaderWindow::updatePrimary() {
    primary_->setVisible(readingMode_ != ReadingMode::Manual);
    if (readingMode_ == ReadingMode::Steady)
        primary_->setText(running_ ? tr("Pause scrolling") : tr("Start scrolling"));
    else
        primary_->setText(running_ ? tr("Pause listening") : tr("Start listening"));
}

void ReaderWindow::setSpeed(int wordsPerMinute) {
    speed_->setText(tr("%1 words/min").arg(wordsPerMinute));
    speed_->setAccessibleName(speed_->text());
}

void ReaderWindow::setSections(const QVector<Section> &sections) {
    auto *menu = sections_->menu();
    menu->clear();
    for (const auto &section : sections) {
        auto *action = menu->addAction(section.title);
        connect(action, &QAction::triggered, this, [this, start = section.start] { emit jumpRequested(start); });
    }
    sections_->setVisible(!sections.isEmpty());
}

void ReaderWindow::setCaptureExcluded(bool excluded) {
    captureExcluded_ = excluded;
    if (isVisible())
        CaptureExclusion::apply(this, excluded);
    setToolTip(excluded ? tr("Kalipso asked the system to hide this reader from screen sharing. Check the view "
                             "from another participant.")
                        : tr("Screen sharing may include this reader."));
}

void ReaderWindow::showCountdown(int seconds) {
    countdown_->setVisible(seconds > 0);
    if (seconds <= 0)
        return;
    countdown_->setText(QString::number(seconds));
    countdown_->adjustSize();
    const int side = std::max(countdown_->width(), countdown_->height()) + 32;
    countdown_->setGeometry(
        QRect(QPoint(0, 0), QSize(side, side)).translated(text->rect().center() - QPoint(side, side) / 2));
    countdown_->raise();
}

void ReaderWindow::highlight(int position, int length) {
    position_ = position;
    length_ = length;
    const int limit = std::max(0, text->document()->characterCount() - 1);
    QTextEdit::ExtraSelection read;
    read.cursor = QTextCursor(text->document());
    read.cursor.setPosition(std::clamp(position, 0, limit), QTextCursor::KeepAnchor);
    read.format.setForeground(QColor(colors_.muted));
    QTextEdit::ExtraSelection current;
    current.cursor = QTextCursor(text->document());
    current.cursor.setPosition(std::clamp(position, 0, limit));
    current.cursor.setPosition(std::clamp(position + length, 0, limit), QTextCursor::KeepAnchor);
    current.format.setBackground(QColor(colors_.selection));
    current.format.setForeground(QColor(colors_.selectionText));
    current.format.setFontUnderline(true);
    text->setExtraSelections({read, current});
    if (!editing_)
        text->scrollToCharacter(position, isVisible());
}

void ReaderWindow::showOn(QScreen *screen) {
    screen_ = screen ? screen : QGuiApplication::primaryScreen();
    if (screen_) {
        placing_ = true;
        winId();
        windowHandle()->setScreen(screen_);
        const auto available = screen_->availableGeometry();
        const auto saved = QSettings().value("reader/geometry/" + screen_->name()).toRect();
        if (saved.isValid() && available.contains(saved.center()) && !wayland()) {
            setGeometry(saved);
        } else {
            const int extra =
                std::max(0, qRound((primary_->font().pointSizeF() - QApplication::font().pointSizeF()) * 12));
            resize(std::min(680, std::max(320, available.width() - 32)),
                   std::min(available.height() - 32, 260 + extra));
            move(available.center().x() - width() / 2, available.top() + 16);
        }
        placing_ = false;
    }
    show();
    raise();
    activateWindow();
}

void ReaderWindow::saveGeometry() {
    if (placing_ || !isVisible() || !screen_ || wayland())
        return;
    QSettings().setValue("reader/geometry/" + screen_->name(), geometry());
}

void ReaderWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (captureExcluded_ || CaptureExclusion::supported())
        CaptureExclusion::apply(this, captureExcluded_);
}

void ReaderWindow::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    emit hidden();
}

void ReaderWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    saveGeometry();
}

void ReaderWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (auto *grip = findChild<QSizeGrip *>("readerResize")) {
        grip->adjustSize();
        grip->move(width() - grip->width() - 2, height() - grip->height() - 2);
        grip->raise();
    }
    if (countdown_->isVisible())
        showCountdown(countdown_->text().toInt());
    saveGeometry();
}

void ReaderWindow::keyPressEvent(QKeyEvent *event) {
    // Foot pedals and presentation remotes send these keys.
    switch (event->key()) {
    case Qt::Key_Escape:
        if (editing_)
            edit_->setChecked(false);
        else
            hide();
        return;
    case Qt::Key_Space:
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaTogglePlayPause: emit toggleRequested(); return;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown: emit step(1); return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp: emit step(-1); return;
    case Qt::Key_Home: emit resetRequested(); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal: emit speedRequested(10); return;
    case Qt::Key_Minus: emit speedRequested(-10); return;
    default: QWidget::keyPressEvent(event);
    }
}

bool ReaderWindow::eventFilter(QObject *object, QEvent *event) {
    // While reading, the text view forwards control keys instead of scrolling.
    if (object == text && event->type() == QEvent::KeyPress && !editing_) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return event->isAccepted();
    }
    if (object == text && event->type() == QEvent::KeyPress && editing_ &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        edit_->setChecked(false);
        return true;
    }
    return QWidget::eventFilter(object, event);
}
