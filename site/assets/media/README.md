# Product captures

Actual captures of the Linux Qt application (X11, no window manager), using its `--demo` sample script on a clean profile set to steady speed, made with `tools/capture-site-media.sh`. Nothing is composited into them, and no microphone is opened.

- `native-editor.webp`: the editor window, also the video's poster. Lossless.
- `native-walkthrough.mp4` / `.webm`: the same 15-second recording (H.264 and VP9): open the reader with Ctrl+Enter, start it with Space, three-second countdown, steady scrolling while the controls fade, pause (the time left shows), hide.

They do not show macOS, Windows, GNOME Shell or Plasma. The page loads the video only when it comes into view, then plays it once, muted, unless the visitor asks for reduced motion; its controls stay available. Refresh these captures whenever the interface shown changes: `tools/capture-site-media.sh build/kalipso`.
