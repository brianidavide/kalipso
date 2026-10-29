#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Regenerate notes.odp and notes.pptx with LibreOffice Impress through UNO.

Builds slides the way a user would: real title and notes placeholders.
Requires LibreOffice and its Python UNO bridge (python3-uno on Debian).
"""
import os
import pathlib
import subprocess
import tempfile
import time

import uno
from com.sun.star.beans import PropertyValue

SLIDES = [
    ("Welcome", ["Thank everyone for coming.", "Mention the  two  spaces."]),
    ("Agenda", []),
    ("Results", ["Revenue grew by twelve percent.", "Pause for questions."]),
]


def prop(name, value):
    item = PropertyValue()
    item.Name, item.Value = name, value
    return item


def main():
    here = pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as profile:
        office = subprocess.Popen(['soffice', f'-env:UserInstallation=file://{profile}', '--headless',
                                   '--invisible', '--accept=pipe,name=kalipso-fixtures;urp;'])
        try:
            resolver = uno.getComponentContext().ServiceManager.createInstanceWithContext(
                'com.sun.star.bridge.UnoUrlResolver', uno.getComponentContext())
            for _ in range(100):
                try:
                    context = resolver.resolve('uno:pipe,name=kalipso-fixtures;urp;StarOffice.ComponentContext')
                    break
                except Exception:
                    time.sleep(0.2)
            desktop = context.ServiceManager.createInstanceWithContext('com.sun.star.frame.Desktop', context)
            document = desktop.loadComponentFromURL('private:factory/simpress', '_blank', 0, (prop('Hidden', True),))
            pages = document.DrawPages
            while pages.Count < len(SLIDES):
                pages.insertNewByIndex(pages.Count - 1)
            for index, (title, notes) in enumerate(SLIDES):
                page = pages.getByIndex(index)
                page.Layout = 1  # Title, content
                for shape_index in range(page.Count):
                    shape = page.getByIndex(shape_index)
                    if shape.ShapeType == 'com.sun.star.presentation.TitleTextShape':
                        shape.String = title
                notes_page = page.NotesPage
                for shape_index in range(notes_page.Count):
                    shape = notes_page.getByIndex(shape_index)
                    if shape.ShapeType == 'com.sun.star.presentation.NotesShape' and notes:
                        shape.String = '\n'.join(notes)
            for name, filter_name in (('notes.odp', 'impress8'), ('notes.pptx', 'Impress MS PowerPoint 2007 XML')):
                document.storeToURL((here / name).as_uri(), (prop('FilterName', filter_name),))
            document.close(True)
        finally:
            office.terminate()
            office.wait(timeout=30)


if __name__ == '__main__':
    main()
