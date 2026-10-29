// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "accessibility.h"
#include "backdrop.h"
#include "tracker.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTextEdit>
#include <QTimer>

class QComboBox;
class QProgressBar;
class QPropertyAnimation;
class QToolButton;
class QAbstractButton;
class QPainter;
class ScriptHighlighter;
class DragHandle;

enum class Mirror { None, Horizontal, Vertical, Both };
enum class ReadingMode { Manual, Steady, Voice };

// The script text inside the reader. Paints mirrored for beam-splitter rigs and
// keeps the reading position on a fixed line.
class ReaderView : public QTextEdit {
    Q_OBJECT
public:
    explicit ReaderView(QWidget *parent = nullptr);
    void setMirror(Mirror mirror);
    Mirror mirror() const { return mirror_; }
    // Fraction of the visible height, from the top, where the current line sits.
    void setReadingLine(double fraction);
    void scrollToCharacter(int character, bool animate);
    void setTextColor(const QColor &color) { textColor_ = color; }
    void setMarkerColor(const QColor &color) {
        markerColor_ = color;
        viewport()->update();
    }
    ScriptHighlighter *highlighter() const { return highlighter_; }
signals:
    void characterClicked(int character);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPoint unmirrored(QPoint point) const;
    void paintMarker(QPainter &painter, int lineCenter) const;
    int currentLineCenter() const;
    static constexpr int kMarkerSpace = 22;
    Mirror mirror_ = Mirror::None;
    double readingLine_ = 0.3;
    int anchor_ = 0;
    QColor textColor_, markerColor_;
    ScriptHighlighter *highlighter_;
    QPropertyAnimation *scroll_;
};

// Frameless, always-on-top reading window placed near the camera.
class ReaderWindow : public QWidget {
    Q_OBJECT
public:
    explicit ReaderWindow(QWidget *parent = nullptr);
    ReaderView *text;

    void showOn(QScreen *screen);
    void highlight(int position, int length);
    void setTextSize(int points);
    void setReaderTheme(const QString &theme);
    void setMirror(Mirror mirror) { text->setMirror(mirror); }
    void setReadingLine(double fraction) { text->setReadingLine(fraction); }
    void setMode(ReadingMode mode);
    void setRunning(bool running, const QString &status);
    void setStatus(const QString &status);
    void setSpeed(int wordsPerMinute);
    void setSections(const QVector<Section> &sections);
    void setCaptureExcluded(bool excluded);
    // Shows a large number over the text; 0 hides it.
    void showCountdown(int seconds);
    void setTitle(const QString &title);
    // Fade the bars out while reading; they return with the mouse or a key.
    void setAutoHideControls(bool hide);
    bool controlsVisible() const { return controlsVisible_; }
    void setProgress(int read, int total);

    QString colorSource() const { return backdrop_->source(); }
    bool usesWindowPortal() const { return backdrop_->usesWindowPortal(); }
    void chooseBackgroundWindow() { backdrop_->chooseWindow(); }
    void stopWindowSharing() { backdrop_->stopWindowSharing(); }

signals:
    void step(int direction);
    void toggleRequested();
    void resetRequested();
    void speedRequested(int delta);
    void modeRequested(ReadingMode mode);
    void jumpRequested(int character);
    void colorStatus(const QString &text);
    void hidden();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void applyPalette(const Design::Palette &palette);
    void saveGeometry();
    void updatePrimary();
    void revealControls();
    void setControlsVisible(bool visible);

    StatusLabel *status_;
    QPushButton *primary_;
    QToolButton *modes_;
    DragHandle *handle_;
    QProgressBar *progress_;
    QList<QPair<QAbstractButton *, QString>> icons_;
    QWidget *speedBox_;
    QLabel *speed_;
    QToolButton *sections_;
    QLabel *countdown_;
    QPushButton *edit_;
    Backdrop *backdrop_;
    Design::Palette colors_ = Design::dark;
    QString resolvedTheme_ = "dark";
    QElapsedTimer paletteAge_;
    ReadingMode readingMode_ = ReadingMode::Voice;
    bool running_ = false;
    bool editing_ = false;
    bool compact_ = false;
    bool autoHide_ = true;
    bool controlsVisible_ = true;
    QTimer idle_;
    QList<QWidget *> bars_;
    bool captureExcluded_ = false;
    bool placing_ = false;
    int position_ = 0, length_ = 0;
    QPointer<QScreen> screen_;
};
