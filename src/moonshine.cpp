// SPDX-License-Identifier: AGPL-3.0-or-later
#include "moonshine.h"
#include "../third_party/moonshine/moonshine-c-api.h"
#include <QElapsedTimer>
#include <QLibrary>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <vector>

struct MoonshineRecognizer::Impl {
    QLibrary library;
    int32_t transcriber=-1;
    std::array<int32_t,2> streams{-1,-1};
    std::array<double,2> pendingSeconds{},lastPassSeconds{},audioSeconds{};
    std::array<uint64_t,2> firstIncomplete{};
    std::array<QString,2> lastText{};
    decltype(&moonshine_load_transcriber_from_files) load=nullptr;
    decltype(&moonshine_free_transcriber) free=nullptr;
    decltype(&moonshine_create_stream) create=nullptr;
    decltype(&moonshine_start_stream) start=nullptr;
    decltype(&moonshine_stop_stream) stop=nullptr;
    decltype(&moonshine_free_stream) release=nullptr;
    decltype(&moonshine_transcribe_add_audio_to_stream) add=nullptr;
    decltype(&moonshine_transcribe_stream) transcribe=nullptr;
};
MoonshineRecognizer::MoonshineRecognizer():impl_(std::make_unique<Impl>()){}
MoonshineRecognizer::~MoonshineRecognizer(){unload();}
void MoonshineRecognizer::unload(){
    auto &p=*impl_;
    for(auto &stream:p.streams){
        if(stream>=0&&p.transcriber>=0){
            // Stop is cancellation: do not emit a late final into an edited script.
            if(p.stop)p.stop(p.transcriber,stream);
            if(p.release)p.release(p.transcriber,stream);
        }
        stream=-1;
    }
    if(p.transcriber>=0&&p.free)p.free(p.transcriber);
    p.transcriber=-1;
    // Leave an explicitly empty library object after cleanup.
    if(p.library.isLoaded())p.library.unload();
    p.library.setFileName(QString());
    p.pendingSeconds={};p.lastPassSeconds={};p.audioSeconds={};p.firstIncomplete={};p.lastText={};
}
bool MoonshineRecognizer::load(const QString &library,const QString &model,const QString &language,int architecture,bool meeting,QString &error){
    unload();auto &p=*impl_;
    if(architecture<2||architecture>5){error="Select a Moonshine streaming model architecture.";return false;}
    p.library.setFileName(library);
    if(!p.library.load()){error="Cannot load Moonshine and its ONNX Runtime dependency: "+p.library.errorString();return false;}
#define LOAD(member,name) p.member=reinterpret_cast<decltype(p.member)>(p.library.resolve(name))
    LOAD(load,"moonshine_load_transcriber_from_files");LOAD(free,"moonshine_free_transcriber");
    LOAD(create,"moonshine_create_stream");LOAD(start,"moonshine_start_stream");
    LOAD(stop,"moonshine_stop_stream");LOAD(release,"moonshine_free_stream");
    LOAD(add,"moonshine_transcribe_add_audio_to_stream");LOAD(transcribe,"moonshine_transcribe_stream");
#undef LOAD
    if(!p.load||!p.free||!p.create||!p.start||!p.stop||!p.release||!p.add||!p.transcribe){error="The library does not provide the supported Moonshine streaming API.";unload();return false;}
    const moonshine_option_t options[]={{"return_audio_data","false"},{"max_tokens_per_second","13.0"}};
    const auto baseLanguage=language.toLower().replace('_','-').section('-',0,0);
    const bool nonLatin=baseLanguage=="ja"||baseLanguage=="zh";
    p.transcriber=p.load(model.toUtf8().constData(),uint32_t(architecture),options,nonLatin?2:1,MOONSHINE_HEADER_VERSION);
    if(p.transcriber<0){error="Moonshine could not load this model or API version ("+QString::number(p.transcriber)+"). Check the model architecture and runtime version.";unload();return false;}
    for(int channel=0;channel<(meeting?2:1);++channel){
        p.streams[channel]=p.create(p.transcriber,0);
        if(p.streams[channel]<0||p.start(p.transcriber,p.streams[channel])<0){error="Moonshine could not start the recognition stream.";unload();return false;}
    }
    return true;
}
bool MoonshineRecognizer::consume(int channel,const QByteArray &pcm,QList<Event> &events,QString &error){
    auto &p=*impl_;
    if(channel<0||channel>1||p.streams[channel]<0||pcm.size()%2){error="Invalid Moonshine audio stream or PCM input.";return false;}
    std::vector<float> audio(size_t(pcm.size()/2));
    for(size_t i=0;i<audio.size();++i)audio[i]=qFromLittleEndian<qint16>(pcm.constData()+2*i)/32768.0F;
    if(p.add(p.transcriber,p.streams[channel],audio.data(),audio.size(),16000,0)<0){error="Moonshine could not accept the captured audio.";return false;}
    p.pendingSeconds[channel]+=audio.size()/16000.0;
    p.audioSeconds[channel]+=audio.size()/16000.0;
    // Match the SDK's adaptive update floor; bounded capture backpressure lives in SpeechService.
    const auto interval=std::clamp(p.lastPassSeconds[channel],0.5,2.0);
    if(p.pendingSeconds[channel]+1e-9<interval)return true;
    transcript_t *result=nullptr;QElapsedTimer timer;timer.start();
    const auto status=p.transcribe(p.transcriber,p.streams[channel],0,&result);
    p.lastPassSeconds[channel]=timer.nsecsElapsed()/1e9;p.pendingSeconds[channel]=0;
    if(status<0||!result){error="Moonshine could not transcribe the audio.";return false;}
    // Completed lines are immutable. Copy borrowed UTF-8 before the next API call.
    auto copyLines=[&](const transcript_t *result){
    for(uint64_t i=p.firstIncomplete[channel];i<result->line_count;++i){
        const auto &line=result->lines[i];
        const auto text=QString::fromUtf8(line.text?line.text:"");
        if(!text.isEmpty()&&(text!=p.lastText[channel]||line.is_complete))events.append({text,line.is_complete!=0});
        p.lastText[channel]=text;
        if(line.is_complete){p.firstIncomplete[channel]=i+1;p.lastText[channel].clear();}
    }};
    copyLines(result);
    // The upstream API retains text history. Rotate only at a completed, quiet
    // boundary, draining before restart so buffered audio is not silently dropped.
    if(result->line_count>=64&&p.firstIncomplete[channel]==result->line_count){
        const auto &last=result->lines[result->line_count-1];
        if(p.audioSeconds[channel]-(last.start_time+last.duration)>=1.0){
            if(p.stop(p.transcriber,p.streams[channel])<0||p.transcribe(p.transcriber,p.streams[channel],0,&result)<0||!result){error="Moonshine could not finish the recognition segment.";return false;}
            copyLines(result);
            if(p.start(p.transcriber,p.streams[channel])<0){error="Moonshine could not resume after releasing transcript history.";return false;}
            p.firstIncomplete[channel]=0;p.lastText[channel].clear();p.audioSeconds[channel]=0;
        }
    }
    if(p.firstIncomplete[channel]>=256){error="Listening stopped at the transcript history limit. Restart listening to continue.";return false;}
    return true;
}
