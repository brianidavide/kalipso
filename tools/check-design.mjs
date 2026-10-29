// SPDX-License-Identifier: AGPL-3.0-or-later
// Run with PLAYWRIGHT=/path/to/playwright/index.mjs and the static site on :8770.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const pw=await import(pathToFileURL(process.env.PLAYWRIGHT).href);
const base=process.env.DESIGN_URL||'http://localhost:8770';
const out='artifacts/ui-test/browser';await mkdir(out,{recursive:true});
const sizes=[[390,844],[360,740],[844,390],[412,915],[750,832],[466,678],[951,669],[326,326],[440,706],[440,744],[421,616],[861,609],[820,1180],[1180,820],[1440,900]];
const report=[];
for(const engine of ['chromium','firefox','webkit']){
 const browser=await pw[engine].launch({headless:true});const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));page.on('response',r=>{if(r.status()>=400)errors.push(`${r.status()} ${r.url()}`)});
 const isExternal=url=>new URL(url).origin!==new URL(base).origin&&!url.startsWith('data:');
 assert.equal(isExternal('blob:'+base+'/local-video'),false);assert.equal(isExternal('https://example.invalid/remote'),true);
 const external=[];page.on('request',r=>{if(isExternal(r.url()))external.push(r.url())});
 const overflow=()=>page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1);
 let cases=0;
 for(const [width,height] of sizes)for(const route of ['/']){
  await page.setViewportSize({width,height});await page.goto(base+route);await page.evaluate(()=>document.fonts.ready);await page.waitForTimeout(100);
  assert.equal(await overflow(),false,`${engine} ${route} ${width} overflow`);
  assert.equal(await page.locator('h1').count(),1);
  cases++;
  if(route==='/'&&engine==='chromium')await page.screenshot({path:`${out}/site-${width}x${height}.png`});
 }
 await page.setViewportSize({width:820,height:1000});await page.goto(base);
 const video=page.locator('video');assert.equal(await video.evaluate(v=>v.paused&&v.controls&&!v.autoplay),true);
 await video.evaluate(v=>Promise.race([v.play(),new Promise((_,reject)=>setTimeout(()=>reject(new Error('Video did not start')),10000))]));
 await page.waitForFunction(()=>document.querySelector('video').currentTime>.1);
 await video.evaluate(v=>v.pause());assert.equal(await video.evaluate(v=>v.error),null);

 const position=()=>page.locator('#reading-text').getAttribute('data-position');
 await page.locator('#next-word').click();assert.equal(await position(),'3');
 await page.locator('#edit-toggle').click();assert.equal(await page.locator('#edit-toggle').getAttribute('aria-expanded'),'true');
 const original=await page.locator('#script').inputValue();await page.locator('#script').fill('Hello '+original);assert.equal(await position(),'4');
 await page.locator('#script').press('Escape');assert.equal(await page.locator('#edit-panel').isVisible(),false);assert.equal(await page.locator('#edit-toggle').evaluate(e=>e===document.activeElement),true);
 await page.locator('#reset-reading').click();assert.equal(await position(),'-1');
 await page.locator('#edit-toggle').click();await page.locator('#script').fill('');assert.equal(await page.locator('#next-word').isDisabled(),true);assert.match(await page.locator('#reading-text').innerText(),/Add your script/);
 await page.locator('#script').fill('<img src=x onerror=alert(1)> safe text');assert.equal(await page.locator('#reading-text img').count(),0);
 await page.locator('#script').fill('تحدث بالوتيرة التي تناسبك. 自分のペースで話しましょう。');await page.locator('#next-word').click();assert.notEqual(await position(),'-1');
 await page.goto(base);for(const summary of await page.locator('details summary').all()){await summary.click();assert.equal(await summary.evaluate(e=>e.parentElement.open),true)}
 await page.emulateMedia({reducedMotion:'reduce'});await page.locator('#edit-toggle').click();assert.equal(await page.locator('#edit-panel').evaluate(e=>getComputedStyle(e).animationDuration),'0s');
 await page.locator('#script').fill('A long script. '.repeat(300));await page.locator('#next-word').click();assert.equal(await overflow(),false);
 // Mirror: flips only the text, survives a reload through the address, and can be preset with a link.
 await page.goto(base);await page.locator('#reader-mirror').selectOption('horizontal');assert.match(await page.locator('#reading-text').evaluate(e=>getComputedStyle(e).transform),/^matrix\(-1, 0, 0, 1/);assert.equal(await page.locator('#next-word').evaluate(e=>getComputedStyle(e).transform),'none');
 await page.reload();assert.equal(await page.locator('#reader-mirror').inputValue(),'horizontal');await page.goto(base+'/?mirror=vertical');assert.match(await page.locator('#reading-text').evaluate(e=>getComputedStyle(e).transform),/^matrix\(1, 0, 0, -1/);
 await page.locator('#reader-mirror').selectOption('none');assert.equal(new URL(page.url()).searchParams.has('mirror'),false);
 await page.goto(base);await page.keyboard.press('Tab');assert.equal(await page.locator('.skip').evaluate(e=>e===document.activeElement),true);
 // Negative control: deliberately broken layout must be detected.
 await page.addStyleTag({content:'body{width:220vw!important}'});assert.equal(await overflow(),true);await page.reload();assert.equal(await overflow(),false);
 // Negative control: remove the advance handler; a click must fail our behavior assertion.
 await page.locator('#next-word').evaluate(e=>e.replaceWith(e.cloneNode(true)));await page.locator('#next-word').click();assert.equal(await position(),'-1');await page.reload();await page.locator('#next-word').click();assert.equal(await position(),'3');
 await page.goto(base);await page.waitForTimeout(250);assert.equal(await page.evaluate(()=>document.getAnimations().filter(a=>a.playState==='running').length),0);
 await page.addStyleTag({content:'@keyframes bad{to{opacity:.5}}h1{animation:bad 1s infinite!important}'});await page.waitForTimeout(80);assert.ok(await page.evaluate(()=>document.getAnimations().some(a=>a.playState==='running')));await page.reload();
 await page.setViewportSize({width:1440,height:1000});
 if(engine==='chromium'){
  for(const id of ['headline','try','inside','install','questions']){await page.locator('#'+id).scrollIntoViewIfNeeded();await page.screenshot({path:`${out}/desktop-${id}.png`})}
  await page.setViewportSize({width:390,height:844});for(const id of ['try','inside','install','questions']){await page.locator('#'+id).scrollIntoViewIfNeeded();await page.screenshot({path:`${out}/mobile-${id}.png`})}
 }
 assert.deepEqual(errors,[],engine+' page errors');assert.deepEqual(external,[],engine+' external requests');
 report.push({engine,viewportCases:cases,interactionChecks:20,negativeControls:3,errors,externalRequests:external.length});await browser.close();console.log(JSON.stringify(report.at(-1)));
}
await writeFile(`${out}/results.json`,JSON.stringify(report,null,2)+'\n');
