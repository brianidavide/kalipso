# Kalipso

An open-source, local-first desktop teleprompter: **KDE Plasma and GNOME**, plus a standalone Qt app with experimental **Apple Silicon macOS and Windows x64** builds.

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
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-wayland pipewire pipewire-audio pkgconf python nodejs

# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-wayland pipewire-bin libpipewire-0.3-dev pkg-config python3 nodejs

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
node desktop/tracker.test.mjs
python3 desktop/package.py
```

Frontend archives are written to `artifacts/packages/`. See [desktop installation](desktop/README.md) for installation and removal.

The optional standalone Qt editor can be started with `./build/kalipso`. Its first-release interface focuses on the script, reader and local speech; deferred conversation/LLM tools are not exposed. See [standalone usage](docs/standalone.md). For a user-local installation, configure with `-DCMAKE_INSTALL_PREFIX="$HOME/.local"`, rebuild, then run `cmake --install build`. The configured prefix also determines the companion's D-Bus activation path.

## Development

`desktop/` contains the widget, extension and shared tracker; `src/` contains native code; `tests/` and `benchmarks/` contain executable checks. `site/` is the static website. `design/tokens.json` supplies shared visual values; regenerate derived sources with `python3 tools/generate-design-tokens.py`.

Build-tool versions are pinned in `tools/dependency-versions.json`: Qt 6.12.0, Node 26.12.1, Python 3.15.2 and CMake 4.4.4. CI tests the latest stable Qt on Linux/macOS/Windows and retains a distribution-Qt compatibility lane on Linux. These build versions are separate from the minimum source-build requirements above. GitHub Actions are pinned to verified commits; Dependabot checks Actions and browser tools weekly. `python3 tools/check-dependencies.py` and its scheduled workflow report newer stable build tools without silently changing release inputs. Moonshine is at 0.1.5; Vosk remains on the latest downloadable 0.3.45 wheel until a newer source build is packaged and verified ([tracking issue](https://github.com/brianidavide/kalipso/issues/26)).

For browser checks, run `npm ci --prefix tools/browser` and `tools/browser/node_modules/.bin/playwright install --with-deps`. Then serve `site/` on port 8770 and run `PLAYWRIGHT=/path/to/playwright/index.mjs node tools/check-design.mjs` with Playwright and its Chromium, Firefox and WebKit browsers installed separately.

The [delivery plan](https://github.com/brianidavide/kalipso/issues/25) tracks native packages, real-device qualification, presentation-note integration and store publication. The Plasma widget is intended for [KDE Store](https://store.kde.org/browse/); the GNOME extension for extensions.gnome.org. Flathub is not part of the planned distribution path.

Generated packages, test output, raw recordings, models, local configuration and working notes are excluded from Git. Curated product captures in `site/assets/media/` are website assets. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Original code is **AGPL-3.0-or-later**. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md). AppStream metadata is CC0-1.0. Dependencies and models retain their own licenses.

Kalipso is an independent project and is not affiliated with KDE or GNOME.

On KDE/Wayland, **Choose background window…** follows the colors of a window selected through the desktop sharing dialog. Install `xdg-desktop-portal` and `xdg-desktop-portal-kde` for this optional mode. See [color sources and sharing controls](docs/accessibility.md#window-colors-on-kdewayland).
