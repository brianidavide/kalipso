// SPDX-License-Identifier: AGPL-3.0-or-later
// Speech model downloads against a server on this computer: what is kept, and what is refused.
#include "speech_download.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>

namespace {
quint32 crc32(const QByteArray &data) {
    quint32 crc = 0xffffffff;
    for (const auto byte : data) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320 & (0 - (crc & 1)));
    }
    return ~crc;
}

struct Member {
    QString name;
    QByteArray data;
    bool link = false;
};

// A ZIP archive with stored members; a link is marked as a Unix symbolic link.
QByteArray zip(const QList<Member> &members) {
    QByteArray body, directory;
    const auto u16 = [](QByteArray &out, quint16 value) {
        char bytes[2];
        qToLittleEndian(value, bytes);
        out.append(bytes, 2);
    };
    const auto u32 = [](QByteArray &out, quint32 value) {
        char bytes[4];
        qToLittleEndian(value, bytes);
        out.append(bytes, 4);
    };
    for (const auto &member : members) {
        const auto name = member.name.toUtf8();
        const quint32 offset = body.size(), crc = crc32(member.data), size = member.data.size();
        u32(body, 0x04034b50);
        for (const quint16 value : {quint16(20), quint16(0), quint16(0), quint16(0), quint16(0)})
            u16(body, value);
        u32(body, crc);
        u32(body, size);
        u32(body, size);
        u16(body, name.size());
        u16(body, 0);
        body += name + member.data;
        u32(directory, 0x02014b50);
        u16(directory, (3 << 8) | 20); // Made on Unix.
        for (const quint16 value : {quint16(20), quint16(0), quint16(0), quint16(0), quint16(0)})
            u16(directory, value);
        u32(directory, crc);
        u32(directory, size);
        u32(directory, size);
        u16(directory, name.size());
        for (const quint16 value : {quint16(0), quint16(0), quint16(0), quint16(0)})
            u16(directory, value);
        u32(directory, quint32(member.link ? 0120777 : 0100644) << 16);
        u32(directory, offset);
        directory += name;
    }
    QByteArray end;
    u32(end, 0x06054b50);
    u16(end, 0);
    u16(end, 0);
    u16(end, members.size());
    u16(end, members.size());
    u32(end, directory.size());
    u32(end, body.size());
    u16(end, 0);
    return body + directory + end;
}

QString sha(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

// Serves fixed files over plain HTTP; a "slow" file sends its first bytes and then waits.
class Server : public QTcpServer {
public:
    QHash<QString, QByteArray> files;
    QString slow;
    QStringList requested;
    Server() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const auto request = socket->readAll();
                    const auto path = QString::fromLatin1(request.split(' ').value(1));
                    requested.append(path);
                    if (!files.contains(path)) {
                        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    const auto &data = files[path];
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(data.size()) +
                                  "\r\nConnection: close\r\n\r\n");
                    if (path == slow) {
                        socket->write(data.left(1024));
                        return; // The rest never comes.
                    }
                    socket->write(data);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QString url(const QString &path) const { return QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path); }
};

// Every file under a folder, relative to it.
QStringList filesUnder(const QString &root) {
    QStringList found;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        found.append(QDir(root).relativeFilePath(it.next()));
    found.sort();
    return found;
}
} // namespace

class DownloadTests : public QObject {
    Q_OBJECT

    QByteArray runtime_ = zip({{"vosk/libvosk.so", "native library"}, {"vosk/__init__.py", "python"}});
    QByteArray model_ = zip({{"vosk-model-small-xx-1/am/final.mdl", "acoustic model"},
                             {"vosk-model-small-xx-1/conf/mfcc.conf", "features"},
                             {"vosk-model-small-xx-1/README", "readme"}});

    // A catalog shaped like tools/data/speech-catalog.json, pointing at the local server.
    QJsonObject catalog(Server &server, const QByteArray &runtime, const QByteArray &model,
                        const QString &modelHash = {}) {
        server.files["/runtime.whl"] = runtime;
        server.files["/model.zip"] = model;
        server.files["/moon/encoder.ort"] = "encoder";
        return QJsonObject{
            {"vosk",
             QJsonObject{
                 {"version", "9.9"},
                 {"url", server.url("/runtime.whl")},
                 {"sha256", sha(runtime)},
                 {"bytes", runtime.size()},
                 {"library", "vosk/libvosk.so"},
                 {"nativeFiles", QJsonArray{"vosk/libvosk.so"}},
                 {"models", QJsonObject{{"xx", QJsonObject{{"name", "vosk-model-small-xx-1"},
                                                           {"url", server.url("/model.zip")},
                                                           {"sha256", modelHash.isEmpty() ? sha(model) : modelHash},
                                                           {"bytes", model.size()},
                                                           {"license", "Apache-2.0"}}}}}}}};
    }

