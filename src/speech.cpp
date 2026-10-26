// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

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
    qputenv("MOONSHINE_ORT_SINGLE_THREAD","1"); // Set once, before the inference worker starts.
    worker_->moveToThread(&thread_);
    connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);
    connect(worker_,&SpeechWorker::ready,this,[this](quint64 generation) {
        if (!active_ || generation!=generation_) return;
        capture(0,microphone_);
        if (meeting_) capture(1,meetingTarget_);
        emit listening();
        emit status(meeting_ ? tr("Listening · microphone and meeting audio") : tr("Listening · microphone"));
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
    if (QStandardPaths::findExecutable("pw-record").isEmpty()) {
        emit status(tr("Install pipewire-audio (Arch) or pipewire-bin (Debian) to enable audio capture.")); return;
    }
    if (!QFileInfo(model).isDir()) { emit status(tr("Choose an extracted speech model folder in Settings.")); return; }
    if (meeting && meetingTarget.trimmed().isEmpty()) {
        emit status(tr("Choose the meeting output node in Settings before listening to meeting audio.")); return;
    }
    active_=true; meeting_=meeting; microphone_=microphone; meetingTarget_=meetingTarget;
    const auto generation=++generation_;
    epoch_->store(generation);
    emit status(tr("Loading the local speech model…"));
    QMetaObject::invokeMethod(worker_,[=,this] { worker_->load(library,model,meeting,generation,backend,language,architecture); });
}
void SpeechService::stop() {
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
void SpeechService::capture(int channel,const QString &target) {
    auto *process=new QProcess(this); processes_[channel]=process;
    QStringList arguments{"--raw","--format","s16","--rate","16000","--channels","1","--latency","100ms"};
    if (!target.trimmed().isEmpty()) arguments << "--target" << target.trimmed();
    if (channel==1) arguments << "--properties" << "{\"stream.capture.sink\":true}";
    arguments << "-";
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,channel] {
        buffers_[channel].append(process->readAllStandardOutput());
        // At most two seconds of pending audio per channel. Stop explicitly
        // instead of growing RAM or silently showing increasingly stale text.
        if (buffers_[channel].size()+queued_[channel]>64000) {
            stop(); emit status(tr("Recognition cannot keep up. Listening stopped; try a smaller model.")); return;
        }
        constexpr int chunk=6400; // 200 ms, mono PCM16 at 16 kHz
        while (buffers_[channel].size()>=chunk) {
            const auto pcm=buffers_[channel].first(chunk); buffers_[channel].remove(0,chunk);
            queued_[channel]+=chunk; const auto generation=generation_;
            QMetaObject::invokeMethod(worker_,[this,channel,pcm,generation] { worker_->consume(channel,pcm,generation); });
        }
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
    process->start("pw-record",arguments);
}
