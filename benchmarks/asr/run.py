#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Serial CPU-only comparison. Executes real models, never downloads during runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import signal
import statistics
import subprocess
import time
import unicodedata
import wave

ROOT=Path(__file__).resolve().parents[2]
CONFIGS=['vosk-small','whisper-tiny-q5','whisper-base-q5','whisper-small-q5','moonshine-tiny']

def normalized(text):
    text=unicodedata.normalize('NFKC',text).casefold()
    return ' '.join(''.join(c if unicodedata.category(c)[0] in 'LNM' else ' ' for c in text).split())

def distance(reference,hypothesis):
    previous=list(range(len(hypothesis)+1))
    for i,a in enumerate(reference,1):
        current=[i]
        for j,b in enumerate(hypothesis,1):current.append(min(current[-1]+1,previous[j]+1,previous[j-1]+(a!=b)))
        previous=current
    return previous[-1]

def score(reference,hypothesis,language):
    ref=normalized(reference);hyp=normalized(hypothesis)
    chars_ref=ref.replace(' ','');chars_hyp=hyp.replace(' ','')
    result={'charErrors':distance(chars_ref,chars_hyp),'referenceChars':len(chars_ref)}
    result['cer']=result['charErrors']/max(1,len(chars_ref))
    if language not in ('ja','zh'):
        result.update(wordErrors=distance(ref.split(),hyp.split()),referenceWords=len(ref.split()))
        result['wer']=result['wordErrors']/max(1,result['referenceWords'])
    return result

def measure(command,env,cpus,log,timeout=180):
    with log.open('w') as handle:
        child=subprocess.Popen(command,stdout=handle,stderr=subprocess.STDOUT,env=env,
            preexec_fn=lambda:os.sched_setaffinity(0,cpus),start_new_session=True)
        began=time.monotonic();pss=[];affinities=set();thread_counts=[]
        while True:
            pid,status,usage=os.wait4(child.pid,os.WNOHANG)
            if pid:break
            try:
                for line in Path(f'/proc/{child.pid}/smaps_rollup').read_text().splitlines():
                    if line.startswith('Pss:'):pss.append(int(line.split()[1]));break
            except (FileNotFoundError,ProcessLookupError):pass
            if len(pss) in (10,20):
                tasks=list(Path(f'/proc/{child.pid}/task').glob('*'))
                thread_counts.append(len(tasks))
                for task in tasks:
                    try:
                        for line in (task/'status').read_text().splitlines():
                            if line.startswith('Cpus_allowed_list:'):affinities.add(line.split(':',1)[1].strip())
                    except (FileNotFoundError,ProcessLookupError):pass
            if time.monotonic()-began>timeout:
                os.killpg(child.pid,signal.SIGKILL);pid,status,usage=os.wait4(child.pid,0);break
            time.sleep(.05)
        child.returncode=os.waitstatus_to_exitcode(status)
    return {'exitCode':child.returncode,'processWallSeconds':time.monotonic()-began,
        'cpuSeconds':usage.ru_utime+usage.ru_stime,'peakRssKiB':usage.ru_maxrss,
        'sampledPeakPssKiB':max(pss,default=0),'pssSamples':len(pss),'observedThreadAffinities':sorted(affinities),'sampledMaxThreads':max(thread_counts,default=0)}