    struct Run {
        QList<QVariant> finished;
        SpeechDownload::Error error = SpeechDownload::Error::None;
        QString detail;
    };
    Run run(SpeechDownload &download, const QString &backend, const QString &language,
            const std::function<void()> &whileRunning = {}) {
        Run result;
        bool done = false;
        connect(&download, &SpeechDownload::finished, this, [&](const InstalledSpeech &speech) {
            result.finished = {speech.backend, speech.library, speech.model, speech.language};
            done = true;
        });
        connect(&download, &SpeechDownload::failed, this, [&](SpeechDownload::Error error, const QString &detail) {
            result.error = error;
            result.detail = detail;
            done = true;
        });
        download.start(backend, language);
        if (whileRunning)
            whileRunning();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 10000); }();
        download.disconnect(this);
        return result;
    }

private slots:
    void keepsOnlyVerifiedNativeFilesAndTheModel() {
        Server server;
        QTemporaryDir root;
        SpeechDownload download;
        download.setCatalog(catalog(server, runtime_, model_));
        download.setRoot(root.path());
        download.allowLocalHttp();
        const auto result = run(download, "vosk", "xx");
        QCOMPARE(result.error, SpeechDownload::Error::None);
        QCOMPARE(result.finished.value(0).toString(), QString("vosk"));
        QCOMPARE(result.finished.value(1).toString(), root.path() + "/runtimes/vosk-9.9/vosk/libvosk.so");
        QCOMPARE(result.finished.value(2).toString(), root.path() + "/models/vosk/vosk-model-small-xx-1");
        const auto files = filesUnder(root.path());
        // The Python package is not kept; the native library, its notices and sources are.
        QVERIFY(files.contains("runtimes/vosk-9.9/vosk/libvosk.so"));
        QVERIFY(!files.contains("runtimes/vosk-9.9/vosk/__init__.py"));
        QVERIFY(files.contains("runtimes/vosk-9.9/source.json"));
        QVERIFY(files.contains("runtimes/vosk-9.9/notices/vosk-0.3.45-COPYING"));
        QVERIFY(files.contains("models/vosk/vosk-model-small-xx-1/am/final.mdl"));
        QVERIFY(files.contains("models/vosk/vosk-model-small-xx-1/kalipso-source.json"));
        for (const auto &file : files)
            QVERIFY2(!file.startsWith(".downloads") && !file.contains(".install-"), qPrintable(file));
        QCOMPARE(guessModelLanguage("vosk", root.path() + "/models/vosk/vosk-model-small-xx-1"), QString("xx"));

        // Installed: a second request downloads nothing.
        server.requested.clear();
        QCOMPARE(SpeechCatalog::plan("vosk", "xx", root.path(), catalog(server, runtime_, model_)).parts.size(), 0);
        QCOMPARE(run(download, "vosk", "xx").error, SpeechDownload::Error::None);
        QVERIFY(server.requested.isEmpty());
    }

    void aWrongFingerprintLeavesNothingInstalled() {
        Server server;
        QTemporaryDir root;
        SpeechDownload download;
        download.setCatalog(catalog(server, runtime_, model_, QString(64, u'0')));
        download.setRoot(root.path());
        download.allowLocalHttp();
        const auto result = run(download, "vosk", "xx");
        QCOMPARE(result.error, SpeechDownload::Error::Fingerprint);
        QVERIFY(!QFileInfo::exists(root.path() + "/models/vosk/vosk-model-small-xx-1"));
        QVERIFY(QDir(root.path() + "/.downloads").isEmpty());
    }

    void unsafeArchivesAreRefusedBeforeWriting() {
        for (const auto &bad : {QList<Member>{{"vosk-model-small-xx-1/am/final.mdl", "ok"}, {"../escaped", "bad"}},
                                QList<Member>{{"vosk-model-small-xx-1/am/final.mdl", "ok"},
                                              {"vosk-model-small-xx-1/conf/mfcc.conf", "/etc/passwd", true}},
                                QList<Member>{{"/absolute", "bad"}}}) {
            Server server;
            QTemporaryDir root;
            SpeechDownload download;
            download.setCatalog(catalog(server, runtime_, zip(bad)));
            download.setRoot(root.path());
            download.allowLocalHttp();
            const auto result = run(download, "vosk", "xx");
            QCOMPARE(result.error, SpeechDownload::Error::Archive);
            QVERIFY(!QFileInfo::exists(root.path() + "/models/vosk/vosk-model-small-xx-1"));
            QVERIFY(!QFileInfo::exists(root.path() + "/escaped"));
            QVERIFY(!QFileInfo::exists(root.path() + "/models/escaped"));
            QVERIFY(QDir(root.path() + "/models/vosk").isEmpty());
        }
    }

    void aFileLargerThanTheCatalogSaysIsStopped() {
        Server server;
        QTemporaryDir root;
        SpeechDownload download;
        auto source = catalog(server, runtime_, model_);
        auto vosk = source["vosk"].toObject();
        vosk["bytes"] = runtime_.size() - 10;
        source["vosk"] = vosk;
        download.setCatalog(source);
        download.setRoot(root.path());
        download.allowLocalHttp();
        QCOMPARE(run(download, "vosk", "xx").error, SpeechDownload::Error::Fingerprint);
        QVERIFY(!QFileInfo::exists(root.path() + "/runtimes/vosk-9.9"));
    }

    void cancellingRemovesTheUnfinishedFile() {
        Server server;
        QTemporaryDir root;
        SpeechDownload download;
        auto big = QByteArray(256 * 1024, 'x');
        download.setCatalog(catalog(server, zip({{"vosk/libvosk.so", big}}), model_));
        server.slow = "/runtime.whl";
        download.setRoot(root.path());
        download.allowLocalHttp();
        QSignalSpy progress(&download, &SpeechDownload::progress);
        const auto result = run(download, "vosk", "xx", [&] {
            QTRY_VERIFY(!server.requested.isEmpty());
            QTRY_VERIFY(QFileInfo(root.path() + "/.downloads").isDir() && !QDir(root.path() + "/.downloads").isEmpty());
            download.cancel();
        });
        QCOMPARE(result.error, SpeechDownload::Error::Cancelled);
        QVERIFY(!download.active());
        QVERIFY(QDir(root.path() + "/.downloads").isEmpty());
        QVERIFY(!QFileInfo::exists(root.path() + "/runtimes/vosk-9.9"));
    }

    void onlySecureOrLocalSourcesAreFetched() {
        Server server;
        QTemporaryDir root;
        SpeechDownload download;
        auto source = catalog(server, runtime_, model_);
        auto vosk = source["vosk"].toObject();
        vosk["url"] = "http://example.com/runtime.whl";
        source["vosk"] = vosk;
        download.setCatalog(source);
        download.setRoot(root.path());
        download.allowLocalHttp(); // Plain HTTP only from this computer.
        const auto result = run(download, "vosk", "xx");
        QCOMPARE(result.error, SpeechDownload::Error::Network);
        QVERIFY(result.detail.contains("HTTPS"));
    }

    void missingLanguagesAndNamesComeFromTheCatalog() {
        Server server;
        QTemporaryDir root;
        SpeechDownload download;
        download.setCatalog(catalog(server, runtime_, model_));
        download.setRoot(root.path());
        download.allowLocalHttp();
        QCOMPARE(run(download, "vosk", "it").error, SpeechDownload::Error::Unsupported);
        QVERIFY(server.requested.isEmpty());
        // The built-in catalog: every language has a model and a name of its own.
        const auto languages = SpeechCatalog::languages();
        QCOMPARE(languages, (QStringList{"de", "en", "es", "it", "ja", "zh"}));
        QCOMPARE(SpeechCatalog::languageName("it"), QString("Italiano"));
        QCOMPARE(SpeechCatalog::downloadBackend("it"), QString("vosk"));
        QCOMPARE(SpeechCatalog::downloadBackend("en"), QString("moonshine"));
        QCOMPARE(SpeechCatalog::downloadBackend("en", "vosk"), QString("vosk"));
        for (const auto &code : languages) {
            const auto plan = SpeechCatalog::plan(SpeechCatalog::downloadBackend(code), code, root.path());
            QVERIFY(plan.valid);
            for (const auto &part : plan.parts) {
                QCOMPARE(part.url.scheme(), QString("https"));
                QCOMPARE(part.sha256.size(), 64);
                QVERIFY2(part.bytes > 0, qPrintable(part.url.toString()));
            }
        }
    }
};

QTEST_GUILESS_MAIN(DownloadTests)
#include "download_tests.moc"
