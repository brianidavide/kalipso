// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "moonshine.h"
#include "system_speech/client.h"
#include <QLibrary>
#include <QObject>
#include <QProcess>
#include <QThread>
#ifdef KALIPSO_QT_AUDIO
#include "pcm_converter.h"
#include <QAudioSource>
#endif
#include <atomic>
#include <memory>

// What to listen with. Native libraries are loaded only when listening starts.
struct SpeechRequest {
    QString backend = QStringLiteral("moonshine"); // "moonshine", "vosk" or "system"
    QString library;
    QString model;
    QString microphone; // Empty selects the default input.
    QString language;
    int architecture = 2; // Moonshine streaming architecture, 2..5.
};

struct VoskModel;
struct VoskRecognizer;

// Runs recognition on a worker thread. Every call carries the generation it was
// issued for; work from a cancelled generation is dropped.
class SpeechWorker : public QObject {
    Q_OBJECT
public:
    explicit SpeechWorker(std::shared_ptr<std::atomic<quint64>> epoch) : epoch_(std::move(epoch)) {}
    ~SpeechWorker() override;

public slots:
    void load(const SpeechRequest &request, quint64 generation);
    void consume(const QByteArray &pcm, quint64 generation);
    void unload();

signals:
    void ready(quint64 generation);
    void error(quint64 generation, const QString &message);
    void transcript(const QString &text, bool final, quint64 generation);
    void consumed(int bytes, quint64 generation);

private:
    bool current(quint64 generation) const { return epoch_->load() == generation; }
    bool loadVosk(const SpeechRequest &request, quint64 generation);

    std::shared_ptr<std::atomic<quint64>> epoch_;
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
    bool active() const { return active_; }

signals:
    void transcript(const QString &text, bool final);
    void listening();
    void status(const QString &text);
    void stopped();

private:
    void startCapture();
    void queueAudio(const QByteArray &pcm);
    void captureReady();
    void fail(const QString &message);

    SystemSpeechClient system_;
    QString script_;
    int scriptPosition_ = -1;
    bool systemMode_ = false;
#ifdef KALIPSO_QT_AUDIO
    QAudioSource *audio_ = nullptr;
    PcmConverter converter_;
#else
    QProcess *recorder_ = nullptr;
#endif
    QThread thread_;
    std::shared_ptr<std::atomic<quint64>> epoch_ = std::make_shared<std::atomic<quint64>>(0);
    SpeechWorker *worker_;
    int queued_ = 0;
    QByteArray buffer_;
    quint64 generation_ = 0;
    bool active_ = false;
    QString microphone_;
};
