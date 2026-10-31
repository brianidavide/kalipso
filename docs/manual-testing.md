# Testing on real computers

Automated tests run in CI on virtual machines without a microphone, screen reader, second monitor or GPU. These checks need a real computer. Each section names the issue to report results in. Say which system, desktop and version, Kalipso commit, and speech engine you used.

Use a test script with at least two `#` sections, a `[cue]` and a few paragraphs.

## Microphone, permissions and listening (#6, #14)

On each system you test (Linux, Windows, macOS):

1. Choose **Follow my voice** and press **Start listening**. The system should ask for microphone permission the first time.
2. **Deny** the permission. The reader should say that the microphone is unavailable. It must not show "Listening".
3. Allow the permission (system settings), start again and read a few lines. The current word should follow you; the `[cue]` should be skipped.
4. Pause and resume five times in a row. Each pause should stop the microphone; the system's microphone indicator should turn off.
5. While listening: hide the reader (Escape), minimize it, and quit Kalipso. Each one should turn the microphone off. Showing the reader again must not resume listening.
6. In **Settings → Speech → Microphone**, every microphone of the system should be listed, the first entry naming the system's one. **Test microphone**: the bar should move when you speak and the note should say that Kalipso hears you; mute the microphone in the system and the note should say that no sound arrives.
7. Unplug a USB microphone or headset while listening. With that microphone chosen in Settings, the reader should say that no sound arrives from it, by name, and must not freeze or switch to another microphone. With **System microphone**, listening should continue with the system's new microphone.

## Leaving the reader out of screen sharing (#32)

- **Windows:** share the whole screen in Teams, Zoom or Meet, and record with OBS (Display Capture). The reader should not appear to other participants or in the recording; the editor window should.
- **KDE Plasma 6.6 or later:** open the reader's window menu (Alt+F3) → **Hide from Screencast**, then share the screen. The reader should not appear.
- **macOS, GNOME, older Plasma:** the reader is expected to appear; share a single window instead. Confirm the documentation says so.

Always check from a second device what other participants actually see.

## Screen readers and keyboard (#13)

With Orca (Linux), NVDA (Windows) or VoiceOver (macOS):

1. Move through the editor window with Tab only. Every control should be reached and announced with a name (for example "Open reader, button").
2. Open Settings with the keyboard. Each setting should be announced with its label and its current value.
3. Open the reader (Ctrl+Enter). The reading position and status ("Paused · …", "Scrolling · …") should be announced when they change, without repeating every word while scrolling.
4. Set system text size to 200%. Nothing should be cut off and no window should need horizontal scrolling.
5. Repeat steps 1–3 in the Plasma widget and the GNOME extension.

## Reader placement and monitors (#8)

1. With two monitors, choose the second in **Settings → Monitor**, move and resize the reader, quit and restart. It should come back in the same place on the same monitor.
2. Unplug the second monitor while the reader is on it. The reader should move to a visible monitor.
3. Use monitors with different scaling (for example 100% and 200%). Text and controls should stay sharp and correctly sized when the reader moves between them.
4. On Wayland, note which of these work. Wayland lets the desktop decide window positions, so record what happens rather than expecting X11 behaviour.

## Automatic reader colors (#15)

1. Choose **Reader appearance → Automatic screen colors** (GNOME: **Reader colors → Automatic**), put the reader over a light window, then over a dark one. The reader should switch to light or dark within a few seconds and stay readable.
2. **KDE (Wayland):** use **Choose background window…** with a GPU-backed session. The system should ask which window to share, and sharing should stop when you choose manual colors or hide the reader.
3. **macOS:** the system should ask for Screen Recording permission. Denying it should leave readable fixed colors.

## Pedals, clickers and the phone remote (#28)

1. Connect a Bluetooth foot pedal or presentation clicker (most send arrows, Page Up/Down or Space). Each press should step or start/pause without the editor taking the keys.
2. Turn on **Settings → Remote control**, scan the QR code with a phone on the same Wi-Fi, and use every button: start/pause, previous/next, section, slower/faster, back to start.
3. Turn remote control off. The phone page should report that Kalipso is not reachable.

## Current desktop versions (#12)

Install the Plasma widget and the GNOME extension on the current stable releases (for example Plasma 6.5 or 6.6, GNOME 49 to 51). For each:

1. Add the widget or enable the extension. It should load without errors (`journalctl --user -f` while loading).
2. Try every button, the three reading modes, mirror, the countdown, font and line spacing, then log out and back in. The script, position and settings should be kept.

## Resource use with real speech (#4)

With Moonshine and with Vosk, read aloud for five minutes and note:

- CPU and memory of `kalipso-engine` or the standalone app (System Monitor, Task Manager or Activity Monitor);
- how long the current word lags behind your voice (roughly: immediately, under a second, more);
- whether the laptop fan or battery use changes noticeably.

See [performance](performance.md) for the measurements already made without real speech.
