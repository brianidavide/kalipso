#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Regenerate the import fixtures with LibreOffice through UNO.

notes.odp and notes.pptx: slides built the way a user would, with real title
and notes placeholders. script.odt and script.docx: a Writer document with
headings, a bulleted list, bold and italic words, a comment and a footnote.
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
            write_document(desktop, here)
        finally:
            office.terminate()
            office.wait(timeout=30)


def write_document(desktop, here):
    from com.sun.star.text.ControlCharacter import PARAGRAPH_BREAK
    document = desktop.loadComponentFromURL('private:factory/swriter', '_blank', 0, (prop('Hidden', True),))
    text = document.Text
    cursor = text.createTextCursor()

    def paragraph(style, parts, bulleted=False):
        cursor.ParaStyleName = style
        cursor.NumberingStyleName = 'List 1' if bulleted else ''  # 'List 1' is the "Bullet •" list style.
        for part, weight, posture in parts:
            cursor.CharWeight = weight
            cursor.CharPosture = posture
            text.insertString(cursor, part, False)
        cursor.CharWeight, cursor.CharPosture = 100.0, 0  # NORMAL weight and posture
        text.insertControlCharacter(cursor, PARAGRAPH_BREAK, False)

    from com.sun.star.awt.FontSlant import ITALIC, NONE
    bold, normal = 150.0, 100.0
    paragraph('Heading 1', [('Opening', normal, NONE)])
    paragraph('Standard', [('Hello ', normal, NONE), ('everyone', bold, NONE), (' and ', normal, NONE),
                           ('welcome', normal, ITALIC), (' to the launch.', normal, NONE)])
    paragraph('Standard', [('First point', normal, NONE)], bulleted=True)
    paragraph('Standard', [('Second point', normal, NONE)], bulleted=True)
    paragraph('Heading 2', [('Results', normal, NONE)])
    paragraph('Standard', [('Revenue grew by twelve percent.', normal, NONE)])
    # A comment and a footnote are not part of what is read aloud.
    end = text.getEnd()
    annotation = document.createInstance('com.sun.star.text.textfield.Annotation')
    annotation.Content = 'Reviewer comment'
    text.insertTextContent(end, annotation, False)
    footnote = document.createInstance('com.sun.star.text.Footnote')
    text.insertTextContent(text.getEnd(), footnote, False)
    footnote.setString('Footnote text')
    for name, filter_name in (('script.odt', 'writer8'), ('script.docx', 'MS Word 2007 XML')):
        document.storeToURL((here / name).as_uri(), (prop('FilterName', filter_name),))
    document.close(True)


if __name__ == '__main__':
    main()
