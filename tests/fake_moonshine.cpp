// SPDX-License-Identifier: AGPL-3.0-or-later
// Lifecycle test double. No inference and no quality/performance claims.
#include "../third_party/moonshine/moonshine-c-api.h"
#include <cstring>
static transcript_line_t lines[65]{};
static transcript_t result{lines,64};
static int starts=0,stops=0,frees=0,drains=0,withoutAudio=0;
extern "C" {
int fake_starts(){return starts;} int fake_stops(){return stops;} int fake_frees(){return frees;}
int fake_drains(){return drains;} int fake_without_audio(){return withoutAudio;}
int32_t moonshine_load_transcriber_from_files(const char*,uint32_t,const moonshine_option_t *options,uint64_t count,int32_t version){
    starts=stops=frees=drains=withoutAudio=0;
    for(uint64_t i=0;i<count;++i)if(std::strcmp(options[i].name,"return_audio_data")==0&&std::strcmp(options[i].value,"false")==0)withoutAudio=1;
    for(auto &line:lines){line={};line.text="completed";line.is_complete=1;}
    lines[64].text="drained pending audio";
    return version==MOONSHINE_HEADER_VERSION?7:-1;
}
void moonshine_free_transcriber(int32_t){++frees;}
int32_t moonshine_create_stream(int32_t,uint32_t){return 9;}
int32_t moonshine_start_stream(int32_t,int32_t){++starts;result.line_count=64;return 0;}
int32_t moonshine_stop_stream(int32_t,int32_t){++stops;return 0;}
int32_t moonshine_free_stream(int32_t,int32_t){return 0;}
int32_t moonshine_transcribe_add_audio_to_stream(int32_t,int32_t,const float*,uint64_t,int32_t,uint32_t){return 0;}
int32_t moonshine_transcribe_stream(int32_t,int32_t,uint32_t,transcript_t **out){
    if(stops>=starts){result.line_count=65;++drains;}
    *out=&result;return 0;
}
}
