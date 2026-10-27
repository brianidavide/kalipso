# Local speech: choose and install your engine

The Plasma widget and GNOME extension now offer **Moonshine** and **Vosk**. Moonshine is the preferred choice for new installations; existing Vosk configurations remain selected. Each engine has its own saved library, model, language and architecture. Changing the selection does not download anything, open the microphone or load both models. Stop listening before switching.

The widget and extension provide local teleprompting. Meeting assistance and LLM generation are outside their current scope.

## Components and store boundaries

| Component | Installed by | Purpose |
| --- | --- | --- |
| Plasma widget or GNOME extension | Development archive; desktop store after release | Script editing, reading, engine selection |
| `kalipso-engine` | Installer below or a future native package | Shared local recognition service |
| Qt 6 Core/DBus and `pw-record` | Distribution package manager | Service and microphone capture |
| Chosen native runtime | Explicit `--download`, distribution package or manual path | Moonshine + matching ONNX Runtime, **or** Vosk |
| Chosen language model | Explicit `--download` or manual path | Recognition for that language |

**A source-only store archive does not install the native companion, libraries or models.** The frontend's **Local speech setup** button opens this document; it is not a one-click installer. Missing components are reported before microphone capture. Users can install one engine or both, then select the one they want.

The runtime path is C++/Qt Core/DBus → PipeWire PCM → the selected native C API. Python is installation/test tooling only. No Python process, webview, HTTP audio upload or WAV spool is involved in listening. Moonshine's ORT sessions use serial execution; only the selected model is loaded on Start and released on Stop.

## Install on EndeavourOS / Arch

From a checkout of this repository, install the source-build prerequisites:

```sh
sudo pacman -S --needed base-devel cmake qt6-base pipewire pipewire-audio pkgconf python
```

Choose **one** of these examples. The language is explicit and is never inferred from the desktop locale:

```sh
# Moonshine Tiny Streaming, English:
python3 tools/install-local-engine.py --backend moonshine --language en --download

# Vosk small, Italian:
python3 tools/install-local-engine.py --backend vosk --language it --download
```

The installer builds the companion, verifies SHA-256 hashes, installs only the selected native runtime and language model under `~/.local`, saves that engine's profile, and registers session D-Bus activation. It does not open the microphone. Repeating installation for another backend preserves the first profile. Stop the running companion before replacing an installed executable; the configuration command rejects changes while listening.

Automatic downloads currently use these pinned, tested Linux x86_64 packages:

- Moonshine Voice 0.1.5: native library plus its matching ONNX Runtime library, with their relative directory layout and license notices. Requires glibc 2.34 or newer. The Python package's code, dependencies, TTS assets and recordings are not installed.
- Vosk 0.3.45: native library and upstream license. The version is pinned by the installer, not advertised as the newest Vosk release.
- Download catalog: Moonshine Tiny in `en`, `de`, `es`, `ja`, `zh`; Vosk small in those five plus `it`. Run `python3 tools/install-local-engine.py --list-models` to inspect it. This small verified download catalog is not a statement that the runtimes support only these languages. Other compatible models can be configured manually.

**There is no official Italian Moonshine model in this catalog.** Creating one is an experimental training project. The installer rejects that unsupported combination instead of downloading another language. Use Vosk for Italian today; recognition quality depends on the model and recording conditions.

Downloads require the explicit `--download` flag. Their sources, versions, file hashes and model licenses are in [the catalog](../tools/data/speech-catalog.json). Interrupted or failed-hash downloads are not accepted as installed files. Existing files are verified on reinstall. Downloads are cached in `~/.cache/kalipso-downloads`; use `--cache` and `--prefix` to relocate them. Python 3.11+ is required for this installer.

## Existing libraries/models or another architecture

Use a distribution-provided runtime or a compatible native build and an extracted model:

```sh
python3 tools/install-local-engine.py --backend vosk --language it \
  --library /usr/lib/libvosk.so --model /absolute/path/to/vosk-model-small-it-0.22

python3 tools/install-local-engine.py --backend moonshine --language de \
  --library /absolute/path/to/moonshine_voice/libmoonshine.so \
  --model /absolute/path/to/tiny-streaming-model --architecture 2
```

