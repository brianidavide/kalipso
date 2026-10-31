// SPDX-License-Identifier: AGPL-3.0-or-later
#include "microphone.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#ifdef KALIPSO_QT_AUDIO
#include <QAudioDevice>
#include <QMediaDevices>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>

std::optional<Microphone> Microphones::find(const QString &id) const {
    const auto wanted = id.trimmed();
    for (const auto &input : inputs)
        if (wanted.isEmpty() ? input.id == defaultId : input.id == wanted || input.name == wanted)
            return input;
    if (wanted.isEmpty() && !inputs.isEmpty())
        return inputs.first(); // The system names no microphone, or a sink monitor.
    return std::nullopt;
}

Microphones Microphones::fromPipeWire(const QByteArray &dump) {
    Microphones result;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(dump, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        return result;
    result.known = true;
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        const auto type = object.value("type").toString();
        if (type == QLatin1String("PipeWire:Interface:Node")) {
            const auto props = object.value("info").toObject().value("props").toObject();
            const auto kind = props.value("media.class").toString();
            const auto id = props.value("node.name").toString();
            if (id.isEmpty() ||
                (kind != QLatin1String("Audio/Source") && kind != QLatin1String("Audio/Source/Virtual") &&
                 kind != QLatin1String("Audio/Duplex")))
                continue;
            auto name = props.value("node.description").toString();
            if (name.isEmpty())
                name = props.value("node.nick").toString();
            result.inputs.append({id, name.isEmpty() ? id : name});
        } else if (type == QLatin1String("PipeWire:Interface:Metadata") &&
                   object.value("props").toObject().value("metadata.name").toString() == QLatin1String("default")) {
            for (const auto &entry : object.value("metadata").toArray()) {
                const auto item = entry.toObject();
                if (item.value("key").toString() != QLatin1String("default.audio.source"))
                    continue;
                // JSON values come parsed; other PipeWire versions print them as a string.
                auto setting = item.value("value");
                if (setting.isString())
                    setting = QJsonDocument::fromJson(setting.toString().toUtf8()).object();
                result.defaultId = setting.toObject().value("name").toString();
            }
        }
    }
    return result;
}

Microphones Microphones::list() {
#ifdef KALIPSO_QT_AUDIO
    Microphones result;
    result.known = true;
    for (const auto &device : QMediaDevices::audioInputs())
        result.inputs.append({QString::fromUtf8(device.id()), device.description()});
    result.defaultId = QString::fromUtf8(QMediaDevices::defaultAudioInput().id());
    return result;
#else
    QProcess dump;
    dump.start("pw-dump", QStringList());
    if (!dump.waitForFinished(2000) || dump.exitStatus() != QProcess::NormalExit || dump.exitCode() != 0) {
        dump.kill();
        dump.waitForFinished(500);
        return {};
    }
    return fromPipeWire(dump.readAllStandardOutput());
#endif
}

namespace {
// The microphone to open, or why there is none. followSystem: it is the system's choice, and capture
// follows that choice when it changes.
std::optional<Microphone> choose(const QString &microphone, QString *problem, bool *followSystem = nullptr) {
#ifndef KALIPSO_QT_AUDIO
    if (QStandardPaths::findExecutable("pw-record").isEmpty()) {
        *problem = MicrophoneCapture::tr("Install pipewire-audio (Arch) or pipewire-bin (Debian) to enable audio "
                                         "capture.");
        return std::nullopt;
    }
#endif
    const auto devices = Microphones::list();
    if (followSystem)
        *followSystem = microphone.trimmed().isEmpty();
    if (!devices.known) // Without the list, the system picks; the name stays generic.
        return Microphone{microphone.trimmed(), microphone.trimmed().isEmpty()
                                                    ? MicrophoneCapture::tr("the system microphone")
                                                    : microphone.trimmed()};
    if (devices.inputs.isEmpty()) {
        *problem = MicrophoneCapture::tr("Kalipso finds no microphone. Connect one, or turn it on in the system sound "
                                         "settings.");
        return std::nullopt;
    }
    const auto found = devices.find(microphone);
    if (!found)
        *problem = MicrophoneCapture::tr("The microphone chosen in Settings is not connected. Connect it, or choose "
                                         "another one.");
    else if (followSystem && found->id != devices.defaultId)
        *followSystem = false; // The system names none, or not a microphone: this one, not the speakers.
    return found;
}
} // namespace

QString MicrophoneCapture::problem(const QString &microphone) {
    QString reason;
    choose(microphone, &reason);
    return reason;
}

