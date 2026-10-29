// SPDX-License-Identifier: AGPL-3.0-or-later
#include "import.h"
#include "library.h"
#include "remote.h"
#include "session_stats.h"
#include "tracker.h"
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <thread>

namespace {
// Sends one raw HTTP request from another thread while this thread keeps
// serving, and returns the status code and body.
QPair<int, QByteArray> http(quint16 port, const QByteArray &method, const QString &path,
                            const QByteArray &headers = {}) {
    QByteArray reply;
    std::atomic<bool> done = false;
    std::thread client([&] {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(2000)) {
            socket.write(method + " " + path.toLatin1() + " HTTP/1.1\r\nHost: localhost\r\n" + headers + "\r\n");
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
// An uncompressed ZIP archive, enough to feed crafted files to the importer.
QByteArray storedZip(const QList<QPair<QString, QByteArray>> &files) {
    QByteArray data, central;
    const auto u16 = [](QByteArray &out, quint16 value) { out.append(char(value & 0xff)).append(char(value >> 8)); };
    const auto u32 = [&u16](QByteArray &out, quint32 value) {
        u16(out, quint16(value & 0xffff));
        u16(out, quint16(value >> 16));
    };
    for (const auto &[name, content] : files) {
        const auto offset = quint32(data.size());
        const auto path = name.toUtf8();
        const auto size = quint32(content.size());
        u32(data, 0x04034b50);
        for (int i = 0; i < 5; ++i)
            u16(data, 0); // version, flags, method (stored), time, date
        u32(data, 0);     // CRC, not checked by the importer
        u32(data, size);
        u32(data, size);
        u16(data, quint16(path.size()));
        u16(data, 0);
        data += path + content;
        u32(central, 0x02014b50);
        for (int i = 0; i < 6; ++i)
            u16(central, 0); // versions, flags, method, time, date
        u32(central, 0);
        u32(central, size);
        u32(central, size);
        u16(central, quint16(path.size()));
        for (int i = 0; i < 4; ++i)
            u16(central, 0); // extra, comment, disk, internal attributes
        u32(central, 0);
        u32(central, offset);
        central += path;
    }
    QByteArray end;
    u32(end, 0x06054b50);
    u16(end, 0);
    u16(end, 0);
    u16(end, quint16(files.size()));
    u16(end, quint16(files.size()));
    u32(end, quint32(central.size()));
    u32(end, quint32(data.size()));
    u16(end, 0);
    return data + central + end;
}

QByteArray relationships(const QString &id, const QString &type, const QString &target) {
    return QString(
               R"(<Relationships><Relationship Id="%1" Type="http://x/relationships/%2" Target="%3"/></Relationships>)")
        .arg(id, type, target)
        .toUtf8();
}

// A one-slide presentation whose slide is listed `references` times.
QByteArray presentation(const QString &slideTarget, const QString &notesTarget, int references = 1) {
    QString ids;
    for (int i = 0; i < references; ++i)
        ids += R"(<p:sldId id="256" r:id="rId1"/>)";
    const auto xml =
        QString(R"(<p:presentation xmlns:p="p" xmlns:r="r"><p:sldIdLst>%1</p:sldIdLst></p:presentation>)").arg(ids);
    const QByteArray notes =
        R"(<p:notes xmlns:p="p" xmlns:a="a"><p:cSld><p:spTree><p:sp><p:nvSpPr><p:nvPr><p:ph type="body"/></p:nvPr></p:nvSpPr><p:txBody><a:p><a:r><a:t>Hello notes</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:notes>)";
    return storedZip({{"ppt/presentation.xml", xml.toUtf8()},
                      {"ppt/_rels/presentation.xml.rels", relationships("rId1", "slide", slideTarget)},
                      {"ppt/slides/slide1.xml", "<p:sld xmlns:p=\"p\"/>"},
                      {"ppt/slides/_rels/slide1.xml.rels", relationships("rId2", "notesSlide", notesTarget)},
                      {"ppt/notesSlides/notesSlide1.xml", notes}});
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

    void survivesDamagedIndexUnicodeEmptyAndReadOnlyStorage() {
        QTemporaryDir directory;
        QString unicode, empty;
        {
            ScriptLibrary library(directory.path());
            unicode = library.create(QString::fromUtf8("# Bienvenue 👋\nتحدث ببطء. 自分のペースで。"));
            empty = library.create(QString());
            QVERIFY(!unicode.isEmpty() && !empty.isEmpty());
            library.setPosition(unicode, 3);
            QVERIFY(library.flush());
        }
        // A damaged index loses positions but never scripts.
        QFile index(directory.filePath("library.json"));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write("{\"broken\": [1, 2");
        index.close();
        ScriptLibrary reopened(directory.path());
        QCOMPARE(reopened.load(unicode), QString::fromUtf8("# Bienvenue 👋\nتحدث ببطء. 自分のペースで。"));
        QCOMPARE(reopened.entries().size(), 2);
        QVERIFY(reopened.contains(empty));
        QCOMPARE(reopened.load(empty), QString());
        // Unwritable storage is reported, and the saved text stays as it was.
        QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const bool saved = reopened.save(unicode, "Changed");
        QFile::setPermissions(directory.path(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(!saved);
        QVERIFY(reopened.load(unicode).startsWith(QString::fromUtf8("# Bienvenue 👋")));
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

    void importsDocumentsAsMarkdown_data() {
        QTest::addColumn<QString>("file");
        QTest::newRow("writer odt") << "script.odt";
        QTest::newRow("writer-exported docx") << "script.docx";
    }
    void importsDocumentsAsMarkdown() {
        QFETCH(QString, file);
        const auto path = QFINDTESTDATA("fixtures/" + file);
        QVERIFY2(!path.isEmpty(), qPrintable(file));
        const auto result = ScriptImport::read(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        // Headings become sections, the list stays together, emphasis is kept,
        // and the comment and footnote are not part of the script.
        QCOMPARE(result.text, QString("# Opening\n\nHello **everyone** and *welcome* to the launch.\n\n"
                                      "- First point\n- Second point\n\n## Results\n\n"
                                      "Revenue grew by twelve percent."));
        QCOMPARE(ScriptTracker::sections(result.text).size(), 2);
    }

    void importsCraftedDocumentsSafely() {
        // Word: translated style ids are still headings by their English names; deleted
        // text, field codes and switched-off bold are ignored.
        const QByteArray styles =
            R"(<w:styles xmlns:w="w"><w:style w:styleId="Titolo1"><w:name w:val="heading 1"/></w:style></w:styles>)";
        const QByteArray document = R"(<w:document xmlns:w="w"><w:body>
<w:p><w:pPr><w:pStyle w:val="Titolo1"/></w:pPr><w:r><w:rPr><w:b/></w:rPr><w:t>Apertura</w:t></w:r></w:p>
<w:p><w:r><w:rPr><w:b w:val="0"/></w:rPr><w:t xml:space="preserve">Buongiorno </w:t></w:r><w:del><w:r><w:delText>vecchio</w:delText></w:r></w:del><w:r><w:instrText>PAGE</w:instrText></w:r><w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>a tutti</w:t></w:r></w:p>
<w:p></w:p></w:body></w:document>)";
        auto result = ScriptImport::fromDocx(storedZip({{"word/document.xml", document}, {"word/styles.xml", styles}}));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.text, QString("# Apertura\n\nBuongiorno **a tutti**"));
        // OpenDocument: nested spans combine; tracked deletions and the table of contents are skipped.
        const QByteArray content =
            R"(<office:document-content xmlns:office="o" xmlns:style="s" xmlns:text="t" xmlns:fo="f">
<office:automatic-styles><style:style style:name="T1" style:family="text"><style:text-properties fo:font-style="italic"/></style:style></office:automatic-styles>
<office:body><office:text><text:tracked-changes><text:changed-region><text:deletion><text:p>Removed</text:p></text:deletion></text:changed-region></text:tracked-changes>
<text:table-of-content><text:index-body><text:p>Contents 1</text:p></text:index-body></text:table-of-content>
<text:h text:outline-level="3">Deep</text:h><text:p>One<text:s text:c="2"/><text:span text:style-name="T1">two</text:span></text:p>
</office:text></office:body></office:document-content>)";
        result = ScriptImport::fromOdt(storedZip({{"content.xml", content}}));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.text, QString("### Deep\n\nOne  *two*"));
        QVERIFY(!ScriptImport::fromDocx("not a zip").error.isEmpty());
        QVERIFY(!ScriptImport::fromOdt(storedZip({{"content.xml", "<x/>"}})).error.isEmpty()); // No text.
        QVERIFY(!ScriptImport::fromDocx(storedZip({{"word/document.xml", "<w:document xmlns:w=\"w\"><w:p>"}}))
                     .error.isEmpty());
    }

    void importsCraftedPresentationsSafely() {
        // Normal relative targets.
        auto result = ScriptImport::fromPptx(presentation("slides/slide1.xml", "../notesSlides/notesSlide1.xml"));
        QCOMPARE(result.sections, 1);
        QVERIFY(result.text.contains("Hello notes"));
        // Absolute targets are relative to the package root.
        result = ScriptImport::fromPptx(presentation("/ppt/slides/slide1.xml", "/ppt/notesSlides/notesSlide1.xml"));
        QCOMPARE(result.sections, 1);
        // Targets above the root are ignored instead of crashing.
        result = ScriptImport::fromPptx(presentation("../../../../slides/slide1.xml", "../../../../../x.xml"));
        QCOMPARE(result.sections, 0);
        QVERIFY(!result.error.isEmpty());
        // A slide listed many times is imported once.
        result = ScriptImport::fromPptx(presentation("slides/slide1.xml", "../notesSlides/notesSlide1.xml", 40));
        QCOMPARE(result.sections, 1);
        // A huge space count in a tiny .odp stays small.
        const QByteArray content =
            R"(<office:document-content xmlns:office="o" xmlns:draw="d" xmlns:presentation="p" xmlns:text="t"><office:body><office:presentation><draw:page><presentation:notes><draw:frame><draw:text-box><text:p>A<text:s text:c="1000000000"/>B</text:p></draw:text-box></draw:frame></presentation:notes></draw:page></office:presentation></office:body></office:document-content>)";
        result = ScriptImport::fromOdp(storedZip({{"content.xml", content}}));
        QCOMPARE(result.sections, 1);
        QVERIFY(result.text.size() < 1000);
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
        // Another web page on the network cannot send commands through the browser.
        QCOMPARE(http(remote.port(), "POST", base + "command/next", "Origin: http://evil.example\r\n").first, 403);
        QCOMPARE(commands.size(), 0);
        QCOMPARE(http(remote.port(), "POST", base + "command/next", "Origin: http://localhost\r\n").first, 204);
        QTRY_COMPARE(commands.size(), 1);
        commands.clear();
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
