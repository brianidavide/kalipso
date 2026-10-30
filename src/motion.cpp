// SPDX-License-Identifier: AGPL-3.0-or-later
#include "motion.h"
#include "design_tokens.h"
#include <QAbstractButton>
#include <QFocusEvent>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <cmath>
#if defined(Q_OS_WIN)
#define NOMINMAX // windows.h must not replace std::max.
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace Motion {
bool reduced() {
    static const bool value = [] {
        if (qEnvironmentVariableIsSet("KALIPSO_REDUCED_MOTION"))
            return qEnvironmentVariableIntValue("KALIPSO_REDUCED_MOTION") != 0;
#if defined(Q_OS_WIN)
        BOOL animations = TRUE;
        return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
#elif defined(Q_OS_MACOS)
        Boolean valid = false;
        const bool reduce =
            CFPreferencesGetAppBooleanValue(CFSTR("reduceMotion"), CFSTR("com.apple.universalaccess"), &valid);
        return valid && reduce;
#else
        const QSettings kde(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kdeglobals",
                            QSettings::IniFormat);
        if (kde.value("KDE/AnimationDurationFactor", 1.0).toDouble() <= 0.0)
            return true;
        if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("GNOME"), Qt::CaseInsensitive)) {
            QProcess gsettings;
            gsettings.start("gsettings", {"get", "org.gnome.desktop.interface", "enable-animations"});
            if (gsettings.waitForFinished(1000) && gsettings.readAllStandardOutput().trimmed() == "false")
                return true;
        }
        return false;
#endif
    }();
    return value;
}

void pressed(QPainter &painter, const QRectF &rect) {
    const auto centre = rect.center();
    painter.translate(centre);
    painter.scale(0.97, 0.97);
    painter.translate(-centre);
}
} // namespace Motion

IconMotion::IconMotion(QAbstractButton *button) : QObject(button), button_(button) {
    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        progress_ = value.toDouble();
        button_->update();
    });
    button->installEventFilter(this);
}

bool IconMotion::eventFilter(QObject *, QEvent *event) {
    switch (event->type()) {
    case QEvent::Enter: run(true); break;
    case QEvent::Leave:
        if (!keyboardFocus_)
            run(false);
        break;
    case QEvent::FocusIn: {
        // Like :focus-visible: keyboard focus moves the icon, a click does not add to the hover.
        const auto reason = static_cast<QFocusEvent *>(event)->reason();
        keyboardFocus_ =
            reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
        if (keyboardFocus_)
            run(true);
        break;
    }
    case QEvent::FocusOut:
        keyboardFocus_ = false;
        if (!button_->underMouse())
            run(false);
        break;
    default: break;
    }
    return false;
}

void IconMotion::run(bool forward) {
    const double target = forward ? 1.0 : 0.0;
    if (Motion::reduced() || button_->property("motion").toString().isEmpty()) {
        animation_.stop();
        progress_ = 0;
        return;
    }
    if (animation_.state() != QAbstractAnimation::Running && progress_ == target)
        return;
    animation_.stop();
    animation_.setStartValue(progress_);
    animation_.setEndValue(target);
    animation_.setDuration(
        std::max(1, int(std::abs(target - progress_) * (forward ? Design::Duration::enter : Design::Duration::exit))));
    animation_.setEasingCurve(forward ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    animation_.start();
}

QTransform IconMotion::transform(const QRectF &icon) const {
    QTransform result;
    const double p = progress_;
    if (p <= 0)
        return result;
    const auto kind = button_->property("motion").toString();
    // Forward and back follow the reading direction.
    const double ahead = button_->isRightToLeft() ? -1 : 1;
    const auto centre = icon.center();
    const auto about = [&](auto change) {
        result.translate(centre.x(), centre.y());
        change();
        result.translate(-centre.x(), -centre.y());
    };
    if (kind == QLatin1String("forward"))
        result.translate(3 * p * ahead, 0);
    else if (kind == QLatin1String("back"))
        result.translate(-3 * p * ahead, 0);
    else if (kind == QLatin1String("down"))
        result.translate(0, 2 * p);
    else if (kind == QLatin1String("out"))
        result.translate(2 * p * ahead, -2 * p);
    else if (kind == QLatin1String("grow"))
        about([&] { result.scale(1 + 0.1 * p, 1 + 0.1 * p); });
    else if (kind == QLatin1String("turn"))
        about([&] { result.rotate(45 * p); }); // One tooth of the gear.
    else if (kind == QLatin1String("shrink"))
        about([&] { result.scale(1 - 0.12 * p, 1 - 0.12 * p); });
    else if (kind == QLatin1String("tilt"))
        about([&] { result.rotate(-14 * p); });
    return result;
}