double MicrophoneCapture::level(const QByteArray &pcm) {
    int peak = 0;
    for (qsizetype i = 0; i + 1 < pcm.size(); i += 2) {
        qint16 sample;
        std::memcpy(&sample, pcm.constData() + i, sizeof sample);
        peak = std::max(peak, std::abs(int(sample)));
    }
    if (peak == 0)
        return 0;
    return std::clamp((20 * std::log10(peak / 32768.0) + 60) / 60, 0.0, 1.0);
}

MicrophoneCapture::MicrophoneCapture(QObject *parent) : QObject(parent) {
    silenceCheck_.setInterval(250);
    connect(&silenceCheck_, &QTimer::timeout, this, [this] {
        if (active_ && !silent_ && sinceSound_.elapsed() >= silenceTimeout_) {
            silent_ = true;
            emit silent(true);
        }
    });
}

MicrophoneCapture::~MicrophoneCapture() { stop(); }

void MicrophoneCapture::stop() {
    active_ = false;
    silent_ = false;
    silenceCheck_.stop();
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
    errors_.clear();
    pending_.clear();
#endif
}

void MicrophoneCapture::fail(const QString &message) {
    stop();
    emit failed(message);
}

void MicrophoneCapture::receive(const QByteArray &pcm) {
    if (!active_)
        return;
    // A muted or unplugged microphone sends exact zeros, or nothing at all.
    if (std::any_of(pcm.cbegin(), pcm.cend(), [](char byte) { return byte != 0; })) {
        sinceSound_.restart();
        if (silent_) {
            silent_ = false;
            emit silent(false);
        }
    }
    emit audio(pcm);
}

void MicrophoneCapture::start(const QString &microphone) {
    stop();
    QString reason;
    bool followSystem = false;
    const auto chosen = choose(microphone, &reason, &followSystem);
    if (!chosen)
        return fail(reason);
    name_ = chosen->name;
    active_ = true;
#ifdef KALIPSO_QT_AUDIO
    auto device = QMediaDevices::defaultAudioInput();
    for (const auto &candidate : QMediaDevices::audioInputs())
        if (!chosen->id.isEmpty() && QString::fromUtf8(candidate.id()) == chosen->id)
            device = candidate;
    if (device.isNull())
        return fail(tr("Kalipso finds no microphone. Connect one, or turn it on in the system sound settings."));
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
    connect(input, &QIODevice::readyRead, this, [this, input] { receive(converter_.convert(input->readAll())); });
    sinceSound_.start();
    silenceCheck_.start();
    emit started(name_);
#else
    recorder_ = new QProcess(this);
    // Named Kalipso in the system's sound settings, where the microphone in use is listed.
    QStringList arguments{"--raw",      "--format", "s16",       "--rate", QString::number(kSampleRate),
                          "--channels", "1",        "--latency", "100ms"};
    if (followSystem) {
        arguments << "--properties" << R"({ "application.name": "Kalipso", "media.name": "Kalipso" })";
    } else {
        // Unplugged, it is not swapped for another microphone: capture ends and says so.
        arguments << "--properties"
                  << R"({ "application.name": "Kalipso", "media.name": "Kalipso", "node.dont-reconnect": true })"
                  << "--target" << chosen->id;
    }
    arguments << "-";
    connect(recorder_, &QProcess::readyReadStandardOutput, this, [this] {
        // Whole 16-bit samples only: a read can end between the two bytes of one.
        pending_ += recorder_->readAllStandardOutput();
        const auto whole = pending_.size() & ~qsizetype(1);
        if (whole == 0)
            return;
        const auto pcm = pending_.first(whole);
        pending_.remove(0, whole);
        receive(pcm);
    });
    connect(recorder_, &QProcess::readyReadStandardError, this,
            [this] { errors_ = (errors_ + recorder_->readAllStandardError()).right(2000); });
    connect(recorder_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Audio capture could not start. Check that PipeWire is installed and running."));
    });
    connect(recorder_, &QProcess::finished, this, [this] {
        const auto lines = QString::fromLocal8Bit(errors_).trimmed().split('\n');
        const auto detail = lines.last().trimmed();
        fail(detail.isEmpty()
                 ? tr("Audio capture from %1 stopped. Start listening again; if it stops again, check the microphone "
                      "in the system sound settings.")
                       .arg(name_)
                 : tr("Audio capture from %1 stopped: %2").arg(name_, detail));
    });
    connect(recorder_, &QProcess::started, this, [this] {
        sinceSound_.start();
        silenceCheck_.start();
        emit started(name_);
    });
    recorder_->start("pw-record", arguments);
#endif
}
