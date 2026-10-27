// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech.h"
#include "screen_colors/portal.h"
#include "config.h"
#include "settings_migration.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QSocketNotifier>
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

static constexpr auto serviceName="io.github.brianidavide.Kalipso.Engine";
static constexpr auto objectPath="/io/github/brianidavide/Kalipso/Engine";
static int shutdownWriteFd=-1;
static void requestShutdown(int) {const int saved=errno;const char byte=1;if(shutdownWriteFd>=0){const auto ignored=::write(shutdownWriteFd,&byte,1);(void)ignored;}errno=saved;}
static QString json(const QJsonObject &value) { return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)); }

class Engine final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.brianidavide.Kalipso.Engine")
public:
    Engine() : watcher_(QString(),QDBusConnection::sessionBus(),QDBusServiceWatcher::WatchForUnregistration) {
        connect(&colors_,&PortalColors::colorChanged,this,[this](const QColor &color){color_=color.name();});
        connect(&colorWatcher_,&QDBusServiceWatcher::serviceUnregistered,this,[this](const QString &name){if(name==colorOwner_)stopColors();});
        colorWatcher_.setConnection(QDBusConnection::sessionBus());colorWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        idle_.start();state_["backend"]=SpeechConfig::selected();
        connect(&speech_,&SpeechService::listening,this,[this]{state_["status"]="listening";publish();});
        connect(&speech_,&SpeechService::status,this,[this](const QString &message){
            state_["message"]=message;
            if(!speech_.active()) {state_["status"]="error";releaseOwner();}
            publish();
        });
        connect(&speech_,&SpeechService::transcript,this,[this](int channel,const QString &text,bool final){
            if(channel==0) {state_["text"]=text;state_["final"]=final;state_["sequence"]=++sequence_;
                events_.append(QJsonObject{{"sequence",sequence_},{"text",text},{"final",final}});
                while(events_.size()>64)events_.removeFirst();
                state_["events"]=events_;}
            else {state_["meetingText"]=text;state_["meetingFinal"]=final;state_["meetingSequence"]=++meetingSequence_;}
            publish();
        });
        connect(&watcher_,&QDBusServiceWatcher::serviceUnregistered,this,[this](const QString &name){if(name==owner_)stop();});
        connect(&timer_,&QTimer::timeout,this,[this]{
            if(!owner_.isEmpty() && lease_.elapsed()>10000) stop();
            if(!colorOwner_.isEmpty()&&colorLease_.elapsed()>10000)stopColors();
            if(owner_.isEmpty() && colorOwner_.isEmpty() && idle_.elapsed()>90000) QCoreApplication::quit();
        });
        timer_.start(1000);
    }
