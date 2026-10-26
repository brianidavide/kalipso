// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import org.kde.plasma.workspace.dbus as DBus
Item {
    id:engine
    visible:false
    property string status:"Choose a speech engine to check its setup."
    property bool active:false
    property bool failed:false
    property bool busy:false
    property string backend:"moonshine"
    property string language:""
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
            if(reply.isError){failed=true;active=false;poll.stop();console.warn("Local speech: "+reply.error.message);status="Local speech is unavailable. Open Local speech setup to install the companion and a language model.";return}
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
            status=deps.ready?backend+" · "+(language||"configured model")+" · ready":deps.missing.join(' ')
        },generation)
    }
    function selectBackend(id) {
        if(active||busy)return
        busy=true
        call('SelectBackend',function(changed){if(changed){backend=id;settings()}},generation,[id])
    }
    function start() {
        if(active||busy)return
        const epoch=++generation;failed=false;lastSequence=-1;busy=true;status="Checking local speech components"
        call('GetDependencies',function(value){
            const deps=JSON.parse(value)
            if(!deps.ready){failed=true;status=deps.missing.join(' ');return}
            busy=true
            call('Start',function(started){active=started;busy=true;call('GetState',receive,epoch);if(started)poll.start()},epoch)
        },epoch)
    }
    function stop() {
        ++generation;poll.stop();active=false;busy=false;failed=false
        call('Stop',function(){status="Microphone stopped"},generation)
    }
    Timer{id:poll;interval:250;repeat:true;onTriggered:{if(!engine.busy){engine.busy=true;engine.call('GetState',engine.receive,engine.generation)}}}
    Component.onDestruction:{if(active||busy)stop()}
}
