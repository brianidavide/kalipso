// SPDX-License-Identifier: AGPL-3.0-or-later
#include "backdrop.h"
#include "system_speech/client.h"
#include <QApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QWindow>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
Backdrop::Backdrop(QWidget *reader):QObject(reader),reader_(reader){
    reader->installEventFilter(this);timer_.setInterval(2000);timer_.setTimerType(Qt::VeryCoarseTimer);
    timeout_.setSingleShot(true);timeout_.setInterval(30000);connect(&timeout_,&QTimer::timeout,this,[this]{stop();emit unavailable(tr("Screen color sampling timed out. The opaque reader keeps its contrast."));});
    connect(&timer_,&QTimer::timeout,this,&Backdrop::sample);
    connect(&helper_,&QProcess::readyReadStandardOutput,this,[this]{
        output_+=helper_.readAllStandardOutput();if(output_.size()>4096){stop();emit unavailable(tr("Screen color sampling returned invalid data."));return;}
        while(output_.contains('\n')){
            const int end=int(output_.indexOf('\n'));const auto line=output_.left(end);output_.remove(0,end+1);pending_=false;timeout_.stop();
            const auto event=QJsonDocument::fromJson(line).object();const QColor color(event.value("color").toString());
            if(event.value("type")=="color"&&color.isValid()&&enabled_&&reader_->isVisible())emit colorChanged(color);
            else if(event.value("type")=="error"){stop();emit unavailable(event.value("message").toString());return;}
        }
    });
    connect(&helper_,&QProcess::readyReadStandardError,this,[this]{helper_.readAllStandardError();});
    connect(&helper_,&QProcess::errorOccurred,this,[this]{stop();emit unavailable(tr("Screen colors are unavailable. The opaque reader keeps its contrast."));});
}
QString Backdrop::source()const{
#ifdef Q_OS_MACOS
    return tr("Colors behind the reader · macOS screen-recording permission required");
#elif defined(Q_OS_WIN)
    return tr("Colors around the reader · no recording or saved images");
#else
    return QGuiApplication::platformName()=="xcb"?tr("Colors around the reader · no recording or saved images"):tr("Screen pixels are unavailable here; the reader uses a fixed high-contrast palette.");
#endif
}
void Backdrop::setEnabled(bool enabled){enabled_=enabled;stop();if(enabled&&reader_->isVisible()){timer_.start();sample();}}
void Backdrop::stop(){timer_.stop();timeout_.stop();helper_.blockSignals(true);helper_.kill();helper_.waitForFinished(500);helper_.blockSignals(false);pending_=false;output_.clear();}
bool Backdrop::eventFilter(QObject *object,QEvent *event){
    if(object==reader_){if(event->type()==QEvent::Hide)stop();else if(event->type()==QEvent::Show&&enabled_){timer_.start();QTimer::singleShot(0,this,&Backdrop::sample);}}
    return QObject::eventFilter(object,event);
}
void Backdrop::sample(){
    if(!enabled_||!reader_->isVisible()||pending_)return;
    const QRect rect=reader_->frameGeometry();
#ifdef Q_OS_MACOS
    const QJsonObject area{{"x",rect.x()},{"y",rect.y()},{"width",rect.width()},{"height",rect.height()}};
    const auto data=QJsonDocument(area).toJson(QJsonDocument::Compact);
    pending_=true;timeout_.start();
    if(helper_.state()==QProcess::NotRunning)helper_.start(SystemSpeechClient::helperPath(),{"--colors",QString::fromUtf8(data)});
    else helper_.write(data+'\n');
#else
#ifndef Q_OS_WIN
    if(QGuiApplication::platformName()!="xcb"){stop();emit unavailable(source());return;}
#endif
    const QList<QPoint> points{{rect.left()-3,rect.center().y()},{rect.right()+3,rect.center().y()},{rect.center().x(),rect.top()-3},{rect.center().x(),rect.bottom()+3}};
    int red=0,green=0,blue=0,count=0;
#ifdef Q_OS_WIN
    const auto dc=GetDC(nullptr);if(!dc)return;
#endif
    for(const auto &point:points){
        auto *screen=QGuiApplication::screenAt(point);if(!screen)continue;
        QColor color;
#ifdef Q_OS_WIN
        // Qt geometry is logical; GDI uses physical pixels, relative to each monitor.
        const auto offset=point-screen->geometry().topLeft();const auto scale=screen->devicePixelRatio();
        const auto pixel=GetPixel(dc,screen->geometry().x()+qRound(offset.x()*scale),screen->geometry().y()+qRound(offset.y()*scale));
        if(pixel==CLR_INVALID)continue;
        color=QColor(GetRValue(pixel),GetGValue(pixel),GetBValue(pixel));
#else
        const auto local=point-screen->geometry().topLeft();const auto image=screen->grabWindow(0,local.x(),local.y(),1,1).toImage();
        if(image.isNull())continue;
        color=image.pixelColor(0,0);
#endif
        red+=color.red();green+=color.green();blue+=color.blue();++count;
    }
#ifdef Q_OS_WIN
    ReleaseDC(nullptr,dc);
#endif
    if(count)emit colorChanged(QColor(red/count,green/count,blue/count));
    else{stop();emit unavailable(tr("Screen colors are unavailable. The opaque reader keeps its contrast."));}
#endif
}
