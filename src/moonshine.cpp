// SPDX-License-Identifier: AGPL-3.0-or-later
#include "moonshine.h"
#include "../third_party/moonshine/moonshine-c-api.h"
#include <QElapsedTimer>
#include <QLibrary>
#include <QtEndian>
#include <algorithm>
#include <vector>

namespace {
constexpr double kSampleRate = 16000.0;
// The upstream stream keeps its whole text history; rotate it periodically.
constexpr uint64_t kRotateAfterLines = 64;
constexpr uint64_t kHistoryLimit = 256;
} // namespace

struct MoonshineRecognizer::Impl {
    QLibrary library;
    int32_t transcriber = -1;
    int32_t stream = -1;
    double pendingSeconds = 0, lastPassSeconds = 0, audioSeconds = 0;
    uint64_t firstIncomplete = 0;
    QString lastText;
    decltype(&moonshine_load_transcriber_from_files) load = nullptr;
    decltype(&moonshine_free_transcriber) free = nullptr;
    decltype(&moonshine_create_stream) create = nullptr;
    decltype(&moonshine_start_stream) start = nullptr;
    decltype(&moonshine_stop_stream) stop = nullptr;
    decltype(&moonshine_free_stream) release = nullptr;
    decltype(&moonshine_transcribe_add_audio_to_stream) add = nullptr;
    decltype(&moonshine_transcribe_stream) transcribe = nullptr;

    // Completed lines are immutable. Copy borrowed UTF-8 before the next API call.
    void collect(const transcript_t *result, QList<Event> &events) {
        for (uint64_t i = firstIncomplete; i < result->line_count; ++i) {
            const auto &line = result->lines[i];
            const auto text = QString::fromUtf8(line.text ? line.text : "");
            if (!text.isEmpty() && (text != lastText || line.is_complete))
                events.append({text, line.is_complete != 0});
            lastText = text;
            if (line.is_complete) {
                firstIncomplete = i + 1;
                lastText.clear();
            }
        }
    }
};

MoonshineRecognizer::MoonshineRecognizer() : impl_(std::make_unique<Impl>()) {}
MoonshineRecognizer::~MoonshineRecognizer() { unload(); }

void MoonshineRecognizer::unload() {
    auto &p = *impl_;
    if (p.stream >= 0 && p.transcriber >= 0) {
        // Stop is cancellation: do not emit a late final into an edited script.
        if (p.stop)
            p.stop(p.transcriber, p.stream);
        if (p.release)
            p.release(p.transcriber, p.stream);
    }
    p.stream = -1;
    if (p.transcriber >= 0 && p.free)
        p.free(p.transcriber);
    p.transcriber = -1;
    if (p.library.isLoaded())
        p.library.unload();
    p.library.setFileName(QString());
    p.pendingSeconds = p.lastPassSeconds = p.audioSeconds = 0;
    p.firstIncomplete = 0;
    p.lastText.clear();
}

