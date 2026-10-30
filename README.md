# Kalipso

A local-first teleprompter for your desktop. Keep your script next to your camera, read naturally, and let Kalipso follow your voice — speech recognition runs on your device.

[Website](https://brianidavide.github.io/kalipso/) · [Writing scripts](docs/scripts.md) · [Standalone app](docs/standalone.md) · [Plasma and GNOME](desktop/README.md) · [Voice following](docs/local-engine.md)

> **Development preview.** There is no store listing or signed installer yet; you build from source or use the draft packages attached to [releases](https://github.com/brianidavide/kalipso/releases).

## What it does

- **Three ways to advance:** follow your voice, scroll at a steady speed (words per minute, with a countdown), or step manually with the keyboard, a foot pedal, a presentation clicker or your phone.
- **Live editing:** change the script while you read; your place is kept.
- **Your scripts are kept:** saved automatically on your device with your reading position.
- **Script cues:** `[look at camera]` is shown but never read by voice following; `# Heading` lines become sections you can jump to. See [writing scripts](docs/scripts.md).
- **Import:** speaker notes from PowerPoint (`.pptx`) and LibreOffice Impress (`.odp`), one section per slide; Word (`.docx`) and Writer (`.odt`) documents with their headings, lists and emphasis.
- **Presenting with LibreOffice Impress:** the reader follows the running slideshow and moves to each slide's notes (Linux; Windows and macOS not yet verified).
- **Teleprompter rigs:** mirror the text horizontally or vertically for beam-splitter glass.
- **Readable everywhere:** light, dark or automatic colors with WCAG AA contrast, large text and full keyboard control. See [accessibility](docs/accessibility.md).
- **Private by design:** recognition runs locally (Moonshine, Vosk, or the operating system's on-device recognizer on macOS and Windows). Nothing is uploaded.

## Choose your version

| Version | Runs on | Best for |
| --- | --- | --- |
| [Standalone app](docs/standalone.md) | Linux, Windows x64, Apple Silicon Macs (macOS 14.4+) | The full feature set: script library, import, movable reader, phone remote |
| [Plasma widget](desktop/README.md#kde-plasma-6) | KDE Plasma 6 | A reader on your desktop or panel |
| [GNOME extension](desktop/README.md#gnome-shell-48) | GNOME Shell 48 | A reader under the top bar |

All three work without speech components (manual and steady modes). Voice following on Linux needs the [local speech companion and a language model](docs/local-engine.md).

## Install

### Arch Linux and EndeavourOS

The package builds the app, the voice companion, the Plasma widget, the GNOME Shell extension and the KWin
script in one go:

```sh
git clone https://github.com/brianidavide/kalipso.git
cd kalipso/packaging/arch/kalipso-git
makepkg -si
kalipso-voice-setup --language it    # once, for voice following: your language (it, en, de, es, ja, zh)
```

Then open **Kalipso** from the application menu, or add the **Kalipso** widget to the desktop or panel
(right click → Add or Manage Widgets). On AUR the packages will be `kalipso` (published versions) and
`kalipso-git` (the latest changes).

### Debian 13 / Ubuntu 24.04 or newer

```sh
sudo apt install build-essential cmake ninja-build git qt6-base-dev qt6-svg-dev qt6-l10n-tools qt6-tools-dev libpipewire-0.3-dev pkg-config pipewire-bin
git clone https://github.com/brianidavide/kalipso.git
cd kalipso
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
~/.local/bin/kalipso-voice-setup --language en    # once, for voice following
```

Run `kalipso`, or try it with a sample script: `kalipso --demo --prompter`. To build a `.deb` instead, run
`cd build && cpack -G DEB`. Published versions will have the `.deb` ready on the releases page.

### Windows and macOS

Download the `.zip` (Windows) or `.tar.gz` (macOS) from the [releases page](https://github.com/brianidavide/kalipso/releases)
when a version is published, unpack it and start Kalipso. The packages are not signed yet: Windows
SmartScreen asks you to confirm (*More info → Run anyway*); on macOS open it once, then choose *Open
Anyway* in System Settings → Privacy & Security. Voice following uses the speech recognition built into
the system; see [macOS and Windows builds](docs/system-speech.md), also for building them yourself.

### Plasma widget and GNOME extension only

Install the widget from the KDE Store or the extension from extensions.gnome.org (when listed), or build
them: `python3 desktop/package.py`, then follow [installing the widget or extension](desktop/README.md).
For voice following they need the **companion**: download `kalipso-companion` from the
[releases page](https://github.com/brianidavide/kalipso/releases), unpack it and run
`./install --language it` (your language). See [voice following on Linux](docs/local-engine.md).

## Limitations

- **Screen sharing:** on Windows the standalone app asks the system to keep the reader out of screen shares and recordings; KDE Plasma 6.6+ can hide it too ([how](docs/standalone.md#screen-sharing)). macOS offers no reliable way for an app to do this, so share a single window there. Always check what other participants see.
- **Desktop versions:** the GNOME extension declares Shell 48; the Plasma widget is tested with Plasma 6.3.
- **Speech models:** the download catalog has Moonshine models for English, German, Spanish, Japanese and Chinese, and Vosk models for those plus Italian. Other compatible models can be configured by hand.
- **Interface languages:** English and Italian.

## License

Kalipso is free software under the **AGPL-3.0-or-later** ([LICENSE](LICENSE)); third-party components keep their own licenses ([notices](THIRD_PARTY_NOTICES.md)). AppStream metadata is CC0-1.0. Kalipso is an independent project, not affiliated with KDE or GNOME.

Contributing, tests and the release process: [CONTRIBUTING.md](CONTRIBUTING.md). Resource use and latency: [performance](docs/performance.md).
