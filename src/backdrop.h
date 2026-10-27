// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
#include <QProcess>
#include <QTimer>
#include <QWidget>

// Samples only in memory. The palette always paints an opaque reading surface.
class Backdrop:public QObject {
    Q_OBJECT
public:
    explicit Backdrop(QWidget *reader);
    ~Backdrop() override {stop();}
    void setEnabled(bool enabled);
    QString source() const;
signals:
    void colorChanged(const QColor &color);
    void unavailable(const QString &reason);
protected:
    bool eventFilter(QObject *object,QEvent *event) override;
private:
    void sample();
    void stop();
    QWidget *reader_;
    QTimer timer_,timeout_;
    QProcess helper_;
    QByteArray output_;
    bool enabled_=false,pending_=false;
};
