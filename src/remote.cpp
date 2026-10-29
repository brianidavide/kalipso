// SPDX-License-Identifier: AGPL-3.0-or-later
#include "remote.h"
#include "../third_party/qrcodegen/qrcodegen.hpp"
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QPainter>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <algorithm>

namespace {
constexpr qsizetype kMaxRequest = 8192;
constexpr int kMaxConnections = 16;
const QStringList kCommands{"toggle", "next",  "previous",         "faster",
                            "slower", "reset", "section-previous", "section-next"};

// The remote page. Inline script is avoided so a strict CSP can apply.
constexpr auto kPage = R"(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kalipso remote</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#17191F;color:#F7F8FC}
body{margin:0;padding:16px;display:grid;gap:12px;max-width:32rem;margin-inline:auto}
h1{font-size:1.25rem;margin:0}
p{margin:0;color:#A7AFBF;min-height:1.5em}
button{font:inherit;font-size:1.25rem;min-height:4.5rem;border-radius:12px;border:2px solid #8F98A9;background:#242933;color:#F7F8FC}
button:focus-visible{outline:3px solid #F7F8FC;outline-offset:2px}
#toggle{background:#345CF5;border-color:#345CF5;color:#fff;min-height:7rem}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
</style></head><body>
<h1>Kalipso remote</h1>
<p id="state" role="status">Connecting…</p>
<button id="toggle" data-command="toggle">Start or pause</button>
<div class="row"><button data-command="previous">Previous</button><button data-command="next">Next</button></div>
<div class="row"><button data-command="section-previous">&#8249; Section</button><button data-command="section-next">Section &#8250;</button></div>
<div class="row"><button data-command="slower">Slower</button><button data-command="faster">Faster</button></div>
<button data-command="reset">Back to start</button>
<script src="app.js"></script>
</body></html>)";

constexpr auto kScript = R"(const state=document.getElementById('state');
async function refresh(){
  try{
    const r=await fetch('state',{cache:'no-store'});const s=await r.json();
    const mode={manual:'Manual',steady:'Steady speed',voice:'Follow my voice'}[s.mode]||'';
    state.textContent=`${mode} · ${s.running?'running':'paused'} · word ${s.position} of ${s.total}`+(s.mode==='steady'?` · ${s.speed} words/min`:'');
  }catch(e){state.textContent='Kalipso is not reachable. Check that remote control is still on.';}
}
for(const b of document.querySelectorAll('button[data-command]'))
  b.addEventListener('click',async()=>{await fetch('command/'+b.dataset.command,{method:'POST'}).catch(()=>{});refresh();});
refresh();setInterval(refresh,1500);
)";

QByteArray response(int status, const QByteArray &type, const QByteArray &body) {
    const QByteArray reason = status == 200   ? "OK"
                              : status == 204 ? "No Content"
                              : status == 403 ? "Forbidden"
                              : status == 405 ? "Method Not Allowed"
                                              : "Not Found";
    return "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n" + "Content-Type: " + type +
           "\r\nContent-Length: " + QByteArray::number(body.size()) +
           "\r\nCache-Control: no-store\r\nConnection: close\r\nReferrer-Policy: no-referrer\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; "
           "connect-src 'self'; frame-ancestors 'none'\r\n\r\n" +
           body;
}
} // namespace

RemoteControl::RemoteControl(QObject *parent) : QObject(parent) {
    server_.setMaxPendingConnections(kMaxConnections);
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (auto *socket = server_.nextPendingConnection()) {
            if (connections_ >= kMaxConnections) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            ++connections_;
            connect(socket, &QObject::destroyed, this, [this] { --connections_; });
            serve(socket);
        }
    });
}

