# Standalone app

Run `kalipso` after [building or installing it](../README.md#install). `kalipso --demo --prompter` opens a sample script without touching your saved scripts or the microphone.

## Scripts

Scripts are saved automatically as you type, together with your reading position, and reopen where you left off. Use the script picker to switch between them, and **New script**, **Import…**, **Export…** and **Delete** to manage them. See [writing scripts](scripts.md) for cues, sections and the import formats.

## The reader

**Open reader** (Ctrl+Enter) shows a frameless reading window that stays on top.

- **Move** it by dragging its title, **resize** it from the bottom-right corner. Its size and position are remembered for each monitor (on Wayland, the compositor decides the position).
- The current word is highlighted and already-read words are dimmed. The current line stays at the height chosen in **Settings → Current line position** (top, upper third or centre).
- **Settings → Font** and **Line spacing** change the reader's typeface and spacing. Any installed font works, including ones designed for easier reading such as Atkinson Hyperlegible or OpenDyslexic. Lines cut by the window's edges fade out rather than ending abruptly.
- Below the editor, the status shows the reading time of the whole script at the steady speed, as a rehearsal guide.
- **Edit** lets you change the text inside the reader; changes go straight to the script.
- **Sections** jumps to a `#` heading. Clicking a word moves the reading position there.
- While the reader scrolls or listens, its controls fade out after a moment and come back when you move the mouse, press a key or pause (Settings → Hide the reader's controls while reading). The text never moves when they do.
- In steady speed, the status shows the time left at the current speed.
- **Hide** (or Escape) closes the reader and turns the microphone off. Showing it again never restarts the microphone.

### Reading modes

Choose the mode in the reader:

| Mode | How the reader advances |
| --- | --- |
| **Manual** | Only when you step: **Previous**/**Next**, keys, foot pedal or phone. |
| **Steady speed** | One word at a time at the chosen words per minute. **Slower**/**Faster** change it while scrolling; each script remembers its own speed. An optional 3- or 5-second countdown comes first. |
| **Follow my voice** | Local speech recognition matches what you say to the script. If you go off script, the reader says so and catches up when you return — even after skipping ahead or going back. |

After a voice session, the status line summarizes it: duration, words read, pace, long pauses and recognized filler words (many recognizers drop fillers, so this is a lower bound).

### Keys, foot pedals and clickers

With the reader focused:

| Key | Action |
| --- | --- |
| → ↓ Page Down | Next word |
| ← ↑ Page Up | Previous word |
| Space, Play/Pause | Start or pause (scrolling or listening) |
| + and − | Faster / slower steady speed |
| Home | Back to the start |
| Escape | Finish editing, or hide the reader |

Most Bluetooth foot pedals and presentation clickers send Page Up/Down or arrow keys, so they work without setup.

### Phone remote

**Settings → Remote control → Allow a phone on this network to control the reader** shows a link and a QR code. Open it on a phone on the same network to start, pause, step, jump to the previous or next section and change speed. The page never receives the script text. A new secret link is created every time you turn it on, and it stops when Kalipso closes. Windows and some Linux firewalls ask whether to allow Kalipso on the network the first time; the phone must be on the same network as the computer.

### Mirroring

**Settings → Mirror text** flips the reader horizontally (beam-splitter teleprompter glass), vertically, or both. The controls stay readable.

## Screen sharing

On **Windows** 10 version 2004 or later, **Settings → Hide the reader from screen sharing and recordings** asks Windows to leave the reader out of screen captures (Microsoft's `WDA_EXCLUDEFROMCAPTURE`). It is on by default. Check what other participants see before relying on it.

On **macOS** an app cannot reliably hide its own window: Apple documents the old "do not share" window setting as unused, and screen-sharing apps built on ScreenCaptureKit capture the window anyway. Share a single window (for example your slides) instead of the whole screen.

On **Linux**, applications cannot hide their own windows from capture; the compositor decides. On **KDE Plasma 6.6 or later**, focus the reader, press Alt+F3 and choose **More Actions → Hide from Screencast**. Plasma 6.7 or later can make this permanent with a window rule (System Settings → Window Management → Window Rules) matching the window class `io.github.brianidavide.kalipso` and the title `Kalipso — reading`.

The optional KWin script keeps the reader above other windows, places it near the camera (on Wayland only the compositor can position windows) and requests the same exclusion where KWin lets scripts set it:

```sh
python3 desktop/package.py
kpackagetool6 --type KWin/Script --install artifacts/packages/kalipso-kwin.kwinscript
```

Then enable **Kalipso companion** in System Settings → Window Management → KWin Scripts.

## Speech

**Settings → Speech** chooses the engine:

- **Moonshine** or **Vosk**: install a runtime and a language model with the [local speech installer](local-engine.md); installed models then appear under **Installed language model**. Models installed by hand can be chosen with **Other model installed by hand…**.
- **System · on-device** (macOS and Windows): the operating system's recognizer. See [macOS and Windows builds](system-speech.md).

Listening only starts when you press **Start listening**, and stops when you pause, hide the reader or close Kalipso.

## Interface

**Settings** also has light and dark appearances, reader colors (including automatic colors, see [accessibility](accessibility.md)), interface text size up to 200%, reader text size, and the interface language (system, English or Italiano; applies after a restart).
