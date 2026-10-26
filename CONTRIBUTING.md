# Contributing

Build and run the tests in README before proposing changes. Keep original code under AGPL-3.0-or-later and retain third-party notices. Contributors retain their copyrights; no copyright assignment is requested.

Keep the repository focused on source code, test fixtures, build/distribution configuration, license notices and installation documentation. Store working notes, research captures, generated reports, packages and benchmark output outside Git, under `artifacts/` when needed. Do not commit meeting recordings, transcripts, API keys, private documents, models or machine-specific configuration.

Use synthetic test fixtures. Describe reproducible failures with the relevant desktop, Qt and audio backend versions. Changes to speech processing should be checked for alignment, latency, resource use and start/stop behavior. Design values belong in `design/tokens.json`; regenerate derived sources using `python3 tools/generate-design-tokens.py`.
