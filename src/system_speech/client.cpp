// SPDX-License-Identifier: AGPL-3.0-or-later
#include "client.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

SystemSpeechClient::SystemSpeechClient(QObject *parent, QString executable)
    : QObject(parent), executable_(executable.isEmpty()?helperPath():std::move(executable)) {}
SystemSpeechClient::~SystemSpeechClient(){stop();}
bool SystemSpeechClient::platformSupported(){
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}
QString SystemSpeechClient::helperPath(){
    const auto base=QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    const auto bundled=QDir(base).filePath("../Helpers/kalipso-system-speech");
    if(QFileInfo::exists(bundled))return bundled;
#endif
#ifdef Q_OS_WIN
    return QDir(base).filePath("kalipso-system-speech.exe");
#else
    return QDir(base).filePath("kalipso-system-speech");
#endif
}
void SystemSpeechClient::stop(){
    if(process_){
        auto *p=process_;process_=nullptr;p->disconnect(this);
        p->kill();p->waitForFinished(1000);p->deleteLater();
    }
    output_.clear();
}
void SystemSpeechClient::fail(const QString &text){stop();emit failed(text);}
void SystemSpeechClient::check(const QString &language){launch("check",language);}
void SystemSpeechClient::install(const QString &language){launch("install",language);}
void SystemSpeechClient::start(const QString &language,const QStringList &phrases){phrases_=phrases;launch("listen",language);}
void SystemSpeechClient::launch(const QString &operation,const QString &language){
    stop();operation_=operation;receivedResult_=false;revision_=0;
    if(language.trimmed().isEmpty()){emit failed(tr("Choose a recognition language first."));return;}
    if(!QFileInfo::exists(executable_)){emit failed(tr("The system speech helper is not installed on this platform."));return;}
    auto *p=new QProcess(this);process_=p;
    connect(p,&QProcess::readyReadStandardOutput,this,&SystemSpeechClient::readOutput);
    connect(p,&QProcess::readyReadStandardError,this,[p]{p->readAllStandardError();});
    connect(p,&QProcess::errorOccurred,this,[this,p](QProcess::ProcessError){if(process_==p)fail(tr("Cannot start system speech: %1").arg(p->errorString()));});
    connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p](int code,QProcess::ExitStatus status){
        if(process_!=p)return;
        readOutput();if(process_!=p)return;
        const bool success=code==0&&status==QProcess::NormalExit&&receivedResult_&&operation_!="listen";
        stop();if(success)emit completed();else emit failed(tr("System speech ended. Check the language and microphone, then restart."));
    });
    connect(p,&QProcess::started,this,[this]{if(operation_=="listen")updateScript(phrases_);});
    p->start(executable_,{"--"+operation,language});
}
void SystemSpeechClient::updateScript(const QStringList &phrases){
    phrases_=phrases;
    if(!process_||operation_!="listen"||process_->state()!=QProcess::Running)return;
    // Apple transcription does not need script constraints.
#ifdef Q_OS_MACOS
    return;
#endif
    if(process_->bytesToWrite()>65536){fail(tr("Script updates cannot keep up. Restart listening."));return;}
    const QJsonObject command{{"revision",++revision_},{"phrases",QJsonArray::fromStringList(phrases)}};
    process_->write(QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n');
}
void SystemSpeechClient::readOutput(){
    if(!process_)return;
    output_+=process_->readAllStandardOutput();
    if(output_.size()>65536){fail(tr("Invalid system speech response."));return;}
    while(output_.contains('\n')){
        const int end=int(output_.indexOf('\n'));const auto line=output_.left(end);output_.remove(0,end+1);
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(line,&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()){fail(tr("Invalid system speech response."));return;}
        const auto event=doc.object();const auto type=event.value("type").toString();
        if(type=="error"){fail(event.value("message").toString(tr("System speech failed.")));return;}
        if(type=="capabilities"){receivedResult_=true;emit capabilities(event);}
        else if(type=="ready"){receivedResult_=true;emit ready();}
        else if(type=="status")emit message(event.value("message").toString());
        else if(type=="complete"){receivedResult_=true;}
        else if(type=="transcript"){
            if(operation_!="listen")continue;
            if(event.contains("revision")&&event.value("revision").toInt()!=revision_)continue;
            const auto text=event.value("text").toString();
            if(text.size()>16000){fail(tr("System speech returned too much text."));return;}
            emit transcript(text,event.value("final").toBool());
        }
        if(!process_)return;
    }
}
