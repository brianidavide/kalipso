// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
#include <QDBusMessage>
#include <QObject>
#include <QTimer>
#include <memory>

// A user-selected window, delivered by the desktop portal. No frames leave memory.
class PortalColors final : public QObject {
    Q_OBJECT
public:
    explicit PortalColors(QObject *parent=nullptr);
    ~PortalColors() override;
    void start();
    void stop();
    bool active() const {return active_;}
    QString status() const {return status_;}
signals:
    void colorChanged(const QColor &color);
    void statusChanged(const QString &status);
private slots:
    void response(uint code,const QVariantMap &results,const QDBusMessage &message);
    void sessionClosed();
private:
    struct Stream;
    std::unique_ptr<Stream> stream_;
    void request(const QString &method,QVariantList arguments,QVariantMap options={});
    void openRemote(uint node);
    void fail(const QString &reason);
    void publish(const QString &status);
    QString requestPath_,sessionPath_,stage_,status_;
    QTimer timeout_;
    quint64 generation_=0;
    bool active_=false;
};
