#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reject ignored files and generated artifacts in the Git index."""
from pathlib import PurePosixPath
import subprocess


def tracked(*arguments):
    data = subprocess.check_output(['git', 'ls-files', '-z', *arguments])
    return [p for p in data.decode().split('\0') if p]


allowed_roots = {'.github', 'benchmarks', 'design', 'desktop', 'docs', 'packaging',
                 'site', 'src', 'tests', 'third_party', 'tools', 'translations', 'LICENSES'}
allowed_files = {'.clang-format', '.gitignore', 'CMakeLists.txt', 'CONTRIBUTING.md', 'LICENSE',
                 'README.md', 'THIRD_PARTY_NOTICES.md'}
generated_suffixes = {'.png', '.jpg', '.jpeg', '.webp', '.mp4', '.webm', '.wav', '.mp3', '.pcm',
                      '.zip', '.plasmoid', '.so', '.dll', '.dylib', '.exe', '.bin', '.onnx', '.ort',
                      '.log', '.jsonl', '.pyc'}
errors = set(tracked('-ci', '--exclude-standard'))
for name in tracked():
    path = PurePosixPath(name)
    if name not in allowed_files and path.parts[0] not in allowed_roots:
        errors.add(name)
    # Curated product captures are shipped website content, explicitly requested
    # by the owner. Diagnostic screenshots must still remain in ignored artifacts/.
    product_media = path.parent == PurePosixPath('site/assets/media') and path.suffix in {'.webp', '.mp4', '.webm'}
    if path.suffix in generated_suffixes and not product_media:
        errors.add(name)
if errors:
    raise SystemExit('Files excluded from source distribution:\n' + '\n'.join(sorted(errors)))
print('Repository contains only permitted source and distribution files.')
