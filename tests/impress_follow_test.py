#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run src/impress/impress_follow.py against a real LibreOffice Impress slideshow.

Needs LibreOffice, a Python that can import uno, and a display (xvfb-run is
fine). Exits 77 (skipped) when any of them is missing.
"""
import json
import os
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
HELPER = ROOT / 'src' / 'impress' / 'impress_follow.py'
FIXTURE = ROOT / 'tests' / 'fixtures' / 'notes.odp'
SKIP = 77


class OfficeUnavailable(Exception):
    pass


def main():
    office = shutil.which('soffice') or shutil.which('libreoffice')
    if not office or not os.environ.get('DISPLAY'):
        print('skipped: needs LibreOffice and a display')
        return SKIP
    try:
        import uno  # noqa: F401
        from com.sun.star.beans import PropertyValue  # noqa: F401
    except ImportError:
        print('skipped: Python cannot import uno')
        return SKIP

    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        presentation = work / 'talk notes.odp'  # A space, as in real file names.
        shutil.copy(FIXTURE, presentation)
        pipe = f'kalipso-test-{os.getpid()}'
        soffice = subprocess.Popen([office, f'-env:UserInstallation={(work / "profile").as_uri()}', '--norestore',
                                    '--nologo', '--nodefault', f'--accept=pipe,name={pipe};urp;', str(presentation)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        helper = subprocess.Popen([sys.executable, str(HELPER), pipe, str(presentation)], stdout=subprocess.PIPE,
                                  text=True)
        lines = queue.Queue()
        threading.Thread(target=lambda: [lines.put(json.loads(line)) for line in helper.stdout], daemon=True).start()

        def expect(state, timeout=60, **fields):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if soffice.poll() is not None and state != 'closed':
                    raise OfficeUnavailable(f'LibreOffice exited ({soffice.returncode}); is the display usable?')
                try:
                    event = lines.get(timeout=1)
                except queue.Empty:
                    continue
                if event.get('state') == state and all(event.get(k) == v for k, v in fields.items()):
                    return event
            raise AssertionError(f'no {state} {fields} within {timeout} s')

        try:
            expect('connecting', timeout=10)
            expect('waiting', timeout=90)
            desktop = connect(pipe)
            document = find(desktop, uno.systemPathToFileUrl(str(presentation)))
            show = document.getPresentation()
            show.start()
            expect('slide', slide=1, notes='Thank everyone for coming.\nMention the  two  spaces.')
            controller = wait_for(lambda: show.getController())
            controller.gotoSlideIndex(2)
            expect('slide', slide=3, notes='Revenue grew by twelve percent.\nPause for questions.')
            controller.gotoSlideIndex(1)
            expect('slide', slide=2, notes='')  # A slide without notes.
            show.end()
            expect('ended')
            document.close(True)
            expect('closed')
            helper.wait(timeout=10)
            assert helper.returncode == 0, helper.returncode
        except OfficeUnavailable as reason:
            print(f'skipped: {reason}')
            return SKIP
        finally:
            helper.kill()
            soffice.terminate()
            try:
                soffice.wait(timeout=30)
            except subprocess.TimeoutExpired:
                soffice.kill()

        # Negative control: a presentation that is not open is never reported as a slide.
        bad = subprocess.run([sys.executable, str(HELPER), 'kalipso-no-such-pipe', str(presentation)],
                             capture_output=True, text=True, timeout=60,
                             env={**os.environ, 'KALIPSO_IMPRESS_CONNECT_SECONDS': '2'})
        assert '"slide"' not in bad.stdout and '"error"' in bad.stdout, bad.stdout
    print('impress: slideshow start, slide changes, empty notes, end and close PASS; negative control fails as expected')
    return 0


def connect(pipe):
    import uno
    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext('com.sun.star.bridge.UnoUrlResolver', local)
    context = wait_for(lambda: resolver.resolve(f'uno:pipe,name={pipe};urp;StarOffice.ComponentContext'))
    return context.ServiceManager.createInstanceWithContext('com.sun.star.frame.Desktop', context)


def find(desktop, url):
    def lookup():
        components = desktop.getComponents().createEnumeration()
        while components.hasMoreElements():
            component = components.nextElement()
            if hasattr(component, 'getURL') and component.getURL() == url:
                return component
        return None
    return wait_for(lookup)


def wait_for(probe, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = probe()
            if value:
                return value
        except Exception:  # Not ready yet (for example no connection).
            pass
        time.sleep(0.3)
    raise AssertionError('timed out')


if __name__ == '__main__':
    sys.exit(main())
