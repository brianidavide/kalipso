// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {contrastRatio,PaletteChoice} from '../desktop/contrast.mjs';
const tokens=JSON.parse(readFileSync(new URL('../design/tokens.json',import.meta.url)));
let pairs=0;
for(const [name,p] of Object.entries(tokens.palette)){
 for(const background of ['background','surface','hover'])for(const text of ['text','muted']){
  const ratio=contrastRatio(p[text],p[background]);assert.ok(ratio>=4.5,`${name} ${text}/${background}: ${ratio}`);pairs++;
 }
 for(const [text,background] of [['actionText','action'],['actionText','actionHover'],['selectionText','selection'],['accent','background']]){
  const ratio=contrastRatio(p[text],p[background]);assert.ok(ratio>=4.5,`${name} ${text}/${background}: ${ratio}`);pairs++;
 }
 for(const background of ['background','surface','hover'])for(const indicator of ['focus','border']){
  const ratio=contrastRatio(p[indicator],p[background]);assert.ok(ratio>=3,`${name} ${indicator}/${background}: ${ratio}`);pairs++;
 }
}
assert.equal(contrastRatio('#000000','#ffffff'),21);
assert.ok(contrastRatio('#777777','#ffffff')<4.5); // A commonly rounded "4.5" must fail.
assert.throws(()=>contrastRatio('#ffffff80','#ffffff'));
const selection=new PaletteChoice();assert.equal(selection.sample('#000000',0),'light');assert.equal(selection.sample('#ffffff',1000),'light');assert.equal(selection.sample('#ffffff',2500),'dark');
assert.equal(selection.sample('#888888',5000),'dark');
console.log(`${pairs} palette contrast pairs pass; failing contrast, transparency and rapid alternation rejected`);
