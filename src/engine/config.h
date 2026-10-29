// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QSettings>
#include <QStandardPaths>

// Companion configuration: one saved profile per speech engine.
namespace SpeechConfig {
inline bool supported(const QString &backend) {
    return backend == QLatin1String("moonshine") || backend == QLatin1String("vosk");
}

inline QString selected() {
    QSettings config;
    // Preserve existing installations; new installations prefer Moonshine.
    return config.value("speech/backend", config.contains("speech/model") ? "vosk" : "moonshine").toString();
}

inline QVariant value(const QString &backend, const QString &key, const QVariant &fallback = {}) {
    QSettings config;
    const QString path = "profiles/" + backend + "/" + key;
    if (config.contains(path))
        return config.value(path);
    if (backend == QLatin1String("vosk") && config.contains("speech/" + key))
        return config.value("speech/" + key); // Settings written before engine profiles existed.
    return fallback;
}

inline QString library(const QString &backend) {
    return value(backend, "library", backend == QLatin1String("moonshine") ? "libmoonshine.so" : "libvosk.so")
        .toString();
}

inline QStringList requiredSymbols(const QString &backend) {
    if (backend == QLatin1String("moonshine"))
        return {"moonshine_load_transcriber_from_files", "moonshine_free_transcriber", "moonshine_create_stream",
                "moonshine_start_stream", "moonshine_stop_stream", "moonshine_free_stream",
                "moonshine_transcribe_add_audio_to_stream", "moonshine_transcribe_stream"};
    return {"vosk_model_new", "vosk_model_free", "vosk_recognizer_new", "vosk_recognizer_free",
            "vosk_recognizer_accept_waveform", "vosk_recognizer_result", "vosk_recognizer_partial_result"};
}

inline bool modelComplete(const QString &backend, const QString &model) {
    if (model.isEmpty())
        return false;
    const auto has = [&model](const QString &file) { return QFileInfo(model + "/" + file).isFile(); };
    if (backend == QLatin1String("moonshine")) {
        // Current releases name the frontend graph frontend.model.ort.
        if (!has("frontend.ort") && !has("frontend.model.ort"))
            return false;
        for (const auto *file : {"encoder.ort", "adapter.ort", "cross_kv.ort", "decoder_kv.ort",
                                 "streaming_config.json", "tokenizer.bin"})
            if (!has(file))
                return false;
        return true;
    }
    return has("am/final.mdl") && has("conf/mfcc.conf");
}

// Reports what is missing without opening the microphone or loading a model.
inline QJsonObject dependencies(const QString &backend) {
    const auto model = value(backend, "model").toString();
    const auto lib = library(backend);
    QJsonArray missing;
    if (!supported(backend))
        missing.append("Unsupported speech engine. Choose Moonshine or Vosk.");
    if (QStandardPaths::findExecutable("pw-record").isEmpty())
        missing.append("PipeWire capture is missing: install pipewire-audio on Arch or pipewire-bin on Debian.");

    QLibrary runtime(lib);
    bool api = supported(backend) && runtime.load();
    for (const auto &symbol : requiredSymbols(backend))
        api = api && runtime.resolve(symbol.toLatin1().constData());
    runtime.unload();
    if (!api)
        missing.append(backend == QLatin1String("moonshine")
                           ? "Moonshine library or its ONNX Runtime dependency is missing or incompatible. "
                             "Install/configure the Moonshine runtime."
                           : "Vosk library is missing or incompatible. Install/configure the Vosk runtime.");
    if (!modelComplete(backend, model))
        missing.append(backend == QLatin1String("moonshine")
                           ? "Moonshine streaming model is missing. Install a language model and configure its "
                             "extracted folder."
                           : "Extracted Vosk model is missing. Install a language model and configure its "
                             "extracted folder.");
    return {{"ready", missing.isEmpty()},
            {"backend", backend},
            {"missing", missing},
            {"library", lib},
            {"model", model},
            {"language", value(backend, "language").toString()},
            {"architecture", value(backend, "architecture", 2).toInt()},
            {"configFile", QSettings().fileName()},
            {"modelLoadTested", false}, // This check never loads the model.
            {"setup", "https://github.com/brianidavide/kalipso/blob/main/docs/local-engine.md"}};
}

inline QJsonArray backends() {
    QJsonArray result;
    for (const QString id : {"moonshine", "vosk"})
        result.append(QJsonObject{{"id", id},
                                  {"label", id == QLatin1String("moonshine") ? "Moonshine" : "Vosk"},
                                  {"preferred", id == QLatin1String("moonshine")},
                                  {"model", value(id, "model").toString()},
                                  {"language", value(id, "language").toString()}});
    return result;
}
} // namespace SpeechConfig
