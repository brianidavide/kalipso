// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QTransform>
#include <QVariantAnimation>

class QAbstractButton;
class QPainter;

// Motion follows the website's rules: short, by meaning, and none when the system asks for less motion.
namespace Motion {
// The system's choice: KDE (animation speed set to instant), GNOME (animations off), Windows (animations
// inside windows off) or macOS (Reduce motion). KALIPSO_REDUCED_MOTION=1 or 0 overrides it.
bool reduced();
// A press shrinks the control slightly about its centre (instant, like the website's buttons).
void pressed(QPainter &painter, const QRectF &rect);
} // namespace Motion

// Moves a button's icon by its meaning while the pointer is over the button or it has keyboard focus,
// like the website's icons. The button's "motion" property names the movement: forward, back, down, out,
// grow, shrink, turn or tilt. Entering eases out over Duration::enter; leaving is
// shorter and accelerates. Still with reduced motion.
class IconMotion : public QObject {
public:
    explicit IconMotion(QAbstractButton *button);
    // Where to paint the icon that would otherwise fill `icon`.
    QTransform transform(const QRectF &icon) const;
    double progress() const { return progress_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void run(bool forward);
    QAbstractButton *button_;
    QVariantAnimation animation_;
    double progress_ = 0;
    bool keyboardFocus_ = false;
};
