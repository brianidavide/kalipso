// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include "system_speech/grammar.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#ifdef KALIPSO_QT_AUDIO
#include <QAudioDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QPermissions>
#endif

namespace {
constexpr int kSampleRate = 16000;
constexpr int kChunkBytes = 6400;       // 200 ms of 16-bit mono PCM.
constexpr int kMaxBacklogBytes = 64000; // 2 s; beyond this the model cannot keep up.
} // namespace

SpeechWorker::~SpeechWorker() { unload(); }

void SpeechWorker::unload() {
    moonshine_.unload();
    if (recognizer_ && recognizerFree_)
        recognizerFree_(recognizer_);
    recognizer_ = nullptr;
    if (model_ && modelFree_)
        modelFree_(model_);
    model_ = nullptr;
    vosk_.unload();
    backend_.clear();
}

void SpeechWorker::load(const SpeechRequest &request, quint64 generation) {
    if (!current(generation))
        return;
    unload();
    generation_ = generation;
    backend_ = request.backend;
    if (request.backend == QLatin1String("moonshine")) {
        QString message;
        if (!moonshine_.load(request.library, request.model, request.language, request.architecture, message)) {
            emit error(generation, message);
            return;
        }
    } else if (request.backend == QLatin1String("vosk")) {
        if (!loadVosk(request, generation))
            return;
    } else {
        emit error(generation, tr("Unsupported speech engine."));
        return;
    }
    if (!current(generation)) {
        unload();
        return;
    }
    emit ready(generation);
}

bool SpeechWorker::loadVosk(const SpeechRequest &request, quint64 generation) {
    vosk_.setFileName(request.library);
    if (!vosk_.load()) {
        emit error(generation, tr("Cannot load libvosk. Select a valid Vosk library in Settings."));
        return false;
    }
    const auto resolve = [this](auto &member, const char *name) {
        member = reinterpret_cast<std::remove_reference_t<decltype(member)>>(vosk_.resolve(name));
        return member != nullptr;
    };
    const bool complete =
        resolve(modelNew_, "vosk_model_new") & resolve(modelFree_, "vosk_model_free") &
        resolve(recognizerNew_, "vosk_recognizer_new") & resolve(recognizerFree_, "vosk_recognizer_free") &
        resolve(accept_, "vosk_recognizer_accept_waveform") & resolve(result_, "vosk_recognizer_result") &
        resolve(partial_, "vosk_recognizer_partial_result");
    if (!complete) {
        unload();
        emit error(generation, tr("This library does not provide the required Vosk API."));
        return false;
    }
    if (auto setLogLevel = reinterpret_cast<void (*)(int)>(vosk_.resolve("vosk_set_log_level")))
        setLogLevel(-1);
    model_ = modelNew_(request.model.toUtf8().constData());
    if (!current(generation)) {
        unload();
        return false;
    }
    if (!model_) {
        unload();
        emit error(generation, tr("Cannot open this speech model. Choose the extracted model folder."));
        return false;
    }
    recognizer_ = recognizerNew_(model_, float(kSampleRate));
    if (!recognizer_) {
        unload();
        emit error(generation, tr("Not enough resources to start speech recognition."));
        return false;
    }
    return true;
}

void SpeechWorker::consume(const QByteArray &pcm, quint64 generation, quint64 sequence) {
    if (sequence < skipBefore_->load()) {
        emit consumed(int(pcm.size()), generation, true);
        return;
    }
    const bool live = generation == generation_ && current(generation);
    if (live && backend_ == QLatin1String("moonshine")) {
        QList<MoonshineRecognizer::Event> events;
        QString message;
        if (!moonshine_.consume(pcm, events, message))
            emit error(generation, message);
        else if (current(generation))
            for (const auto &event : events)
                emit transcript(event.text, event.final, generation);
    } else if (live && recognizer_) {
        const int result = accept_(recognizer_, pcm.constData(), int(pcm.size()));
        if (result < 0) {
            emit error(generation, tr("The speech recognizer could not process the audio."));
        } else {
            const auto json = result ? result_(recognizer_) : partial_(recognizer_);
            const auto data = QJsonDocument::fromJson(json).object();
            emit transcript(data.value(result ? "text" : "partial").toString(), result != 0, generation);
        }
    }
    emit consumed(int(pcm.size()), generation, false);
}

