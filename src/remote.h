// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QHostAddress>
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
    struct Network {
        QString name; // Interface, e.g. "wlp2s0".
        QHostAddress address;
        bool likelyVirtual; // Containers, VMs, VPNs: listed last, never the default.
    };
    // Local IPv4 networks a phone could reach, physical ones first.
    static QList<Network> networks();
    // Listens only on this address. A port of 0, or one already taken, picks a free port.
    bool start(const QHostAddress &address = QHostAddress::AnyIPv4, quint16 port = 0);
    void stop();
    bool active() const { return server_.isListening(); }
    QString url() const; // The listening address (or the first network) with the secret path.
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
