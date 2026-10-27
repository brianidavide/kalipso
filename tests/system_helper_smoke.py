# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exercise the actual OS helper's read-only language probe without a microphone."""
import json
import subprocess
import sys

result = subprocess.run([sys.argv[1], '--check', 'en-US'], capture_output=True,
                        text=True, timeout=45, check=False)
events = [json.loads(line) for line in result.stdout.splitlines()]
assert result.returncode in (0, 1), (result.returncode, result.stderr)
assert len(events) == 1, events
event = events[0]
assert event.get('type') in ('capabilities', 'error'), event
assert isinstance(event.get('message'), str) and event['message'], event
if event['type'] == 'capabilities':
    assert result.returncode == 0, result.returncode
    for flag in ('available', 'installed', 'supported'):
        assert isinstance(event.get(flag), bool), event
    assert isinstance(event.get('locales'), list), event
else:
    assert result.returncode == 1, result.returncode
print(json.dumps(event))
