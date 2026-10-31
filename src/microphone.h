// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#ifdef KALIPSO_QT_AUDIO
#include "pcm_converter.h"
#include <QAudioSource>
#endif
#include <optional>

// A microphone as the system names it. The id is what Settings saves: the PipeWire node name on
// Linux, the device id elsewhere. An empty id means the system's microphone, whichever it is.
struct Microphone {
    QString id;
    QString name;
};

struct Microphones {
    QList<Microphone> inputs;
    QString defaultId;  // The system's microphone; empty when the system names none.
    bool known = false; // False when the list cannot be read: capture still tries the default.

    // The microphone listening with `id` uses. The system's microphone is never a sink monitor:
    // with no microphone to choose from, there is none, instead of the sound of the speakers.
    std::optional<Microphone> find(const QString &id) const;

    static Microphones list();
    // Reads pw-dump's JSON: the audio inputs and the default one.
    static Microphones fromPipeWire(const QByteArray &dump);
};

// Captures 16 kHz mono 16-bit audio from a microphone: pw-record on Linux, Qt Multimedia
// elsewhere. Names the microphone it opened, and says when nothing arrives from it, which is what a
// muted or disconnected microphone sends.
class MicrophoneCapture : public QObject {
    Q_OBJECT
public:
    static constexpr int kSampleRate = 16000;
    explicit MicrophoneCapture(QObject *parent = nullptr);
    ~MicrophoneCapture() override;

    // Why capture cannot start with this microphone, or empty when it can. Opens nothing.
    static QString problem(const QString &microphone);
    // The loudest sample of 16-bit PCM on a 0 (-60 dB and below) to 1 (full scale) meter scale.
    static double level(const QByteArray &pcm);

    void start(const QString &microphone);
    void stop();
    bool active() const { return active_; }
    QString name() const { return name_; }
    void setSilenceTimeout(int milliseconds) { silenceTimeout_ = milliseconds; }

signals:
    void started(const QString &name);
    void audio(const QByteArray &pcm);
    // True when no sound came for the silence timeout; false when sound arrives again.
    void silent(bool silent);
    void failed(const QString &message);

private:
    void receive(const QByteArray &pcm);
    void fail(const QString &message);

    bool active_ = false;
    bool silent_ = false;
    QString name_;
    int silenceTimeout_ = 4000;
    QElapsedTimer sinceSound_;
    QTimer silenceCheck_;
#ifdef KALIPSO_QT_AUDIO
    QAudioSource *audio_ = nullptr;
    PcmConverter converter_;
#else
    QProcess *recorder_ = nullptr;
    QByteArray errors_, pending_;
#endif
};
