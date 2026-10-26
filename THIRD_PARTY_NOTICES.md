# Third-party notices

The generated character classification tables in `desktop/unicode-tokens.mjs`
and its Plasma package and website copies derive from Unicode Character Database 15.1.0 through Python
3.13's `unicodedata`. Unicode data © 1991–2026 Unicode, Inc.; distributed under
[Unicode License v3](LICENSES/Unicode-3.0.txt). The generation and tokenization code written
for this project remains AGPL-3.0-or-later. Python is used only for generation, not by either
frontend at runtime.

Moonshine, ONNX Runtime, Vosk, language models, Qt and PipeWire are separate runtime dependencies and retain their
own licenses. No speech model, Vosk shared library, audio recording or Whisper binary is
bundled in the source-only store archives. See [local installation](docs/local-engine.md).

The native Moonshine adapter includes the unmodified MIT C ABI header from v0.1.5;
[provenance and license](third_party/moonshine/README.md). Pinned native-runtime
[notices](third_party/native-notices/README.txt) are copied into user-local installs.
The optional installer downloads upstream wheels and extracts only the native
libraries and their notices; no third-party runtime/model binary is stored in Git.
