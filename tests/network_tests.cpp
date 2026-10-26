// SPDX-License-Identifier: AGPL-3.0-or-later
#include "llm.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtTest>

class NetworkTests : public QObject {
    Q_OBJECT
private slots:
    void refusesInsecureRemoteAndEmbeddedCredentials() {
        QVERIFY(!LlmClient::validateEndpoint(QUrl("http://example.org/v1/chat/completions")).isEmpty());
        QVERIFY(!LlmClient::validateEndpoint(QUrl("https://user:secret@example.org/v1")).isEmpty());
        QVERIFY(!LlmClient::validateEndpoint(QUrl("https://example.org/v1?api_key=secret")).isEmpty());
        QVERIFY(LlmClient::validateEndpoint(QUrl("http://127.0.0.1:8080/v1/chat/completions")).isEmpty());
        QVERIFY(LlmClient::validateEndpoint(QUrl("https://example.org/v1/chat/completions")).isEmpty());
    }
    void requestAndResponse() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[&,socket] {
                received+=socket->readAll();
                const int boundary=received.indexOf("\r\n\r\n"); if(boundary<0) return;
                const auto headers=received.first(boundary);
                QRegularExpression length("Content-Length: (\\d+)",QRegularExpression::CaseInsensitiveOption);
                auto match=length.match(QString::fromLatin1(headers));
                if(!match.hasMatch() || received.size()<boundary+4+match.captured(1).toInt()) return;
                const QByteArray body=R"({"choices":[{"message":{"content":"Una risposta utile."}}]})";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body); socket->disconnectFromHost();
            });
        });
        LlmClient client; QSignalSpy done(&client,&LlmClient::completed); QSignalSpy failed(&client,&LlmClient::failed);
        client.generate(7,QUrl(QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort())),"test-model","synthetic-key","Quale passo?",QString("x").repeated(12000));
        QTRY_COMPARE(done.size(),1); QCOMPARE(failed.size(),0); QCOMPARE(done[0][0].toULongLong(),7ULL);
        QCOMPARE(done[0][1].toString(),QString("Una risposta utile."));
        QVERIFY(received.contains("Bearer synthetic-key"));
        auto body=QJsonDocument::fromJson(received.mid(received.indexOf("\r\n\r\n")+4)).object();
        QCOMPARE(body["model"].toString(),QString("test-model"));
        const auto content=body["messages"].toArray().at(1).toObject()["content"].toString();
        QVERIFY(content.contains("Quale passo?")); QVERIFY(content.size()<8200);
    }
    void cancellationDoesNotPublishAnAnswer() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        LlmClient client; QSignalSpy done(&client,&LlmClient::completed); QSignalSpy failed(&client,&LlmClient::failed);
        client.generate(8,QUrl(QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort())),"test",{},"question",{});
        client.cancel(); QTest::qWait(30); QCOMPARE(done.size(),0); QCOMPARE(failed.size(),0);
    }
    void providerFailures_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<QByteArray>("body");
        QTest::newRow("redirect") << 302 << QByteArray("redirect");
        QTest::newRow("server-error") << 500 << QByteArray("private provider error content");
        QTest::newRow("invalid-json") << 200 << QByteArray("not json");
        QTest::newRow("oversized") << 200 << QByteArray(1024*1024+1000,'x');
    }
    void providerFailures() {
        QFETCH(int,status); QFETCH(QByteArray,body);
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); bool sent=false;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[&,socket] {
                socket->readAll(); if(sent) return; sent=true;
                const auto response="HTTP/1.1 "+QByteArray::number(status)+" Test\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nLocation: https://example.invalid/redirect\r\nConnection: close\r\n\r\n"+body;
                socket->write(response); socket->disconnectFromHost();
            });
        });
        LlmClient client; QSignalSpy done(&client,&LlmClient::completed); QSignalSpy failed(&client,&LlmClient::failed);
        client.generate(9,QUrl(QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort())),"test",{},"question",{});
        QTRY_COMPARE(failed.size(),1); QCOMPARE(done.size(),0);
        QVERIFY(!failed[0][1].toString().contains("private provider error content"));
    }
};
QTEST_GUILESS_MAIN(NetworkTests)
#include "network_tests.moc"