SpeechService::SpeechService(QObject *parent) : QObject(parent), worker_(new SpeechWorker(epoch_, skipBefore_)) {
    connect(&system_, &SystemSpeechClient::ready, this, [this] {
        if (active_ && systemMode_)
            captureReady();
    });
    connect(&system_, &SystemSpeechClient::transcript, this, [this](const QString &text, bool final) {
        if (active_ && systemMode_)
            emit transcript(text, final);
    });
    connect(&system_, &SystemSpeechClient::failed, this, [this](const QString &message) {
        if (systemMode_)
            fail(message);
    });
    connect(&system_, &SystemSpeechClient::message, this, &SpeechService::status);

    qputenv("MOONSHINE_ORT_SINGLE_THREAD", "1"); // Must be set before the worker loads ONNX Runtime.
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &SpeechWorker::ready, this, [this](quint64 generation) {
        if (active_ && generation == generation_)
            startCapture();
    });
    connect(worker_, &SpeechWorker::error, this, [this](quint64 generation, const QString &message) {
        if (active_ && generation == generation_)
            fail(message);
    });
    connect(worker_, &SpeechWorker::transcript, this, [this](const QString &text, bool final, quint64 generation) {
        if (active_ && generation == generation_)
            emit transcript(text, final);
    });
    connect(worker_, &SpeechWorker::consumed, this, [this](int bytes, quint64 generation, bool skipped) {
        if (generation == generation_ && !skipped) // Skipped audio left the count when it was skipped.
            queued_ = std::max(0, queued_ - bytes);
    });
    thread_.start(QThread::LowPriority);
}

