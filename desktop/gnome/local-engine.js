// SPDX-License-Identifier: AGPL-3.0-or-later
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
const name='io.github.brianidavide.Kalipso.Engine';
const path='/io/github/brianidavide/Kalipso/Engine';
export class LocalEngine {
    constructor(update) {this.update=update;this.closed=false;this.pending=false;this.owned=false;this.timer=0;this.signal=0;}
    call(method,activate=true,parameters=null) {
        return new Promise((resolve,reject)=>Gio.DBus.session.call(name,path,name,method,parameters,null,
            activate?Gio.DBusCallFlags.NONE:Gio.DBusCallFlags.NO_AUTO_START,5000,null,(_bus,result)=>{
                try{resolve(Gio.DBus.session.call_finish(result).deep_unpack()[0]);}catch(error){reject(error);}
            }));
    }
    async settings() {
        const choices=JSON.parse(await this.call('GetBackends'));
        const dependencies=JSON.parse(await this.call('GetDependencies'));
        return {...choices,dependencies};
    }
    async select(backend) {
        if(this.closed||this.pending||this.owned)return null;
        this.pending=true;
        try {
            if(!await this.call('SelectBackend',true,new GLib.Variant('(s)',[backend])))throw Error('The speech engine was not changed.');
            return await this.settings();
        } finally {this.pending=false;}
    }
    async start() {
        if(this.closed||this.pending||this.owned)return;
        this.pending=true;this.cancelled=false;
        this.update({status:'loading',message:_('Checking local speech components'),text:'',sequence:-1});
        try {
            const dependencies=JSON.parse(await this.call('GetDependencies'));
            if(this.closed||this.cancelled)return;
            if(!dependencies.ready)throw Error(dependencies.missing.join(' '));
            this.owned=await this.call('Start');
            if(this.closed||this.cancelled){if(this.owned)await this.call('Stop',false);this.release();if(!this.closed)this.update(JSON.parse(await this.call('GetState',false)));return;}
            if(this.owned&&!this.signal)this.signal=Gio.DBus.session.signal_subscribe(name,name,'StateChanged',path,null,Gio.DBusSignalFlags.NONE,(_c,_s,_p,_i,_n,args)=>{
                if(!this.closed&&!this.cancelled&&this.owned)this.update(JSON.parse(args.deep_unpack()[0]));
            });
            this.update(JSON.parse(await this.call('GetState')));
            if(this.owned)this.timer=GLib.timeout_add(GLib.PRIORITY_DEFAULT,2000,()=>{
                this.call('GetState',false).then(value=>{
                    if(this.closed)return;
                    const state=JSON.parse(value);if(!state.active)this.release();this.update(state);
                }).catch(error=>{if(!this.closed){this.release();this.update({status:'error',message:String(error.message),text:'',sequence:-1});}});
                return GLib.SOURCE_CONTINUE;
            });
        }catch(error){
            if(!this.closed&&!this.cancelled)this.update({status:'error',message:_('Local speech unavailable. %s See docs/local-engine.md.').format(String(error.message)),text:'',sequence:-1});
        }finally{this.pending=false;}
    }
    release(){this.owned=false;if(this.timer)GLib.source_remove(this.timer);this.timer=0;}
    stop() {
        this.cancelled=true;
        if(this.owned||this.pending)this.call('Stop',false).then(()=>this.call('GetState',false)).then(value=>{if(!this.closed)this.update(JSON.parse(value));}).catch(()=>{});
        this.release();
    }
    destroy(){this.closed=true;this.stop();if(this.signal)Gio.DBus.session.signal_unsubscribe(this.signal);this.signal=0;}
}
