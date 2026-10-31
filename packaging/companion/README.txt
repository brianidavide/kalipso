Kalipso companion
=================

Voice following for the Kalipso Plasma widget and GNOME Shell extension. The companion listens to the
microphone and recognises speech on this computer; audio never leaves it. You do not need this if you
use the Kalipso app, or installed the Kalipso package for your distribution: it includes the companion.

Needs: Linux x86_64, PipeWire and Qt 6 (present on current KDE Plasma and GNOME desktops).

Install:

    ./install

This copies the companion to ~/.local/bin and registers it with your desktop. It does not open the
microphone. Then, in the widget or extension, open Settings and choose the language you speak: Kalipso
downloads its speech model once (about 40 to 60 MB, checked against a pinned SHA-256 fingerprint). If it
says the companion is missing, log out and back in.

To download the language from the terminal at the same time: ./install --language it
(the languages: ~/.local/bin/kalipso-engine --list-models).

To remove it: rm ~/.local/bin/kalipso-engine ~/.local/share/dbus-1/services/io.github.brianidavide.Kalipso.Engine.service
and the folders ~/.local/share/kalipso and ~/.local/share/licenses/kalipso-engine.

Source code, licence (AGPL-3.0-or-later) and help: https://github.com/brianidavide/kalipso
