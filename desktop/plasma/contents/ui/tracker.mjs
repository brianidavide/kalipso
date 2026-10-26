// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared source-only tracker for GJS and QML; speech is provided by the local companion.
import {tokenize} from './unicode-tokens.mjs';
export class Tracker {
    constructor(text='') {this.text='';this.words=[];this.position=-1;this.last='';this.setText(text);}
    tokenize(text) {return tokenize(text);}
    setText(text) {
        if(text===this.text)return;
        let anchor=this.words[this.position]?.start??0, prefix=0,suffix=0;
        while(prefix<Math.min(this.text.length,text.length)&&this.text[prefix]===text[prefix])prefix++;
        while(suffix<this.text.length-prefix&&suffix<text.length-prefix&&this.text[this.text.length-1-suffix]===text[text.length-1-suffix])suffix++;
        if(anchor>=this.text.length-suffix)anchor+=text.length-this.text.length;
        else if(anchor>=prefix)anchor=prefix;
        const started=this.position>=0;this.text=text;this.words=this.tokenize(text);this.position=-1;this.last='';
        if(started&&this.words.length){this.position=0;this.words.forEach((w,i)=>{if(w.start<=anchor)this.position=i;});}
    }
    seek(index) {this.position=Math.max(-1,Math.min(index,this.words.length-1));this.last='';}
    follow(text,final=false) {
        if(!text || text===this.last){if(final)this.last='';return false;}
        this.last=final?'':text;
        let spoken=this.tokenize(text).slice(-8);const m=spoken.length;
        if(!m||!this.words.length)return false;
        if(m===1){if(this.words[this.position+1]?.word===spoken[0].word){this.position++;return true;}return false;}
        let best=Infinity,end=this.position;const first=Math.max(0,this.position-24),last=Math.min(this.words.length,this.position+81);
        for(let start=first;start<last;start++) {
            let previous=Array.from({length:m+1},(_,i)=>i);
            for(let n=1;n<=m+2 && start+n<=last;n++) {
                const current=[n];
                for(let i=1;i<=m;i++)current[i]=Math.min(previous[i]+1,current[i-1]+1,previous[i-1]+Number(spoken[i-1].word!==this.words[start+n-1].word));
                if(n>=Math.max(2,m-2)&&current[m]<=Math.floor(m/3)) {
                    const exact=spoken.filter(w=>this.words.slice(start,start+n).some(candidate=>candidate.word===w.word)).length;
                    const score=current[m]*100+Math.abs(start+n-1-this.position)*.1;
                    if(exact>=2 && score<best){best=score;end=start+n-1;}
                }
                previous=current;
            }
        }
        if(!Number.isFinite(best)||end===this.position)return false;this.position=end;return true;
    }
    escaped(text) {return text.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
    readingMarkup(readColor, nextColor, previousWords=4) {
        // A bounded excerpt keeps the reading position visible even in long scripts.
        const first=Math.max(0,this.position-previousWords), last=Math.min(this.words.length,this.position+13);
        const start=this.words[first]?.start??this.text.length;
        const finish=last<this.words.length?this.words[last].start:this.text.length;
        const readEnd=this.position<0?0:this.words[this.position].start+this.words[this.position].length;
        return `<span foreground="${readColor}">${first?'… ':''}</span><span foreground="${readColor}">${this.escaped(this.text.slice(start,readEnd).replace(/\s+/g,' '))}</span><span foreground="${nextColor}">${this.escaped(this.text.slice(Math.max(start,readEnd),finish).replace(/\s+/g,' '))}${last<this.words.length?' …':''}</span>`;
    }
    markup(readColor="#91999f", nextColor="#f6f8fa") {
        const end=this.position<0?0:this.words[this.position].start+this.words[this.position].length;
        return `<span foreground="${readColor}">${this.escaped(this.text.slice(0,end))}</span><span foreground="${nextColor}">${this.escaped(this.text.slice(end))}</span>`;
    }
}
