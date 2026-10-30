# Releasing Kalipso

## Versions

Kalipso goes up one step at a time: 0.1, 0.2 … 0.9, 1.0, 1.1 … 1.9, 2.0. There are ten steps between
public releases:

| Version | What happens when it is tagged |
|---|---|
| 0.1 (the first) and every X.0 | Built and **published** as a GitHub release, marked latest. |
| Every other step (0.2 … 0.9, 1.1 … 1.9, …) | Built and kept as a **draft** release: the same files, visible to maintainers only. |

`tools/version.py` keeps the version the same in every place that states it (CMake, the AppStream
metadata, the Plasma widget, the GNOME extension and the KWin script); CI fails if they differ.

## Making a version

1. Check that `main` is green and that the manual checks in [manual-testing.md](manual-testing.md) that
   matter for this version have been done.
2. Write the version. For a published version, give the release date:

   ```sh
   python3 tools/version.py set "$(python3 tools/version.py next)"            # a step: draft
   python3 tools/version.py set 1.0 --stable 2027-03-01                         # a public release
   ```

3. Commit through a pull request as usual, then tag the merged commit and push the tag:

   ```sh
   git tag v1.0 && git push origin v1.0
   ```

4. The **Release** workflow builds every package on Linux, macOS and Windows, writes `SHA256SUMS`,
   attests where each file was built (`gh attestation verify <file> -R brianidavide/kalipso`), and
   creates the release: published for 0.1 and X.0, a draft otherwise. Its notes list the merged pull
   requests and explain how to open unsigned packages.

## What is in a release

- Linux: a `.deb` (Debian 13, Ubuntu 24.04 or newer) and a `.tar.gz`, both with the voice companion and
  `kalipso-voice-setup`.
- `kalipso-companion-vX.Y-linux-x86_64.tar.gz`: the companion alone, for people who use only the Plasma
  widget or the GNOME extension; `./install --language it` sets up voice following under `~/.local`.
- Windows x64: a portable `.zip` with the app and its Qt libraries.
- macOS Apple Silicon: the app bundle in a `.tar.gz`, signed ad hoc only.
- The Plasma widget (`.plasmoid`), the GNOME Shell extension (`.zip`) and the KWin script.

## Not done yet

- **Signing.** Windows and macOS packages are unsigned (issue #9). SmartScreen warns on every new
  version, and Smart App Control blocks unsigned apps outright. Options for an open-source project:
  the SignPath Foundation (free for OSI-licensed projects, on application and approval, with a GitHub
  Actions integration), or a Certum open-source certificate in the developer's name. Azure Artifact
  Signing only accepts individual developers in the United States and Canada. On macOS, Developer ID
  signing and notarization need the Apple Developer Program (99 USD a year).
- **A Windows installer** (issue #10): an installer adds little until the packages are signed.
- **Store listings** (issues #20 and #21): the Plasma widget on the KDE Store and the extension on
  extensions.gnome.org are uploaded by hand by the maintainer.

## Arch Linux (AUR)

`packaging/arch/kalipso` (published versions) and `packaging/arch/kalipso-git` (the latest `main`) are the
PKGBUILDs; CI builds `kalipso-git` from every change and installs it in an Arch container. The **AUR**
workflow publishes them: the release workflow calls it for 0.1 and every X.0 (`kalipso` gets the version
and the source fingerprint), and it can be run by hand from the Actions tab to update `kalipso-git`. It
needs the repository secret `AUR_SSH_KEY`, the private SSH key of the AUR account that maintains the
packages; without it the job only warns. The first push creates each package on AUR.
