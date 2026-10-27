// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const pw=await import(pathToFileURL(process.env.PLAYWRIGHT).href);
const axe=await readFile(process.env.AXE_CORE,'utf8');
const base=process.env.DESIGN_URL||'http://localhost:8770';
const out='artifacts/accessibility/browser';await mkdir(out,{recursive:true});
const report=[];
for(const engine of ['chromium','firefox','webkit']){
 const browser=await pw[engine].launch();const page=await browser.newPage({viewport:{width:1280,height:960}});
 const errors=[];page.on('pageerror',e=>errors.push(String(e)));
 for(const theme of ['dark','light']){
  await page.goto(base);await page.locator('#reader-appearance').selectOption(theme);await page.locator('#edit-toggle').click();await page.locator('#next-word').click();
  await page.evaluate(axe);
  const result=await page.evaluate(async()=>axe.run(document,{runOnly:{type:'tag',values:['wcag2a','wcag2aa','wcag21a','wcag21aa','wcag22aa']}}));
  await writeFile(`${out}/${engine}-${theme}.json`,JSON.stringify(result,null,2));
  assert.deepEqual(result.violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})),[],`${engine} ${theme}`);
  await page.screenshot({path:`${out}/${engine}-${theme}.png`});
  report.push({engine,theme,passes:result.passes.length,incomplete:result.incomplete.map(v=>v.id)});
 }
 for(const width of [320,640,1280]){
  await page.setViewportSize({width,height:960});await page.goto(base);await page.locator('#edit-toggle').click();
  await page.addStyleTag({content:'html{font-size:200%!important}*{line-height:1.5!important;letter-spacing:.12em!important;word-spacing:.16em!important}p{margin-bottom:2em!important}'});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false,`${engine} ${width} enlarged text overflow`);
 }
 await page.setViewportSize({width:1280,height:960});await page.goto(base);await page.keyboard.press('Tab');assert.equal(await page.locator('.skip').evaluate(e=>e===document.activeElement),true);
 await page.locator('#edit-toggle').focus();await page.keyboard.press('Enter');assert.equal(await page.locator('#script').evaluate(e=>e===document.activeElement),true);await page.keyboard.press('Escape');assert.equal(await page.locator('#edit-toggle').evaluate(e=>e===document.activeElement),true);
 // Deliberately break text contrast; the real browser checker must report it.
 await page.addStyleTag({content:'.opening-copy{color:#ddd!important;background:white!important}'});await page.evaluate(axe);
 const broken=await page.evaluate(async()=>axe.run(document,{runOnly:['color-contrast']}));assert.ok(broken.violations.some(v=>v.id==='color-contrast'));
 assert.deepEqual(errors,[]);await browser.close();console.log(engine+' accessibility and negative control passed');
}
await writeFile(`${out}/summary.json`,JSON.stringify(report,null,2));