Arch offers [`vosk-api`](https://archlinux.org/packages/extra/x86_64/vosk-api/); its model is a separate download. Vosk folders must contain `am/final.mdl` and `conf/mfcc.conf`. Moonshine needs its complete streaming model folder, including tokenizer, frontend graph/weights, encoder, adapter, decoder and streaming configuration. Keep the matching ONNX Runtime shared library beside the runtime in the publisher's expected layout. Do not point at a ZIP, an offline Moonshine checkpoint or an unconverted training checkpoint.

Moonshine architecture IDs are 2 Tiny Streaming, 3 Base Streaming, 4 Small Streaming and 5 Medium Streaming. This integration has executed Tiny; larger architectures require their own resource and quality validation. `--language ja` / `zh` also selects the publisher's higher token-rate guard for those languages. Specify the actual model language.

On Debian, the build/capture prerequisites are `build-essential`, `cmake`, `qt6-base-dev`, `libpipewire-0.3-dev`, `pkg-config`, `pipewire-bin` and Python 3.11+. The companion requires Qt Core/Gui/DBus even on GNOME; these are explicit dependencies, not assumed GNOME components. Automatic native downloads are Linux x86_64 only; supply compatible libraries manually for other platforms.

## Manual companion build without the Python installer

```sh
cmake -S . -B build-local-engine -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-local-engine --target kalipso-engine --parallel 2
cmake --install build-local-engine --component Engine
"$HOME/.local/bin/kalipso-engine" --configure --backend vosk --language it \
  --library /usr/lib/libvosk.so --model /absolute/path/to/extracted-model
```

Use the same prefix at CMake configuration and installation: the D-Bus service file contains the configured executable path. Reconfigure before changing prefixes.

## Check, select and listen

```sh
"$HOME/.local/bin/kalipso-engine" --list-backends
"$HOME/.local/bin/kalipso-engine" --check
```

The check returns JSON with selected backend, language, library/model paths and missing dependencies. It checks symbols/files without loading weights or opening the microphone; `modelLoadTested:false` is intentional. Start performs the actual model load and reports incompatibility through the frontend.

Build/install the development frontends:

```sh
python3 desktop/package.py
kpackagetool6 --type Plasma/Applet --install artifacts/packages/kalipso-plasma.plasmoid
gnome-extensions install --force artifacts/packages/kalipso-gnome.zip
```

These are development packages, not store-approved releases. GNOME declares Shell 48 because that is the version executed. Select **Moonshine** or **Vosk**, then **Start listening**. Stop before switching. The other frontend cannot take over an active microphone session. Changing the backend clears old recognition events; each frontend consumes every new event once, including multiple completed lines delivered in one update.

If D-Bus activation is not discovered, or an older companion is still running after an update, log out and back in. For diagnosis, run `~/.local/bin/kalipso-engine --serve` in a terminal, then start from the frontend.

## Lifecycle and current limits

Stop cancels capture and unloads model/recognizer state. The empty companion exits after 90 seconds without client calls; a disconnected frontend stops capture and an abandoned lease expires after ten seconds. The worker queue is bounded to two seconds of pending audio, stopping with an error rather than growing indefinitely when recognition cannot keep up.

Moonshine is configured not to retain completed audio. Its native transcript history rotates after 64 completed lines at a detected quiet boundary, draining pending audio before restart. If no suitable quiet boundary is reached by 256 completed lines, recognition stops with a restart message. Long-session behavior and unusual speech-boundary cases need broader target-machine tests; the unit lifecycle test is not a long-duration acoustic validation.

Numeric forms such as `100%` and `one-hundred percent` are not normalized to the same script tokens. Whisper is still a benchmark candidate and is **not** an available runtime selection. Screen-share invisibility is unverified.

Change a profile or input while stopped:

```sh
"$HOME/.local/bin/kalipso-engine" --configure --backend moonshine \
  --language en --model /path/to/another-compatible-streaming-model --architecture 2
"$HOME/.local/bin/kalipso-engine" --configure --microphone 'PIPEWIRE_NODE_NAME'
"$HOME/.local/bin/kalipso-engine" --configure --microphone ''
```

An optional second audio channel is available through `--meeting-target` / `--meeting-enabled true|false`, but its UI and AI features are outside this MVP. It never advances the microphone-driven script.

## Removal

Stop listening and disable/remove the frontend. Remove the companion executable, D-Bus activation file and companion license from your chosen prefix. Configuration normally resides in `~/.config/Kalipso/engine.conf` (or `XDG_CONFIG_HOME`).

Downloaded components belong to `~/.local/share/kalipso/runtimes/` and `~/.local/share/kalipso/models/`; each backend/version has its own directory. Delete only the backend/model directories you no longer need. Removing a widget does not remove potentially shared models. Manually supplied models and system packages remain owned by their original installation method.

The Plasma **Choose background window…** action also uses this companion. It requires a running PipeWire service and a window-capable desktop portal (`xdg-desktop-portal-kde` on KDE). It does not require a speech model or microphone access. Desktop-theme, Light and Dark colors remain available without the companion. See [window color permissions and limits](accessibility.md#window-colors-on-kdewayland).
