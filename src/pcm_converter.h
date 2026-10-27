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
    void reset(int rate,int channels,Format format){
        rate_=rate;channels_=channels;format_=format;pending_.clear();samples_=0;next_=0;previous_=0;
        if(rate>16000){filters_[0].reset(rate,0.541196100146197);filters_[1].reset(rate,1.30656296487638);}
    }
    QByteArray convert(const QByteArray &input){
        QByteArray output;
        if(rate_<=0||channels_<=0||channels_>32){pending_.clear();return output;}
        pending_+=input;
        const int bytes=format_==Unsigned8?1:format_==Signed16?2:4;
        const int frame=bytes*channels_;
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
            value=std::clamp(value/channels_,-1.0,1.0);
            if(rate_>16000)for(auto &filter:filters_)value=filter.apply(value);
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
    // Fourth-order low-pass prevents high-frequency input from aliasing into speech.
    struct LowPass {
        double b0=0,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void reset(int rate,double q){
            const double omega=2.0*3.141592653589793*6800.0/rate;
            const double c=std::cos(omega),alpha=std::sin(omega)/(2.0*q),a0=1.0+alpha;
            b0=(1.0-c)/(2.0*a0);b1=2.0*b0;b2=b0;a1=-2.0*c/a0;a2=(1.0-alpha)/a0;z1=z2=0;
        }
        double apply(double x){const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
    } filters_[2];
    int rate_=0,channels_=0;Format format_=Signed16;
    QByteArray pending_;quint64 samples_=0;double next_=0,previous_=0;
};
