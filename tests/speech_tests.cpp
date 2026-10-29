// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include <QSignalSpy>
#include <QtTest>

class SpeechTests : public QObject {
    Q_OBJECT
private slots:
    void missingLibraryIsRecoverable() {
        auto epoch = std::make_shared<std::atomic<quint64>>(1);
        SpeechWorker worker(epoch);
        QSignalSpy error(&worker, &SpeechWorker::error);
        QSignalSpy ready(&worker, &SpeechWorker::ready);
        worker.load({"vosk", "/missing/kalipso-test-lib.so", "/missing-model"}, 1);
        QCOMPARE(error.size(), 1);
        QCOMPARE(ready.size(), 0);
        worker.unload();
        worker.unload();
    }
    void missingMoonshineIsRecoverable() {
        auto epoch = std::make_shared<std::atomic<quint64>>(1);
        SpeechWorker worker(epoch);
        QSignalSpy error(&worker, &SpeechWorker::error);
        QSignalSpy ready(&worker, &SpeechWorker::ready);
        worker.load({"moonshine", "/missing/moonshine.so", "/missing-model", {}, "en", 2}, 1);
        QCOMPARE(error.size(), 1);
        QCOMPARE(ready.size(), 0);
        worker.unload();
        worker.unload();
    }
    void unsupportedBackendCannotFallBackSilently() {
        auto epoch = std::make_shared<std::atomic<quint64>>(1);
        SpeechWorker worker(epoch);
        QSignalSpy error(&worker, &SpeechWorker::error);
        worker.load({"unknown", "/missing.so", "/missing"}, 1);
        QCOMPARE(error.size(), 1);
    }
    void obsoleteLoadsAreSkippedBeforeLibraryAccess() {
        auto epoch = std::make_shared<std::atomic<quint64>>(2);
        SpeechWorker worker(epoch);
        QSignalSpy error(&worker, &SpeechWorker::error);
        QSignalSpy ready(&worker, &SpeechWorker::ready);
        worker.load({"vosk", "/missing/kalipso-test-lib.so", "/missing-model"}, 1);
        QCOMPARE(error.size(), 0);
        QCOMPARE(ready.size(), 0);
    }
    void moonshineRotatesHistoryOnlyAfterDrainingAtQuietBoundary() {
        QLibrary library(FAKE_MOONSHINE_LIBRARY);
        // Keep the test observer alive after the recognizer releases its handle.
        library.setLoadHints(QLibrary::PreventUnloadHint);
        QVERIFY(library.load());
        auto count = [&](const char *symbol) {
            const auto function = reinterpret_cast<int (*)()>(library.resolve(symbol));
            if (!function) {
                QTest::qFail(qPrintable(library.errorString()), __FILE__, __LINE__);
                return -1;
            }
            return function();
        };
        // Resolve the observer before cleanup: Qt 6.4 clears its shared resolve handle
        // after unload, even with PreventUnloadHint retaining the mapped code.
        const auto freeCount = reinterpret_cast<int (*)()>(library.resolve("fake_frees"));
        QVERIFY(freeCount);
        MoonshineRecognizer recognizer;
        QString error;
        QList<MoonshineRecognizer::Event> events;
        QVERIFY2(recognizer.load(FAKE_MOONSHINE_LIBRARY, "test-model", "en", 2, error), qPrintable(error));
        QCOMPARE(count("fake_without_audio"), 1);
        for (int i = 0; i < 3; ++i)
            QVERIFY(recognizer.consume(QByteArray(6400, 0), events, error));
        QCOMPARE(events.size(), 64);
        QCOMPARE(count("fake_starts"), 1);
        QCOMPARE(count("fake_stops"), 0);
        events.clear();
        for (int i = 0; i < 3; ++i)
            QVERIFY(recognizer.consume(QByteArray(6400, 0), events, error));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].text, QString("drained pending audio"));
        QCOMPARE(count("fake_starts"), 2);
        QCOMPARE(count("fake_drains"), 1);
        recognizer.unload();
        recognizer.unload();
        QCOMPARE(freeCount(), 1);
    }
    void regionalLanguageTagsKeepMoonshineLanguageOptions() {
        QLibrary library(FAKE_MOONSHINE_LIBRARY);
        library.setLoadHints(QLibrary::PreventUnloadHint);
        QVERIFY(library.load());
        const auto nonLatin = reinterpret_cast<int (*)()>(library.resolve("fake_non_latin"));
        QVERIFY(nonLatin);
        for (const auto &language : {"en-US", "ja-JP", "zh_CN"}) {
            MoonshineRecognizer recognizer;
            QString error;
            QVERIFY2(recognizer.load(FAKE_MOONSHINE_LIBRARY, "test-model", language, 2, error), qPrintable(error));
            QCOMPARE(nonLatin(), QString(language) == "en-US" ? 0 : 1);
        }
    }
    void fallingBehindSkipsAudioUnlessItKeepsHappening() {
        CatchUpPolicy policy;
        QCOMPARE(policy.overloaded(0), CatchUpPolicy::Skip);
        QCOMPARE(policy.overloaded(70000), CatchUpPolicy::Skip); // The first spike has left the window.
        QCOMPARE(policy.overloaded(90000), CatchUpPolicy::Skip);
        QCOMPARE(policy.overloaded(100000), CatchUpPolicy::Stop); // Three within a minute: too slow.
        policy.reset();
        QCOMPARE(policy.overloaded(100500), CatchUpPolicy::Skip);
        // Skipped chunks are counted as consumed without reaching the recognizer.
        auto epoch = std::make_shared<std::atomic<quint64>>(0);
        auto skip = std::make_shared<std::atomic<quint64>>(5);
        SpeechWorker worker(epoch, skip);
        QSignalSpy consumed(&worker, &SpeechWorker::consumed);
        worker.consume(QByteArray(6400, 0), 0, 4);
        QCOMPARE(consumed.size(), 1);
        QCOMPARE(consumed.first().at(2).toBool(), true);
        worker.consume(QByteArray(6400, 0), 0, 5);
        QCOMPARE(consumed.last().at(2).toBool(), false);
    }

    void cancelledAudioProducesNoTranscript() {
        auto epoch = std::make_shared<std::atomic<quint64>>(2);
        SpeechWorker worker(epoch);
        QSignalSpy transcript(&worker, &SpeechWorker::transcript);
        QSignalSpy consumed(&worker, &SpeechWorker::consumed);
        worker.consume(QByteArray(6400, 0), 1);
        QCOMPARE(transcript.size(), 0);
        QCOMPARE(consumed.size(), 1);
    }
};
QTEST_GUILESS_MAIN(SpeechTests)
#include "speech_tests.moc"
