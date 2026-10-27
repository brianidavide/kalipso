#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real PipeWire buffers through a fake consent portal, isolated from user sessions."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='kalipso-pipewire-test-') as directory:
    env = dict(os.environ, XDG_RUNTIME_DIR=directory, KALIPSO_PIPEWIRE_TEST='1')
    children = []
    try:
        with open(Path(directory)/'server.log', 'w+') as log:
            children.append(subprocess.Popen(['pipewire'], env=env, stdout=log, stderr=log))
            for _ in range(100):
                if (Path(directory)/'pipewire-0').exists():
                    break
                time.sleep(.05)
            else:
                raise RuntimeError('The isolated PipeWire server did not start')
            children.append(subprocess.Popen(['wireplumber'], env=env, stdout=log, stderr=log))
            for fmt, color, argb in [('BGRx', '#ff0000', '0xffff0000'), ('RGBx', '#0000ff', '0xff0000ff')]:
                producer = subprocess.Popen(['gst-launch-1.0', '-q', 'videotestsrc', 'is-live=true',
                    'pattern=solid-color', 'foreground-color='+argb, '!',
                    'video/x-raw,format='+fmt+',width=64,height=48,framerate=1/1', '!',
                    'pipewiresink', 'mode=provide', 'stream-properties=props,node.name=kalipso-test-video'],
                    env=env, stdout=log, stderr=log)
                children.append(producer)
                for _ in range(100):
                    objects = json.loads(subprocess.check_output(['pw-dump'], env=env))
                    nodes = [o['id'] for o in objects if o.get('type') == 'PipeWire:Interface:Node'
                             and o.get('info', {}).get('props', {}).get('node.name') == 'kalipso-test-video']
                    if nodes:
                        break
                    if producer.poll() is not None:
                        raise RuntimeError('The test video producer stopped')
                    time.sleep(.1)
                else:
                    raise RuntimeError('The test video node did not appear')
                subprocess.run([binary, 'realPipeWireFramesAreSampledAndReleased'], check=True,
                    env=dict(env, KALIPSO_TEST_VIDEO_NODE=str(nodes[0]), KALIPSO_TEST_VIDEO_COLOR=color))
                producer.terminate()
                producer.wait(timeout=5)
    except Exception:
        print((Path(directory)/'server.log').read_text(), file=sys.stderr)
        raise
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
