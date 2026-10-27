// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import org.kde.plasma.workspace.dbus as DBus
Item {
    id:colors
    visible:false
    property bool active:false
    property bool busy:false
    property int generation:0
    property string status:"Choose the window behind the reader. Desktop permission is required."
    property string sample:""
    signal sampled(string color)
    readonly property string clientToken:Date.now().toString(36)+"_"+Math.random().toString(36).slice(2)
    readonly property string service:"io.github.brianidavide.Kalipso.Engine"
    function call(method,callback,epoch) {
        const reply=DBus.SessionBus.asyncCall({service:service,path:"/io/github/brianidavide/Kalipso/Engine",iface:service,member:method,arguments:[clientToken]})
        const done=function(){
            try {
                if(epoch!==generation)return
                busy=false
                if(reply.isError){active=false;poll.stop();status="Window colors unavailable. Install or update the Kalipso companion and the desktop sharing portal.";return}
                callback(reply.value)
            } finally {reply.destroy()}
        }
        if(reply.isFinished)done();else reply.finished.connect(done)
    }
    function start(){
        if(active||busy)return
        const epoch=++generation;busy=true;sample="";active=true
        status="Choose the window behind your reader in the desktop sharing dialog"
        call("StartColors",function(started){active=started;if(started)poll.start()},epoch)
    }
    function stop(){
        const wasActive=active||busy;++generation;active=false;busy=false;poll.stop();sample=""
        status="Window sharing stopped · Choose a background window to resume"
        if(wasActive)call("StopColors",function(){},generation)
    }
    Timer {
        id:poll;interval:2000;repeat:true;triggeredOnStart:true
        onTriggered:{
            if(colors.busy)return
            colors.busy=true
            colors.call("GetColorState",function(value){
                const state=JSON.parse(value);colors.active=state.active;colors.status=state.message
                if(state.active&&/^#[0-9a-f]{6}$/i.test(state.color)){colors.sample=state.color;colors.sampled(state.color)}
                if(!state.active){poll.stop();colors.call("StopColors",function(){},colors.generation)}
            },colors.generation)
        }
    }
    Component.onDestruction:stop()
}
