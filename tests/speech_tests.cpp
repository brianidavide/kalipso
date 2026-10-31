// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fake_pipewire.h"
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

    void pipeWireListsMicrophonesAndTheSystemOne() {
        const QByteArray dump = R"([
            {"id": 40, "type": "PipeWire:Interface:Node", "info": {"props": {"media.class": "Audio/Sink",
                "node.name": "speakers", "node.description": "Speakers"}}},
            {"id": 41, "type": "PipeWire:Interface:Node", "info": {"props": {"media.class": "Audio/Source",
                "node.name": "alsa_input.usb", "node.description": "Blue Yeti"}}},
            {"id": 42, "type": "PipeWire:Interface:Node", "info": {"props": {"media.class": "Audio/Source",
                "node.name": "bluez_input.headset", "node.nick": "Headset"}}},
            {"id": 43, "type": "PipeWire:Interface:Node", "info": {"props": {"media.class": "Stream/Input/Audio",
                "node.name": "pw-record"}}},
            {"id": 44, "type": "PipeWire:Interface:Metadata", "props": {"metadata.name": "default"},
             "metadata": [{"subject": 0, "key": "default.audio.sink", "value": {"name": "speakers"}},
                          {"subject": 0, "key": "default.audio.source", "value": {"name": "bluez_input.headset"}}]}
        ])";
        // What listening with a saved choice opens; "none" when it opens nothing.
        const auto opens = [](const Microphones &list, const QString &id) {
            const auto found = list.find(id);
            return found ? found->id : QString("none");
        };
        const auto devices = Microphones::fromPipeWire(dump);
        QVERIFY(devices.known);
        QCOMPARE(devices.inputs.size(), 2); // Not the speakers, not Kalipso's own capture stream.
        QCOMPARE(devices.inputs[0].name, QString("Blue Yeti"));
        QCOMPARE(devices.inputs[1].name, QString("Headset"));
        QCOMPARE(opens(devices, QString()), QString("bluez_input.headset"));
        QCOMPARE(opens(devices, "alsa_input.usb"), QString("alsa_input.usb"));
        QCOMPARE(opens(devices, "Blue Yeti"), QString("alsa_input.usb")); // Typed by hand before the list.
        QCOMPARE(opens(devices, "alsa_input.unplugged"), QString("none"));

        // Some PipeWire versions print the default as a string of JSON.
        auto quoted = dump;
        quoted.replace(R"({"name": "bluez_input.headset"})", "\"{\\\"name\\\": \\\"alsa_input.usb\\\"}\"");
        QCOMPARE(opens(Microphones::fromPipeWire(quoted), QString()), QString("alsa_input.usb"));
        // A default that is not a microphone (the speakers' monitor) falls back to a microphone.
        auto monitor = dump;
        monitor.replace(R"({"name": "bluez_input.headset"})", R"({"name": "speakers.monitor"})");
        QCOMPARE(opens(Microphones::fromPipeWire(monitor), QString()), QString("alsa_input.usb"));
        // No microphone at all: none, rather than the sound of the speakers.
        const auto speakersOnly = Microphones::fromPipeWire(R"([{"id": 40, "type": "PipeWire:Interface:Node",
            "info": {"props": {"media.class": "Audio/Sink", "node.name": "speakers"}}}])");
        QVERIFY(speakersOnly.known);
        QCOMPARE(opens(speakersOnly, QString()), QString("none"));
        QVERIFY(!Microphones::fromPipeWire("pw-dump: can't connect").known);
    }

    void levelFollowsTheLoudestSample() {
        QCOMPARE(MicrophoneCapture::level(QByteArray(320, 0)), 0.0);
        QVERIFY(MicrophoneCapture::level(FakePipeWire::tone(1, 20)) > 0.99);
        QVERIFY(std::abs(MicrophoneCapture::level(FakePipeWire::tone(0.0316, 20)) - 0.5) < 0.01); // -30 dB
        QVERIFY(MicrophoneCapture::level(FakePipeWire::tone(0.0005, 20)) < 0.05);                 // A quiet room.
    }

