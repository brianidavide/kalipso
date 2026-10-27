// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QColor>
#include <cstddef>
#include <cstdint>

namespace ScreenColors {
// Sparse grid: at most 256 pixels per sample, independent of display resolution.
// Validate the complete layout before touching a pixel supplied by PipeWire.
inline QColor average(const void *data,size_t bytes,int width,int height,int stride,bool bgr){
    if(!data||width<=0||height<=0||width>16384||height>16384||stride<width*4)return {};
    const auto needed=size_t(height-1)*size_t(stride)+size_t(width)*4;
    if(needed>bytes)return {};
    uint64_t red=0,green=0,blue=0,count=0;
    const auto *pixels=static_cast<const uint8_t*>(data);
    const int columns=qMin(width,16),rows=qMin(height,16);
    for(int row=0;row<rows;++row)for(int column=0;column<columns;++column){
        const int x=(2*column+1)*width/(2*columns),y=(2*row+1)*height/(2*rows);
        const auto *p=pixels+size_t(y)*size_t(stride)+size_t(x)*4;
        red+=p[bgr?2:0];green+=p[1];blue+=p[bgr?0:2];++count;
    }
    return QColor(int(red/count),int(green/count),int(blue/count));
}
}
