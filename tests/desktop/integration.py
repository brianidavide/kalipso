#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real desktop redesign integration check. Start fresh isolated GNOME/Plasma lab hosts.
Requires both English profiles in artifacts/desktop-test/engine-config and the
pinned FLEURS benchmark fixture. UI coordinates target only this laboratory host.
The test owns its companion process and always terminates it.
"""
from pathlib import Path
import subprocess,os,json,ast,time
root=Path(__file__).resolve().parents[2];p=root/'artifacts/desktop-test'
env={**os.environ,'DBUS_SESSION_BUS_ADDRESS':(p/'bus-address').read_text(),'XDG_RUNTIME_DIR':'/tmp/kalipso-audio-probe','PULSE_SERVER':'unix:/tmp/kalipso-audio-probe/pulse/native'}
name='io.github.brianidavide.Kalipso.Engine';path='/io/github/brianidavide/Kalipso/Engine'
def run(args,extra=None):return subprocess.check_output(args,env={**env,**(extra or {})},text=True).strip()
def call(method,*args,gnome=False):return run(['gdbus','call','--session','--dest','org.gnome.Shell' if gnome else name,'--object-path','/io/github/brianidavide/Kalipso/Reader' if gnome else path,'--method',('io.github.brianidavide.Kalipso.Reader' if gnome else name)+'.'+method,*args])
def state(gnome=False):return json.loads(ast.literal_eval(call('GetState',gnome=gnome))[0])
def xenv(d):return {'DISPLAY':(p/f'{d}-display').read_text(),'XAUTHORITY':(p/f'{d}-xauthority').read_text()}
def click(d,x,y):run(['xdotool','mousemove',str(x),str(y),'click','1'],xenv(d));time.sleep(.25)
def key(d,value):run(['xdotool','key',value],xenv(d));time.sleep(.15)
def shot(d,name):run(['import','-window','0x200004' if d=='gnome' else '0x20000b',str(root/f'artifacts/ui-test/{name}.png')],xenv(d))
def pos(d):
 if d=='gnome':return state(True)['position']
 rows=[line.split('KALIPSO_TEST_STATE ',1)[1] for line in (p/'plasma.log').read_text().splitlines() if 'KALIPSO_TEST_STATE ' in line]
 return json.loads(rows[-1])['position']

(root/'artifacts/ui-test').mkdir(parents=True,exist_ok=True)
log=(root/'artifacts/ui-test/engine-live.log').open('w')
engine=subprocess.Popen([str(root/'build/kalipso-engine'),'--serve'],env={**env,'XDG_CONFIG_HOME':str(root/'artifacts/desktop-test/engine-config')},stdout=log,stderr=subprocess.STDOUT)
try:
    time.sleep(.3)

    def wait(check,seconds=12):
     start=time.monotonic()
     while time.monotonic()-start<seconds:
      if check():return
      time.sleep(.1)
     raise AssertionError('Condition timed out')
    def gclick(label):
     item=next(item for item in state(True)['controls'] if item['label'].removeprefix('✓ ')==label)
     assert item['x'] is not None,label
     click('gnome',round(item['x']+item['width']/2),round(item['y']+item['height']/2))
    manifest=json.load(open(os.environ.get('KALIPSO_ASR_MANIFEST','/tmp/kalipso-asr-benchmark/manifest.json')));ref=manifest['records'][0]['reference'];wav=manifest['records'][0]['absoluteWav']
    run(['xdotool','windowsize','0x20000b','640','360'],xenv('plasma'))
    # Ensure overview is dismissed; opening the editor gives the extension keyboard focus.
    gclick('Edit script');key('gnome','Escape')
    call('SetScript',ref,gnome=True);gclick('Reset')
    # Plasma: source editor reached by a real click, then keyboard input.
    click('plasma',495,38);click('plasma',160,390);key('plasma','ctrl+a');run(['xdotool','type','--clearmodifiers','--delay','1',ref],xenv('plasma'));click('plasma',485,38)
    baseline=run(['pactl','list','short','source-outputs']);results=[]
    for desktop in ['gnome','plasma']:
     for backend in ['moonshine','vosk']:
      if desktop=='gnome':
       gclick('Settings');gclick('Follow my voice');time.sleep(.3);gclick('Moonshine' if backend=='moonshine' else 'Vosk');time.sleep(.6);gclick('Done');gclick('Reset');gclick('Start listening')
      else:
       click('plasma',590,38);click('plasma',167,338);time.sleep(.3);click('plasma',76 if backend=='moonshine' else 156,386);time.sleep(.6);click('plasma',590,38);click('plasma',305,251);click('plasma',85,251)
      wait(lambda:state()['status']=='listening')
      assert state()['backend']==backend,(desktop,backend,state())
      try:call('SelectBackend','vosk' if backend=='moonshine' else 'moonshine')
      except subprocess.CalledProcessError:pass
      else:raise AssertionError('Backend changed during capture')
      run(['paplay','--device=kalipso_test',wav]);time.sleep(2)
      position=pos(desktop);assert position>=10,(desktop,backend,position,state())
      shot(desktop,f'{desktop}-{backend}-redesigned')
      if desktop=='gnome':gclick('Pause')
      else:click('plasma',85,251)
      wait(lambda:not state()['active']);time.sleep(.4)
      assert pos(desktop)==position
      assert run(['pactl','list','short','source-outputs'])==baseline
      # Resume must preserve the position before any new audio is played.
      if desktop=='gnome':gclick('Start listening')
      else:click('plasma',85,251)
      wait(lambda:state()['status']=='listening');assert pos(desktop)==position
      if desktop=='gnome':gclick('Pause')
      else:click('plasma',85,251)
      wait(lambda:not state()['active']);time.sleep(.3)
      assert run(['pactl','list','short','source-outputs'])==baseline
      result={'desktop':desktop,'backend':backend,'position':position,'units':17,'pauseReleasesCapture':True,'resumeKeepsPosition':True,'activeSwitchRejected':True}
      results.append(result);print(json.dumps(result),flush=True)
      (root/'artifacts/ui-test/native-results.json').write_text(json.dumps(results,indent=2)+'\n')
    (root/'artifacts/ui-test/native-results.json').write_text(json.dumps(results,indent=2)+'\n')
finally:
    engine.terminate();engine.wait(timeout=20);log.close()
