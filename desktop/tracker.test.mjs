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

// Cues and headings: shown, never spoken (mirrors tests/core_tests.cpp).
import {cues, headings, sections, tokenize} from './tracker.mjs';
const cued = '# Opening\nHello [smile] everyone\n[look at camera]\nThanks [unclosed\n## Q&A ##';
assert.deepEqual(tokenize(cued).map(w => w.word), ['hello', 'everyone', 'thanks', 'unclosed']);
assert.equal(cues(cued).length, 2);
assert.deepEqual(sections(cued).map(s => s.title), ['Opening', 'Q&A']);
assert.equal(sections(cued)[1].start, cued.indexOf('## Q&A'));
assert.equal(headings('#hashtag is text').length, 0);
const cueTracker = new Tracker('Welcome everyone [pause] today we launch');
assert(cueTracker.follow('welcome everyone today', true));
assert.equal(cueTracker.position, 2);
assert(!cueTracker.follow('[unk]', true));
const styledMarkup = new Tracker('# Intro\nHi <there> [smile] all').readingMarkup('#777777', '#ffffff');
assert(styledMarkup.includes('<b># Intro</b>') && styledMarkup.includes('<i>[smile]</i>') && styledMarkup.includes('&lt;there&gt;'));
console.log('cues and headings: shown, styled and skipped by voice PASS');

// Long skips and backtracking recover only on an unambiguous strong match.
const filler = Array.from({length: 150}, (_, i) => `parola${i}`).join(' ');
const jumpy = new Tracker(`inizio del discorso ${filler} la conclusione arriva adesso finalmente`);
assert(jumpy.follow('inizio del discorso', true));
assert(jumpy.follow('la conclusione arriva adesso', true));
assert.equal(jumpy.position, jumpy.words.length - 2);
assert(jumpy.follow('inizio del discorso parola0', true));
assert.equal(jumpy.position, 3);
const offScript = new Tracker('today we present the new project plan');
assert(offScript.follow('today we present', true));
offScript.follow('by the way the weather is lovely', true);
assert(!offScript.lost);
offScript.follow('and my cat says hello to everyone', true);
assert(offScript.lost);
assert(offScript.follow('the new project', true));
assert(!offScript.lost);
const seeker = new Tracker('uno due tre');
seeker.seekCharacter(5); assert.equal(seeker.position, 1);
seeker.seekCharacter(-1); assert.equal(seeker.position, -1);
console.log('tracker: long-jump recovery, off-script state and character seeking PASS');
