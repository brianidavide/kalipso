// SPDX-License-Identifier: AGPL-3.0-or-later
#include "reader.h"
#include "icons.h"
#include "platform/capture_exclusion.h"
#include "script_highlighter.h"
#include <QAbstractButton>
#include <QAbstractTextDocumentLayout>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
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
#include <algorithm>
#include <cmath>

namespace {
bool horizontal(Mirror mirror) { return mirror == Mirror::Horizontal || mirror == Mirror::Both; }
bool vertical(Mirror mirror) { return mirror == Mirror::Vertical || mirror == Mirror::Both; }
bool wayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }

} // namespace

// The reader has no title bar: dragging the grip and title moves the window.
// Works on Wayland too, where applications cannot position their own windows.
class DragHandle : public QWidget {
public:
    DragHandle() : grip(new QLabel), title(new QLabel) {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(Design::Metric::space1);
        layout->addWidget(grip);
        layout->addWidget(title, 1);
        title->setProperty("role", "muted");
        setCursor(Qt::SizeAllCursor);
        setToolTip(QObject::tr("Drag to move the reader"));
    }
    QLabel *grip, *title;
    QSize sizeHint() const override {
        return {std::min(320, layout()->sizeHint().width()), layout()->sizeHint().height()};
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        QWidget::mousePressEvent(event);
    }
};

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
    if (std::abs(format.bottomMargin() - bottom) > 1 || format.leftMargin() != kMarkerSpace) {
        format.setBottomMargin(bottom);
        format.setLeftMargin(kMarkerSpace); // Room for the current-line marker.
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
    // Through the glass, a mirrored line appears where the unmirrored one would.
    const int target = std::clamp(center - int(viewport()->height() * readingLine_), verticalScrollBar()->minimum(),
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

// A small triangle in the left margin points at the current line.
void ReaderView::paintMarker(QPainter &painter, int lineCenter) const {
    if (!isReadOnly() || !markerColor_.isValid() || document()->isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(markerColor_);
    const int half = std::max(5, fontMetrics().height() / 6);
    painter.drawPolygon(
        QPolygon({QPoint(2, lineCenter - half), QPoint(2 + half + 2, lineCenter), QPoint(2, lineCenter + half)}));
    painter.restore();
}

int ReaderView::currentLineCenter() const {
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(anchor_, 0, std::max(0, document()->characterCount() - 1)));
    return cursorRect(cursor).center().y();
}

void ReaderView::paintEvent(QPaintEvent *event) {
    // Editing shows the text unmirrored, so the caret and mouse work normally.
    if (mirror_ == Mirror::None || !isReadOnly()) {
        QTextEdit::paintEvent(event);
        QPainter painter(viewport());
        paintMarker(painter, currentLineCenter());
        return;
    }
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
    painter.translate(dx, dy); // Marker in viewport coordinates, mirrored with the text.
    paintMarker(painter, currentLineCenter());
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
    resize(680, 360);
    setMinimumSize(320, 180);

    using namespace Design::Metric;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top bar: what you are reading, and quiet window actions.
    auto *topBar = new QWidget;
    topBar->setObjectName("readerBar");
    bars_.append(topBar);
    auto *bar = new FlowLayout(topBar);
    bar->setContentsMargins(space3, space1, space2, space1);
    bar->setSpacing(space1);
    handle_ = new DragHandle;
    handle_->title->setText(tr("Kalipso"));
    sections_ = new QToolButton;
    sections_->setText(tr("Sections"));
    sections_->setObjectName("readerSections");
    sections_->setAccessibleName(tr("Jump to section"));
    sections_->setProperty("quiet", true);
    sections_->setPopupMode(QToolButton::InstantPopup);
    sections_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sections_->setMenu(new QMenu(sections_));
    sections_->hide();
    edit_ = new AccessibleButton(tr("Edit"));
    edit_->setAccessibleName(tr("Edit"));
    edit_->setToolTip(tr("Edit the script in the reader"));
    edit_->setCheckable(true);
    edit_->setProperty("quiet", true);
    auto *hide = new AccessibleButton(tr("Hide"));
    hide->setObjectName("readerHide");
    hide->setAccessibleName(tr("Hide"));
    hide->setProperty("quiet", true);
    hide->setToolTip(tr("Hide the reader (Esc). The microphone turns off."));
    bar->addWidget(handle_);
    bar->addTrailing(sections_);
    bar->addTrailing(edit_);
    bar->addTrailing(hide);
    layout->addWidget(topBar);

    progress_ = new QProgressBar;
    progress_->setObjectName("readerProgress");
    progress_->setAccessibleName(tr("Reading progress"));
    progress_->setTextVisible(false);
    progress_->setRange(0, 1);
    layout->addWidget(progress_);

    text = new ReaderView;
    text->setPlaceholderText(tr("Add your script in the editor to begin."));
    text->installEventFilter(this);
    auto *page = new QVBoxLayout;
    page->setContentsMargins(space4, space2, space5, space2);
    page->addWidget(text);
    layout->addLayout(page, 1);
    countdown_ = new QLabel(text);
    countdown_->setObjectName("readerCountdown");
    countdown_->setAlignment(Qt::AlignCenter);
    countdown_->setAttribute(Qt::WA_TransparentForMouseEvents);
    countdown_->hide();

    // Bottom bar: step, start or pause, and how the reader advances.
    auto *controlBar = new QWidget;
    controlBar->setObjectName("readerControls");
    bars_.append(controlBar);
    auto *controls = new FlowLayout(controlBar);
    controls->setContentsMargins(space3, space2, space3, space2);
    controls->setSpacing(space2);
    auto *previous = new AccessibleButton(QString());
    previous->setAccessibleName(tr("Previous word"));
    previous->setToolTip(tr("Previous word (← or Page Up)"));
    previous->setProperty("quiet", true);
    auto *next = new AccessibleButton(QString());
    next->setAccessibleName(tr("Next word"));
    next->setToolTip(tr("Next word (→ or Page Down)"));
    next->setProperty("quiet", true);
    primary_ = new AccessibleButton(tr("Start listening"));
    primary_->setObjectName("readerListeningButton");
    primary_->setProperty("primary", true);
    primary_->setToolTip(tr("Start or pause (Space)"));
    // The reading mode: one compact drop-down that fits a narrow reader.
    modes_ = new QToolButton;
    modes_->setObjectName("readerMode");
    modes_->setPopupMode(QToolButton::InstantPopup);
    modes_->setProperty("quiet", true);
    auto *modeMenu = new QMenu(modes_);
    auto *modeGroup = new QActionGroup(modes_);
    for (const auto &[label, mode] :
         {std::pair{tr("Manual"), ReadingMode::Manual}, std::pair{tr("Steady speed"), ReadingMode::Steady},
          std::pair{tr("Follow my voice"), ReadingMode::Voice}}) {
        auto *action = modeMenu->addAction(label);
        action->setCheckable(true);
        action->setData(int(mode));
        modeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { emit modeRequested(mode); });
    }
    modes_->setMenu(modeMenu);
    speedBox_ = new QWidget;
    auto *speedLayout = new QHBoxLayout(speedBox_);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->setSpacing(space1);
    auto *slower = new AccessibleButton(QStringLiteral("−"));
    slower->setAccessibleName(tr("Slower"));
    slower->setToolTip(tr("Slower (−)"));
    slower->setProperty("quiet", true);
    auto *faster = new AccessibleButton(QStringLiteral("+"));
    faster->setAccessibleName(tr("Faster"));
    faster->setToolTip(tr("Faster (+)"));
    faster->setProperty("quiet", true);
    speed_ = new QLabel;
    speed_->setObjectName("readerSpeed");
    speedLayout->addWidget(slower);
    speedLayout->addWidget(speed_);
    speedLayout->addWidget(faster);
    status_ = new StatusLabel(tr("Microphone off"));
    status_->setObjectName("readerListeningStatus");
    status_->setProperty("role", "muted");
    for (auto *widget : QList<QWidget *>{previous, primary_, next, modes_, speedBox_})
        controls->addWidget(widget);
    controls->addTrailing(status_);
    layout->addWidget(controlBar);
    icons_ = {{previous, "previous"}, {next, "next"}, {sections_, "sections"}, {edit_, "edit"}, {hide, "close"}};

    // Clicking a control must not move keyboard focus off the script: pedals and
    // clickers send Space and arrows, which would otherwise activate or edit.
    for (auto *control : findChildren<QAbstractButton *>())
        control->setFocusPolicy(Qt::TabFocus);
    // While reading, the bars fade out (keeping their space, so the text does
    // not move) and come back with the mouse, a key, or a pause.
    idle_.setSingleShot(true);
    idle_.setInterval(2500);
    connect(&idle_, &QTimer::timeout, this, [this] { setControlsVisible(false); });
    for (auto *bar : bars_) {
        auto *effect = new QGraphicsOpacityEffect(bar);
        effect->setOpacity(1.0);
        bar->setGraphicsEffect(effect);
        bar->setMouseTracking(true);
        bar->installEventFilter(this);
    }
    text->viewport()->setMouseTracking(true);
    text->viewport()->installEventFilter(this);
    auto *grip = new QSizeGrip(this);
    grip->setObjectName("readerResize");
    grip->setToolTip(tr("Drag to resize the reader"));

    connect(previous, &QPushButton::clicked, this, [this] { emit step(-1); });
    connect(next, &QPushButton::clicked, this, [this] { emit step(1); });
    connect(hide, &QPushButton::clicked, this, &QWidget::hide);
    connect(primary_, &QPushButton::clicked, this, &ReaderWindow::toggleRequested);
    connect(slower, &QPushButton::clicked, this, [this] { emit speedRequested(-10); });
    connect(faster, &QPushButton::clicked, this, [this] { emit speedRequested(10); });
    connect(text, &ReaderView::characterClicked, this, &ReaderWindow::jumpRequested);
    connect(edit_, &QPushButton::toggled, this, [this](bool enabled) {
        editing_ = enabled;
        text->setReadOnly(!enabled);
        // Scroll bars only while editing; reading follows the current line.
        text->setVerticalScrollBarPolicy(enabled ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        text->style()->unpolish(text); // The style sheet depends on readOnly.
        text->style()->polish(text);
        edit_->setText(enabled ? tr("Done") : compact_ ? QString() : tr("Edit"));
        edit_->setAccessibleName(enabled ? tr("Done") : tr("Edit"));
        edit_->setIcon(enabled ? QIcon() : Icons::icon("edit", QColor(colors_.text), QColor(colors_.muted)));
        text->setFocus();
        text->viewport()->update();
        if (!enabled)
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
    text->setMarkerColor(QColor(palette.accent));
    countdown_->setStyleSheet(QString("color:%1; background:%2; border: 1px solid %3; border-radius:16px;")
                                  .arg(palette.text, palette.surface, palette.border));
    for (const auto &[button, name] : icons_)
        if (!(button == edit_ && editing_))
            button->setIcon(Icons::icon(name, QColor(palette.text), QColor(palette.muted)));
    handle_->grip->setPixmap(Icons::icon("grip", QColor(palette.muted)).pixmap(18, 18));
    updatePrimary();
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
    for (auto *action : modes_->menu()->actions())
        if (action->data().toInt() == int(mode)) {
            action->setChecked(true);
            modes_->setText(action->text());
            modes_->setAccessibleName(tr("Reading mode: %1").arg(action->text()));
        }
    speedBox_->setVisible(mode == ReadingMode::Steady);
    updatePrimary();
}

void ReaderWindow::setRunning(bool running, const QString &status) {
    const bool changed = running != running_;
    running_ = running;
    updatePrimary();
    setStatus(status);
    if (changed) // Status updates while reading must not bring the controls back.
        revealControls();
}

void ReaderWindow::setAutoHideControls(bool hide) {
    autoHide_ = hide;
    revealControls();
}

void ReaderWindow::revealControls() {
    setControlsVisible(true);
    if (running_ && autoHide_ && !editing_)
        idle_.start();
    else
        idle_.stop();
}

void ReaderWindow::setControlsVisible(bool visible) {
    controlsVisible_ = visible;
    for (auto *bar : bars_)
        static_cast<QGraphicsOpacityEffect *>(bar->graphicsEffect())->setOpacity(visible ? 1.0 : 0.0);
}

void ReaderWindow::setStatus(const QString &status) { status_->setText(status); }

void ReaderWindow::updatePrimary() {
    primary_->setVisible(readingMode_ != ReadingMode::Manual);
    if (readingMode_ == ReadingMode::Steady)
        primary_->setText(running_ ? tr("Pause scrolling") : tr("Start scrolling"));
    else
        primary_->setText(running_ ? tr("Pause listening") : tr("Start listening"));
    primary_->setIcon(Icons::icon(running_ ? "pause" : "play", QColor(colors_.actionText)));
}

void ReaderWindow::setTitle(const QString &title) {
    handle_->title->setText(title.isEmpty() ? tr("Kalipso") : title);
    handle_->title->setToolTip(title);
}

void ReaderWindow::setProgress(int read, int total) {
    progress_->setRange(0, std::max(1, total));
    progress_->setValue(std::clamp(read, 0, std::max(1, total)));
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
    // The current word: accent text on a soft tint, underlined; calmer than a solid block.
    current.format.setBackground(QColor(colors_.hover));
    current.format.setForeground(QColor(colors_.accent));
    current.format.setFontUnderline(true);
    current.format.setUnderlineColor(QColor(colors_.accent));
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
                   std::min(available.height() - 32, 360 + extra));
            move(available.center().x() - width() / 2, available.top() + 16);
        }
        placing_ = false;
    }
    show();
    raise();
    activateWindow();
    text->setFocus();
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
    // A narrow reader keeps the window actions as icons so the text keeps its room.
    const bool compact = width() < 480;
    if (compact != compact_) {
        compact_ = compact;
        sections_->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        if (!editing_)
            edit_->setText(compact ? QString() : tr("Edit"));
        auto *hide = findChild<QPushButton *>("readerHide");
        hide->setText(compact ? QString() : tr("Hide"));
    }
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
    revealControls();
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
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter ||
        event->type() == QEvent::MouseButtonPress)
        revealControls();
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
