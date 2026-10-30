# SPDX-License-Identifier: AGPL-3.0-or-later
"""The version scheme: ten steps between public releases, and every file states the same version."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('version', Path(__file__).resolve().parent.parent / 'tools/version.py')
version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version)

steps = ['0.1']
while steps[-1] != '2.1':
    steps.append(version.following(steps[-1]))
assert steps == ['0.1', '0.2', '0.3', '0.4', '0.5', '0.6', '0.7', '0.8', '0.9', '1.0', '1.1', '1.2', '1.3', '1.4',
                 '1.5', '1.6', '1.7', '1.8', '1.9', '2.0', '2.1'], steps
assert [step for step in steps if version.is_public(step)] == ['0.1', '1.0', '2.0']
for wrong in ['1.10', '1', '1.0.0', 'v1.0', '01.0']:
    try:
        version.parse(wrong)
    except SystemExit:
        continue
    raise AssertionError(f'{wrong} accepted')
version.current()  # Every file states the same version.
print('version scheme: steps, public releases and consistency PASS')
