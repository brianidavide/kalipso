// SPDX-License-Identifier: AGPL-3.0-or-later
// Kalipso companion for KWin: keeps the reader above other windows, places it
// near the camera the first time it appears, and asks KWin to leave it out of
// screen captures where KWin supports that. Other participants' view is the
// only proof that a capture excluded it.
const READER_CAPTION = 'Kalipso — reading';
let lastFrame = null; // Where the user last put the reader in this session.

function isReader(window) {
    const identity = String(window.resourceClass);
    return (identity === 'io.github.brianidavide.kalipso' || identity === 'kalipso') &&
        String(window.caption).indexOf(READER_CAPTION) === 0;
}

function apply(window) {
    if (!isReader(window))
        return;
    if ('excludeFromCapture' in window)
        window.excludeFromCapture = true;
    window.keepAbove = true;
    window.skipTaskbar = true;
    window.skipSwitcher = true;
    const frame = window.frameGeometry;
    if (lastFrame) {
        frame.x = lastFrame.x;
        frame.y = lastFrame.y;
        frame.width = lastFrame.width;
        frame.height = lastFrame.height;
    } else {
        const area = workspace.clientArea(KWin.PlacementArea, window);
        frame.width = Math.min(680, area.width - 32);
        frame.x = area.x + Math.round((area.width - frame.width) / 2);
        frame.y = area.y; // Right under the camera, or under a top panel.
    }
    window.frameGeometry = frame;
    window.frameGeometryChanged.connect(() => {
        const current = window.frameGeometry;
        lastFrame = {x: current.x, y: current.y, width: current.width, height: current.height};
    });
}

workspace.windowAdded.connect(apply);
workspace.windowList().forEach(apply);