#ifndef KALIPSO_QT_AUDIO
    void captureOpensTheSystemMicrophoneByName() {
        FakePipeWire pipewire;
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}, {"bluez_input.headset", "Headset"}}, "alsa_input.usb");
        pipewire.setSound(FakePipeWire::tone(0.5, 200));
        MicrophoneCapture capture;
        QSignalSpy started(&capture, &MicrophoneCapture::started);
        QSignalSpy audio(&capture, &MicrophoneCapture::audio);
        capture.start(QString());
        QVERIFY(started.wait(3000));
        QCOMPARE(started.first().first().toString(), QString("Blue Yeti"));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.isEmpty(), 3000);
        // The system's microphone: capture follows the system's choice, even when it changes.
        QVERIFY(!pipewire.recordArguments().contains("--target"));
        for (const auto &chunk : audio)
            QCOMPARE(chunk.first().toByteArray().size() % 2, 0); // Whole samples only.

        // Chosen in Settings: that one, and not another one when it is unplugged.
        capture.start("bluez_input.headset");
        QTRY_COMPARE_WITH_TIMEOUT(capture.name(), QString("Headset"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(pipewire.recordArguments().contains("bluez_input.headset"), 3000);
        auto arguments = pipewire.recordArguments();
        QCOMPARE(arguments.at(arguments.indexOf("--target") + 1), QString("bluez_input.headset"));
        QVERIFY(arguments.at(arguments.indexOf("--properties") + 1).contains(R"("node.dont-reconnect": true)"));

        // The system names a sink monitor: a microphone, not the sound of the speakers.
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}}, "speakers.monitor");
        capture.start(QString());
        QTRY_VERIFY_WITH_TIMEOUT(pipewire.recordArguments().contains("alsa_input.usb"), 3000);
        arguments = pipewire.recordArguments();
        QCOMPARE(arguments.at(arguments.indexOf("--target") + 1), QString("alsa_input.usb"));
        capture.stop();
    }

    void captureSaysWhenThereIsNoMicrophoneWithoutOpeningOne() {
        FakePipeWire pipewire;
        pipewire.setNodes({{"speakers", "Speakers", "Audio/Sink"}}, QString());
        MicrophoneCapture capture;
        QSignalSpy failed(&capture, &MicrophoneCapture::failed);
        capture.start(QString());
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains("finds no microphone"));
        QVERIFY(MicrophoneCapture::problem(QString()).contains("finds no microphone"));
        QVERIFY(pipewire.recordArguments().isEmpty());

        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}}, "alsa_input.usb");
        capture.start("alsa_input.unplugged");
        QCOMPARE(failed.size(), 2);
        QVERIFY(failed.last().first().toString().contains("not connected"));
        QVERIFY(pipewire.recordArguments().isEmpty());
        QVERIFY(MicrophoneCapture::problem("alsa_input.usb").isEmpty()); // Negative control.
    }

    void captureNoticesAMutedMicrophoneAndItsReturn() {
        FakePipeWire pipewire;
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}}, "alsa_input.usb");
        pipewire.setSound(FakePipeWire::tone(0, 200)); // Muted: exact zeros.
        MicrophoneCapture capture;
        capture.setSilenceTimeout(300);
        QSignalSpy silent(&capture, &MicrophoneCapture::silent);
        capture.start(QString());
        QVERIFY(silent.wait(3000));
        QCOMPARE(silent.first().first().toBool(), true);
        capture.stop();

        // Negative control: a quiet room is not silence.
        pipewire.setSound(FakePipeWire::tone(0.0005, 1000));
        silent.clear();
        capture.start(QString());
        QVERIFY(!silent.wait(800));
        capture.stop();
    }

    void captureSaysWhyPipeWireStopped() {
        FakePipeWire pipewire;
        pipewire.setNodes({{"alsa_input.usb", "Blue Yeti"}}, "alsa_input.usb");
        pipewire.failWith("error: stream disconnected\n");
        MicrophoneCapture capture;
        QSignalSpy failed(&capture, &MicrophoneCapture::failed);
        capture.start(QString());
        QVERIFY(failed.wait(3000));
        QCOMPARE(failed.first().first().toString(),
                 QString("Audio capture from Blue Yeti stopped: error: stream disconnected"));
        QVERIFY(!capture.active());
    }
#endif
};
QTEST_GUILESS_MAIN(SpeechTests)
#include "speech_tests.moc"