public slots:
    bool StartColors(const QString &clientToken){
        if(!calledFromDBus())return false;
        if(clientToken.isEmpty()||clientToken.size()>128){sendErrorReply(QDBusError::InvalidArgs,"A widget identifier is required.");return false;}
        const auto caller=message().service();
        if(!colorOwner_.isEmpty()&&(caller!=colorOwner_||clientToken!=colorToken_)){sendErrorReply(QDBusError::AccessDenied,"Another widget is sharing a background window.");return false;}
        colorOwner_=caller;colorToken_=clientToken;colorWatcher_.addWatchedService(caller);colorLease_.start();idle_.restart();color_.clear();colors_.start();return true;
    }
    QString GetColorState(const QString &clientToken){
        if(!calledFromDBus()||(!colorOwner_.isEmpty()&&(message().service()!=colorOwner_||clientToken!=colorToken_))){sendErrorReply(QDBusError::AccessDenied,"Only the sharing widget can read its color state.");return {};}
        colorLease_.restart();idle_.restart();
        return json({{"active",colors_.active()},{"message",colors_.status()},{"color",color_}});
    }
    void StopColors(const QString &clientToken){
        if(!calledFromDBus()||(!colorOwner_.isEmpty()&&(message().service()!=colorOwner_||clientToken!=colorToken_))){sendErrorReply(QDBusError::AccessDenied,"Only the sharing widget can stop its session.");return;}
        stopColors();
    }
    QString GetDependencies() {idle_.restart();return json(SpeechConfig::dependencies(SpeechConfig::selected()));}
    QString GetBackends() {idle_.restart();return json({{"selected",SpeechConfig::selected()},{"backends",SpeechConfig::backends()}});}
    bool SelectBackend(const QString &backend) {
        if(!calledFromDBus())return false;
        if(speech_.active()||!owner_.isEmpty()){sendErrorReply(QDBusError::AccessDenied,"Stop listening before changing the speech engine.");return false;}
        if(!SpeechConfig::supported(backend)){sendErrorReply(QDBusError::InvalidArgs,"Choose Moonshine or Vosk.");return false;}
        QSettings config;config.setValue("speech/backend",backend);config.sync();
        if(config.status()!=QSettings::NoError){sendErrorReply(QDBusError::Failed,"Cannot save the speech engine selection.");return false;}
        state_["backend"]=backend;state_["text"]="";state_["final"]=false;state_["sequence"]=++sequence_;events_={};state_["events"]=events_;
        state_["status"]="ready";state_["message"]="Speech engine selected. Start listening when its model is configured.";idle_.restart();publish();return true;
    }
    QString GetState() {
        idle_.restart();
        if(calledFromDBus() && message().service()==owner_)lease_.restart();
        return json(state_);
    }
    bool Start() {
        if(!calledFromDBus())return false;
        const auto caller=message().service();
        if(!owner_.isEmpty()) {
            if(caller==owner_) {lease_.restart();return true;}
            sendErrorReply(QDBusError::AccessDenied,"Another frontend owns the microphone. Stop listening there first.");return false;
        }
        const auto check=SpeechConfig::dependencies(SpeechConfig::selected());
        if(!check.value("ready").toBool()) {
            state_["status"]="missing-dependencies";state_["message"]=check.value("missing").toArray().first().toString();publish();return false;
        }
        owner_=caller;watcher_.addWatchedService(owner_);lease_.start();
        state_["status"]="loading";state_["message"]="Loading local speech model";
        state_["backend"]=check.value("backend");events_={};state_["events"]=events_;
        state_["text"]="";state_["meetingText"]="";state_["final"]=false;state_["meetingFinal"]=false;
        state_["sequence"]=++sequence_;state_["meetingSequence"]=++meetingSequence_;publish();
        QSettings config;
        speech_.start(check.value("library").toString(),check.value("model").toString(),
                      config.value("speech/microphone").toString(),config.value("speech/meetingTarget").toString(),
                      config.value("speech/meetingEnabled",false).toBool(),check.value("backend").toString(),
                      check.value("language").toString(),check.value("architecture").toInt());
        return speech_.active();
    }
    void Stop() {
        if(calledFromDBus() && !owner_.isEmpty() && message().service()!=owner_) {
            sendErrorReply(QDBusError::AccessDenied,"Only the listening frontend can stop its session.");return;
        }
        stop();
    }
signals:
    void StateChanged(const QString &state);
