# macOS and Windows development builds

Kalipso's standalone Qt application can use a local system recognizer, Moonshine or Vosk. These ports are experimental. The Plasma widget and GNOME extension remain Linux frontends. Native builds and automated tests do not validate microphone permissions, recognition accuracy or latency on physical hardware.

## System recognition

In Settings, select **System speech**, choose a language and click **Check availability**. Start listening from the editor. Pause stops the helper process and releases its microphone. Script editing and manual navigation remain available. There is no automatic cloud fallback.

- **macOS:** requires macOS 26+, hardware supported by Apple's SpeechTranscriber, and an available language. Kalipso checks the system's language list. **Install system language** explicitly downloads Apple's speech assets; listening itself does not request a download. Recognition uses SpeechAnalyzer on the device and the default microphone. Allow microphone access when prompted. If access is denied, enable it in System Settings → Privacy & Security → Microphone.
- **Windows:** uses an installed Microsoft desktop SAPI recognizer in-process. It recognizes short phrases around the current reading position, so add a script before listening. Available languages depend on installed recognizers; Windows display-language support alone is not sufficient. Install a matching speech language through Windows settings and check again. Recognition uses the default microphone. This backend is for following a known script, not general conversation transcription.

Moonshine and Vosk remain selectable alternatives. Supply an extracted model and a compatible native library for your OS and CPU architecture; a Linux `.so` cannot be used on macOS or Windows. The existing automatic asset installation tools target Linux. These alternatives use Qt Multimedia for microphone capture on macOS/Windows. Backend settings are stored separately, and choosing an engine does not download anything.

These builds do not guarantee exclusion from screen sharing. Verify the view seen by another participant before using the overlay in a call.

## Build

Both ports require CMake 3.22+, a C++20 compiler and Qt Core, Widgets, Network, Multimedia and Test: Qt 6.10+ on macOS, Qt 6.8+ on Windows. Python is optional for additional tests. Windows uses MSVC 2022 and the Windows SDK. macOS additionally requires Xcode 26+ with the macOS 26 SDK and Swift compiler.

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

On Windows:

```powershell
cmake --install build --config Release --prefix artifacts/kalipso
windeployqt --release artifacts/kalipso/bin/kalipso.exe
```

Keep `kalipso-system-speech.exe` beside `kalipso.exe`. CI packages Qt runtime dependencies. The application is not Authenticode signed.

Before release, test each native backend on physical hardware: first-run permission grant and denial, missing/installed language assets, offline reading for at least ten minutes, pause/restart, script edits and seeking during speech, microphone removal, and comparison of CPU, memory and recognition accuracy against Moonshine/Vosk with the same recording and language. Native APIs alone do not establish which engine uses fewer resources.
