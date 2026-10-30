Kalipso companion
=================

Voice following for the Kalipso Plasma widget and GNOME Shell extension. The companion listens to the
microphone and recognises speech on this computer; audio never leaves it. You do not need this if you
use the Kalipso app, or installed the Kalipso package for your distribution (it includes the companion:
run kalipso-voice-setup instead).

Needs: Linux x86_64, PipeWire, Qt 6 and Python 3 (all present on current KDE Plasma and GNOME desktops).

Set up, choosing your language (for example it, en, de, es, ja, zh):

    ./install --language it

This downloads one speech runtime and one language model (verified against pinned SHA-256 fingerprints),
installs the companion under ~/.local and registers it with your desktop. It does not open the
microphone. Then, in the widget or extension: Settings > Follow my voice > Start listening. If it says
the companion is missing, log out and back in.

Moonshine is used where it has the language, otherwise Vosk (Italian uses Vosk). To choose:
./install --language en --backend vosk. To see the languages: python3 tools/install-local-engine.py --list-models

To remove it: rm ~/.local/bin/kalipso-engine ~/.local/share/dbus-1/services/io.github.brianidavide.Kalipso.Engine.service
and the folder ~/.local/share/kalipso.

Source code, licence (AGPL-3.0-or-later) and help: https://github.com/brianidavide/kalipso
