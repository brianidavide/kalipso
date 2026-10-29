# SPDX-License-Identifier: AGPL-3.0-or-later
"""Follow a LibreOffice Impress slideshow for Kalipso.

Run with LibreOffice's Python (or a Python that can import uno):
    python impress_follow.py PIPE_NAME PRESENTATION_PATH

Connects to LibreOffice through a local named pipe (opened by Kalipso when it
starts LibreOffice), finds the presentation, and prints one JSON object per
line whenever something changes:
    {"state": "connecting" | "waiting" | "ended" | "closed" | "error", "message": ...}
    {"state": "slide", "slide": 3, "notes": "Speaker notes of slide 3"}
It only reads the document through documented UNO interfaces
(XPresentation2, XSlideShowController, XPresentationPage) and never changes it.
"""
import json
import os
import sys
import time

import uno
from com.sun.star.connection import NoConnectException
from com.sun.star.lang import DisposedException
from com.sun.star.uno import RuntimeException

POLL_SECONDS = 0.3
CONNECT_SECONDS = float(os.environ.get('KALIPSO_IMPRESS_CONNECT_SECONDS', '60'))
NOTES_SHAPE = 'com.sun.star.presentation.NotesShape'


def emit(**fields):
    print(json.dumps(fields, ensure_ascii=False), flush=True)


def connect(pipe):
    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext('com.sun.star.bridge.UnoUrlResolver', local)
    deadline = time.monotonic() + CONNECT_SECONDS
    while True:
        try:
            context = resolver.resolve(f'uno:pipe,name={pipe};urp;StarOffice.ComponentContext')
            return context.ServiceManager.createInstanceWithContext('com.sun.star.frame.Desktop', context)
        except NoConnectException:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def find_document(desktop, url):
    components = desktop.getComponents().createEnumeration()
    while components.hasMoreElements():
        component = components.nextElement()
        if hasattr(component, 'getURL') and component.getURL() == url and hasattr(component, 'Presentation'):
            return component
    return None


def notes_of(page):
    notes = page.getNotesPage()
    for index in range(notes.getCount()):
        shape = notes.getByIndex(index)
        if shape.getShapeType() == NOTES_SHAPE:
            return shape.getString()
    return ''


def follow(desktop, url):
    seen = False
    last = None
    while True:
        document = find_document(desktop, url)
        if document is None:
            if seen:
                emit(state='closed')
                return
            state = ('waiting', None)
        else:
            seen = True
            presentation = document.getPresentation()
            controller = presentation.getController() if presentation.isRunning() else None
            if controller is None or controller.getCurrentSlide() is None:
                state = ('ended' if last and last[0] == 'slide' else 'waiting', None)
            else:
                page = controller.getCurrentSlide()
                state = ('slide', (controller.getCurrentSlideIndex() + 1, notes_of(page)))
        if state != last:
            if state[0] == 'slide':
                emit(state='slide', slide=state[1][0], notes=state[1][1])
            else:
                emit(state=state[0])
            last = state
        time.sleep(POLL_SECONDS)


def main():
    if len(sys.argv) != 3:
        emit(state='error', message='usage: impress_follow.py PIPE_NAME PRESENTATION_PATH')
        return 2
    pipe, path = sys.argv[1], sys.argv[2]
    emit(state='connecting')
    try:
        follow(connect(pipe), uno.systemPathToFileUrl(path))
    except NoConnectException:
        emit(state='error', message='LibreOffice did not open a connection.')
        return 1
    except (DisposedException, RuntimeException):
        emit(state='closed')  # LibreOffice quit.
    return 0


if __name__ == '__main__':
    sys.exit(main())
