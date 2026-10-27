# Accessibility

Kalipso targets **WCAG 2.2 Level AA**, including applicable Level A criteria, across the website, standalone application, Plasma widget and GNOME extension. For native software, the interpretation follows [WCAG2ICT](https://www.w3.org/TR/wcag2ict-22/). WCAG 3.0 is a draft, not this product's conformance target.

This development preview does **not** yet make a complete AA conformance claim. Automated checks cannot establish all success criteria. In particular, end-to-end VoiceOver, NVDA and Orca testing, native display scaling, multi-monitor sampling, permission dialogs and all keyboard flows on supported desktop versions remain release gates. Passing color checks is not equivalent to passing the whole standard.

## Colors and reading

Both light and dark reading palettes have an opaque background. Normal text, placeholders, remaining words and already-read words use at least **4.5:1** contrast. Essential control borders and focus indicators use at least **3:1** against their intended backgrounds. Read words remain available in the script; dimming does not remove them. Position counters and explicit state labels supplement color.

Choose reader colors in Settings. The standalone application's interface also has light/dark appearances and a 100%, 150% or 200% text-size setting. The shell frontends respect desktop text scaling and provide separate reading-size controls. Keyboard navigation, visible focus, scrolling and explicit edit labels remain available.

Automatic color changes select a complete, verified palette, rather than applying an arbitrary sampled background or using transparent text surfaces. Changes are rate limited and use a luminance dead band to avoid rapid alternation. A denied, failed or unsupported sample leaves an opaque palette in place.

| Surface | Automatic color source |
| --- | --- |
| macOS standalone | ScreenCaptureKit samples the region behind the reader, excluding Kalipso. Selecting automatic screen colors can request Screen Recording permission. No image files are saved. Real-device capture and multi-monitor placement still need validation. |
| Windows standalone | A few pixels immediately outside the reader, every two seconds. These describe the surroundings, not pixels hidden behind the window. No video stream is opened. |
| Linux standalone on X11 | Small samples immediately outside the reader, every two seconds. |
| Linux standalone on Wayland | Screen pixels are unavailable through this sampling backend. A fixed opaque palette remains; the app reports this limitation. |
| Plasma widget | Automatic selection from the desktop palette. This is **not** detection of pixels behind the widget. No screen-capture session or companion is required for colors. |
| GNOME extension | A surrounding pixel sampled through GNOME Shell every two seconds while automatic mode is visible. No image files or video stream are produced. |
| Browser demonstration | The computed opaque background of its containing page. It cannot inspect other applications. |

Automatic mode is separate from microphone capture. Sampling stops when the reader is hidden or automatic mode is disabled. No captured screen data is sent to speech engines or an LLM.

Continuously detecting the actual hidden background on KDE/Wayland would require an additional compositor/portal capture path and its consent flow. That feature is not represented by the desktop-theme option.

## Keyboard and text

- Tab/Shift+Tab navigate controls; Enter/Space activate buttons. Escape closes an editor/settings pane or hides the standalone reader.
- In the standalone app, Ctrl+Enter opens the reader; Ctrl+Left/Right moves the reading position. The editable script is available independently of the excerpt.
- Pause stops voice following without deleting the script. Manual reading controls provide an alternative to speaking.
- The browser demonstration has a skip link, associated field labels, status messages, reduced-motion behavior and forced-colors support.

## Verification

`node tests/contrast.test.mjs` checks palette combinations, exact contrast thresholds, opaque colors and rapid-alternation protection. Qt UI tests check read-text/selection contrast, accessible field names and status roles, wrapping controls, text scaling and editing behavior.

The browser checker runs axe-core A/AA rules in Chromium, Firefox and WebKit, plus text enlargement, spacing overrides, narrow viewports, keyboard editing and deliberate failures to verify that the checks detect regressions:

```sh
PLAYWRIGHT=/path/to/playwright/index.mjs \
AXE_CORE=/path/to/axe-core/axe.min.js \
node tools/check-accessibility.mjs
```

Serve `site/` at `http://localhost:8770` first, or set `DESIGN_URL`. Browser tooling is a development dependency, not part of the application. Screenshots and reports go to ignored `artifacts/` directories.

Release review must cover all applicable [WCAG 2.2 A/AA criteria](https://www.w3.org/TR/WCAG22/), including meaningful sequence, keyboard operation and no traps, focus order/visibility, text resizing/reflow, target size, error identification, names/roles/values and status announcements. Media, authentication and time-limit criteria must be reassessed if those features are introduced. User-provided script content and operating-system dialogs also require attention in complete workflow testing.
