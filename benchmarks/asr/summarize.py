#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Aggregate executed runs; repeats never increase the accuracy sample size."""
import argparse
import csv
import json
import statistics
from pathlib import Path

CONFIGS=['vosk-small','whisper-tiny-q5','whisper-base-q5','whisper-small-q5','moonshine-tiny','moonshine-tiny-single']
LANGS=['en','de','es','ja','zh']

def read_rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]

def summarize(directory):
    rows=read_rows(directory/'batch.jsonl')
    main=[r for r in rows if r['repeat']==0]
    assert len(main)==150 and len({(r['id'],r['config']) for r in main})==150
    assert all(r['exitCode']==0 for r in main), 'Batch failures must be reviewed before aggregation'
    aggregates=[]
    for config in CONFIGS:
        for lang in LANGS:
            subset=[r for r in main if r['config']==config and r['language']==lang]
            assert len(subset)==5
            item={'config':config,'language':lang,'clips':len(subset),
                  'audioSeconds':sum(r['audioSeconds'] for r in subset)}
            for unit in ['char','word']:
                key='referenceChars' if unit=='char' else 'referenceWords'
                if key not in subset[0]:continue
                errors=sum(r[unit+'Errors'] for r in subset);denominator=sum(r[key] for r in subset)
                item.update({unit+'Errors':errors,key:denominator,('cer' if unit=='char' else 'wer'):errors/denominator})
            item.update(medianRtf=statistics.median(r['decodeRtf'] for r in subset),
                        cpuSecondsPerAudioSecond=sum(r['cpuSeconds'] for r in subset)/item['audioSeconds'],
                        maxPeakRssMiB=max(r['peakRssKiB']/1024 for r in subset))
            aggregates.append(item)
    with (directory/'quality.csv').open('w') as output:
        writer=csv.DictWriter(output,lineterminator='\n',fieldnames=list(dict.fromkeys(k for a in aggregates for k in a)))
        writer.writeheader();writer.writerows(aggregates)
    lines=['| Configuration | EN WER | DE WER | ES WER | JA CER | ZH CER |',
           '| --- | ---: | ---: | ---: | ---: | ---: |']
    for config in CONFIGS:
        values=[next(a for a in aggregates if a['config']==config and a['language']==lang) for lang in LANGS]
        lines.append('| '+config+' | '+' | '.join(f"{a['cer' if a['language'] in ('ja','zh') else 'wer']:.1%}" for a in values)+' |')
    lines+=['','| Configuration | Batch median RTF | Process CPU s / audio s | Peak RSS range MiB | Median model load s |',
            '| --- | ---: | ---: | ---: | ---: |']
    for config in CONFIGS:
        subset=[r for r in main if r['config']==config]
        lines.append(f"| {config} | {statistics.median(r['decodeRtf'] for r in subset):.3f} | {sum(r['cpuSeconds'] for r in subset)/sum(r['audioSeconds'] for r in subset):.3f} | {min(r['peakRssKiB']/1024 for r in subset):.0f}–{max(r['peakRssKiB']/1024 for r in subset):.0f} | {statistics.median(r['modelLoadMs']/1000 for r in subset):.3f} |")
    lines+=['','| Configuration | EN first-clip RTF range, 3 runs |', '| --- | ---: |']
    for config in CONFIGS:
        subset=[r for r in rows if r['config']==config and r['id']==main[0]['id']]
        assert len(subset)==3
        lines.append(f"| {config} | {min(r['decodeRtf'] for r in subset):.3f}–{max(r['decodeRtf'] for r in subset):.3f} |")
    if (directory/'tracking-stream.json').exists():
        traces=json.loads((directory/'tracking-stream.json').read_text());stream=read_rows(directory/'stream.jsonl')
        lines+=['','| Configuration | Stream completed | First tracker advance range s | Final position range | Backward moves total | EOF lag range s |',
                '| --- | ---: | ---: | ---: | ---: | ---: |']
        for config in CONFIGS:
            subset=[r for r in stream if r['config']==config and r['exitCode']==0]
            tracked=[r for r in traces if r['config']==config]
            first=[r['firstAdvanceSeconds'] for r in tracked if r['firstAdvanceSeconds'] is not None]
            coverage=[r['finalCoverage'] for r in tracked];lag=[r['finishLagSeconds'] for r in subset]
            lines.append(f"| {config} | {len(subset)}/5 | {min(first):.2f}–{max(first):.2f} ({len(tracked)-len(first)} no advance) | {min(coverage):.0%}–{max(coverage):.0%} | {sum(r['backwardMoves'] for r in tracked)} | {min(lag):.2f}–{max(lag):.2f} |" if first else f'| {config} | {len(subset)}/5 | no advancement | — | — | — |')
    if (directory/'stream.jsonl').exists():
        stream=read_rows(directory/'stream.jsonl')
        lines+=['','| Configuration | Paced process CPU s / audio s (completed runs) | Paced peak RSS range MiB |',
                '| --- | ---: | ---: |']
        for config in CONFIGS:
            subset=[r for r in stream if r['config']==config and r['exitCode']==0]
            if not subset:continue
            lines.append(f"| {config} | {sum(r['cpuSeconds'] for r in subset)/sum(r['audioSeconds'] for r in subset):.3f} | {min(r['peakRssKiB']/1024 for r in subset):.0f}–{max(r['peakRssKiB']/1024 for r in subset):.0f} |")
    if (directory/'controls.jsonl').exists() and len(read_rows(directory/'controls.jsonl'))==2*len(CONFIGS):
        controls=read_rows(directory/'controls.jsonl')
        tracked=json.loads((directory/'tracking-controls.json').read_text())
        lines+=['','| Configuration | Silence text | Silence tracker moves | Pause final position | Pause EOF lag s |',
                '| --- | --- | ---: | ---: | ---: |']
        for config in CONFIGS:
            silence=next(r for r in controls if r['config']==config and r['id']=='control-silence')
            pause=next(r for r in controls if r['config']==config and r['id']=='control-pause')
            silence_trace=next((r for r in tracked if r['config']==config and r['id']=='control-silence'),None)
            pause_trace=next((r for r in tracked if r['config']==config and r['id']=='control-pause'),None)
            # Streaming Whisper's final text is just the last window: inspect all events.
            nonempty=False
            if silence['exitCode']==0:
                report=json.loads((directory/silence['output']).read_text())
                nonempty=any(e.get('text','').strip() for e in report['events'] if e.get('text'))
            text=('yes' if nonempty else 'none') if silence['exitCode']==0 else 'failed'
            moves=str(len(silence_trace['movements'])) if silence_trace else 'failed'
            position=f"{pause_trace['finalCoverage']:.0%}" if pause_trace else 'failed'
            lag=f"{pause['finishLagSeconds']:.2f}" if pause['exitCode']==0 else 'failed'
            lines.append(f'| {config} | {text} | {moves} | {position} | {lag} |')
    (directory/'tables.md').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    summarize(parser.parse_args().directory)
