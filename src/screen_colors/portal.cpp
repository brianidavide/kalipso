// SPDX-License-Identifier: AGPL-3.0-or-later
#include "portal.h"
#include "pixels.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QUuid>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/buffers.h>
#include <unistd.h>

namespace {
constexpr auto service="org.freedesktop.portal.Desktop";
constexpr auto path="/org/freedesktop/portal/desktop";
constexpr auto iface="org.freedesktop.portal.ScreenCast";
QString token(){return "kalipso_"+QUuid::createUuid().toString(QUuid::Id128);}
QString busSender(){auto name=QDBusConnection::sessionBus().baseService().mid(1);return name.replace('.','_');}
void closePath(const QString &target,const char *interface){
    if(target.isEmpty())return;
    auto call=QDBusMessage::createMethodCall(service,target,interface,"Close");
    QDBusConnection::sessionBus().asyncCall(call);
}
}

struct PortalColors::Stream {
    PortalColors *owner;
    quint64 generation;
    pw_main_loop *loop=nullptr;
    pw_context *context=nullptr;
    pw_core *core=nullptr;
    pw_stream *stream=nullptr;
    spa_hook listener{},coreListener{};
    spa_video_info_raw format{};
    std::unique_ptr<QSocketNotifier> notifier;
    QElapsedTimer age;
    bool errorQueued=false;
    explicit Stream(PortalColors *p):owner(p),generation(p->generation_){}
    ~Stream(){
        notifier.reset();
        if(stream){spa_hook_remove(&listener);pw_stream_destroy(stream);}
        if(core){spa_hook_remove(&coreListener);pw_core_disconnect(core);}
        if(context)pw_context_destroy(context);
        if(loop){pw_loop_leave(pw_main_loop_get_loop(loop));pw_main_loop_destroy(loop);}
    }
    void fail(){
        if(errorQueued)return;errorQueued=true;
        auto *p=owner;const auto epoch=generation;
        QMetaObject::invokeMethod(p,[p,epoch]{if(p->generation_==epoch)p->fail(tr("Window colors are unavailable. Choose the window again or use Light or Dark."));},Qt::QueuedConnection);
    }
    static void state(void *data,pw_stream_state,pw_stream_state next,const char *){
        if(next==PW_STREAM_STATE_ERROR||next==PW_STREAM_STATE_UNCONNECTED)static_cast<Stream*>(data)->fail();
    }
    static void coreError(void *data,uint32_t,int,int,const char *){static_cast<Stream*>(data)->fail();}
    static void param(void *data,uint32_t id,const spa_pod *pod){
        auto *self=static_cast<Stream*>(data);
        if(id!=SPA_PARAM_Format||!pod)return;
        self->format={};
        if(spa_format_video_raw_parse(pod,&self->format)<0){self->fail();return;}
        // Readable CPU buffers only; GPU-only DMA-BUFs cannot be sampled here.
        uint8_t storage[256];spa_pod_builder builder=SPA_POD_BUILDER_INIT(storage,sizeof(storage));
        const spa_pod *params[]={static_cast<spa_pod*>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_ParamBuffers,SPA_PARAM_Buffers,
            SPA_PARAM_BUFFERS_dataType,SPA_POD_CHOICE_FLAGS_Int((1<<SPA_DATA_MemPtr)|(1<<SPA_DATA_MemFd))))};
        if(pw_stream_update_params(self->stream,params,1)<0)self->fail();
    }
    static void process(void *data){
        auto *self=static_cast<Stream*>(data);
        pw_buffer *buffer=pw_stream_dequeue_buffer(self->stream);if(!buffer)return;
        QColor color;
        if(!self->age.isValid()||self->age.elapsed()>=2000){
            const auto &f=self->format;const auto *b=buffer->buffer;
            if(b->n_datas==1&&b->datas[0].data&&b->datas[0].chunk){
                const auto &d=b->datas[0];const auto &chunk=*d.chunk;
                const bool bgr=f.format==SPA_VIDEO_FORMAT_BGRx||f.format==SPA_VIDEO_FORMAT_BGRA;
                const bool rgb=f.format==SPA_VIDEO_FORMAT_RGBx||f.format==SPA_VIDEO_FORMAT_RGBA;
                if((bgr||rgb)&&!(chunk.flags&SPA_CHUNK_FLAG_CORRUPTED)&&chunk.offset<=d.maxsize&&chunk.size<=d.maxsize-chunk.offset)
                    color=ScreenColors::average(static_cast<const char*>(d.data)+chunk.offset,chunk.size,int(f.size.width),int(f.size.height),chunk.stride,bgr);
            }
            if(color.isValid())self->age.restart();
        }
        pw_stream_queue_buffer(self->stream,buffer);
        if(color.isValid()){
            auto *p=self->owner;const auto epoch=self->generation;
            QMetaObject::invokeMethod(p,[p,epoch,color]{if(p->generation_!=epoch)return;p->timeout_.stop();p->publish(tr("Following the selected window · Sharing active · No images saved"));emit p->colorChanged(color);},Qt::QueuedConnection);
        }
    }
    bool open(int fd,uint node){
        static const bool initialized=[](){pw_init(nullptr,nullptr);return true;}();(void)initialized;
        loop=pw_main_loop_new(nullptr);if(!loop){::close(fd);return false;}
        pw_loop_enter(pw_main_loop_get_loop(loop));
        context=pw_context_new(pw_main_loop_get_loop(loop),pw_properties_new("module.rt","false","module.rt.args","{ rtportal.enabled = false rtkit.enabled = false }",nullptr),0);if(!context){::close(fd);return false;}
        core=pw_context_connect_fd(context,fd,nullptr,0);if(!core)return false;
        static const pw_core_events coreEvents=[](){pw_core_events e{};e.version=PW_VERSION_CORE_EVENTS;e.error=coreError;return e;}();
        pw_core_add_listener(core,&coreListener,&coreEvents,this);
        stream=pw_stream_new(core,"Kalipso window colors",pw_properties_new(PW_KEY_MEDIA_TYPE,"Video",PW_KEY_MEDIA_CATEGORY,"Capture",PW_KEY_MEDIA_ROLE,"Screen",nullptr));
        if(!stream)return false;
        static const pw_stream_events events=[](){pw_stream_events e{};e.version=PW_VERSION_STREAM_EVENTS;e.state_changed=state;e.param_changed=param;e.process=process;return e;}();
        pw_stream_add_listener(stream,&listener,&events,this);
        uint8_t storage[1024];spa_pod_builder builder=SPA_POD_BUILDER_INIT(storage,sizeof(storage));
        spa_rectangle minimum{1,1},maximum{16384,16384},preferred{640,480};spa_fraction rate{1,1},zero{0,1};
        const spa_pod *params[]={static_cast<spa_pod*>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Format,SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType,SPA_POD_Id(SPA_MEDIA_TYPE_video),SPA_FORMAT_mediaSubtype,SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format,SPA_POD_CHOICE_ENUM_Id(5,SPA_VIDEO_FORMAT_BGRx,SPA_VIDEO_FORMAT_BGRx,SPA_VIDEO_FORMAT_RGBx,SPA_VIDEO_FORMAT_BGRA,SPA_VIDEO_FORMAT_RGBA),
            SPA_FORMAT_VIDEO_size,SPA_POD_CHOICE_RANGE_Rectangle(&preferred,&minimum,&maximum),
            SPA_FORMAT_VIDEO_framerate,SPA_POD_CHOICE_RANGE_Fraction(&rate,&zero,&rate),
            SPA_FORMAT_VIDEO_maxFramerate,SPA_POD_CHOICE_RANGE_Fraction(&rate,&zero,&rate)))};
        if(pw_stream_connect(stream,PW_DIRECTION_INPUT,node,pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS),params,1)<0)return false;
        auto *pwloop=pw_main_loop_get_loop(loop);
        notifier=std::make_unique<QSocketNotifier>(pw_loop_get_fd(pwloop),QSocketNotifier::Read);
        QObject::connect(notifier.get(),&QSocketNotifier::activated,owner,[this,pwloop]{if(pw_loop_iterate(pwloop,0)<0)fail();});
        return true;
    }
};