private:
    void publish(){state_["active"]=speech_.active();emit StateChanged(json(state_));}
    void releaseOwner(){if(!owner_.isEmpty())watcher_.removeWatchedService(owner_);owner_.clear();idle_.restart();}
    void stop(){speech_.stop();releaseOwner();state_["status"]="stopped";state_["message"]="Microphone stopped";publish();}
    void stopColors(){colors_.stop();colorWatcher_.removeWatchedService(colorOwner_);colorOwner_.clear();colorToken_.clear();color_.clear();idle_.restart();}
    PortalColors colors_;
    QDBusServiceWatcher colorWatcher_;
    QElapsedTimer colorLease_;
    QString colorOwner_,colorToken_,color_;
    QJsonArray events_;
    SpeechService speech_;
    QDBusServiceWatcher watcher_;
    QTimer timer_;
    QElapsedTimer lease_,idle_;
    QString owner_;
    int sequence_=0,meetingSequence_=0;
    QJsonObject state_{{"status","ready"},{"message","Local engine ready"},{"active",false},{"text",""},{"final",false},{"sequence",0},{"meetingSequence",0},{"backend","moonshine"}};
};

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QCoreApplication::setOrganizationName("Kalipso");QCoreApplication::setApplicationName("engine");
    importLegacySettings("Teleprompter", "engine");
    QCommandLineParser args;args.setApplicationDescription("Optional local speech companion for the Plasma and GNOME frontends.");args.addHelpOption();
    for(const auto name:{"serve","configure","check","list-backends"})args.addOption(QCommandLineOption(name));
    for(const auto name:{"backend","library","model","language","architecture","microphone","meeting-target"})args.addOption(QCommandLineOption(name,QString("Set %1").arg(name),"value"));
    args.addOption(QCommandLineOption("meeting-enabled","Enable or disable optional meeting output capture","true|false"));args.process(app);
    if(args.isSet("configure")) {
        const auto backend=args.isSet("backend")?args.value("backend"):SpeechConfig::selected();
        bool archOk=true;int arch=2;
        if(args.isSet("architecture")){arch=args.value("architecture").toInt(&archOk);archOk=archOk&&arch>=2&&arch<=5;}
        if(!SpeechConfig::supported(backend)||!archOk){std::fprintf(stderr,"Choose --backend moonshine|vosk and a streaming --architecture 2|3|4|5.\n");return 2;}
        if(args.isSet("meeting-enabled")&&args.value("meeting-enabled")!="true"&&args.value("meeting-enabled")!="false"){std::fprintf(stderr,"--meeting-enabled must be true or false\n");return 2;}
        QDBusInterface running(serviceName,objectPath,serviceName);
        if(running.isValid()){
            const QDBusReply<QString> reply=running.call("GetState");
            if(reply.isValid()&&QJsonDocument::fromJson(reply.value().toUtf8()).object().value("active").toBool()){
                std::fprintf(stderr,"Stop listening before configuring a speech engine.\n");return 2;
            }
        }
        QSettings config;config.setValue("speech/backend",backend);
        for(const auto name:{"library","model","language"})if(args.isSet(name))config.setValue("profiles/"+backend+"/"+name,args.value(name));
        if(args.isSet("architecture"))config.setValue("profiles/"+backend+"/architecture",arch);
        if(args.isSet("microphone"))config.setValue("speech/microphone",args.value("microphone"));
        if(args.isSet("meeting-target"))config.setValue("speech/meetingTarget",args.value("meeting-target"));
        if(args.isSet("meeting-enabled"))config.setValue("speech/meetingEnabled",args.value("meeting-enabled")=="true");
        config.sync();if(config.status()!=QSettings::NoError){std::fprintf(stderr,"Cannot write configuration\n");return 1;}
    }
    if(args.isSet("list-backends")){
        const auto output=json({{"selected",SpeechConfig::selected()},{"backends",SpeechConfig::backends()}}).toUtf8();std::printf("%s\n",output.constData());return 0;
    }
    if(args.isSet("check") || args.isSet("configure")) {
        const auto result=SpeechConfig::dependencies(SpeechConfig::selected());const auto output=json(result).toUtf8();std::printf("%s\n",output.constData());
        return result.value("ready").toBool()?0:1;
    }
    if(!args.isSet("serve")){args.showHelp(2);}
    auto bus=QDBusConnection::sessionBus();Engine engine;
    if(!bus.registerObject(objectPath,&engine,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals) || !bus.registerService(serviceName)) {
        std::fprintf(stderr,"Cannot register session D-Bus service: %s\n",bus.lastError().message().toUtf8().constData());return 1;
    }
    int shutdownPipe[2];
    if(::pipe2(shutdownPipe,O_NONBLOCK|O_CLOEXEC)!=0){std::perror("shutdown pipe");return 1;}
    shutdownWriteFd=shutdownPipe[1];
    std::signal(SIGTERM,requestShutdown);std::signal(SIGINT,requestShutdown);
    QSocketNotifier notifier(shutdownPipe[0],QSocketNotifier::Read);
    QObject::connect(&notifier,&QSocketNotifier::activated,&app,[&]{char bytes[32];const auto ignored=::read(shutdownPipe[0],bytes,sizeof(bytes));(void)ignored;app.quit();});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&engine,&Engine::Stop);
    const auto result=app.exec();shutdownWriteFd=-1;::close(shutdownPipe[0]);::close(shutdownPipe[1]);return result;
}
#include "main.moc"
