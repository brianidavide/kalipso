# Install the Plasma widget or GNOME extension

From a clone of the repository, build both archives:

```sh
git clone https://github.com/brianidavide/kalipso.git
cd kalipso
python3 desktop/package.py
```

The archives are written to `artifacts/packages/`.

## KDE Plasma 6

Use **Install Widget From Local File** in the widget chooser, or:

```sh
kpackagetool6 --type Plasma/Applet --install artifacts/packages/kalipso-plasma.plasmoid
```

Use `--upgrade` instead of `--install` to update an existing installation. Add **Kalipso** to the desktop or a panel and place it near your webcam. To uninstall:

```sh
kpackagetool6 --type Plasma/Applet --remove io.github.brianidavide.kalipso
```

## GNOME Shell 48

```sh
gnome-extensions install --force artifacts/packages/kalipso-gnome.zip
```

Log out and back in, then enable it:

```sh
gnome-extensions enable kalipso@brianidavide.github.io
```

The play icon in the top bar shows or hides the reader. The extension declares GNOME Shell 48, the version it is tested with; GNOME refuses to enable it on other versions. To uninstall:

```sh
gnome-extensions uninstall kalipso@brianidavide.github.io
```

## Using the reader

- **Edit script** to paste or write your text. It is saved automatically, with your reading position, and comes back after a restart.
- `[Bracketed cues]` are shown in italics but never read by voice following. Lines starting with `#` are sections: use **Sections** (Plasma) or **‹ Section / Section ›** (GNOME) to jump between them.
- **Settings** chooses how the reader advances:
  - **Manual**: **Next words** and **Previous** move four words; **Reset** returns to the start.
  - **Steady speed**: scrolls word by word at the chosen words per minute, after an optional 3- or 5-second countdown (**Countdown before steady scrolling**).
  - **Follow my voice**: needs the [local speech companion](../docs/local-engine.md). **Settings → Language you speak** then downloads your language's speech model once, with its progress and **Cancel download**.
- **Mirror** flips the text for beam-splitter teleprompter glass.
- **Reader font** and **Line spacing**: the interface font, a serif, or a font designed for easier reading (Atkinson Hyperlegible, OpenDyslexic, Lexend) when it is installed; normal, relaxed or wide lines.
- Keys (for foot pedals and clickers): arrows or Page Up/Down step, Space starts or pauses, + and − change the steady speed, Home returns to the start.

The interface follows your desktop language (English or Italian). Hiding the reader stops the microphone. The reader can appear in screen sharing; check what other participants see.
