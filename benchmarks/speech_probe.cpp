// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 4 || args.size() > 6) {
        std::fprintf(stderr, "Usage: speech_probe LIBRARY MODEL PCM16_MONO_16000 [vosk|moonshine] [language]\n");
        return 2;
    }
    qputenv("MOONSHINE_ORT_SINGLE_THREAD", "1");
    auto epoch = std::make_shared<std::atomic<quint64>>(1);
    SpeechWorker worker(epoch);
    bool failed = false, ready = false;
    QObject::connect(&worker, &SpeechWorker::error, &app, [&](quint64, const QString &error) {
        failed = true;
        qWarning() << error;
    });
    QObject::connect(&worker, &SpeechWorker::ready, &app, [&](quint64) { ready = true; });
    QJsonArray final;
    QObject::connect(&worker, &SpeechWorker::transcript, &app, [&](const QString &text, bool isFinal, quint64) {
        if (isFinal && !text.isEmpty())
            final.append(text);
    });
    QElapsedTimer elapsed;
    elapsed.start();
    SpeechRequest request;
    request.library = args[1];
    request.model = args[2];
    request.backend = args.value(4, "vosk");
    request.language = args.value(5, "en");
    worker.load(request, 1);
    const auto loadMs = elapsed.elapsed();
    if (failed || !ready)
        return 1;
    QFile file(args[3]);
    if (!file.open(QIODevice::ReadOnly))
        return 2;
    qint64 bytes = 0;
    elapsed.restart();
    while (!file.atEnd()) {
        const auto chunk = file.read(6400);
        bytes += chunk.size();
        worker.consume(chunk, 1);
    }
    for (int i = 0; i < 10; ++i)
        worker.consume(QByteArray(6400, 0), 1);
    const QJsonObject report{{"model_load_ms", loadMs},
                             {"recognition_ms", elapsed.elapsed()},
                             {"audio_seconds", bytes / 32000.0},
                             {"final_transcripts", final},
                             {"failed", failed}};
    const auto json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, json.size(), stdout);
    return failed || final.isEmpty() ? 1 : 0;
}
