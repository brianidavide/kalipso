// SPDX-License-Identifier: AGPL-3.0-or-later
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import Pango from 'gi://Pango';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import {Tracker} from './tracker.mjs';
import {LocalEngine} from './local-engine.js';
import {tokens as T} from './tokens.js';
const C=T.color,N=T.native;
const example='A little preparation makes room for a better conversation.\n\nTake your time. Leave space for a pause. Your next words will be here when you are ready.';
const setupURL='https://github.com/brianidavide/kalipso/blob/main/docs/local-engine.md';
export default class Kalipso extends Extension {
    enable() {
        this.diagnostics=GLib.getenv('KALIPSO_TEST_API')==='1';
        this.tracker=new Tracker(example);this._status='Choose a speech engine to check its setup.';
        this._lastSequence=-1;this._backend='moonshine';this._active=false;this._mode='manual';this._pane='';this._speechState='idle';
        this.local=new LocalEngine(state=>{
            this._status=state.message||state.status;this._speechState=state.status;
            this._active=Boolean(state.active);if(state.backend)this._backend=state.backend;
            for(const event of state.events??[state])if(Number.isInteger(event.sequence)&&event.sequence>this._lastSequence){
                this.tracker.follow(event.text,event.final);this._lastSequence=event.sequence;
            }
            if(this.actor)this.render();
        });
        this.indicator=new PanelMenu.Button(0,'Kalipso',true);
        this.indicator.add_child(new St.Icon({icon_name:'media-playback-start-symbolic',style_class:'system-status-icon'}));
        this.indicator.connect('button-press-event',()=>{this.toggle();return Clutter.EVENT_STOP;});
        this.indicator.connect('key-press-event',(_actor,event)=>{
            if([Clutter.KEY_Return,Clutter.KEY_space].includes(event.get_key_symbol())){this.toggle();return Clutter.EVENT_STOP;}
            return Clutter.EVENT_PROPAGATE;
        });
        Main.panel.addToStatusArea(this.uuid,this.indicator);
        this.actor=new St.BoxLayout({vertical:true,reactive:true,can_focus:true,style:`background-color:${C.overlay};color:${C.overlayText};padding:${N.padding}px;spacing:${N.gap}px;border:1px solid ${C.overlayLine};border-radius:${N.radius}px;`});
        const row=()=>new St.BoxLayout({style:`spacing:${T.space[2]};`,x_expand:true});
        const header=row();this.header=header;
        header.add_child(new St.Label({text:'Kalipso',x_expand:true,y_align:Clutter.ActorAlign.CENTER,style:`font-weight:bold;font-size:${N.bodySize}pt;`}));
        this.editButton=this.button('Edit script',()=>this.showPane(this._pane==='edit'?'':'edit'));
        this.settingsButton=this.button('Settings',()=>this.showPane(this._pane==='settings'?'':'settings'));
        header.add_child(this.editButton);header.add_child(this.settingsButton);header.add_child(this.button('Hide',()=>this.hide()));
        this.reader=new Clutter.Text({font_name:`Sans ${N.readerSize}`,line_wrap:true,ellipsize:0,y_align:Clutter.ActorAlign.START,x_align:Clutter.ActorAlign.START});
        this.readerWrap=new St.Widget({layout_manager:new Clutter.BinLayout(),x_expand:true});this.readerWrap.add_child(this.reader);this.readerWrap.clip_to_allocation=true;
        this.transport=row();
        this.primary=this.button('Next words',()=>{if(this._mode==='manual')this.seek(this.tracker.position+4);else if(this._active||this.local.pending||this.local.owned)this.stop();else this.start();},true);
        this.previous=this.button('Previous',()=>this.seek(this.tracker.position-4));
        this.reset=this.button('Reset',()=>this.seek(-1));
        for(const child of [this.primary,this.previous,this.reset])this.transport.add_child(child);
        this.counter=new St.Label({x_expand:true,x_align:Clutter.ActorAlign.END,y_align:Clutter.ActorAlign.CENTER,style:`font-size:${N.labelSize}pt;color:${C.overlayMuted};`});this.transport.add_child(this.counter);
        this.stateLabel=new St.Label({style:`font-size:${N.labelSize}pt;color:${C.reading};`});
        this.editPane=new St.BoxLayout({vertical:true,style:`spacing:${N.gap}px;padding-top:${N.gap}px;border-top:1px solid ${C.overlayLine};`});
        this.editPane.add_child(new St.Label({text:'SCRIPT',style:`font-size:${N.labelSize}pt;color:${C.overlayMuted};`}));
        this.editor=new St.Entry({text:example,can_focus:true,x_expand:true,style:`background-color:${C.surface};color:${C.ink};border-radius:${N.buttonRadius}px;padding:${N.gap}px;font-size:${N.bodySize}pt;min-height:${N.padding*4}px;`});
        this.editor.clutter_text.set_single_line_mode(false);this.editor.clutter_text.set_line_wrap(true);
        this.editor.clutter_text.connect('text-changed',()=>{this.tracker.setText(this.editor.get_text());this.render();});
        const editScroll=new St.ScrollView({style:`max-height:${N.editorHeight+N.padding*2}px;`,x_expand:true});
        const editContent=new St.BoxLayout({vertical:true,x_expand:true});editContent.add_child(this.editor);editScroll.set_child(editContent);this.editPane.add_child(editScroll);
        this.editPane.add_child(new St.Label({text:'Changes appear immediately · This session only',style:`font-size:${N.labelSize}pt;color:${C.overlayMuted};`}));
        this.settingsPane=new St.BoxLayout({vertical:true,style:`spacing:${N.gap}px;padding-top:${N.gap}px;border-top:1px solid ${C.overlayLine};`});
        const modeRow=row();this.manualButton=this.button('Manual',()=>this.mode('manual'));this.voiceButton=this.button('Follow my voice',()=>this.mode('voice'));
        modeRow.add_child(this.manualButton);modeRow.add_child(this.voiceButton);this.settingsPane.add_child(modeRow);
        const engineRow=row();this.engineButtons=[];
        for(const id of ['moonshine','vosk']){const button=this.button(id==='moonshine'?'Moonshine':'Vosk',()=>this.chooseEngine(id));this.engineButtons.push([id,button]);engineRow.add_child(button);}
        this.settingsPane.add_child(engineRow);
        this.details=new St.Label({style:`font-size:${N.labelSize}pt;color:${C.overlayMuted};`});this.details.clutter_text.set_line_wrap(true);this.details.clutter_text.set_ellipsize(Pango.EllipsizeMode.NONE);this.settingsPane.add_child(this.details);
        const sizeRow=row();sizeRow.add_child(new St.Label({text:'Reading size',x_expand:true,y_align:Clutter.ActorAlign.CENTER}));
        this._readerSize=N.readerSize;sizeRow.add_child(this.button('Smaller',()=>this.resize(-2)));sizeRow.add_child(this.button('Larger',()=>this.resize(2)));this.settingsPane.add_child(sizeRow);
        this.settingsPane.add_child(this.button('Local speech setup ↗',()=>{try{Gio.AppInfo.launch_default_for_uri(setupURL,null);}catch(error){this._status=String(error.message);this.render();}}));
        const privacy=new St.Label({text:'The reader may appear in screen sharing. Test your sharing setup before a call.',style:`font-size:${N.labelSize}pt;color:${C.overlayMuted};`});privacy.clutter_text.set_line_wrap(true);privacy.clutter_text.set_ellipsize(Pango.EllipsizeMode.NONE);this.settingsPane.add_child(privacy);this.privacy=privacy;
        this.secondaryContent=new St.BoxLayout({vertical:true,x_expand:true});this.secondaryContent.add_child(this.editPane);this.secondaryContent.add_child(this.settingsPane);
        this.secondaryScroll=new St.ScrollView({x_expand:true});this.secondaryScroll.set_child(this.secondaryContent);
        for(const child of [header,this.readerWrap,this.transport,this.stateLabel,this.secondaryScroll])this.actor.add_child(child);
        this.actor.connect('key-press-event',(_actor,event)=>{if(event.get_key_symbol()===Clutter.KEY_Escape){if(this._pane)this.showPane('');else this.hide();return Clutter.EVENT_STOP;}return Clutter.EVENT_PROPAGATE;});
        Main.layoutManager.addChrome(this.actor,{trackFullscreen:true});
        this.desktopSettings=new Gio.Settings({schema_id:'org.gnome.desktop.interface'});
        this.place=()=>{const m=Main.layoutManager.primaryMonitor;if(!m)return;const scale=this.desktopSettings.get_double('text-scaling-factor');header.get_first_child().visible=scale<1.6;const width=Math.min(N.width*Math.min(scale,1.2),m.width-32);this.actor.width=width;this.primary.width=Math.ceil((N.primaryWidth-28)*scale+28);this.reader.width=width-N.padding*2;this.details.width=this.reader.width-20;this.privacy.width=this.reader.width-20;this.secondaryContent.width=this.reader.width-20;this.editor.width=this.reader.width-20;this.actor.set_position(m.x+Math.round((m.width-width)/2),m.y+Main.panel.height+16);
            const controls=N.padding*2+N.gap*5+header.get_preferred_height(width)[1]+this.transport.get_preferred_height(width)[1]+this.stateLabel.get_preferred_height(width)[1];
            this.readerWrap.height=Math.min(Math.round(this._readerSize*scale*4/3*4.5),Math.max(60,m.height-Main.panel.height-32-controls-(this._pane?120:0)));
            const line=this.reader.get_layout().get_line_readonly(0);
            const lineHeight=line?line.get_pixel_extents()[1].height:1;
            this.readerWrap.height=Math.max(lineHeight,Math.floor(this.readerWrap.height/lineHeight)*lineHeight);
            const base=controls+this.readerWrap.height;
            this.secondaryScroll.set_style(`max-height:${Math.max(80,m.height-Main.panel.height-32-base)}px;`);};
        this.monitorSignal=Main.layoutManager.connect('monitors-changed',this.place);this.fontSignal=this.desktopSettings.connect('changed::text-scaling-factor',()=>this.render());this.place();
        if(this.diagnostics){
        const xml='<node><interface name="io.github.brianidavide.Kalipso.Reader"><method name="GetState"><arg type="s" direction="out"/></method><method name="SetScript"><arg type="s" direction="in"/></method><method name="Start"/><method name="Stop"/></interface></node>';
        this.testApi=Gio.DBusExportedObject.wrapJSObject(xml,{GetState:()=>JSON.stringify({position:this.tracker.position,script:this.tracker.text,status:this._status,mode:this._mode,pane:this._pane,visible:this.actor.visible,controls:[this.primary,this.previous,this.reset,this.editButton,this.settingsButton,...this.engineButtons.map(item=>item[1]),this.manualButton,this.voiceButton].map(button=>{const [x,y]=button.get_transformed_position();return {label:button.label,x,y,width:button.width,height:button.height};}),markup:this.tracker.markup(),attributes:this.reader.get_attributes()?.to_string()}),SetScript:text=>this.editor.set_text(text),Start:()=>{this._mode='voice';this.start();},Stop:()=>this.stop()});
        this.testApi.export(Gio.DBus.session,'/io/github/brianidavide/Kalipso/Reader');
        }
        this.render();
    }
    button(label,action,primary=false){
        const button=new St.Button({label,can_focus:true,reactive:true,style_class:primary?'tp-button tp-primary':'tp-button'});
        button.connect('clicked',action);return button;
    }
    resize(delta){this._readerSize=Math.max(18,Math.min(40,this._readerSize+delta));this.reader.set_font_name(`Sans ${this._readerSize}`);this.place();}
    seek(position){this.tracker.seek(position);this.render();}
    mode(mode){if(this._active||this.local.pending)this.stop();this._mode=mode;if(mode==='voice')this.loadSettings();this.render();}
    showPane(pane){this._pane=pane;this.render();if(pane==='settings')this.loadSettings();if(pane==='edit')this.editor.grab_key_focus();}
    toggle(){if(this.actor.visible)this.hide();else{this.actor.show();this.place();}}
    hide(){this.stop();this._pane='';this.actor.hide();}
    async loadSettings(){try{const settings=await this.local.settings();if(this.actor)this.showSettings(settings);}catch(error){if(this.actor){this._status='Local speech is not installed. Open Local speech setup to install a companion and language model.';this.render();}}}
    showSettings(settings){this._backend=settings.selected;const d=settings.dependencies;this._status=d.ready?`${this._backend==='moonshine'?'Moonshine':'Vosk'} · ${d.language||'configured language'} · Ready`:d.missing.join(' ');this.render();}
    async chooseEngine(id){
        if(this._active||this.local.pending||this.local.owned)return;
        try{const request=this.local.select(id);this.render();const settings=await request;if(this.actor&&settings)this.showSettings(settings);}
        catch(error){if(this.actor){this._status=String(error.message);}}
        finally{if(this.actor)this.render();}
    }
    render(){
        if(!this.actor)return;
        this.reader.set_markup(this.tracker.words.length?this.tracker.readingMarkup(C.overlayMuted,C.overlayText,this.desktopSettings.get_double('text-scaling-factor')>=1.8?-1:this.desktopSettings.get_double('text-scaling-factor')>=1.5?0:4):`<span foreground="${C.overlayMuted}">Add your script to begin.</span>`);
        this.counter.set_text(`${this.tracker.position+1} / ${this.tracker.words.length}`);
        const listening=this._active||this.local.owned, pending=this.local.pending;
        this.primary.label=this._mode==='manual'?'Next words':listening?'Pause':pending?'Cancel':'Start listening';
        this.primary.reactive=Boolean(this.tracker.words.length)&&(this._mode!=='manual'||this.tracker.position<this.tracker.words.length-1);this.previous.reactive=this.tracker.position>=0;this.previous.can_focus=this.previous.reactive;this.reset.reactive=this.tracker.position>=0;this.reset.can_focus=this.reset.reactive;this.primary.can_focus=this.primary.reactive;
        this.stateLabel.set_text(this._mode==='manual'?(this.tracker.words.length&&this.tracker.position===this.tracker.words.length-1?'End of script · Microphone off':'Manual · Microphone off'):pending?'Starting local speech…':listening?'Listening · On this device':this._speechState==='error'?'Speech unavailable · Open Settings':'Paused · Microphone off');
        this.editPane.visible=this._pane==='edit';this.settingsPane.visible=this._pane==='settings';this.secondaryScroll.visible=Boolean(this._pane);
        this.editButton.label=this._pane==='edit'?'Done editing':'Edit script';
        this.settingsButton.label=this._pane==='settings'?'Done':'Settings';
        this.details.set_text(this._status);this.manualButton.label=(this._mode==='manual'?'✓ ':'')+'Manual';this.voiceButton.label=(this._mode==='voice'?'✓ ':'')+'Follow my voice';
        for(const [id,button] of this.engineButtons){button.label=(id===this._backend?'✓ ':'')+(id==='moonshine'?'Moonshine':'Vosk');button.reactive=!listening&&!pending;button.can_focus=button.reactive;button.opacity=button.reactive?255:130;}
        this.place?.();
        if(this.diagnostics)console.log('KALIPSO_TEST_STATE '+JSON.stringify({position:this.tracker.position,status:this._status}));
    }
    start(){if(this.local.pending||this.local.owned)return;this._lastSequence=-1;this.local.start().finally(()=>{if(this.actor)this.render();});this.render();}
    stop(){this.local.stop();this._active=false;this._speechState='idle';this.render();}
    disable(){this.local?.destroy();this.local=null;this.testApi?.unexport();this.testApi=null;if(this.monitorSignal)Main.layoutManager.disconnect(this.monitorSignal);this.monitorSignal=0;if(this.fontSignal)this.desktopSettings.disconnect(this.fontSignal);this.fontSignal=0;this.desktopSettings=null;this.actor?.destroy();this.actor=null;this.indicator?.destroy();this.indicator=null;}
}
