# Voice following on Linux

Voice following needs three things on your computer:

| Component | What it does | How you get it |
| --- | --- | --- |
| `kalipso-engine` (the companion) | Captures the microphone and runs recognition for the widget and extension | Included in the Kalipso packages, in the `kalipso-companion` download, or built from source |
| A speech runtime | Moonshine (with its ONNX Runtime) **or** Vosk | Downloaded by the installer, or a library you already have |
| A language model | Recognition for one language | Downloaded by the installer, or a model folder you already have |

The standalone app runs recognition itself but uses the same runtimes and models, and lists the ones installed here automatically.

Recognition stays on your computer: audio goes from PipeWire to the native library and nowhere else. Only the selected model is loaded, and only while you are listening.

## Install

Choose the way that matches how you installed Kalipso. Each one asks for your language explicitly (it is
never guessed from the desktop), downloads one speech runtime and one language model, checks them
against pinned SHA-256 fingerprints and never opens the microphone. Moonshine is used where it has the
language, Vosk otherwise (Italian uses Vosk); add `--backend vosk` or `--backend moonshine` to choose.

**You installed a Kalipso package** (Arch, EndeavourOS, the `.deb`): the companion is already there.

```sh
kalipso-voice-setup --language it
```

**You use only the widget or the extension** (from the KDE Store or extensions.gnome.org): download
`kalipso-companion-…-linux-x86_64.tar.gz` from the [releases page](https://github.com/brianidavide/kalipso/releases),
then:

```sh
tar -xzf kalipso-companion-*-linux-x86_64.tar.gz
cd kalipso-companion-*-linux-x86_64
./install --language it
```

It installs the companion under `~/.local` and registers it with your desktop. It needs Python 3, Qt 6 and
PipeWire, which current Plasma and GNOME desktops have.

**From the source code** (a clone of this repository, a C++ compiler, CMake, Qt 6 and PipeWire): the
installer builds the companion first.

```sh
# Debian 13 / Ubuntu 24.04 or newer
sudo apt install build-essential cmake git qt6-base-dev libpipewire-0.3-dev pkg-config pipewire-bin python3
# Arch / EndeavourOS
sudo pacman -S --needed base-devel cmake git qt6-base pipewire pipewire-audio pkgconf python

git clone https://github.com/brianidavide/kalipso.git
cd kalipso
python3 tools/install-local-engine.py --language it --download
```

Run it again with another language or engine to add it; each engine keeps its own settings. `--list-models`
shows the catalog.

| Engine | Version | Catalog languages | Platform |
| --- | --- | --- | --- |
| Moonshine (Tiny streaming) | 0.1.5 | en, de, es, ja, zh | Linux x86_64, glibc 2.34+ |
| Vosk (small models) | 0.3.45 | en, de, es, ja, zh, it | Linux x86_64 |

There is no Italian Moonshine model; Vosk is used for Italian. Sources, hashes and model licenses are in [the catalog](../tools/data/speech-catalog.json). Downloads are cached in `~/.cache/kalipso-downloads` (`--cache` and `--prefix` change the locations).

## Use it

In the widget or extension: **Settings → Follow my voice**, choose **Moonshine** or **Vosk**, then **Start listening**. Pause releases the microphone and keeps your place. Stop listening before switching engine; only one frontend can listen at a time.

If the widget says the companion is missing right after installing, log out and back in so D-Bus discovers it. To see what is happening, run `~/.local/bin/kalipso-engine --serve` in a terminal and start listening.

## Your own runtime or model

```sh
python3 tools/install-local-engine.py --backend vosk --language it \
  --library /usr/lib/libvosk.so --model /path/to/vosk-model-small-it-0.22

python3 tools/install-local-engine.py --backend moonshine --language de \
  --library /path/to/moonshine_voice/libmoonshine.so \
  --model /path/to/tiny-streaming-model --architecture 2
```

- A Vosk model folder contains `am/final.mdl` and `conf/mfcc.conf`. Arch packages the runtime as [`vosk-api`](https://archlinux.org/packages/extra/x86_64/vosk-api/).
- A Moonshine model is a complete *streaming* model folder (tokenizer, frontend, encoder, adapter, decoder and `streaming_config.json`); keep the ONNX Runtime library next to `libmoonshine`. Architectures: 2 Tiny, 3 Base, 4 Small, 5 Medium. Only Tiny is in the catalog and has been exercised here.

Without the Python installer:

```sh
cmake -S . -B build-engine -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-engine --target kalipso-engine
cmake --install build-engine --component Engine
~/.local/bin/kalipso-engine --configure --backend vosk --language it \
  --library /usr/lib/libvosk.so --model /path/to/model
```

Configure and install with the same prefix: the D-Bus service file records the executable's path.

## Check and change settings

```sh
~/.local/bin/kalipso-engine --list-backends   # engines and their saved models
~/.local/bin/kalipso-engine --check           # what is missing, as JSON
~/.local/bin/kalipso-engine --configure --microphone 'PIPEWIRE_NODE_NAME'   # '' for the default
```

`--check` looks for the library symbols and model files without loading the model or opening the microphone. Settings can only change while nobody is listening.

## Behaviour and limits

- Stopping cancels capture and unloads the model. The companion exits after 90 seconds unused; if a frontend disappears, its session ends within ten seconds.
- If recognition falls more than two seconds behind, listening stops with a message instead of buffering without limit.
- Numbers are matched as words: `100%` in the script and "one hundred percent" spoken do not match.
- The companion also serves **Choose background window…** in the Plasma widget, which needs PipeWire and a window-capable desktop portal (`xdg-desktop-portal-kde` on KDE) but no speech model. See [window colors](accessibility.md#window-colors-on-kdewayland).

## Remove

Stop listening and remove the widget or extension first. Then delete:

```sh
rm ~/.local/bin/kalipso-engine
rm ~/.local/share/dbus-1/services/io.github.brianidavide.Kalipso.Engine.service
rm -r ~/.local/share/licenses/kalipso-engine
rm -r ~/.config/Kalipso                       # engine settings
rm -r ~/.local/share/kalipso/runtimes ~/.local/share/kalipso/models   # downloaded speech files
rm -r ~/.cache/kalipso-downloads              # download cache
```

`~/.local/share/kalipso/gnome-extension.json` holds the GNOME extension's script; keep it if you want your script back later. Libraries and models you supplied yourself are not touched.
