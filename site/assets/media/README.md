# Product media

The "In real use" scenes: simulated desktops and apps, drawn for illustration (macOS, Windows, KDE Plasma
and GNOME, without logos or real app interfaces), with real captures of Kalipso placed under the camera.

- `scene-call-macos.webp`: a video call; the app's reader, dark, scrolling at a steady speed.
- `scene-recording-windows.webp`: recording a video; the app's reader, light, scrolling at a steady speed.
- `scene-slides-plasma.webp`: presenting slides; the Plasma widget, stepped by hand.
- `scene-lesson-gnome.webp`: teaching online; the GNOME Shell extension, stepped by hand.

The captures are made from the current build with clean profiles and the scripts in
`tools/scenes/scripts/`; no microphone is opened. The GNOME capture is at normal size (the nested shell
does not scale reliably), the others at twice the size. Rebuild them whenever the interface shown
changes: `tools/scenes/build.sh build/kalipso` (see that script for what it needs).