SpeechService::~SpeechService() {
    stop();
    // Drain pending recognizer work before its shared library is unloaded.
    QMetaObject::invokeMethod(worker_, &SpeechWorker::unload, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void SpeechService::start(const SpeechRequest &request) {
    catchUp_.reset();
    stop();
    if (request.backend == QLatin1String("system")) {
        if (!SystemSpeechClient::platformSupported()) {
            emit status(tr("System speech is available only on macOS and Windows."));
            return;
        }
        systemMode_ = true;
        active_ = true;
        emit status(tr("Starting on-device system speech…"));
        system_.start(request.language, scriptPhrases(script_, scriptPosition_, commands_));
        return;
    }
    if (request.backend != QLatin1String("moonshine") && request.backend != QLatin1String("vosk")) {
        emit status(tr("Unsupported speech engine."));
        return;
    }
#ifndef KALIPSO_QT_AUDIO
    if (QStandardPaths::findExecutable("pw-record").isEmpty()) {
        emit status(tr("Install pipewire-audio (Arch) or pipewire-bin (Debian) to enable audio capture."));
        return;
    }
#endif
    if (!QFileInfo(request.model).isDir()) {
        emit status(tr("Choose an extracted speech model folder in Settings."));
        return;
    }
#ifdef Q_OS_MACOS
    const QMicrophonePermission permission;
    const auto access = qApp->checkPermission(permission);
    if (access == Qt::PermissionStatus::Denied) {
        emit status(tr("Enable microphone access for Kalipso in System Settings."));
        return;
    }
    if (access == Qt::PermissionStatus::Undetermined) {
        active_ = true;
        const auto requestGeneration = generation_;
        emit status(tr("Waiting for microphone permission…"));
        qApp->requestPermission(permission, this, [=, this](const QPermission &result) {
            if (!active_ || generation_ != requestGeneration)
                return;
            if (result.status() == Qt::PermissionStatus::Granted) {
                active_ = false; // Not a user-visible stop: continue starting.
                start(request);
            } else {
                fail(tr("Microphone access was denied."));
            }
        });
        return;
    }
#endif
    active_ = true;
    microphone_ = request.microphone.trimmed();
    const auto generation = ++generation_;
    epoch_->store(generation);
    emit status(tr("Loading the local speech model…"));
    QMetaObject::invokeMethod(worker_, [worker = worker_, request, generation] { worker->load(request, generation); });
}

void SpeechService::setCommandPhrases(const QStringList &phrases) {
    if (commands_ == phrases)
        return;
    commands_ = phrases;
#ifdef Q_OS_WIN
    if (active_ && systemMode_)
        system_.updateScript(scriptPhrases(script_, scriptPosition_, commands_));
#endif
}

void SpeechService::setScript(const QString &script, int position) {
    if (script_ == script && scriptPosition_ == position)
        return;
    script_ = script;
    scriptPosition_ = position;
#ifdef Q_OS_WIN
    if (active_ && systemMode_)
        system_.updateScript(scriptPhrases(script_, scriptPosition_, commands_));
#endif
}

void SpeechService::captureReady() {
    if (!active_)
        return;
    emit listening();
    emit status(tr("Listening · microphone · on this device"));
}

void SpeechService::fail(const QString &message) {
    stop();
    emit status(message);
}

void SpeechService::stop() {
    system_.stop();
    systemMode_ = false;
#ifdef KALIPSO_QT_AUDIO
    if (audio_) {
        audio_->disconnect(this);
        audio_->stop();
        audio_->deleteLater();
        audio_ = nullptr;
    }
#else
    if (recorder_) {
        recorder_->disconnect(this);
        recorder_->kill();
        recorder_->waitForFinished(1000);
        recorder_->deleteLater();
        recorder_ = nullptr;
    }
#endif
    const bool wasActive = active_;
    active_ = false;
    epoch_->store(++generation_);
    queued_ = 0;
    buffer_.clear();
    QMetaObject::invokeMethod(worker_, &SpeechWorker::unload);
    if (wasActive)
        emit stopped();
}

void SpeechService::queueAudio(const QByteArray &pcm) {
    if (!active_ || systemMode_)
        return;
    buffer_.append(pcm);
    if (buffer_.size() + queued_ > kMaxBacklogBytes) {
        if (catchUp_.overloaded(QDateTime::currentMSecsSinceEpoch()) == CatchUpPolicy::Stop) {
            fail(tr("Recognition cannot keep up on this computer. Listening stopped; try a smaller model."));
            return;
        }
        // Skip what is waiting and follow from the next words; the reading position stays.
        skipBefore_->store(sequence_);
        queued_ = 0;
        buffer_.clear();
        emit status(tr("Catching up: a moment of audio was skipped. Following continues."));
        return;
    }
    while (buffer_.size() >= kChunkBytes) {
        const auto data = buffer_.first(kChunkBytes);
        buffer_.remove(0, kChunkBytes);
        queued_ += kChunkBytes;
        QMetaObject::invokeMethod(worker_, [worker = worker_, data, generation = generation_, sequence = sequence_++] {
            worker->consume(data, generation, sequence);
        });
    }
}

void SpeechService::startCapture() {
#ifdef KALIPSO_QT_AUDIO
    auto device = QMediaDevices::defaultAudioInput();
    if (!microphone_.isEmpty()) {
        bool found = false;
        for (const auto &candidate : QMediaDevices::audioInputs()) {
            if (candidate.description() == microphone_ || QString::fromUtf8(candidate.id()) == microphone_) {
                device = candidate;
                found = true;
                break;
            }
        }
        if (!found)
            return fail(tr("The selected microphone is unavailable."));
    }
    if (device.isNull())
        return fail(tr("No microphone is available."));
    auto format = device.preferredFormat();
    QAudioFormat desired;
    desired.setSampleRate(kSampleRate);
    desired.setChannelCount(1);
    desired.setSampleFormat(QAudioFormat::Int16);
    if (device.isFormatSupported(desired))
        format = desired;
    PcmConverter::Format encoding;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8: encoding = PcmConverter::Unsigned8; break;
    case QAudioFormat::Int16: encoding = PcmConverter::Signed16; break;
    case QAudioFormat::Int32: encoding = PcmConverter::Signed32; break;
    case QAudioFormat::Float: encoding = PcmConverter::Float32; break;
    default: return fail(tr("Unsupported microphone sample format."));
    }
    converter_.reset(format.sampleRate(), format.channelCount(), encoding);
    audio_ = new QAudioSource(device, format, this);
    audio_->setBufferSize(format.bytesForDuration(100000));
    connect(audio_, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (active_ && audio_ && state == QAudio::StoppedState && audio_->error() != QAudio::NoError)
            fail(tr("Microphone capture failed. Check device permissions."));
    });
    auto *input = audio_->start();
    if (!active_ || !audio_)
        return;
    if (!input)
        return fail(tr("Cannot start the microphone."));
    connect(input, &QIODevice::readyRead, this, [this, input] { queueAudio(converter_.convert(input->readAll())); });
    captureReady();
#else
    recorder_ = new QProcess(this);
    QStringList arguments{"--raw",      "--format", "s16",       "--rate", QString::number(kSampleRate),
                          "--channels", "1",        "--latency", "100ms"};
    if (!microphone_.isEmpty())
        arguments << "--target" << microphone_;
    arguments << "-";
    connect(recorder_, &QProcess::readyReadStandardOutput, this,
            [this] { queueAudio(recorder_->readAllStandardOutput()); });
    connect(recorder_, &QProcess::readyReadStandardError, this, [this] { recorder_->readAllStandardError(); });
    connect(recorder_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (active_)
            fail(tr("Audio capture failed. Check PipeWire and the selected microphone."));
    });
    connect(recorder_, &QProcess::finished, this, [this] {
        if (active_)
            fail(tr("Audio capture ended. Check PipeWire and restart listening."));
    });
    connect(recorder_, &QProcess::started, this, &SpeechService::captureReady);
    recorder_->start("pw-record", arguments);
#endif
}
