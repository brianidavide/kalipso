# Install Kalipso for your desktop

Build the source archives from the repository root:

```sh
python3 tools/generate-design-tokens.py
python3 desktop/package.py
```

## KDE Plasma

Use “Install Widget From Local File” in the widget chooser, or:

```sh
kpackagetool6 --type Plasma/Applet --install artifacts/packages/kalipso-plasma.plasmoid
```

For an existing Kalipso installation, use `--upgrade` instead of `--install`. Add **Kalipso** and position the widget near your webcam. The desktop host controls its placement.

To uninstall:

```sh
kpackagetool6 --type Plasma/Applet --remove io.github.brianidavide.kalipso
```

## GNOME Shell 48

```sh
gnome-extensions install --force artifacts/packages/kalipso-gnome.zip
```

Log out and back in, then enable the extension:

```sh
gnome-extensions enable kalipso@brianidavide.github.io
```

The panel play icon shows or hides the reader. Hiding or disabling it stops its microphone session. Other Shell versions are not yet declared in the metadata.

To uninstall:

```sh
gnome-extensions disable kalipso@brianidavide.github.io
gnome-extensions uninstall kalipso@brianidavide.github.io
```

## Reading and editing

Start in **Manual**, open **Edit script**, and paste your text. Changes appear immediately. **Next words** and **Previous** move through the script; **Reset** returns to the beginning. Keep a separate copy: the script lasts for the current session.

For voice following, install the [local speech companion and a language model](../docs/local-engine.md), choose **Settings → Follow my voice**, select **Moonshine** or **Vosk**, and press **Start listening**. Pause releases capture and keeps your place. Stop before changing engines. Choosing an engine in Settings does not install it.

These are development packages, not store releases. Check screen sharing from another participant: the reader may be visible.

## Earlier development installations

Disable and remove earlier development widgets/extensions before enabling Kalipso; the new application has its own package identifiers. The companion imports an existing speech configuration on first use when its new configuration is empty. Existing model paths are preserved. Removing a frontend does not remove the companion or models.
