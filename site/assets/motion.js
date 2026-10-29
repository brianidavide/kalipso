// SPDX-License-Identifier: AGPL-3.0-or-later
// Motion that shows what Kalipso does. With reduced motion nothing moves on its own. The scenes play once,
// in under five seconds; the reader in the first screen keeps going, so it has a pause button and stops
// whenever it is out of sight (WCAG 2.2.2).
const reduce=matchMedia('(prefers-reduced-motion: reduce)');
document.documentElement.classList.add('motion');

// The header line shows how far down the page you are, like the reader's progress line.
const pageProgress=document.querySelector('.page-progress');
if(pageProgress){
 let queued=false;
 const update=()=>{queued=false;const room=document.documentElement.scrollHeight-innerHeight;pageProgress.style.setProperty('--progress',room>0?Math.min(1,scrollY/room):0)};
 addEventListener('scroll',()=>{if(!queued){queued=true;requestAnimationFrame(update)}},{passive:true});
 addEventListener('resize',update);update();
}

// Entrances: each block rises in once as it comes into view; siblings follow one another.
for(const group of document.querySelectorAll('[data-reveal-group]'))[...group.querySelectorAll(':scope>[data-reveal]')].forEach((item,index)=>item.style.setProperty('--i',index));
const revealer=new IntersectionObserver(entries=>{for(const entry of entries)if(entry.isIntersecting){entry.target.classList.add('revealed');revealer.unobserve(entry.target)}},{rootMargin:'0px 0px -8% 0px'});
for(const item of document.querySelectorAll('[data-reveal]'))revealer.observe(item);

// Scenes for the three ways: once when they come into view, again when the pointer comes back.
const play=scene=>{scene.classList.remove('play');void scene.offsetWidth;scene.classList.add('play')};
const sceneWatcher=new IntersectionObserver(entries=>{for(const entry of entries)if(entry.isIntersecting){play(entry.target);sceneWatcher.unobserve(entry.target)}},{threshold:.6});
for(const scene of document.querySelectorAll('[data-vignette]')){
 sceneWatcher.observe(scene);
 scene.closest('li')?.addEventListener('pointerenter',()=>{if(scene.classList.contains('play')&&!scene.getAnimations({subtree:true}).some(a=>a.playState==='running'))play(scene)});
}

// The recording plays once, muted, when it comes into view; its controls stay available.
const video=document.querySelector('#watch video');
if(video&&!reduce.matches){
 const videoWatcher=new IntersectionObserver(([entry])=>{if(entry.isIntersecting){videoWatcher.disconnect();video.muted=true;video.play().catch(()=>{})}},{threshold:.6});
 videoWatcher.observe(video);
}

// The reader in the first screen follows a voice: the marked word moves at a calm speaking pace, said
// words fade, and the line being read stays on the second row, next to the marker.
const live=document.querySelector('[data-live-reader]'),toggle=document.querySelector('[data-live-toggle]');
if(live&&toggle){
 const text=live.querySelector('.live-text'),bar=live.querySelector('.live-progress span'),left=live.querySelector('[data-live-left]');
 const pace=60000/165,highlight=document.createElement('span'),words=[];
 highlight.className='live-highlight';
 const tokens=text.textContent.trim().split(/\s+/);text.replaceChildren(highlight);
 for(const token of tokens){const span=document.createElement('span');span.textContent=token;if(/^\[.+\]$/.test(token))span.className='cue';else words.push(span);text.append(span,' ')}
 let index=-1,line=0,phase='reading',timer=0,wanted=!reduce.matches,visible=false;
 function show(next){
  index=next;
  words.forEach((word,k)=>word.classList.toggle('said',k<index));
  const word=words[index],pitch=parseFloat(getComputedStyle(text).lineHeight);
  const row=word?Math.round((word.offsetTop-words[0].offsetTop)/pitch):0;
  // A new line: the text glides up and the mark jumps to the line start instead of sweeping across.
  highlight.classList.toggle('jump',row!==line);line=row;
  highlight.hidden=!word;
  if(word){highlight.style.inlineSize=`${word.offsetWidth+8}px`;highlight.style.blockSize=`${word.offsetHeight}px`;highlight.style.transform=`translate(${word.offsetLeft-4}px,${word.offsetTop}px)`}
  text.style.transform=`translateY(${(1-row)*pitch}px)`;
  bar.style.transform=`scaleX(${Math.max(0,index+1)/words.length})`;
  const seconds=Math.round((words.length-Math.max(0,index)-1)*pace/1000);
  left.textContent=seconds>0?`${seconds} s left`:'End of script';
  live.dataset.word=index;
 }
 const running=()=>wanted&&visible&&!document.hidden;
 function tick(){
  timer=0;let wait;
  if(phase==='rest'){live.classList.add('rewinding');phase='rewind';wait=420}
  else if(phase==='rewind'){show(-1);live.classList.remove('rewinding');phase='reading';wait=700}
  else if(index>=words.length-1){live.dataset.speaking='false';phase='rest';wait=1800}
  else{show(index+1);const said=words[index].textContent,stop=/[.!?]$/.test(said)?560:/[,;:]$/.test(said)?240:0;live.dataset.speaking=String(!stop);wait=pace+stop}
  if(running())timer=setTimeout(tick,wait);
 }
 function update(){
  const go=running();live.dataset.running=String(go);
  if(go&&!timer)timer=setTimeout(tick,pace);
  if(!go){
   clearTimeout(timer);timer=0;live.dataset.speaking='false';
   if(phase!=='reading'){show(-1);live.classList.remove('rewinding');phase='reading'}
  }
  toggle.dataset.state=wanted?'playing':'paused';
  toggle.querySelector('span').textContent=wanted?'Pause animation':'Play animation';
 }
 toggle.addEventListener('click',()=>{wanted=!wanted;update()});
 new IntersectionObserver(([entry])=>{visible=entry.isIntersecting;update()}).observe(live);
 document.addEventListener('visibilitychange',update);
 reduce.addEventListener('change',()=>{if(reduce.matches){wanted=false;update()}});
 new ResizeObserver(()=>show(index)).observe(text);
 // Start mid-sentence, so the first frame already shows said words, the mark and the words to come.
 show(9);update();
 document.fonts.ready.then(()=>show(index));
}
