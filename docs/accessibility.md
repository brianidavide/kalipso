# Accessibility

Kalipso targets **WCAG 2.2 Level AA**, including applicable Level A criteria, across the website, standalone application, Plasma widget and GNOME extension. For native software, the interpretation follows [WCAG2ICT](https://www.w3.org/TR/wcag2ict-22/). WCAG 3.0 is a draft, not this product's conformance target.

This development preview does **not** claim full AA conformance yet: screen-reader, scaling and multi-monitor testing on real machines is still to be done.

## Colors and reading

Both light and dark reading palettes have an opaque background. Normal text, placeholders, remaining words and already-read words use at least **4.5:1** contrast. Essential control borders and focus indicators use at least **3:1** against their intended backgrounds. Read words remain available in the script; dimming does not remove them. Position counters and explicit state labels supplement color.

Choose reader colors in Settings. The standalone application's interface also has light/dark appearances and a 100%, 150% or 200% text-size setting. The shell frontends respect desktop text scaling and provide separate reading-size controls. Keyboard navigation, visible focus, scrolling and explicit edit labels remain available.

Automatic color changes select a complete, verified palette, rather than applying an arbitrary sampled background or using transparent text surfaces. Changes are rate limited and use a luminance dead band to avoid rapid alternation. A denied, failed or unsupported sample leaves an opaque palette in place.

| Surface | Automatic color source |
| --- | --- |
| macOS standalone | ScreenCaptureKit samples the region behind the reader, excluding the reading window. Selecting automatic screen colors can request Screen Recording permission. No image files are saved. Real-device capture and multi-monitor placement still need validation. |
| Windows standalone | A few pixels immediately outside the reader, every two seconds. These describe the surroundings, not pixels hidden behind the window. No video stream is opened. |
| Linux standalone on X11 | Small samples immediately outside the reader, every two seconds. |
| Linux standalone on Wayland | **Choose background window…** opens the desktop ScreenCast portal. Colors follow a sparse sample of that selected window through PipeWire, including when the reader overlaps it. The selection is explicit and is not restored automatically. |
| Plasma widget | **Desktop theme** follows the desktop palette. **Choose background window…** uses the same portal/PipeWire window sampling as the standalone app, through the updated Kalipso companion. Desktop theme and manual colors work without the companion. |
| GNOME extension | A surrounding pixel sampled through GNOME Shell every two seconds while automatic mode is visible. No image files or video stream are produced. |
| Browser demonstration | The computed opaque background of its containing page. It cannot inspect other applications. |

Automatic mode is separate from microphone capture. Sampling stops when the reader is hidden or automatic mode is disabled. No captured screen data is sent anywhere.

### Window colors on KDE/Wayland

In Settings, choose **Choose background window…**, then select the application behind the reader in the desktop permission dialog. Only windows are requested, with the cursor excluded and without a persistent permission token. The standalone app opens its reader before asking. Choose **Stop window sharing**, Light, Dark or (in Plasma) Desktop theme to end capture. Hiding the reader also closes the session; reopening requires another explicit selection. Closing the selected window, cancelling permission or revoking sharing leaves the last opaque palette in place and reports the state. A disconnected Plasma client releases its session, with a ten-second heartbeat timeout as a fallback.

This follows the **selected window's overall colors**, not a geometrically exact patch beneath the reader. Moving the reader over a different application does not select that application automatically: stop sharing and choose the new background window. This distinction avoids relying on global window coordinates that Wayland clients do not reliably have. The reader must not be selected as its own background.

The desktop streams that window continuously while sharing is active. Kalipso requests at most one frame per second, samples no more than 256 pixels every two seconds, and discards each buffer after use. Actual compositor costs depend on the desktop and source window; the low sampling rate is not a zero-cost capture guarantee. Frames stay in memory, are never saved or sent, and are independent of speech processing. Both frontends use the existing contrast palettes, luminance dead band and minimum palette hold time.

Runtime requirements: PipeWire, `xdg-desktop-portal`, and a desktop backend with window ScreenCast support (on KDE, `xdg-desktop-portal-kde`). Plasma also requires the updated Kalipso companion. The build requires PipeWire development headers and pkg-config. Missing services, unsupported stream formats and permission failures keep the readable fallback and allow an explicit retry.

## Keyboard and text

- Tab/Shift+Tab move between controls; Enter/Space activate buttons. Escape closes an editor or settings pane, or hides the reader.
- In the standalone reader and the shell frontends, arrow keys and Page Up/Down step through the script, Space starts or pauses, + and − change the steady speed and Home returns to the start. Ctrl+Enter opens the standalone reader.
- Manual and steady modes are full alternatives to speaking. Pause stops voice following without changing the script.
- Mirroring affects only the script text; the controls stay readable.
- The browser demonstration has a skip link, labelled fields, status messages, reduced-motion behaviour and forced-colors support.

## Checks

`node tests/contrast.test.mjs` checks every palette pair, the exact contrast thresholds, opaque colours and rapid-alternation protection. The Qt UI tests check read-text and selection contrast, accessible names and status roles, wrapping controls, 200% interface text at 320 px without horizontal scrolling, and editing behaviour. `portal_colors` and `portal_colors_stream` exercise the window-colour portal lifecycle with simulated consent and real PipeWire frames.

The browser checker runs axe-core A/AA rules in Chromium, Firefox and WebKit, plus text enlargement, spacing overrides, narrow viewports, keyboard editing and deliberate failures that prove the checks catch regressions. See [CONTRIBUTING.md](../CONTRIBUTING.md#checks) to run it.

Automated checks do not establish conformance on their own. Screen-reader testing (Orca, NVDA, VoiceOver), native display scaling, multiple monitors and operating-system permission dialogs still need people and real machines; please report what you find.