def pcm(wav,output):
    with wave.open(str(wav),'rb') as w:
        assert (w.getframerate(),w.getnchannels(),w.getsampwidth())==(16000,1,2)
        output.write_bytes(w.readframes(w.getnframes()))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,default=Path('/tmp/kalipso-asr-benchmark'))
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/asr-benchmark')
    parser.add_argument('--phase',choices=['batch','stream','controls'],required=True)
    parser.add_argument('--repeat-first',type=int,default=0)
    parser.add_argument('--configs',nargs='+',choices=CONFIGS+['moonshine-tiny-single'],default=CONFIGS)
    parser.add_argument('--whisper-root',type=Path,default=Path('/tmp/kalipso-whisper-compare'))
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build-asr-benchmark')
    parser.add_argument('--vosk-library',type=Path,default=Path('/tmp/kalipso-speech-test/libvosk.so'))
    parser.add_argument('--moonshine-python',type=Path,default=Path('/tmp/kalipso-moonshine/bin/python'))
    parser.add_argument('--moonshine-model-root',type=Path,default=Path('/tmp/kalipso-moonshine/models/download.moonshine.ai/model'))
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((args.data/'manifest.json').read_text());records=manifest['records']
    models=json.loads((args.data/'models.json').read_text())['models']
    vosk={m['language']:m for m in models if m['engine']=='vosk'}
    whisper={m['model']:m for m in models if m['engine']=='whisper.cpp'}
    if not whisper:whisper={m['model']:m for m in models if m['engine'].startswith('whisper')}
    moonroot=args.moonshine_model_root
    moon={}
    for lang in ('en','de','es','ja','zh'):
        candidates=list((moonroot/f'tiny-streaming-{lang}').glob('quantized_*'))
        if len(candidates)!=1:raise RuntimeError(f'Expected one pinned Moonshine model for {lang}, found {len(candidates)}')
        moon[lang]=candidates[0]
    cpus=set();cores=set()
    for cpu in sorted(os.sched_getaffinity(0)):
        topology=Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
        core=(topology/'physical_package_id').read_text().strip(),(topology/'core_id').read_text().strip()
        if core not in cores:cpus.add(cpu);cores.add(core)
        if len(cpus)==2:break
    if len(cpus)!=2:raise RuntimeError('Two distinct physical CPU cores are required for this protocol')
    env={**os.environ,'OMP_NUM_THREADS':'2','OPENBLAS_NUM_THREADS':'2','MKL_NUM_THREADS':'2','CUDA_VISIBLE_DEVICES':''}
    environment={'cpuAffinity':sorted(cpus),'physicalCores':sorted(cores),'threadRequest':2,'gpu':False,'python':os.sys.version,'unicodeVersion':unicodedata.unidata_version,
        'cpuInfo':subprocess.check_output(['lscpu','--json'],text=True),'memoryInfo':Path('/proc/meminfo').read_text(),
        'cgroupCpuMax':Path('/sys/fs/cgroup/cpu.max').read_text(),'cgroupMemoryMax':Path('/sys/fs/cgroup/memory.max').read_text(),
        'whisperCommit':subprocess.check_output(['git','-C',str(args.whisper_root),'rev-parse','HEAD'],text=True).strip(),
        'dateUTC':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'fixtureManifestSha256':hashlib.sha256((args.data/'manifest.json').read_bytes()).hexdigest()}
    environment['configs']=args.configs
    environment['nativeRunnerHashes']={name:hashlib.sha256((args.build_dir/name).read_bytes()).hexdigest() for name in ['vosk_bench','whisper_bench']}
    (args.out/f'environment-{args.phase}-{args.configs[0]}.json').write_text(json.dumps(environment,indent=2)+'\n')
    if args.phase=='stream':records=[next(r for r in records if r['language']==lang) for lang in manifest['languages']]
    if args.phase=='controls':
        base=next(r for r in records if r['language']=='en');source=Path(base['absoluteWav'])
        with wave.open(str(source),'rb') as w:frames=w.readframes(w.getnframes())
        controls=[]
        for suffix,data in [('silence',bytes(3*32000)),('pause',frames[:4*32000]+bytes(2*32000)+frames[4*32000:])]:
            path=args.out/f'control-{suffix}.wav'
            with wave.open(str(path),'wb') as w:w.setparams((1,2,16000,0,'NONE',''));w.writeframes(data)
            controls.append({**base,'id':'control-'+suffix,'absoluteWav':str(path),'duration':len(data)/32000,
                'reference':'' if suffix=='silence' else base['reference']})
        records=controls
    jobs=[]
    for record in records:
        configs=list(args.configs)
        random.Random('kalipso-'+record['id']).shuffle(configs)
        jobs.extend((record,config,0) for config in configs)
    if args.phase=='batch':
        for repeat in range(1,args.repeat_first+1):
            jobs.extend((records[0],config,repeat) for config in args.configs)
    destination=args.out/f'{args.phase}.jsonl'
    existing={}
    if destination.exists():
        for line in destination.read_text().splitlines():
            old=json.loads(line);existing[(old['id'],old['config'],old['repeat'])]=old
    with destination.open('a') as results:
        for i,(record,config,repeat) in enumerate(jobs,1):
            key=(record['id'],config,repeat)
            if key in existing:continue
            label=f'{args.phase}-{record["language"]}-{record["id"].replace(":","_")}-{config}-{repeat}'
            output=args.out/(label+'.json');raw=args.out/(label+'.pcm');pcm(Path(record['absoluteWav']),raw)
            language=record['language'];mode='batch' if args.phase=='batch' else 'stream'
            if config.startswith('moonshine-tiny'):
                command=[str(args.moonshine_python),str(ROOT/'benchmarks/asr/moonshine_worker.py'),'--model-dir',str(moon[language]),'--language',language,'--audio',record['absoluteWav'],'--output-file',str(output)]
            else:
                command=[str(args.build_dir/('vosk_bench' if config=='vosk-small' else 'whisper_bench')),'--audio',str(raw),'--output',str(output),'--language',language,'--mode',mode,'--threads','2','--step-ms','1000','--window-ms','8000']
                if config=='vosk-small':command+=['--library',str(args.vosk_library),'--model',vosk[language]['path']]
                else:
                    size=config.split('-')[1];model=next(m for name,m in whisper.items() if f'{size}-q5_1' in name)
                    command+=['--model',model['path']]
            if args.phase!='batch':command+=['--realtime']
            run_env={**env,'MOONSHINE_ORT_SINGLE_THREAD':'1'} if config=='moonshine-tiny-single' else {k:v for k,v in env.items() if k!='MOONSHINE_ORT_SINGLE_THREAD'}
            measured=measure(command,run_env,cpus,args.out/(label+'.log'))
            row={**measured,'id':record['id'],'language':language,'config':config,'repeat':repeat,'phase':args.phase,
                'audioSeconds':record['duration'],'command':command,'output':output.name}
            if measured['exitCode']==0:
                report=json.loads(output.read_text());row['modelLoadMs']=report['modelLoadMs']
                row['apiDecodeMs']=report['decodeMs']
                row['decodeMs']=report['decodeWallMs'] if config.startswith('moonshine-tiny') and args.phase=='batch' else report['decodeMs']
                row['decodeRtf']=row['decodeMs']/1000/record['duration']
                row['transcript']=report['transcript'];row['reference']=record['reference']
                if args.phase=='batch':row.update(score(record['reference'],report['transcript'],language))
                if args.phase!='batch':
                    events=[event for event in report['events'] if event.get('text')]
                    row['firstNonemptySeconds']=events[0]['wallMs']/1000 if events else None
                    row['maxInputLagSeconds']=max((event['wallMs']/1000-event.get('inputAudioSeconds',event['audioSeconds']) for event in report['events']),default=0)
                    row['decodeEndWallMs']=report['decodeWallMs'] if config.startswith('moonshine-tiny') else report['decodeEndWallMs']
                    row['finishLagSeconds']=max(0,row['decodeEndWallMs']/1000-record['duration'])
            results.write(json.dumps(row,ensure_ascii=False)+'\n');results.flush()
            print(f'{args.phase} {i}/{len(jobs)} {language} {config}: exit={row["exitCode"]} decodeRTF={row.get("decodeRtf",-1):.3f} peakRSS={row["peakRssKiB"]/1024:.1f}MiB',flush=True)

if __name__=='__main__':main()
