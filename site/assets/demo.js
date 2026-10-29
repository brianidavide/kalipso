// SPDX-License-Identifier: AGPL-3.0-or-later
// Manual presentation only. No microphone, network request or inference.
import {Tracker} from './tracker.mjs';
const script=document.querySelector('#script');
if(script){
 const tracker=new Tracker(script.value),reading=document.querySelector('#reading-text'),count=document.querySelector('#read-count'),panel=document.querySelector('#edit-panel'),toggle=document.querySelector('#edit-toggle');
 function render(){
  reading.replaceChildren();
  const first=Math.max(0,tracker.position-4),last=Math.min(tracker.words.length,tracker.position+13),start=tracker.words[first]?.start??0,finish=last<tracker.words.length?tracker.words[last].start:tracker.text.length,end=tracker.position<0?0:tracker.words[tracker.position].start+tracker.words[tracker.position].length;
  // Read words dimmed, the current word marked as in the app, the next words bright.
  const word=tracker.words[tracker.position],current=word?word.start:end;
  for(const [className,text] of [['read-word',(first?'… ':'')+tracker.text.slice(start,current)],['current-word',word?tracker.text.slice(word.start,end):''],['next-word',tracker.text.slice(Math.max(start,end),finish)+(last<tracker.words.length?' …':'')]]){if(!text)continue;const span=document.createElement('span');span.className=className;for(const part of text.replace(/\s+/g,' ').split(/(\[[^\]]*\])/)){if(!part)continue;if(/^\[.*\]$/.test(part)){const cue=document.createElement('span');cue.className='cue';cue.textContent=part;span.append(cue)}else span.append(part)}reading.append(span)}
  document.querySelector('#read-progress').style.inlineSize=`${tracker.words.length?100*(tracker.position+1)/tracker.words.length:0}%`;
  if(!tracker.words.length)reading.textContent='Add your script to begin.';
  count.textContent=`${tracker.position+1} / ${tracker.words.length}`;
  const next=document.querySelector('#next-word'),previous=document.querySelector('#previous-word'),focused=document.activeElement;
  next.disabled=!tracker.words.length||tracker.position===tracker.words.length-1;
  previous.disabled=tracker.position<0;
  // Disabling the focused button would drop keyboard focus to the page; keep it in the reader.
  if(focused===next&&next.disabled)previous.focus();
  if(focused===previous&&previous.disabled)next.focus();
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

// Mirror for teleprompter glass. Kept in the address (?mirror=horizontal), so a
// rig can be preset with a link and a reload keeps it; nothing is stored.
const mirror=document.querySelector('#reader-mirror'),text=document.querySelector('#reading-text');
if(mirror&&text){
 const url=new URL(location.href),initial=url.searchParams.get('mirror');
 if(['horizontal','vertical'].includes(initial))mirror.value=initial;
 const apply=()=>{text.dataset.mirror=mirror.value;const next=new URL(location.href);mirror.value==='none'?next.searchParams.delete('mirror'):next.searchParams.set('mirror',mirror.value);history.replaceState(null,'',next)};
 mirror.addEventListener('change',apply);apply();
}

const appearance=document.querySelector('#reader-appearance'),reader=document.querySelector('[data-reader]');
if(appearance&&reader){
 const update=()=>{reader.dataset.theme=appearance.value;};
 appearance.addEventListener('change',update);update();
}

// The download button names the visitor's system and points at its card; nothing leaves the page.
const platform=(navigator.userAgentData?.platform||navigator.platform||navigator.userAgent).toLowerCase();
const os=platform.includes('mac')?'mac':platform.includes('win')?'windows':platform.includes('linux')||platform.includes('x11')?'linux':'';
const label=document.querySelector('[data-os-label]');
if(os&&label){
 label.textContent={linux:'Get Kalipso for Linux',mac:'Get Kalipso for macOS',windows:'Get Kalipso for Windows'}[os];
 const card=document.querySelector(`.platforms article[data-os="${os}"]`);
 if(card){card.classList.add('yours');const tag=document.createElement('span');tag.className='yours-tag';tag.textContent='Your system';card.prepend(tag)}
}

// Copy buttons: the command is copied as shown; the status line says so for screen readers too.
for(const button of document.querySelectorAll('[data-copy]')){
 button.addEventListener('click',async()=>{
  const text=document.getElementById(button.dataset.copy)?.innerText??'';
  const status=document.querySelector('.copied');
  try{await navigator.clipboard.writeText(text);button.classList.add('done');button.querySelector('span').textContent='Copied';if(status)status.textContent='Command copied to the clipboard.'}
  catch{if(status)status.textContent='Copying is not allowed here: select the command and copy it.'}
  setTimeout(()=>{button.classList.remove('done');button.querySelector('span').textContent='Copy'},2000);
 });
}