PortalColors::PortalColors(QObject *parent):QObject(parent){
    timeout_.setSingleShot(true);connect(&timeout_,&QTimer::timeout,this,[this]{fail(tr("Window sharing timed out. Choose the window again or use Light or Dark."));});
}
PortalColors::~PortalColors(){stop();}
void PortalColors::publish(const QString &status){if(status_==status)return;status_=status;emit statusChanged(status);}
void PortalColors::stop(){
    ++generation_;active_=false;timeout_.stop();stream_.reset();
    auto bus=QDBusConnection::sessionBus();
    if(!requestPath_.isEmpty()){
        bus.disconnect(service,requestPath_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
        closePath(requestPath_,"org.freedesktop.portal.Request");requestPath_.clear();
    }
    if(!sessionPath_.isEmpty()){
        bus.disconnect(service,sessionPath_,"org.freedesktop.portal.Session","Closed",this,SLOT(sessionClosed()));
        closePath(sessionPath_,"org.freedesktop.portal.Session");sessionPath_.clear();
    }
    stage_.clear();publish(tr("Window sharing stopped · Choose a background window to resume"));
}
void PortalColors::fail(const QString &reason){stop();publish(reason);}
void PortalColors::sessionClosed(){fail(tr("Window sharing ended · Choose a background window to resume"));}
void PortalColors::start(){
    stop();active_=true;
    const auto sessionToken=token();sessionPath_=QString(path)+"/session/"+busSender()+"/"+sessionToken;
    QDBusConnection::sessionBus().connect(service,sessionPath_,"org.freedesktop.portal.Session","Closed",this,SLOT(sessionClosed()));
    publish(tr("Choose the window behind your reader in the desktop sharing dialog"));
    request("CreateSession",{},{{"session_handle_token",sessionToken}});
}
void PortalColors::request(const QString &method,QVariantList arguments,QVariantMap options){
    stage_=method;const auto requestToken=token();options.insert("handle_token",requestToken);
    requestPath_=QString(path)+"/request/"+busSender()+"/"+requestToken;
    auto bus=QDBusConnection::sessionBus();
    if(!bus.connect(service,requestPath_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)))){fail(tr("Desktop sharing is unavailable. Install a ScreenCast desktop portal."));return;}
    auto call=QDBusMessage::createMethodCall(service,path,iface,method);arguments.append(options);call.setArguments(arguments);
    const auto epoch=generation_;auto *watcher=new QDBusPendingCallWatcher(bus.asyncCall(call),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,epoch](QDBusPendingCallWatcher *w){
        QDBusPendingReply<QDBusObjectPath> reply=*w;w->deleteLater();if(epoch!=generation_)return;
        if(reply.isError())fail(tr("Desktop window sharing is unavailable. Check the desktop portal and try again."));
    });
    timeout_.start(120000);
}
void PortalColors::response(uint code,const QVariantMap &results,const QDBusMessage &message){
    if(!active_||message.path()!=requestPath_)return;
    QDBusConnection::sessionBus().disconnect(service,requestPath_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
    requestPath_.clear();timeout_.stop();
    if(code!=0){fail(code==1?tr("Window sharing cancelled · The reader keeps its contrast"):tr("Window sharing failed · The reader keeps its contrast"));return;}
    if(stage_=="CreateSession"){
        if(results.value("session_handle").toString()!=sessionPath_){fail(tr("The desktop returned an invalid sharing session."));return;}
        // Only windows: avoids unreliable global coordinates on Wayland and capturing the reader itself.
        request("SelectSources",{QVariant::fromValue(QDBusObjectPath(sessionPath_))},{{"types",uint(2)},{"multiple",false},{"cursor_mode",uint(1)}});
    }else if(stage_=="SelectSources"){
        request("Start",{QVariant::fromValue(QDBusObjectPath(sessionPath_)),QString{}});
    }else if(stage_=="Start"){
        if(!results.contains("streams")){fail(tr("Choose one background window to follow its colors."));return;}
        const auto argument=results.value("streams").value<QDBusArgument>();
        uint node=0;QVariantMap properties;int count=0;
        argument.beginArray();while(!argument.atEnd()){argument.beginStructure();argument>>node>>properties;argument.endStructure();++count;}argument.endArray();
        if(count!=1||!node||(properties.contains("source_type")&&properties.value("source_type").toUInt()!=2)){fail(tr("Choose one background window to follow its colors."));return;}
        openRemote(node);
    }
}
void PortalColors::openRemote(uint node){
    auto call=QDBusMessage::createMethodCall(service,path,iface,"OpenPipeWireRemote");
    call.setArguments({QVariant::fromValue(QDBusObjectPath(sessionPath_)),QVariantMap{}});
    auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call),this);const auto epoch=generation_;
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,epoch,node](QDBusPendingCallWatcher *w){
        QDBusPendingReply<QDBusUnixFileDescriptor> reply=*w;w->deleteLater();if(epoch!=generation_)return;
        if(reply.isError()||!reply.value().isValid()){fail(tr("The desktop could not open the selected window."));return;}
        const int fd=::dup(reply.value().fileDescriptor());
        stream_=std::make_unique<Stream>(this);
        if(fd<0||!stream_->open(fd,node)){fail(tr("The window color stream could not start. Check PipeWire and try again."));return;}
        publish(tr("Window sharing active · Waiting for its colors"));timeout_.start(15000);
    });
    timeout_.start(15000);
}
