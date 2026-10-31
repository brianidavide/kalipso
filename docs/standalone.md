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

After a voice session, the status line summarizes it: duration, words read, pace, long pauses and recognized filler words (many recognizers drop fillers, so this is a lower bound). **More → Save session report…** saves the same as a Markdown file, with the time and pace of each section and the words just before each long pause. Nothing is saved unless you choose to. Filler counting can be turned off in **Settings → Speech**.

**Settings → Speech → Microphone** lists the microphones connected now. **System microphone** names the one the system uses and follows the system's choice when it changes. A microphone you choose stays the one: if it is unplugged, Kalipso says so instead of switching to another. **Test microphone** opens it for up to 30 seconds; a bar moves with your voice and the note says whether Kalipso hears you. Nothing is recorded or sent. While listening, the reader names the microphone and, when no sound arrives from it for a few seconds (muted or unplugged), says so and shows its controls again.

**Settings → Speech → Voice commands** (off by default) lets you move without touching anything while Kalipso follows your voice: say "go next" or "go back" for the next or previous section and "go start" to return to the beginning (in Italian "vai avanti", "torna indietro", "da capo"). A command counts only when you say it on its own, and never when the script itself continues with those words, so reading them aloud still follows the script.

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

**Settings → Keys and pedals** gives any action another key (for example the key a foot pedal sends), adds keys for the previous and next section, or gives a built-in key a different action, such as the arrows changing the speed. A key belongs to one action at a time; clearing it restores the built-in keys. Escape always hides the reader.

Most Bluetooth foot pedals and presentation clickers send Page Up/Down or arrow keys, so they work without setup.

### Presenting with LibreOffice Impress

**More → Present with Impress…** imports the speaker notes of an `.odp` or `.pptx` presentation as a new script, opens the presentation in LibreOffice Impress, and follows the slideshow: when Impress shows a slide, the reader moves to the start of that slide's notes. It matches the notes' first line, not the slide number, so edits to the script and slides without notes are fine. The status line says what is happening ("Following Impress · slide 3", "slide 2 has no notes", "The slideshow ended").

Kalipso talks to LibreOffice only through a local named pipe that it asks LibreOffice to open for this presentation; nothing goes over the network, and the presentation is never changed. **Stop following Impress** (same menu) ends it; LibreOffice stays open. On Linux this needs LibreOffice's Python bridge (`python3-uno` on Debian and Ubuntu). It is tested on Linux with LibreOffice 25.2; Windows and macOS use the Python bundled with LibreOffice but have not been verified yet.

### Phone remote

**Settings → Remote control → Allow a phone on this network to control the reader** shows a link and a QR code. Open it on a phone on the same network to start, pause, step, jump to the previous or next section and change speed. The page never receives the script text. A new secret link is created every time you turn it on, and it stops when Kalipso closes. Windows and some Linux firewalls ask whether to allow Kalipso on the network the first time; the phone must be on the same network as the computer.

### Mirroring

**Settings → Mirror text** flips the reader horizontally (beam-splitter teleprompter glass), vertically, or both. The controls stay readable.

## Screen sharing

On **Windows** 10 version 2004 or later, **Settings → Hide the reader from screen sharing and recordings** asks Windows to leave the reader out of screen captures (Microsoft's `WDA_EXCLUDEFROMCAPTURE`). It is on by default. Check what other participants see before relying on it.

On **macOS** an app cannot reliably hide its own window: Apple documents the old "do not share" window setting as unused, and screen-sharing apps built on ScreenCaptureKit capture the window anyway. Share a single window (for example your slides) instead of the whole screen.

On **Linux**, applications cannot hide their own windows from capture; the compositor decides. On **KDE Plasma 6.6 or later**, focus the reader, press Alt+F3 and choose **More Actions → Hide from Screencast**. Plasma 6.7 or later can make this permanent with a window rule (System Settings → Window Management → Window Rules) matching the window class `io.github.brianidavide.kalipso` and the title `Kalipso — reading`.

On **Plasma with Wayland** only KWin can place windows. While Kalipso runs it loads its KWin script
into KWin (nothing to install or enable): the reader opens at the top of the screen, under the camera,
stays above other windows and asks for the same exclusion where KWin lets scripts set it. The script is
unloaded when Kalipso quits. On X11 the reader does this itself. Drag the reader by its title to move
it, and by any edge to resize it.

The same script is also packaged as **Kalipso companion** (System Settings → Window Management → KWin
Scripts) for anyone who wants it enabled permanently; Kalipso then uses that one.

## Speech

**Settings → Speech**:

- On Linux, **Language you speak** lists the languages Kalipso can recognise, each marked *ready* or with the size of its download. Choosing one that is not ready downloads its speech model once, with **Cancel download** while it runs; Kalipso picks the engine (Moonshine where it has the language, Vosk otherwise). Arrow keys and typing open the list rather than choosing at once, so nothing downloads by accident. See [voice following on Linux](local-engine.md).
- **Other model installed by hand…** takes a Moonshine or Vosk model folder and library you installed yourself.
- **System · on-device** (macOS and Windows): the operating system's recognizer. See [macOS and Windows builds](system-speech.md).

Listening only starts when you press **Start listening**, and stops when you pause, hide the reader or close Kalipso.

## Interface

**Settings** also has light and dark appearances, reader colors (including automatic colors, see [accessibility](accessibility.md)), interface text size up to 200%, reader text size, and the interface language (system, English or Italiano; applies after a restart).
