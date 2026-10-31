// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import org.kde.plasma.workspace.dbus as DBus
Item {
    id:engine
    visible:false
    property string status:i18n("Choose the language you speak.")
    property bool active:false
    property bool failed:false
    property bool busy:false
    property string backend:"moonshine"
    property string language:""
    // The catalog languages ({code, name, backend, ready, bytes}) and the download in progress
    // ({active, stage, language, received, total, error}); see GetLanguages in the companion.
    property var languages:[]
    property var install:({})
    property bool downloads:true
    property bool languagesUnavailable:false
    property string languagesText:""
    property string languageError:""  // Why the companion refused a language, in its words.
    readonly property bool installing:install.active===true
    property int lastSequence:-1
    property int generation:0
    readonly property string service:"io.github.brianidavide.Kalipso.Engine"
    signal transcript(var event)
    function call(method,callback,epoch,args) {
        const reply=DBus.SessionBus.asyncCall({service:service,path:"/io/github/brianidavide/Kalipso/Engine",iface:service,member:method,arguments:args||[]})
        const done=function(){
            try {
            if(epoch!==generation)return
            busy=false
            if(reply.isError){failed=true;active=false;poll.stop();console.warn("Local speech: "+reply.error.message);status=i18n("Local speech is unavailable. Open Local speech setup to install the companion and a language model.");return}
            callback(reply.value)
            } finally {reply.destroy()}
        }
        if(reply.isFinished)done();else reply.finished.connect(done)
    }
    function receive(value) {
        const state=JSON.parse(value);active=state.active
        failed=state.status==="error";status=state.message||state.status
        if(state.backend)backend=state.backend
        const events=state.events||[state]
        for(const event of events){if(Number.isInteger(event.sequence)&&event.sequence>lastSequence){lastSequence=event.sequence;transcript(event)}}
        if(!active)poll.stop()
    }
    function settings() {
        if(active||busy)return
        busy=true
        call('GetDependencies',function(value){
            const deps=JSON.parse(value);backend=deps.backend;language=deps.language||""
            const engineName=backend==="moonshine"?"Moonshine":"Vosk"
            status=deps.ready?i18n("Ready to listen in %1, with %2.",nameOf(language)||i18n("your language"),engineName)
                :!deps.model?i18n("Choose the language you speak.")
                :deps.missing.join(' ')
        },generation)
    }
    function nameOf(code) {
        for(const item of languages)if(item.code===code)return item.name
        return code
    }
    // Language calls never touch the listening state: they have their own replies and errors.
    // Without `failed`, an error means the companion is missing or too old to download languages.
    function request(method,args,callback,failed) {
        const reply=DBus.SessionBus.asyncCall({service:service,path:"/io/github/brianidavide/Kalipso/Engine",iface:service,member:method,arguments:args})
        const done=function(){
            try {
                if(reply.isError){
                    console.warn("Speech languages: "+reply.error.message)
                    if(failed){failed(reply.error.message);return}
                    languagesUnavailable=true;languagePoll.stop();return
                }
                languagesUnavailable=false
                callback(reply.value)
            } finally {reply.destroy()}
        }
        if(reply.isFinished)done();else reply.finished.connect(done)
    }
    function refreshLanguages() {
        request('GetLanguages',[],function(value){
            const info=JSON.parse(value)
            const finished=installing&&!info.install.active
            // Replaced only when something changed, so the buttons keep their focus while polling.
            const text=JSON.stringify(info.languages)
            if(text!==languagesText){languagesText=text;languages=info.languages}
            install=info.install;downloads=info.downloads
            language=info.language||"";backend=info.backend||backend
            if(installing)languagePoll.start();else languagePoll.stop()
            if(finished&&!active&&!busy)settings()
        })
    }
    // Listens in this language from now on, downloading its model first if needed.
    function chooseLanguage(code) {
        if(active||busy||installing)return
        languageError=""
        request('InstallLanguage',[code],function(){refreshLanguages();if(!active&&!busy)settings()},
                function(message){languageError=message})
    }
    function cancelInstall() {request('CancelInstall',[],refreshLanguages)}
    function start() {
        if(active||busy)return
        const epoch=++generation;failed=false;lastSequence=-1;busy=true;status=i18n("Checking local speech components")
        call('GetDependencies',function(value){
            const deps=JSON.parse(value)
            if(!deps.ready){failed=true;status=deps.missing.join(' ');return}
            busy=true
            call('Start',function(started){active=started;busy=true;call('GetState',receive,epoch);if(started)poll.start()},epoch)
        },epoch)
    }
    function stop() {
        ++generation;poll.stop();active=false;busy=false;failed=false
        call('Stop',function(){status=i18n("Microphone stopped")},generation)
    }
    Timer{id:poll;interval:250;repeat:true;onTriggered:{if(!engine.busy){engine.busy=true;engine.call('GetState',engine.receive,engine.generation)}}}
    Timer{id:languagePoll;interval:500;repeat:true;onTriggered:engine.refreshLanguages()}
    Component.onDestruction:{if(active||busy)stop()}
}
