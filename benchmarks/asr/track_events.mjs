// SPDX-License-Identifier: AGPL-3.0-or-later
// Replay actual recognizer events through the same tracker used by both frontends.
import {readFileSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {Tracker} from '../../desktop/tracker.mjs';
const [directory,phase='stream']=process.argv.slice(2);
if(!directory)throw Error('Usage: node track_events.mjs RESULT_DIRECTORY [stream|controls]');
const rows=readFileSync(join(directory,`${phase}.jsonl`),'utf8').trim().split('\n').map(JSON.parse);
const results=[];
for(const row of rows){
    if(row.exitCode!==0)continue;
    const report=JSON.parse(readFileSync(join(directory,row.output),'utf8'));
    const script=row.reference||'Thank you for watching. Welcome everyone to today’s presentation.';
    const tracker=new Tracker(script),movements=[];let prior=-1,first=null;
    const lineStates=new Map();
    for(const event of report.events){
        if(!event.text)continue;
        const final=event.final??event.isComplete??false;
        if(event.lineId!==undefined){
            const key=`${event.text}\0${final}`;
            if(lineStates.get(event.lineId)===key)continue;
            lineStates.set(event.lineId,key);
        }
        if(tracker.follow(event.text,final)){
            if(first===null)first=event.wallMs/1000;
            movements.push({audioSeconds:event.inputAudioSeconds??event.audioSeconds,
                wallSeconds:event.wallMs/1000,from:prior,to:tracker.position});
            prior=tracker.position;
        }
    }
    results.push({id:row.id,config:row.config,language:row.language,units:tracker.words.length,
        finalPosition:tracker.position,finalCoverage:(tracker.position+1)/Math.max(1,tracker.words.length),
        firstAdvanceSeconds:first,backwardMoves:movements.filter(m=>m.to<m.from).length,
        silentScript:!row.reference,movements});
}
writeFileSync(join(directory,`tracking-${phase}.json`),JSON.stringify(results,null,2)+'\n');
console.log(`${results.length} real event traces replayed through the production tracker.`);
