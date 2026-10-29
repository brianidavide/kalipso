// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

// Optional phone remote over the local network. Off until the user enables it;
// every start creates a new secret link. Serves only playback controls and the
// reading position, never script text.
class RemoteControl : public QObject {
    Q_OBJECT
public:
    explicit RemoteControl(QObject *parent = nullptr);
    bool start(quint16 port = 0);
    void stop();
    bool active() const { return server_.isListening(); }
    QString url() const; // First LAN address, or loopback.
    QString token() const { return token_; }
    quint16 port() const { return server_.serverPort(); }
    void setState(const QJsonObject &state) { state_ = state; }
    static QImage qrCode(const QString &text, int scale = 6);

signals:
    // "toggle", "next", "previous", "faster", "slower" or "reset".
    void command(const QString &name);

private:
    void serve(class QTcpSocket *socket);
    int connections_ = 0; // Declared first: sockets owned by server_ update it when destroyed.
    QTcpServer server_;
    QString token_;
    QJsonObject state_;
};