QList<RemoteControl::Network> RemoteControl::networks() {
    static const QStringList virtualPrefixes{"docker", "br-", "virbr", "veth", "vmnet", "vboxnet",
                                             "tun",    "tap", "wg",    "utun", "zt",    "tailscale"};
    QList<Network> found;
    for (const auto &interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const auto name = interface.humanReadableName();
        const bool likelyVirtual =
            interface.type() == QNetworkInterface::Virtual ||
            std::any_of(virtualPrefixes.begin(), virtualPrefixes.end(),
                        [&name](const QString &prefix) { return name.startsWith(prefix, Qt::CaseInsensitive); });
        for (const auto &entry : interface.addressEntries()) {
            const auto address = entry.ip();
            // IPv4 only: global IPv6 addresses could expose the remote beyond the local network.
            if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                !address.isInSubnet(QHostAddress::parseSubnet("169.254.0.0/16")))
                found.append({name, address, likelyVirtual});
        }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const Network &a, const Network &b) { return !a.likelyVirtual && b.likelyVirtual; });
    return found;
}

bool RemoteControl::start(const QHostAddress &address, quint16 port) {
    stop();
    const auto random = [] {
        return QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, u'0');
    };
    token_ = random() + random();
    // A stable port keeps the phone's bookmark and the firewall's permission valid.
    return server_.listen(address, port) || (port != 0 && server_.listen(address, 0));
}

void RemoteControl::stop() {
    server_.close();
    token_.clear();
}

QString RemoteControl::url() const {
    auto host = server_.serverAddress();
    if (host == QHostAddress::AnyIPv4 || host.isNull()) {
        const auto all = networks();
        host = all.isEmpty() ? QHostAddress(QHostAddress::LocalHost) : all.first().address;
    }
    return QStringLiteral("http://%1:%2/%3/").arg(host.toString()).arg(port()).arg(token_);
}

void RemoteControl::serve(QTcpSocket *socket) {
    auto *buffer = new QByteArray;
    QTimer::singleShot(5000, socket, &QTcpSocket::disconnectFromHost);
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QObject::destroyed, socket, [buffer] { delete buffer; });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
        buffer->append(socket->readAll());
        const auto end = buffer->indexOf("\r\n\r\n");
        if (end < 0) {
            if (buffer->size() > kMaxRequest)
                socket->abort();
            return;
        }
        const auto lines = buffer->left(end).split('\n');
        const auto line = lines.first().trimmed().split(' ');
        QByteArray host, origin;
        for (const auto &header : lines.mid(1)) {
            const int colon = int(header.indexOf(':'));
            const auto name = header.left(colon).trimmed().toLower();
            if (name == "host")
                host = header.mid(colon + 1).trimmed();
            else if (name == "origin")
                origin = header.mid(colon + 1).trimmed();
        }
        socket->disconnect(this);
        QByteArray reply = response(404, "text/plain", "Not found");
        // Browsers send Origin with every POST: refuse commands from other web pages.
        if (!origin.isEmpty() && origin != "http://" + host) {
            reply = response(403, "text/plain", "Forbidden");
        } else if (line.size() == 3 && !token_.isEmpty()) {
            const auto method = line[0];
            const auto path = QString::fromLatin1(line[1]);
            const auto prefix = "/" + token_ + "/";
            if (path.startsWith(prefix)) {
                const auto route = path.mid(prefix.size());
                if (route.isEmpty() && method == "GET")
                    reply = response(200, "text/html; charset=utf-8", kPage);
                else if (route == QLatin1String("app.js") && method == "GET")
                    reply = response(200, "text/javascript; charset=utf-8", kScript);
                else if (route == QLatin1String("state") && method == "GET")
                    reply = response(200, "application/json", QJsonDocument(state_).toJson(QJsonDocument::Compact));
                else if (route.startsWith(QLatin1String("command/")) && kCommands.contains(route.mid(8))) {
                    if (method == "POST") {
                        emit command(route.mid(8));
                        reply = response(204, "text/plain", {});
                    } else {
                        reply = response(405, "text/plain", "Use POST");
                    }
                }
            }
        }
        socket->write(reply);
        socket->disconnectFromHost();
    });
}

QImage RemoteControl::qrCode(const QString &text, int scale) {
    const auto code = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int border = 4, size = code.getSize();
    QImage image((size + 2 * border) * scale, (size + 2 * border) * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (code.getModule(x, y))
                painter.fillRect((x + border) * scale, (y + border) * scale, scale, scale, Qt::black);
    return image;
}
