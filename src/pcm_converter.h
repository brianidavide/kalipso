// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

class PcmConverter {
public:
    enum Format { Unsigned8, Signed16, Signed32, Float32 };
    void reset(int rate,int channels,Format format){rate_=rate;channels_=channels;format_=format;pending_.clear();samples_=0;next_=0;previous_=0;}
    QByteArray convert(const QByteArray &input){
        pending_+=input;QByteArray output;
        const int bytes=format_==Unsigned8?1:format_==Signed16?2:4;
        const int frame=bytes*channels_;
        if(rate_<=0||channels_<=0||channels_>32)return output;
        const auto frames=pending_.size()/frame;
        for(qsizetype i=0;i<frames;++i){
            double value=0;
            for(int ch=0;ch<channels_;++ch){
                const auto *p=pending_.constData()+i*frame+ch*bytes;
                if(format_==Unsigned8)value+=(quint8(*p)-128)/128.0;
                else if(format_==Signed16)value+=qFromLittleEndian<qint16>(p)/32768.0;
                else if(format_==Signed32)value+=qFromLittleEndian<qint32>(p)/2147483648.0;
                else{float f;std::memcpy(&f,p,4);value+=std::isfinite(f)?f:0;}
            }
            value/=channels_;
            while(next_<=double(samples_)){
                const double mix=samples_?std::clamp(next_-double(samples_-1),0.0,1.0):1.0;
                const double sample=previous_+(value-previous_)*mix;
                const auto pcm=qint16(std::clamp(sample,-1.0,1.0)*32767.0);
                char data[2];qToLittleEndian(pcm,data);output.append(data,2);
                next_+=double(rate_)/16000.0;
            }
            previous_=value;++samples_;
        }
        pending_.remove(0,frames*frame);return output;
    }
private:
    int rate_=0,channels_=0;Format format_=Signed16;
    QByteArray pending_;quint64 samples_=0;double next_=0,previous_=0;
};