bool MoonshineRecognizer::load(const QString &library, const QString &model, const QString &language,
                               int architecture, QString &error) {
    unload();
    auto &p = *impl_;
    if (architecture < 2 || architecture > 5) {
        error = QStringLiteral("Select a Moonshine streaming model architecture.");
        return false;
    }
    p.library.setFileName(library);
    if (!p.library.load()) {
        error = QStringLiteral("Cannot load Moonshine and its ONNX Runtime dependency: ") + p.library.errorString();
        return false;
    }
    const auto resolve = [&p](auto &member, const char *name) {
        member = reinterpret_cast<std::remove_reference_t<decltype(member)>>(p.library.resolve(name));
        return member != nullptr;
    };
    const bool complete =
        resolve(p.load, "moonshine_load_transcriber_from_files") & resolve(p.free, "moonshine_free_transcriber") &
        resolve(p.create, "moonshine_create_stream") & resolve(p.start, "moonshine_start_stream") &
        resolve(p.stop, "moonshine_stop_stream") & resolve(p.release, "moonshine_free_stream") &
        resolve(p.add, "moonshine_transcribe_add_audio_to_stream") &
        resolve(p.transcribe, "moonshine_transcribe_stream");
    if (!complete) {
        error = QStringLiteral("The library does not provide the supported Moonshine streaming API.");
        unload();
        return false;
    }
    // Japanese and Chinese produce more tokens per second of speech.
    const moonshine_option_t options[] = {{"return_audio_data", "false"}, {"max_tokens_per_second", "13.0"}};
    const auto baseLanguage = language.toLower().replace('_', '-').section('-', 0, 0);
    const bool denseLanguage = baseLanguage == QLatin1String("ja") || baseLanguage == QLatin1String("zh");
    p.transcriber = p.load(model.toUtf8().constData(), uint32_t(architecture), options, denseLanguage ? 2 : 1,
                           MOONSHINE_HEADER_VERSION);
    if (p.transcriber < 0) {
        error = QStringLiteral("Moonshine could not load this model or API version (%1). Check the model "
                               "architecture and runtime version.")
                    .arg(p.transcriber);
        unload();
        return false;
    }
    p.stream = p.create(p.transcriber, 0);
    if (p.stream < 0 || p.start(p.transcriber, p.stream) < 0) {
        error = QStringLiteral("Moonshine could not start the recognition stream.");
        unload();
        return false;
    }
    return true;
}

bool MoonshineRecognizer::consume(const QByteArray &pcm, QList<Event> &events, QString &error) {
    auto &p = *impl_;
    if (p.stream < 0 || pcm.size() % 2) {
        error = QStringLiteral("Invalid Moonshine audio stream or PCM input.");
        return false;
    }
    std::vector<float> audio(size_t(pcm.size() / 2));
    for (size_t i = 0; i < audio.size(); ++i)
        audio[i] = qFromLittleEndian<qint16>(pcm.constData() + 2 * i) / 32768.0F;
    if (p.add(p.transcriber, p.stream, audio.data(), audio.size(), int32_t(kSampleRate), 0) < 0) {
        error = QStringLiteral("Moonshine could not accept the captured audio.");
        return false;
    }
    const double seconds = double(audio.size()) / kSampleRate;
    p.pendingSeconds += seconds;
    p.audioSeconds += seconds;
    // Match the SDK's adaptive update floor; capture backpressure lives in SpeechService.
    if (p.pendingSeconds + 1e-9 < std::clamp(p.lastPassSeconds, 0.5, 2.0))
        return true;

    transcript_t *result = nullptr;
    QElapsedTimer timer;
    timer.start();
    const auto status = p.transcribe(p.transcriber, p.stream, 0, &result);
    p.lastPassSeconds = timer.nsecsElapsed() / 1e9;
    p.pendingSeconds = 0;
    if (status < 0 || !result) {
        error = QStringLiteral("Moonshine could not transcribe the audio.");
        return false;
    }
    p.collect(result, events);

    // Rotate only at a completed, quiet boundary, draining before restart so
    // buffered audio is not silently dropped.
    if (result->line_count >= kRotateAfterLines && p.firstIncomplete == result->line_count) {
        const auto &last = result->lines[result->line_count - 1];
        if (p.audioSeconds - (last.start_time + last.duration) >= 1.0) {
            if (p.stop(p.transcriber, p.stream) < 0 || p.transcribe(p.transcriber, p.stream, 0, &result) < 0 ||
                !result) {
                error = QStringLiteral("Moonshine could not finish the recognition segment.");
                return false;
            }
            p.collect(result, events);
            if (p.start(p.transcriber, p.stream) < 0) {
                error = QStringLiteral("Moonshine could not resume after releasing transcript history.");
                return false;
            }
            p.firstIncomplete = 0;
            p.lastText.clear();
            p.audioSeconds = 0;
        }
    }
    if (p.firstIncomplete >= kHistoryLimit) {
        error = QStringLiteral("Listening stopped at the transcript history limit. Restart listening to continue.");
        return false;
    }
    return true;
}
