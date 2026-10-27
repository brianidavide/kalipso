# Kalipso

An open-source, local-first teleprompter for **KDE Plasma and GNOME**.

Edit your script while reading, keep the next words near your camera, and let local speech recognition follow your voice. Already-read words dim as you speak.

[Website](https://brianidavide.github.io/kalipso/) · [Install the widget or extension](desktop/README.md) · [Set up local speech](docs/local-engine.md)

## Features

- KDE Plasma widget and GNOME Shell extension.
- Live script editing, manual advance, previous words and reset.
- Optional local voice following with selectable Moonshine and Vosk engines.
- Separate language/model profiles; only the selected model loads while listening.
- Pause releases the microphone and preserves the reading position.

**Development preview.** No store release is available yet. GNOME metadata currently targets Shell 48. The Plasma frontend has been exercised with Plasma 6.3 and Qt 6.8; other desktop versions need testing. Scripts in the widget/extension last for the session, so keep a separate copy. The reader may appear in screen sharing.

The widget and extension work manually without speech components. Voice following requires the separately installed native companion, runtime and language model. Selecting an engine does not download it. The download catalog includes Moonshine models in English, German, Spanish, Japanese and Chinese; Vosk also includes Italian. Interface text is currently English.

Experimental standalone ports for macOS and Windows also provide selectable local system recognition. See [requirements and native build instructions](docs/system-speech.md); these are not store releases.

[Accessibility settings and verification status](docs/accessibility.md) describe the WCAG 2.2 AA target, opaque contrast palettes and platform-specific automatic color sources. Full AA conformance is not yet declared.

## Build on Linux

Requires a C++20 compiler, CMake 3.22+, Qt 6.4+ Core/DBus/Widgets/Network, and Qt Test for tests. Python 3.11+ is used by installation and test tools; Node.js runs frontend tests.

```sh
# Arch / EndeavourOS
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-wayland pipewire-audio python nodejs

# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-wayland pipewire-bin python3 nodejs

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
node desktop/tracker.test.mjs
python3 desktop/package.py
```

Frontend archives are written to `artifacts/packages/`. See [desktop installation](desktop/README.md) for installation and removal.

The optional standalone Qt editor can be started with `./build/kalipso`. Its experimental conversation/LLM tools are separate from the desktop teleprompter MVP; [standalone usage](docs/standalone.md) describes their data flow. For a user-local installation, configure with `-DCMAKE_INSTALL_PREFIX="$HOME/.local"`, rebuild, then run `cmake --install build`. The configured prefix also determines the companion's D-Bus activation path.

## Development

`desktop/` contains the widget, extension and shared tracker; `src/` contains native code; `tests/` and `benchmarks/` contain executable checks. `site/` is the static website. `design/tokens.json` supplies shared visual values; regenerate derived sources with `python3 tools/generate-design-tokens.py`.

For browser checks, serve `site/` on port 8770 and run `PLAYWRIGHT=/path/to/playwright/index.mjs node tools/check-design.mjs` with Playwright and its Chromium, Firefox and WebKit browsers installed separately.

Generated packages, test output, recordings, models, local configuration and working notes are excluded from Git. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Original code is **AGPL-3.0-or-later**. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md). AppStream metadata is CC0-1.0. Dependencies and models retain their own licenses.

Kalipso is an independent project and is not affiliated with KDE or GNOME.
