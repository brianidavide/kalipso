#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reject ignored files and generated artifacts in the Git index."""
from pathlib import PurePosixPath
import subprocess


def tracked(*arguments):
    data = subprocess.check_output(['git', 'ls-files', '-z', *arguments])
    return [p for p in data.decode().split('\0') if p]


allowed_roots = {'.github', 'benchmarks', 'design', 'desktop', 'docs', 'packaging',
                 'site', 'src', 'tests', 'third_party', 'tools', 'LICENSES'}
allowed_files = {'.gitignore', 'CMakeLists.txt', 'CONTRIBUTING.md', 'LICENSE',
                 'README.md', 'THIRD_PARTY_NOTICES.md'}
generated_suffixes = {'.png', '.jpg', '.jpeg', '.webp', '.wav', '.mp3', '.pcm',
                      '.zip', '.plasmoid', '.so', '.bin', '.log', '.jsonl', '.pyc'}
errors = set(tracked('-ci', '--exclude-standard'))
for name in tracked():
    path = PurePosixPath(name)
    if name not in allowed_files and path.parts[0] not in allowed_roots:
        errors.add(name)
    if path.suffix in generated_suffixes:
        errors.add(name)
if errors:
    raise SystemExit('Files excluded from source distribution:\n' + '\n'.join(sorted(errors)))
print('Repository contains only permitted source and distribution files.')
