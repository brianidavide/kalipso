// SPDX-License-Identifier: AGPL-3.0-or-later
// KWin 6.7+. This requests exclusion; it cannot verify downstream screen shares.
function apply(window) {
    const identity = String(window.resourceClass);
    if (identity !== 'io.github.brianidavide.kalipso' && identity !== 'kalipso') return;
    if ('excludeFromCapture' in window) window.excludeFromCapture = true;
    if (window.caption.indexOf('Kalipso — reading') !== 0) return;
    window.keepAbove = true;
    window.skipTaskbar = true;
    window.skipSwitcher = true;
    const area = workspace.clientArea(KWin.PlacementArea, window);
    const frame = window.frameGeometry;
    frame.width = Math.min(680, area.width - 32);
    frame.x = area.x + Math.round((area.width - frame.width) / 2);
    frame.y = area.y + 16;
    window.frameGeometry = frame;
}
workspace.windowAdded.connect(apply);
workspace.windowList().forEach(apply);
