#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The .mo files desktop/package.py builds for Plasma and GNOME, read back by gettext."""
import gettext
import io
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'desktop'))
import package  # noqa: E402

mo = gettext.GNUTranslations(io.BytesIO(package.compile_po(ROOT / 'desktop' / 'po' / 'it.po')))
assert mo.gettext('Reader font') == 'Carattere del lettore', mo.gettext('Reader font')
assert mo.ngettext('%1 second', '%1 seconds', 1) == '%1 secondo'
assert mo.ngettext('%1 second', '%1 seconds', 5) == '%1 secondi'
assert mo.ngettext('Starting in %1 second…', 'Starting in %1 seconds…', 3) == 'Inizio tra %1 secondi…'
assert mo.gettext('Untranslated sentence stays as is') == 'Untranslated sentence stays as is'
# Negative control: the previous compiler dropped plural entries, so English came back.
assert mo.ngettext('%1 second', '%1 seconds', 5) != '%1 seconds'
print('package translations: singular and plural Italian strings PASS')
