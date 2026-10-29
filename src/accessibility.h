// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "design_tokens.h"
#include <QAccessibleWidget>
#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <cmath>

namespace Accessibility {
inline double luminance(const QColor &color) {
    const auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}
inline double contrast(const QColor &a, const QColor &b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
inline QPalette palette(const Design::Palette &p) {
    QPalette result;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        for (auto role : {QPalette::Window, QPalette::Button})
            result.setColor(group, role, QColor(p.background));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
            result.setColor(group, role, QColor(p.text));
        result.setColor(group, QPalette::Base, QColor(p.surface));
        result.setColor(group, QPalette::AlternateBase, QColor(p.hover));
        result.setColor(group, QPalette::ToolTipBase, QColor(p.background));
        result.setColor(group, QPalette::PlaceholderText, QColor(p.muted));
        result.setColor(group, QPalette::Highlight, QColor(p.selection));
        result.setColor(group, QPalette::HighlightedText, QColor(p.selectionText));
        result.setColor(group, QPalette::Link, QColor(p.accent));
        result.setColor(group, QPalette::LinkVisited, QColor(p.accent));
        result.setColor(group, QPalette::Mid, QColor(p.border));
        result.setColor(group, QPalette::Dark, QColor(p.border));
    }
    return result;
}
inline QString style(const Design::Palette &p) {
    return QString(
               "QWidget { color:%1; } QMainWindow, QDialog, QWidget#readingWindow, QScrollArea, "
               "QScrollArea>QWidget>QWidget { background:%2; } "
               "QLabel { background:transparent; } QGroupBox { margin-top:16px; padding-top:16px; border:1px solid %3; "
               "} "
               "QPushButton, QToolButton, QComboBox, QLineEdit, QSpinBox, QPlainTextEdit, QTextEdit, QAbstractItemView "
               "{ background:%4; color:%1; border:2px solid %3; border-radius:4px; padding:6px; "
               "selection-background-color:%5; selection-color:%6; } "
               "QPushButton, QToolButton, QComboBox, QSpinBox { min-height:24px; } QPushButton:hover, "
               "QToolButton:hover { background:%7; } "
               "QPushButton:focus, QToolButton:focus, QComboBox:focus, QLineEdit:focus, QSpinBox:focus, "
               "QPlainTextEdit:focus, QTextEdit:focus, QTabBar::tab:selected { border:2px solid %8; } "
               "QSpinBox { min-height:48px; } QSpinBox::up-button, QSpinBox::down-button { width:24px; height:24px; } "
               "QTabBar::tab { padding:8px 12px; border:2px solid %3; background:%4; color:%1; } "
               "QCheckBox { spacing:8px; min-height:24px; } QCheckBox:focus { border:2px solid %8; } "
               "QPushButton#primaryAction { background:%10; color:%11; } QPushButton#primaryAction:hover { "
               "background:%12; } "
               "QTextEdit#readingScript[readOnly=\"true\"] { background:%2; border-color:transparent; } "
               "QTextEdit#readingScript:focus { border-color:%8; } "
               "QPushButton:disabled { color:%9; border-style:dashed; } "
               "QScrollBar { background:%2; } QScrollBar:vertical { width:24px; } QScrollBar:horizontal { height:24px; "
               "} QScrollBar::handle { background:%3; min-height:24px; min-width:24px; } ")
        .arg(p.text, p.background, p.border, p.surface, p.selection, p.selectionText, p.hover, p.focus, p.muted)
        .arg(p.action, p.actionText, p.actionHover);
}
inline void apply(QWidget *widget, const Design::Palette &p) {
    widget->setPalette(palette(p));
    widget->setAutoFillBackground(true);
    widget->setStyleSheet(style(p));
}
} // namespace Accessibility

class StatusLabel : public QLabel {
public:
    explicit StatusLabel(const QString &value, QWidget *parent = nullptr) : QLabel(value, parent) {
        setWordWrap(true);
        setTextFormat(Qt::PlainText);
        setAccessibleName(value);
        QAccessible::registerAccessibleInterface(new QAccessibleWidget(this, QAccessible::StatusBar));
    }
    void setText(const QString &value) {
        if (text() == value)
            return;
        QLabel::setText(value);
        setAccessibleName(value);
        if (!isVisible() || value.isEmpty())
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QAccessibleAnnouncementEvent event(this, value);
#else
        QAccessibleEvent event(this, QAccessible::Alert);
#endif
        QAccessible::updateAccessibility(&event);
    }
};

class AccessibleButton : public QPushButton {
public:
    explicit AccessibleButton(const QString &text, QWidget *parent = nullptr) : QPushButton(text, parent) {
        auto policy = sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    int heightForWidth(int width) const override {
        return std::max(36, fontMetrics()
                                    .boundingRect(QRect(0, 0, std::max(24, width - 24), 10000),
                                                  Qt::TextWordWrap | Qt::AlignCenter, text())
                                    .height() +
                                20);
    }
    QSize sizeHint() const override {
        const int width = std::min(280, fontMetrics().horizontalAdvance(text()) + 28);
        return QSize(width, heightForWidth(width));
    }
    QSize minimumSizeHint() const override { return QSize(std::min(120, sizeHint().width()), 36); }

protected:
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text.clear();
        painter.drawControl(QStyle::CE_PushButton, option);
        const auto area = rect().adjusted(10, 8, -10, -8);
        style()->drawItemText(&painter, area, Qt::TextWordWrap | Qt::AlignCenter, option.palette, isEnabled(), text(),
                              QPalette::ButtonText);
    }
};

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr) : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(8);
    }
    ~FlowLayout() override {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { items_.append(item); }
    int count() const override { return int(items_.size()); }
    QLayoutItem *itemAt(int i) const override { return items_.value(i); }
    QLayoutItem *takeAt(int i) override { return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override {
        QSize size;
        for (auto *item : items_)
            if (!item->isEmpty())
                size = size.expandedTo(item->minimumSize());
        return size;
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    int arrange(const QRect &rect, bool apply) const {
        int x = rect.x(), y = rect.y(), line = 0;
        for (auto *item : items_) {
            if (item->isEmpty())
                continue;
            auto size = item->sizeHint();
            size.setWidth(std::min(size.width(), rect.width()));
            if (item->hasHeightForWidth())
                size.setHeight(item->heightForWidth(size.width()));
            if (x > rect.x() && x + size.width() > rect.right() + 1) {
                x = rect.x();
                y += line + spacing();
                line = 0;
            }
            if (apply)
                item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing();
            line = std::max(line, size.height());
        }
        return y - rect.y() + line;
    }
    QList<QLayoutItem *> items_;
};
