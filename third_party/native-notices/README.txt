Upstream native ASR notices, pinned 2026-10-26

ONNX Runtime 1.23.2 (Moonshine's bundled runtime):
- Source tag: microsoft/onnxruntime v1.23.2, commit a83fc4d58cb48eb68890dd689f94f28288cf2278.
- LICENSE: https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/LICENSE
  SHA-256 2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c
- ThirdPartyNotices.txt: https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/ThirdPartyNotices.txt
  SHA-256 e9e90971a8e75a9a8ac0c6412e29c1202d079998389915aa485f46c816c3b4cc
- Main ONNX Runtime license is MIT. If redistributing its shared object, retain the MIT copyright/license and its ThirdPartyNotices.txt for the third-party components included in that build.

Vosk API 0.3.45:
- Source tag: alphacep/vosk-api v0.3.45, commit cf2560c9f8a49d3d366b433fdabd78c518231bec.
- Root COPYING: https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/COPYING
  SHA-256 bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6
- Root COPYING contains Apache License 2.0. This tag has no root ThirdPartyNotices file (checked repository root and tracked paths). Preserve COPYING with the Vosk native library and include Apache 2.0 attribution/notice text in product notices as applicable. PyPI wheel metadata has an Apache classifier but its License field is blank and it contains no license file, so use the pinned source-tag COPYING.

The installer downloads upstream artifacts to the user's machine, rather than committing these binaries to the app repository. The user-local installation should retain the corresponding notice files alongside/presented with each selected backend. This note is operational guidance, not legal advice.
