// SPDX-License-Identifier: AGPL-3.0-or-later
// Research runner. PCM input is signed 16-bit mono at 16 kHz, little endian.
// No reference transcript is passed to either recognizer.
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <vector>
#ifdef BENCH_WHISPER
#include <whisper.h>
#endif

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QCommandLineParser options;options.addHelpOption();
    for(const auto name:{"model","library","audio","output","language","mode","threads","step-ms","window-ms"})options.addOption({name,QString(name),"value"});
    options.addOption({"realtime","Feed samples at their recorded time, exposing accumulated processing lag"});
    options.process(app);
    const auto mode=options.value("mode").isEmpty()?QString("batch"):options.value("mode");
    QFile file(options.value("audio"));if(!file.open(QIODevice::ReadOnly))return 2;
    const auto pcm=file.readAll();if(pcm.isEmpty()||pcm.size()%2)return 2;
    QJsonArray events;QString transcript;QElapsedTimer load,decode,chunk;
    load.start();double loadMs=0,decodeMs=0,decodeEndWallMs=0;
    auto pace=[&](double audioSeconds){if(options.isSet("realtime")){const double left=audioSeconds*1e6-decode.nsecsElapsed()/1000.0;if(left>0)QThread::usleep(static_cast<unsigned long>(left));}};
    auto event=[&](const QString &text,bool final,double audioSeconds,double chunkMs,double windowStart=0.0){
        events.append(QJsonObject{{"text",text},{"final",final},{"audioSeconds",audioSeconds},
            {"decodeMs",decodeMs},{"chunkMs",chunkMs},{"wallMs",decode.nsecsElapsed()/1e6},{"windowStartSeconds",windowStart}});
    };
#ifdef BENCH_WHISPER
    const auto model=options.value("model").toUtf8();const auto language=options.value("language").toUtf8();
    auto contextParams=whisper_context_default_params();contextParams.use_gpu=false;
    auto *context=whisper_init_from_file_with_params(model.constData(),contextParams);if(!context)return 3;
    loadMs=load.nsecsElapsed()/1e6;
    std::vector<float> samples(pcm.size()/2);
    for(size_t i=0;i<samples.size();++i)samples[i]=qFromLittleEndian<qint16>(pcm.constData()+2*i)/32768.0F;
    auto params=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads=std::max(1,options.value("threads").toInt());params.language=language.constData();
    params.translate=false;params.no_context=true;params.no_timestamps=true;params.print_progress=false;
    params.print_realtime=false;params.print_timestamps=false;params.temperature=0;params.temperature_inc=0;
    params.greedy.best_of=1;params.single_segment=(mode=="stream");
    const int stepMs=std::max(200,options.value("step-ms").toInt());
    const int windowMs=std::max(stepMs,options.value("window-ms").toInt());
    const size_t step=mode=="stream"?size_t(stepMs)*16:samples.size();
    decode.start();
    for(size_t consumed=0;consumed<samples.size();) {
        const size_t end=std::min(samples.size(),consumed+step);
        const size_t begin=mode=="stream"&&end>size_t(windowMs)*16?end-size_t(windowMs)*16:0;
        pace(end/16000.0);chunk.start();const int result=whisper_full(context,params,samples.data()+begin,int(end-begin));
        const double chunkMs=chunk.nsecsElapsed()/1e6;decodeMs+=chunkMs;
        if(result!=0){whisper_free(context);return 4;}
        QString text;
        for(int i=0;i<whisper_full_n_segments(context);++i)text+=QString::fromUtf8(whisper_full_get_segment_text(context,i));
        transcript=text.trimmed();event(transcript,mode=="batch",end/16000.0,chunkMs,begin/16000.0);
        consumed=end;
    }
    decodeEndWallMs=decode.nsecsElapsed()/1e6;
    whisper_free(context);
#else
    QLibrary lib(options.value("library"));if(!lib.load())return 3;
    using Ptr=void *;
    const auto modelNew=reinterpret_cast<Ptr(*)(const char*)>(lib.resolve("vosk_model_new"));
    const auto modelFree=reinterpret_cast<void(*)(Ptr)>(lib.resolve("vosk_model_free"));
    const auto recognizerNew=reinterpret_cast<Ptr(*)(Ptr,float)>(lib.resolve("vosk_recognizer_new"));
    const auto recognizerFree=reinterpret_cast<void(*)(Ptr)>(lib.resolve("vosk_recognizer_free"));
    const auto accept=reinterpret_cast<int(*)(Ptr,const char*,int)>(lib.resolve("vosk_recognizer_accept_waveform"));
    const auto partial=reinterpret_cast<const char*(*)(Ptr)>(lib.resolve("vosk_recognizer_partial_result"));
    const auto result=reinterpret_cast<const char*(*)(Ptr)>(lib.resolve("vosk_recognizer_result"));
    const auto finish=reinterpret_cast<const char*(*)(Ptr)>(lib.resolve("vosk_recognizer_final_result"));
    if(!modelNew||!modelFree||!recognizerNew||!recognizerFree||!accept||!partial||!result||!finish)return 3;
    const auto log=reinterpret_cast<void(*)(int)>(lib.resolve("vosk_set_log_level"));if(log)log(-1);
    Ptr model=modelNew(options.value("model").toUtf8().constData());if(!model)return 3;
    Ptr recognizer=recognizerNew(model,16000);if(!recognizer){modelFree(model);return 3;}
    loadMs=load.nsecsElapsed()/1e6;
    decode.start();
    for(qsizetype offset=0;offset<pcm.size();offset+=6400) {
        const auto size=std::min(qsizetype(6400),pcm.size()-offset);pace((offset+size)/32000.0);chunk.start();
        const int state=accept(recognizer,pcm.constData()+offset,int(size));
        if(state<0){recognizerFree(recognizer);modelFree(model);return 4;}
        const auto response=QJsonDocument::fromJson(state?result(recognizer):partial(recognizer)).object();
        const auto text=response.value(state?"text":"partial").toString();
        const double chunkMs=chunk.nsecsElapsed()/1e6;decodeMs+=chunkMs;
        event(text,state!=0,(offset+size)/32000.0,chunkMs);
        if(state&&!text.isEmpty())transcript+=text+" ";
    }
    chunk.start();const auto tail=QJsonDocument::fromJson(finish(recognizer)).object().value("text").toString();
    const double finalMs=chunk.nsecsElapsed()/1e6;decodeMs+=finalMs;event(tail,true,pcm.size()/32000.0,finalMs);
    decodeEndWallMs=decode.nsecsElapsed()/1e6;
    transcript=(transcript+tail).trimmed();recognizerFree(recognizer);modelFree(model);
#endif
    QJsonObject report{{"transcript",transcript},{"modelLoadMs",loadMs},{"decodeMs",decodeMs},{"decodeEndWallMs",decodeEndWallMs},
        {"audioSeconds",pcm.size()/32000.0},{"mode",mode},{"realtime",options.isSet("realtime")},{"events",events}};
    QFile output(options.value("output"));if(!output.open(QIODevice::WriteOnly))return 5;
    output.write(QJsonDocument(report).toJson());return 0;
}
