// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include "system_speech/grammar.h"
#ifdef KALIPSO_QT_AUDIO
#include <QMediaDevices>
#include <QAudioDevice>
#include <QCoreApplication>
#include <QMicrophonePermission>
#endif

SpeechWorker::~SpeechWorker() { unload(); }
void SpeechWorker::unload() {
    moonshine_.unload();
    for (auto &recognizer : recognizers_) {
        if (recognizer && recognizerFree_) recognizerFree_(recognizer);
        recognizer=nullptr;
    }
    if (model_ && modelFree_) modelFree_(model_);
    model_=nullptr; library_.unload();
}
void SpeechWorker::load(const QString &library, const QString &model, bool meeting, quint64 generation, const QString &backend, const QString &language, int architecture) {
    if(epoch_->load() != generation) return;
    unload(); generation_=generation;backend_=backend;
    if(backend=="moonshine") {
        QString message;
        if(!moonshine_.load(library,model,language,architecture,meeting,message)){emit error(generation,message);return;}
        if(epoch_->load()!=generation){unload();return;}
        emit ready(generation);return;
    }
    if(backend!="vosk"){emit error(generation,tr("Unsupported speech engine."));return;}
    library_.setFileName(library);
    if (!library_.load()) { emit error(generation,tr("Cannot load libvosk. Select a valid Vosk shared library in Settings.")); return; }
#define LOAD(member,name) member = reinterpret_cast<decltype(member)>(library_.resolve(name))
    LOAD(modelNew_,"vosk_model_new"); LOAD(modelFree_,"vosk_model_free");
    LOAD(recognizerNew_,"vosk_recognizer_new"); LOAD(recognizerFree_,"vosk_recognizer_free");
    LOAD(accept_,"vosk_recognizer_accept_waveform"); LOAD(result_,"vosk_recognizer_result");
    LOAD(partial_,"vosk_recognizer_partial_result");
#undef LOAD
    if (!modelNew_ || !modelFree_ || !recognizerNew_ || !recognizerFree_ || !accept_ || !result_ || !partial_) {
        unload(); emit error(generation,tr("This library does not provide the required Vosk API.")); return;
    }
    auto logLevel=reinterpret_cast<void(*)(int)>(library_.resolve("vosk_set_log_level"));
    if (logLevel) logLevel(-1);
    model_=modelNew_(model.toUtf8().constData());
    if(epoch_->load() != generation) { unload(); return; }
    if (!model_) { unload(); emit error(generation,tr("Cannot open this speech model. Choose the extracted model folder.")); return; }
    recognizers_[0]=recognizerNew_(model_,16000.0F);
    if (meeting) recognizers_[1]=recognizerNew_(model_,16000.0F);
    if (!recognizers_[0] || (meeting && !recognizers_[1])) {
        unload(); emit error(generation,tr("Not enough resources to start speech recognition.")); return;
    }
    emit ready(generation);
}
void SpeechWorker::consume(int channel, const QByteArray &pcm, quint64 generation) {
    if(backend_=="moonshine"&&generation==generation_&&epoch_->load()==generation) {
        QList<MoonshineRecognizer::Event> events;QString message;
        if(!moonshine_.consume(channel,pcm,events,message))emit error(generation,message);
        else if(epoch_->load()==generation)for(const auto &event:events)emit transcript(channel,event.text,event.final,generation);
        emit consumed(channel,int(pcm.size()),generation);return;
    }
    if (generation == generation_ && epoch_->load() == generation && channel >= 0 && channel < 2 && recognizers_[channel]) {
        const int result=accept_(recognizers_[channel],pcm.constData(),int(pcm.size()));
        if (result < 0) emit error(generation,tr("The speech recognizer could not process the audio."));
        else {
            const auto data=QJsonDocument::fromJson(result ? result_(recognizers_[channel]) : partial_(recognizers_[channel])).object();
            const auto text=data.value(result ? "text" : "partial").toString();
            emit transcript(channel,text,result!=0,generation);
        }
    }
    emit consumed(channel,int(pcm.size()),generation);
}
SpeechService::SpeechService(QObject *parent) : QObject(parent), worker_(new SpeechWorker(epoch_)) {
    connect(&system_,&SystemSpeechClient::ready,this,[this]{if(active_&&systemMode_)captureReady();});
    connect(&system_,&SystemSpeechClient::transcript,this,[this](const QString &text,bool final){if(active_&&systemMode_)emit transcript(0,text,final);});
    connect(&system_,&SystemSpeechClient::failed,this,[this](const QString &message){if(systemMode_){stop();emit status(message);}});
    connect(&system_,&SystemSpeechClient::message,this,&SpeechService::status);
    qputenv("MOONSHINE_ORT_SINGLE_THREAD","1"); // Set once, before the inference worker starts.
    worker_->moveToThread(&thread_);
    connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);
    connect(worker_,&SpeechWorker::ready,this,[this](quint64 generation) {
        if (!active_ || generation!=generation_) return;
        capture(0,microphone_);
        if (active_ && meeting_) capture(1,meetingTarget_);

    });
    connect(worker_,&SpeechWorker::error,this,[this](quint64 generation,const QString &message) {
        if (generation != generation_ || !active_) return;
        stop(); emit status(message);
    });
    connect(worker_,&SpeechWorker::transcript,this,[this](int channel,const QString &text,bool final,quint64 generation) {
        if (active_ && generation==generation_) emit transcript(channel,text,final);
    });
    connect(worker_,&SpeechWorker::consumed,this,[this](int channel,int bytes,quint64 generation) {
        if (generation==generation_) queued_[channel]-=bytes;
    });
    thread_.start(QThread::LowPriority);
}
SpeechService::~SpeechService() {
    stop();
    // Drain pending recognizer work before its shared library is unloaded.
    QMetaObject::invokeMethod(worker_,&SpeechWorker::unload,Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait();
}
void SpeechService::start(const QString &library,const QString &model,const QString &microphone,
                           const QString &meetingTarget,bool meeting,const QString &backend,const QString &language,int architecture) {
    stop();
    if(backend=="system"){
        if(!SystemSpeechClient::platformSupported()){emit status(tr("System speech is available only on supported macOS and Windows installations."));return;}
        if(meeting||!microphone.trimmed().isEmpty()){emit status(tr("System speech uses the default microphone. Turn off meeting capture and clear the microphone override."));return;}
        systemMode_=true;active_=true;meeting_=false;
        emit status(tr("Starting on-device system speech…"));
        system_.start(language,scriptPhrases(script_,scriptPosition_));return;
    }
    if(backend!="moonshine"&&backend!="vosk"){emit status(tr("Unsupported speech engine."));return;}
#ifndef KALIPSO_QT_AUDIO
    if (QStandardPaths::findExecutable("pw-record").isEmpty()) {
        emit status(tr("Install pipewire-audio (Arch) or pipewire-bin (Debian) to enable audio capture.")); return;
    }
#else
    if(meeting){emit status(tr("Meeting-output capture is currently available only with PipeWire on Linux."));return;}
#endif
    if (!QFileInfo(model).isDir()) { emit status(tr("Choose an extracted speech model folder in Settings.")); return; }
    if (meeting && meetingTarget.trimmed().isEmpty()) {
        emit status(tr("Choose the meeting output node in Settings before listening to meeting audio.")); return;
    }
#ifdef Q_OS_MACOS
    const QMicrophonePermission permission;
    const auto access=QCoreApplication::instance()->checkPermission(permission);
    if(access==Qt::PermissionStatus::Denied){emit status(tr("Enable microphone access for Kalipso in System Settings."));return;}
    if(access==Qt::PermissionStatus::Undetermined){
        active_=true;const auto requestGeneration=generation_;
        emit status(tr("Waiting for microphone permission…"));
        QCoreApplication::instance()->requestPermission(permission,this,[=,this](const QPermission &result){
            if(!active_||generation_!=requestGeneration)return;
            if(result.status()==Qt::PermissionStatus::Granted)start(library,model,microphone,meetingTarget,meeting,backend,language,architecture);
            else{stop();emit status(tr("Microphone access was denied."));}
        });return;
    }
#endif
    active_=true; meeting_=meeting; microphone_=microphone; meetingTarget_=meetingTarget;
    const auto generation=++generation_;
    epoch_->store(generation);
    emit status(tr("Loading the local speech model…"));
    QMetaObject::invokeMethod(worker_,[=,this] { worker_->load(library,model,meeting,generation,backend,language,architecture); });
}
void SpeechService::setScript(const QString &script,int position){
    if(script_==script&&scriptPosition_==position)return;
    script_=script;scriptPosition_=position;
#ifdef Q_OS_WIN
    if(active_&&systemMode_)system_.updateScript(scriptPhrases(script_,scriptPosition_));
#endif
}
void SpeechService::captureReady(){
    if(!active_)return;emit listening();
    emit status(meeting_?tr("Listening · microphone and meeting audio"):tr("Listening · microphone · on this device"));
}
void SpeechService::stop() {
    system_.stop();systemMode_=false;
#ifdef KALIPSO_QT_AUDIO
    if(audio_){audio_->disconnect(this);audio_->stop();audio_->deleteLater();audio_=nullptr;}
#endif
    active_=false; ++generation_;
    epoch_->store(generation_);
    for (auto &process : processes_) {
        if (process) {
            process->disconnect(this);
            process->kill(); process->waitForFinished(1000);
            process->deleteLater(); process=nullptr;
        }
    }
    queued_.fill(0); for (auto &buffer : buffers_) buffer.clear();
    QMetaObject::invokeMethod(worker_,&SpeechWorker::unload);
    emit stopped();
}
void SpeechService::queueAudio(int channel,const QByteArray &pcm){
    if(!active_||systemMode_)return;
    buffers_[channel].append(pcm);
    if(buffers_[channel].size()+queued_[channel]>64000){stop();emit status(tr("Recognition cannot keep up. Listening stopped; try a smaller model."));return;}
    constexpr int chunk=6400;
    while(buffers_[channel].size()>=chunk){
        const auto data=buffers_[channel].first(chunk);buffers_[channel].remove(0,chunk);
        queued_[channel]+=chunk;const auto generation=generation_;
        QMetaObject::invokeMethod(worker_,[this,channel,data,generation]{worker_->consume(channel,data,generation);});
    }
}
void SpeechService::capture(int channel,const QString &target) {
#ifdef KALIPSO_QT_AUDIO
    Q_UNUSED(channel);
    auto device=QMediaDevices::defaultAudioInput();
    if(!target.trimmed().isEmpty()){
        bool found=false;
        for(const auto &candidate:QMediaDevices::audioInputs())if(candidate.description()==target||QString::fromUtf8(candidate.id())==target){device=candidate;found=true;break;}
        if(!found){stop();emit status(tr("The selected microphone is unavailable."));return;}
    }
    if(device.isNull()){stop();emit status(tr("No microphone is available."));return;}
    auto format=device.preferredFormat();
    QAudioFormat desired;desired.setSampleRate(16000);desired.setChannelCount(1);desired.setSampleFormat(QAudioFormat::Int16);
    if(device.isFormatSupported(desired))format=desired;
    if(format.sampleFormat()==QAudioFormat::Unknown){stop();emit status(tr("Unsupported microphone sample format."));return;}
    const auto encoding=format.sampleFormat()==QAudioFormat::UInt8?PcmConverter::Unsigned8:format.sampleFormat()==QAudioFormat::Int16?PcmConverter::Signed16:format.sampleFormat()==QAudioFormat::Int32?PcmConverter::Signed32:PcmConverter::Float32;
    converter_.reset(format.sampleRate(),format.channelCount(),encoding);
    audio_=new QAudioSource(device,format,this);audio_->setBufferSize(format.bytesForDuration(100000));
    connect(audio_,&QAudioSource::stateChanged,this,[this](QAudio::State state){
        if(active_&&audio_&&state==QAudio::StoppedState&&audio_->error()!=QAudio::NoError){stop();emit status(tr("Microphone capture failed. Check device permissions."));}
    });
    auto *input=audio_->start();
    if(!active_||!audio_)return;
    if(!input){stop();emit status(tr("Cannot start the microphone."));return;}
    connect(input,&QIODevice::readyRead,this,[this,input]{queueAudio(0,converter_.convert(input->readAll()));});
    captureReady();
#else
    auto *process=new QProcess(this); processes_[channel]=process;
    QStringList arguments{"--raw","--format","s16","--rate","16000","--channels","1","--latency","100ms"};
    if (!target.trimmed().isEmpty()) arguments << "--target" << target.trimmed();
    if (channel==1) arguments << "--properties" << "{\"stream.capture.sink\":true}";
    arguments << "-";
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,channel] {
        queueAudio(channel,process->readAllStandardOutput());
    });
    connect(process,&QProcess::readyReadStandardError,this,[process] { process->readAllStandardError(); });
    connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) {
        if (!active_) return;
        stop(); emit status(tr("Audio capture failed. Check PipeWire and the selected audio nodes."));
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus) {
        if (!active_) return;
        stop(); emit status(tr("Audio capture ended. Check PipeWire and restart listening."));
    });
    if(channel==0)connect(process,&QProcess::started,this,&SpeechService::captureReady);
    process->start("pw-record",arguments);
#endif
}
