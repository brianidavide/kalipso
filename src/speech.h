// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "microphone.h"
#include "moonshine.h"
#include "system_speech/client.h"
#include <QLibrary>
#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>

// What to listen with. Native libraries are loaded only when listening starts.
struct SpeechRequest {
    QString backend = QStringLiteral("moonshine"); // "moonshine", "vosk" or "system"
    QString library;
    QString model;
    QString microphone; // A Microphone id; empty selects the system's microphone.
    QString language;
    int architecture = 2; // Moonshine streaming architecture, 2..5.
};

struct VoskModel;
struct VoskRecognizer;

// Runs recognition on a worker thread. Every call carries the generation it was
// issued for; work from a cancelled generation is dropped.
// When recognition falls behind (a CPU spike during a video call), the queued audio is
// skipped and following carries on from the next words. Only if it keeps happening is
// the model too slow for this computer, and listening stops with an explanation.
class CatchUpPolicy {
public:
    enum Action { Skip, Stop };
    static constexpr int kMaxSkips = 3;        // Within the window below...
    static constexpr qint64 kWindowMs = 60000; // ...means the model cannot keep up.
    Action overloaded(qint64 nowMs) {
        skips_.removeIf([nowMs](qint64 at) { return nowMs - at > kWindowMs; });
        skips_.append(nowMs);
        return skips_.size() >= kMaxSkips ? Stop : Skip;
    }
    void reset() { skips_.clear(); }

private:
    QList<qint64> skips_;
};

class SpeechWorker : public QObject {
    Q_OBJECT
public:
    explicit SpeechWorker(std::shared_ptr<std::atomic<quint64>> epoch,
                          std::shared_ptr<std::atomic<quint64>> skipBefore = std::make_shared<std::atomic<quint64>>(0))
        : epoch_(std::move(epoch)), skipBefore_(std::move(skipBefore)) {}
    ~SpeechWorker() override;

public slots:
    void load(const SpeechRequest &request, quint64 generation);
    // Chunks numbered below the skip mark are dropped unheard (see CatchUpPolicy).
    void consume(const QByteArray &pcm, quint64 generation, quint64 sequence = 0);
    void unload();

signals:
    void ready(quint64 generation);
    void error(quint64 generation, const QString &message);
    void transcript(const QString &text, bool final, quint64 generation);
    void consumed(int bytes, quint64 generation, bool skipped);

private:
    bool current(quint64 generation) const { return epoch_->load() == generation; }
    bool loadVosk(const SpeechRequest &request, quint64 generation);

    std::shared_ptr<std::atomic<quint64>> epoch_, skipBefore_;
    MoonshineRecognizer moonshine_;
    QString backend_;
    quint64 generation_ = 0;

    QLibrary vosk_;
    VoskModel *model_ = nullptr;
    VoskRecognizer *recognizer_ = nullptr;
    VoskModel *(*modelNew_)(const char *) = nullptr;
    void (*modelFree_)(VoskModel *) = nullptr;
    VoskRecognizer *(*recognizerNew_)(VoskModel *, float) = nullptr;
    void (*recognizerFree_)(VoskRecognizer *) = nullptr;
    int (*accept_)(VoskRecognizer *, const char *, int) = nullptr;
    const char *(*result_)(VoskRecognizer *) = nullptr;
    const char *(*partial_)(VoskRecognizer *) = nullptr;
};

// Owns microphone capture and the recognition thread. Emits transcripts on the
// caller's thread. stop() releases the microphone immediately.
class SpeechService : public QObject {
    Q_OBJECT
public:
    explicit SpeechService(QObject *parent = nullptr);
    ~SpeechService() override;

    void start(const SpeechRequest &request);
    void stop();
    // Script phrases constrain system recognizers that support it (Windows).
    void setScript(const QString &script, int position);
    // Phrases the recognizer must also accept (voice commands); only Windows system speech
    // restricts recognition to known phrases.
    void setCommandPhrases(const QStringList &phrases);
    bool active() const { return active_; }

signals:
    void transcript(const QString &text, bool final);
    void listening();
    void status(const QString &text);
    // No sound from the microphone for a few seconds (muted or unplugged), or sound again.
    void silent(bool silent);
    void stopped();

private:
    void startCapture();
    void queueAudio(const QByteArray &pcm);
    void captureReady();
    QString listeningText() const;
    void fail(const QString &message);

    SystemSpeechClient system_;
    QString script_;
    QStringList commands_;
    int scriptPosition_ = -1;
    bool systemMode_ = false;
    MicrophoneCapture capture_;
    QThread thread_;
    std::shared_ptr<std::atomic<quint64>> epoch_ = std::make_shared<std::atomic<quint64>>(0);
    // Declared before worker_, which is constructed with it.
    std::shared_ptr<std::atomic<quint64>> skipBefore_ = std::make_shared<std::atomic<quint64>>(0);
    SpeechWorker *worker_;
    int queued_ = 0;
    QByteArray buffer_;
    quint64 sequence_ = 0;
    CatchUpPolicy catchUp_;
    quint64 generation_ = 0;
    bool active_ = false;
    QString microphone_;
};
