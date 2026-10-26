#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Measure a process and live descendants. No profiler dependency or root needed.

Usage: python3 tools/measure-process.py --seconds 5 -- build/kalipso --demo
CPU percentages are fractions of ONE logical CPU. Includes a one-second warmup.
Live process-tree sampling can miss short-lived children; use perf/RAPL for energy.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

def sample(pid):
    path = Path('/proc') / str(pid)
    raw = (path / 'stat').read_text().rsplit(')', 1)[1].split()
    ticks = int(raw[11]) + int(raw[12])
    memory = {}
    for line in (path / 'smaps_rollup').read_text().splitlines():
        fields = line.split()
        if fields[0] in ('Rss:', 'Pss:'):
            memory[fields[0][:-1].lower()] = int(fields[1])
    return ticks, memory

def descendants(pid):
    result = {pid}
    for task in (Path('/proc') / str(pid) / 'task').glob('*'):
        try:
            for child in (task / 'children').read_text().split():
                result.update(descendants(int(child)))
        except (FileNotFoundError, ProcessLookupError):
            pass
    return result

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=float, default=5)
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ['--'] else args.command
if not command or args.seconds <= 0:
    parser.error('provide a command and positive duration')
process = subprocess.Popen(command, stdout=subprocess.DEVNULL)
previous = {}
rss, pss = [], []
cpu_ticks = 0
try:
    time.sleep(1)
    started = time.monotonic()
    while time.monotonic() - started < args.seconds and process.poll() is None:
        totals = {'rss': 0, 'pss': 0}
        for pid in descendants(process.pid):
            try:
                ticks, memory = sample(pid)
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                continue
            cpu_ticks += max(0, ticks - previous.get(pid, ticks))
            previous[pid] = ticks
            for key in totals:
                totals[key] += memory.get(key, 0)
        rss.append(totals['rss']); pss.append(totals['pss'])
        time.sleep(.1)
    elapsed = time.monotonic() - started
    if not pss or max(pss) == 0:
        raise RuntimeError('process exited or memory accounting unavailable')
    result = {'command': command, 'elapsed_seconds': round(elapsed, 3), 'samples': len(pss),
              'cpu_percent_one_core': round(cpu_ticks / os.sysconf('SC_CLK_TCK') / elapsed * 100, 3),
              'median_pss_kib': statistics.median(pss), 'peak_pss_kib': max(pss),
              'median_rss_kib': statistics.median(rss), 'exit_during_measurement': process.poll()}
    print(json.dumps(result))
finally:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait()
