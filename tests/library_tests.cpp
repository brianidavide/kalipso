// SPDX-License-Identifier: AGPL-3.0-or-later
#include "import.h"
#include "library.h"
#include "remote.h"
#include "session_stats.h"
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <thread>

namespace {
// Sends one raw HTTP request from another thread while this thread keeps
// serving, and returns the status code and body.
QPair<int, QByteArray> http(quint16 port, const QByteArray &method, const QString &path) {
    QByteArray reply;
    std::atomic<bool> done = false;
    std::thread client([&] {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(2000)) {
            socket.write(method + " " + path.toLatin1() + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
            socket.waitForBytesWritten(2000);
            while (socket.waitForReadyRead(2000))
                reply += socket.readAll();
        }
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 8000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    client.join();
    return {reply.mid(9, 3).toInt(), reply.mid(reply.indexOf("\r\n\r\n") + 4)};
}
} // namespace

class LibraryTests : public QObject {
    Q_OBJECT
private slots:
    void savesLoadsAndRecoversScripts() {
        QTemporaryDir directory;
        QString id;
        {
            ScriptLibrary library(directory.path());
            id = library.create("# Keynote\nHello");
            QVERIFY(!id.isEmpty());
            QCOMPARE(library.entries().first().title, QString("Keynote"));
            library.setPosition(id, 10);
            QVERIFY(library.flush());
        }
        ScriptLibrary reopened(directory.path());
        QCOMPARE(reopened.load(id), QString("# Keynote\nHello"));
        QCOMPARE(reopened.position(id), 10);
        // A lost index is rebuilt from the script files.
        QVERIFY(QFile::remove(directory.filePath("library.json")));
        ScriptLibrary recovered(directory.path());
        QVERIFY(recovered.contains(id));
        QCOMPARE(recovered.entries().first().title, QString("Keynote"));
        QVERIFY(recovered.remove(id));
        QVERIFY(!QFile::exists(directory.filePath(id + ".txt")));
    }

    void rejectsPathsOutsideTheLibrary() {
        QTemporaryDir directory;
        ScriptLibrary library(directory.path());
        QVERIFY(!library.save("../escape", "x"));
        QVERIFY(library.load("../../etc/passwd").isEmpty());
        QVERIFY(!library.remove("../escape"));
    }

    void titlesComeFromTheFirstLine() {
        QCOMPARE(ScriptLibrary::titleFor("\n\n  ## Q&A  \nbody"), QString("Q&A"));
        QCOMPARE(ScriptLibrary::titleFor(""), QString("Untitled script"));
        QCOMPARE(ScriptLibrary::titleFor(QString(80, u'a')).size(), 60);
    }

    void importsTextAndNormalizesLineEndings() {
        QTemporaryDir directory;
        QFile file(directory.filePath("script.md"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("\xEF\xBB\xBF# Title\r\nLine");
        file.close();
        const auto result = ScriptImport::read(file.fileName());
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.text, QString("# Title\nLine"));
        QFile bad(directory.filePath("bad.txt"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("\xff\xfe\xfa");
        bad.close();
        QVERIFY(!ScriptImport::read(bad.fileName()).error.isEmpty());
    }

    void importsSpeakerNotesFromPresentations_data() {
        QTest::addColumn<QString>("file");
        QTest::newRow("impress odp") << "notes.odp";
        QTest::newRow("impress-exported pptx") << "notes.pptx";
    }
    void importsSpeakerNotesFromPresentations() {
        QFETCH(QString, file);
        const auto path = QFINDTESTDATA("fixtures/" + file);
        QVERIFY2(!path.isEmpty(), qPrintable(file));
        const auto result = ScriptImport::read(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.sections, 2);
        QVERIFY2(result.text.startsWith("# Slide 1: Welcome\n\nThank everyone for coming."), qPrintable(result.text));
        QVERIFY(result.text.contains("Mention the  two  spaces."));
        QVERIFY(result.text.contains("# Slide 3: Results\n\nRevenue grew by twelve percent.\nPause for questions."));
        QVERIFY(!result.text.contains("Slide 2")); // A slide without notes adds no section.
    }

    void rejectsDamagedPresentations() {
        QVERIFY(!ScriptImport::fromPptx("not a zip").error.isEmpty());
        QVERIFY(!ScriptImport::fromOdp(QByteArray(100, 'x')).error.isEmpty());
    }

    void remoteControlRequiresTheSecretLink() {
        RemoteControl remote;
        QVERIFY(remote.start());
        QSignalSpy commands(&remote, &RemoteControl::command);
        remote.setState({{"position", 3}, {"total", 10}});
        const auto base = "/" + remote.token() + "/";
        QCOMPARE(http(remote.port(), "GET", base).first, 200);
        QCOMPARE(http(remote.port(), "GET", base + "app.js").first, 200);
        const auto state = http(remote.port(), "GET", base + "state");
        QCOMPARE(state.first, 200);
        QVERIFY(state.second.contains("\"position\":3"));
        QCOMPARE(http(remote.port(), "GET", "/wrong-token/state").first, 404);
        QCOMPARE(http(remote.port(), "GET", base + "command/next").first, 405);
        QCOMPARE(http(remote.port(), "POST", base + "command/rm").first, 404);
        QCOMPARE(commands.size(), 0);
        QCOMPARE(http(remote.port(), "POST", base + "command/next").first, 204);
        QTRY_COMPARE(commands.size(), 1);
        QCOMPARE(commands.first().first().toString(), QString("next"));
        const auto oldToken = remote.token();
        remote.stop();
        QVERIFY(remote.start());
        QVERIFY(remote.token() != oldToken);
        QCOMPARE(http(remote.port(), "GET", "/" + oldToken + "/").first, 404);
        QVERIFY(!RemoteControl::qrCode(remote.url()).isNull());
    }

    void sessionSummaryMeasuresPaceAndPauses() {
        SessionStats stats;
        stats.start(-1, 0);
        stats.advance(9, 2000);
        stats.advance(19, 9000); // 7 s pause.
        stats.advance(5, 9500);  // Backtracking does not count.
        stats.transcript("um so uh we start");
        const auto summary = stats.finish(12000);
        QCOMPARE(summary.words, 20);
        QCOMPARE(summary.durationMs, 12000);
        QCOMPARE(qRound(summary.wordsPerMinute), 100);
        QCOMPARE(summary.longPauses, 1);
        QCOMPARE(summary.longestPauseMs, 7000);
        QCOMPARE(summary.fillers, 2);
    }
};

QTEST_MAIN(LibraryTests)
#include "library_tests.moc"
