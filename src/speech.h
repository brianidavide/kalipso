// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QObject>
#include "moonshine.h"
#include <QLibrary>
#include <QProcess>
#include <QThread>
#include <array>
#include <atomic>
#include <memory>

// Optional native C ABIs are loaded only when listening starts. No Python runtime.
struct VoskModel;
struct VoskRecognizer;
class SpeechWorker : public QObject {
    Q_OBJECT
public:
    explicit SpeechWorker(std::shared_ptr<std::atomic<quint64>> epoch) : epoch_(std::move(epoch)) {}
    ~SpeechWorker() override;
public slots:
    void load(const QString &library, const QString &model, bool meeting, quint64 generation, const QString &backend="vosk", const QString &language={}, int architecture=2);
    void consume(int channel, const QByteArray &pcm, quint64 generation);
    void unload();
signals:
    void ready(quint64 generation);
    void error(quint64 generation, const QString &message);
    void transcript(int channel, const QString &text, bool final, quint64 generation);
    void consumed(int channel, int bytes, quint64 generation);
private:
    std::shared_ptr<std::atomic<quint64>> epoch_;
    MoonshineRecognizer moonshine_;
    QString backend_="vosk";
    QLibrary library_;
    VoskModel *model_ = nullptr;
    std::array<VoskRecognizer *,2> recognizers_{};
    quint64 generation_ = 0;
    VoskModel *(*modelNew_)(const char *) = nullptr;
    void (*modelFree_)(VoskModel *) = nullptr;
    VoskRecognizer *(*recognizerNew_)(VoskModel *,float) = nullptr;
    void (*recognizerFree_)(VoskRecognizer *) = nullptr;
    int (*accept_)(VoskRecognizer *,const char *,int) = nullptr;
    const char *(*result_)(VoskRecognizer *) = nullptr;
    const char *(*partial_)(VoskRecognizer *) = nullptr;
};
class SpeechService : public QObject {
    Q_OBJECT
public:
    explicit SpeechService(QObject *parent=nullptr);
    ~SpeechService() override;
    void start(const QString &library, const QString &model, const QString &microphone,
               const QString &meetingTarget, bool meeting, const QString &backend="vosk",
               const QString &language={}, int architecture=2);
    void stop();
    bool active() const { return active_; }
signals:
    void transcript(int channel, const QString &text, bool final);
    void listening();
    void status(const QString &text);
    void stopped();
private:
    void capture(int channel, const QString &target);
    QThread thread_;
    std::shared_ptr<std::atomic<quint64>> epoch_ = std::make_shared<std::atomic<quint64>>(0);
    SpeechWorker *worker_;
    std::array<QProcess *,2> processes_{};
    std::array<int,2> queued_{};
    std::array<QByteArray,2> buffers_{};
    quint64 generation_ = 0;
    bool active_ = false;
    bool meeting_ = false;
    QString microphone_, meetingTarget_;
};
