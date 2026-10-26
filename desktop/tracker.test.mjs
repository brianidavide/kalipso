// SPDX-License-Identifier: AGPL-3.0-or-later
import assert from 'node:assert/strict';
import {Tracker} from './tracker.mjs';
import {readFileSync} from 'node:fs';
for(const fixture of JSON.parse(readFileSync(new URL('../tests/multilingual-tracker.json',import.meta.url)))) {
    const tracker=new Tracker(fixture.script);
    assert(tracker.follow(fixture.partial),fixture.script);
    const word=tracker.words[tracker.position];
    assert.equal(tracker.text.slice(0,word.start+word.length),fixture.read,fixture.script);
    const before=tracker.position;assert(!tracker.follow(''));assert.equal(tracker.position,before);
    tracker.setText('🎤 '+fixture.script);
    assert.equal(tracker.words[tracker.position].start,word.start+3);
}
const t=new Tracker('uno due tre quattro cinque');
assert(t.follow('uno due'));assert.equal(t.position,1);
assert(!t.follow(''));assert.equal(t.position,1); // silence never advances
assert(!t.follow('uno due'));assert.equal(t.position,1); // repeated partial
assert(!t.follow('a completely unrelated utterance'));assert.equal(t.position,1);
t.setText('prima uno due tre quattro cinque');assert.equal(t.position,2);
assert(t.follow('tre quattro',true));assert.equal(t.position,4);
assert(t.markup().includes('prima uno due tre quattro</span>'));
t.setText('<b>first</b> & second');assert(!t.markup().includes('<b>'));
console.log('tracker: silence, duplicate partial, unrelated speech, live edit anchor, resumption, safe markup PASS');

const long=new Tracker(Array.from({length:400},(_,i)=>`word${i}`).join(' '));
long.seek(300);
const excerpt=long.readingMarkup('#777777','#ffffff');
assert(excerpt.includes('word300</span>'));assert(excerpt.includes('word301'));
assert(!excerpt.includes('word200'));assert(!excerpt.includes('word350'));
long.setText('<script>alert(1)</script>');assert(!long.readingMarkup('#777777','#ffffff').includes('<script>'));
console.log('reader: bounded long-script context and escaped editing PASS');

const huge=new Tracker('one two three four');huge.seek(1);assert(huge.readingMarkup('#777','#fff',-1).includes('three four'));huge.seek(3);assert(!huge.readingMarkup('#777','#fff',-1).includes('one two'));
