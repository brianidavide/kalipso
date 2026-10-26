// SPDX-License-Identifier: AGPL-3.0-or-later
// Manual presentation only. No microphone, network request or inference.
import {Tracker} from './tracker.mjs';
const script=document.querySelector('#script');
if(script){
 const tracker=new Tracker(script.value),reading=document.querySelector('#reading-text'),count=document.querySelector('#read-count'),panel=document.querySelector('#edit-panel'),toggle=document.querySelector('#edit-toggle');
 function render(){
  reading.replaceChildren();
  const first=Math.max(0,tracker.position-4),last=Math.min(tracker.words.length,tracker.position+13),start=tracker.words[first]?.start??0,finish=last<tracker.words.length?tracker.words[last].start:tracker.text.length,end=tracker.position<0?0:tracker.words[tracker.position].start+tracker.words[tracker.position].length;
  for(const [className,text] of [['read-word',(first?'… ':'')+tracker.text.slice(start,end)],['next-word',tracker.text.slice(Math.max(start,end),finish)+(last<tracker.words.length?' …':'')]]){const span=document.createElement('span');span.className=className;span.textContent=text.replace(/\s+/g,' ');reading.append(span)}
  if(!tracker.words.length)reading.textContent='Add your script to begin.';
  count.textContent=`${tracker.position+1} / ${tracker.words.length}`;
  document.querySelector('#next-word').disabled=!tracker.words.length||tracker.position===tracker.words.length-1;
  document.querySelector('#previous-word').disabled=tracker.position<0;
  reading.dataset.position=tracker.position;
  document.querySelector('#read-state').textContent=tracker.words.length&&tracker.position===tracker.words.length-1?'End of script · Microphone off':'Manual demo · Microphone off';
 }
 script.addEventListener('input',()=>{tracker.setText(script.value);render()});
 document.querySelector('#next-word').addEventListener('click',()=>{tracker.seek(tracker.position+4);render()});
 document.querySelector('#previous-word').addEventListener('click',()=>{tracker.seek(tracker.position-4);render()});
 document.querySelector('#reset-reading').addEventListener('click',()=>{tracker.seek(-1);render()});
 function closeEditor(){panel.hidden=true;toggle.textContent='Edit script';toggle.setAttribute('aria-expanded','false');toggle.focus()}
 toggle.addEventListener('click',()=>{if(!panel.hidden){closeEditor();return}panel.hidden=false;toggle.textContent='Done editing';toggle.setAttribute('aria-expanded','true');script.focus()});
 panel.addEventListener('keydown',event=>{if(event.key==='Escape'){closeEditor();event.preventDefault()}});
 render();
}
