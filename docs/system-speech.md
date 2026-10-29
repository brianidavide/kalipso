# macOS and Windows development builds

Kalipso's standalone Qt application can use a local system recognizer, Moonshine or Vosk. These ports are experimental. The Plasma widget and GNOME extension remain Linux frontends. Native builds and automated tests do not validate microphone permissions, recognition accuracy or latency on physical hardware.

## System recognition

In Settings, select **System speech**, choose a language and click **Check availability**. Start listening from the editor. Pause stops the helper process and releases its microphone. Script editing and manual navigation remain available. There is no automatic cloud fallback.

- **macOS:** requires macOS 26+, hardware supported by Apple's SpeechTranscriber, and an available language. Kalipso checks the system's language list. **Install system language** explicitly downloads Apple's speech assets; listening itself does not request a download. Recognition uses SpeechAnalyzer on the device and the default microphone. Allow microphone access when prompted. If access is denied, enable it in System Settings → Privacy & Security → Microphone.
- **Windows:** uses an installed Microsoft desktop SAPI recognizer in-process. It recognizes short phrases around the current reading position, so add a script before listening. Available languages depend on installed recognizers; Windows display-language support alone is not sufficient. Install a matching speech language through Windows settings and check again. Recognition uses the default microphone. This backend is for following a known script, not general conversation transcription.

Moonshine and Vosk remain selectable alternatives. Supply an extracted model and a compatible native library for your OS and CPU architecture; a Linux `.so` cannot be used on macOS or Windows. The existing automatic asset installation tools target Linux. These alternatives use Qt Multimedia for microphone capture on macOS/Windows. Backend settings are stored separately, and choosing an engine does not download anything.

On both systems the reader asks to be left out of screen sharing and recordings (Settings → Hide the reader from screen sharing and recordings). Check the view seen by another participant before relying on it.

## Build

Both ports require CMake 3.22+, a C++20 compiler and Qt Core, Widgets, Network, Multimedia, Test and (for translations) LinguistTools: Qt 6.10+ on macOS, Qt 6.8+ on Windows. Point CMake at Qt with `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>`. Python is optional for additional tests. Windows uses MSVC 2022 and the Windows SDK. macOS additionally requires Xcode 26+ with the macOS 26 SDK and Swift compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

On macOS, run the complete `build/kalipso.app` bundle, which includes its speech helper and microphone usage description. For local testing after deployment:

```sh
macdeployqt build/kalipso.app
codesign --force --deep --sign - --entitlements packaging/macos/entitlements.plist build/kalipso.app
open build/kalipso.app
```

This is an ad-hoc signature, not a Developer ID signature or notarization. CI archives are development artifacts, not store releases. Preserve executable permissions when unpacking the macOS artifact.

On Windows, run these from a *Developer PowerShell for VS 2022* so the compiler is found:

```powershell
cmake --install build --config Release --prefix artifacts/kalipso
windeployqt --release artifacts/kalipso/bin/kalipso.exe
```

Keep `kalipso-system-speech.exe` beside `kalipso.exe`. CI packages also contain the Qt and Microsoft C++ runtimes. The application is not Authenticode signed, so Windows SmartScreen may warn on first launch.
