// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import "tracker.mjs" as Tracking
import "Tokens.js" as Design
import "contrast.mjs" as Contrast

PlasmoidItem {
    id: root

    // Saved per widget in the Plasma configuration (contents/config/main.xml).
    readonly property var config: Plasmoid.configuration
    // "window" while following a window chosen through the sharing portal.
    property bool followWindow: false
    readonly property string appearance: followWindow ? "window" : config.appearance
    property string mode: config.mode          // "manual", "steady" or "voice"
    property int speed: config.speed           // Steady speed, words per minute
    property string mirror: config.mirror      // "none", "horizontal" or "vertical"
    property real readerSize: config.readerSize
    property string readerFont: config.readerFont      // "" is the interface font
    property int lineSpacing: config.lineSpacing       // Percent of the normal line height
    property int countdownLeft: 0                      // Seconds before steady scrolling starts
    // Font choices: a generic serif, plus fonts designed for easier reading when installed.
    readonly property var fontChoices: {
        const installed = Qt.fontFamilies()
        return ["Serif"].concat(["Atkinson Hyperlegible", "Atkinson Hyperlegible Next", "OpenDyslexic", "Lexend"]
            .filter(family => installed.indexOf(family) >= 0))
    }

    property string windowTheme: "dark"
    property var windowPalette: new Contrast.PaletteChoice()
    readonly property var n: Design.tokens.native
    readonly property var p: Design.tokens.palette[appearance === "window" ? windowTheme
        : appearance === "auto" ? Contrast.suggestedTheme(desktopPalette.window.toString()) : appearance]
    readonly property var c: Object.assign({}, Design.tokens.color, {
        overlay: p.background, overlayText: p.text, overlayMuted: p.muted, overlaySurface: p.surface,
        overlayLine: p.hover, overlayBorder: p.border, focus: p.focus, reading: p.accent})

    property var tracker: new Tracking.Tracker(config.script)
    property string markup: ""
    property int readCount: 0
    property int wordCount: 0
    property bool lost: false
    property var sectionList: []
    property string scriptTitle: ""
    property string pane: ""
    readonly property bool atEnd: wordCount > 0 && readCount === wordCount

    signal revealRequested(var item)

    implicitWidth: n.width
    implicitHeight: 360
    preferredRepresentation: fullRepresentation

    SystemPalette { id: desktopPalette; colorGroup: SystemPalette.Active }

    function refresh() {
        markup = tracker.words.length
            ? tracker.readingMarkup(c.overlayMuted, c.overlayText)
                .replace(/<span foreground=/g, "<font color=").replace(/<\/span>/g, "</font>")
            : i18n("Add your script to begin.")
        const firstLine = tracker.text.split("\n").map(line => line.replace(/^\s*#+\s*/, "").trim()).find(line => line)
        scriptTitle = firstLine || i18n("Kalipso")
        readCount = tracker.position + 1
        wordCount = tracker.words.length
        lost = tracker.lost
        savePosition.restart()
        if (Qt.application.name === "plasmawindowed")
            console.log("KALIPSO_TEST_STATE " + JSON.stringify({position: tracker.position, status: audio.status}))
    }
    function seek(index) { tracker.seek(index); refresh() }
    function jumpTo(character) { tracker.seekCharacter(character); refresh() }
    function showPane(next) {
        pane = pane === next ? "" : next
        if (pane === "settings")
            audio.settings()
    }
    function setMode(next) {
        stop()
        config.mode = next
        if (next === "voice")
            audio.settings()
    }
    function chooseAppearance(name) {
        followWindow = false
        config.appearance = name
    }
    function changeSpeed(delta) { config.speed = Math.max(40, Math.min(400, speed + delta)) }
    // The primary action for the current mode.
    function toggle() {
        if (mode === "manual")
            seek(tracker.position + 4)
        else if (mode === "steady")
            (steady.running || countdown.running) ? stop() : startSteady()
        else if (audio.active || audio.busy)
            audio.stop()
        else
            audio.start()
    }
    function startSteady() {
        if (atEnd)
            seek(-1)
        countdownLeft = Math.max(0, config.countdown)
        countdownLeft > 0 ? countdown.start() : steady.start()
    }
    function stop() {
        countdown.stop()
        countdownLeft = 0
        steady.stop()
        if (audio.active || audio.busy)
            audio.stop()
    }

    onAppearanceChanged: { if (appearance !== "window") windowColors.stop(); refresh() }
    onWindowThemeChanged: refresh()
    onVisibleChanged: if (!visible) { windowColors.stop(); stop() }
    Component.onCompleted: {
        if (config.position >= 0)
            tracker.seekCharacter(config.position)
        sectionList = Tracking.sections(tracker.text)
        refresh()
    }

    Connections {
        target: root.Window.window
        function onVisibleChanged() { if (!root.Window.window.visible) { windowColors.stop(); root.stop() } }
        function onVisibilityChanged() { if (root.Window.window.visibility === Window.Minimized) windowColors.stop() }
    }

    Timer {
        id: saveScript
        interval: 800
        onTriggered: root.config.script = root.tracker.text
    }
    Timer {
        id: savePosition
        interval: 1000
        onTriggered: root.config.position = root.tracker.position >= 0 ? root.tracker.words[root.tracker.position].start : -1
    }
    Timer {
        id: countdown
        interval: 1000
        repeat: true
        onTriggered: {
            root.countdownLeft -= 1
            if (root.countdownLeft <= 0) {
                countdown.stop()
                steady.start()
            }
        }
    }
    Timer {
        id: steady
        interval: Math.max(150, Math.round(60000 / root.speed))
        repeat: true
        onTriggered: {
            if (root.tracker.position >= root.tracker.words.length - 1)
                steady.stop()
            else
                root.seek(root.tracker.position + 1)
        }
    }

    LocalEngine {
        id: audio
        onTranscript: function(event) {
            const lostBefore = root.tracker.lost
            if (root.tracker.follow(event.text, event.final) || root.tracker.lost !== lostBefore)
                root.refresh()
        }
    }
    WindowColors {
        id: windowColors
        onSampled: function(color) {
            if (root.appearance === "window")
                root.windowTheme = root.windowPalette.sample(color, Date.now())
        }
    }

    // Buttons follow the same rules as the standalone app: one filled primary
    // action, outlined buttons, quiet buttons for window actions, and chosen
    // options marked by a thicker accent border and bold label.
    component ReaderButton: Button {
        id: control
        property bool primary: false
        property bool quiet: false
        property bool chosen: false
        property string iconName: ""
        Accessible.name: text
        activeFocusOnTab: true
        onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { root.revealRequested(control) })
        font.pointSize: Qt.application.font.pointSize
        font.bold: primary || chosen
        implicitHeight: Math.max(root.n.controlHeight, contentItem.implicitHeight + 16)
        implicitWidth: Math.max(contentItem.implicitWidth + 24,
            primary ? Math.max(root.n.primaryWidth, metrics.advanceWidth(i18n("Start listening")) + 48) : 0)
        FontMetrics { id: metrics; font: control.font }
        width: Math.min(implicitWidth, parent ? parent.width : implicitWidth)
        padding: 8; leftPadding: 12; rightPadding: 12
        opacity: enabled ? 1 : 0.5 // Disabled controls stay readable but look unavailable.
        background: Rectangle {
            radius: root.n.buttonRadius
            color: control.primary ? (control.down ? root.c.actionHover : root.c.action)
                : control.hovered || control.down || control.chosen ? root.c.overlayLine
                : control.quiet ? "transparent" : root.c.overlaySurface
            border.width: control.activeFocus || control.chosen ? 2 : control.quiet ? 0 : 1
            border.color: control.activeFocus ? root.c.focus : control.chosen ? root.c.reading : root.c.overlayBorder
        }
        // Icon and label centred together; the label wraps when the button is narrow.
        contentItem: Item {
            implicitWidth: row.implicitWidth
            implicitHeight: row.implicitHeight
            RowLayout {
                id: row
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width)
                spacing: 6
                Kirigami.Icon {
                    visible: control.iconName !== ""
                    source: control.iconName
                    isMask: true
                    color: control.primary ? root.p.actionText : root.c.overlayText
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    visible: control.text !== ""
                    text: control.text
                    color: control.primary ? root.p.actionText : root.c.overlayText
                    font: control.font
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Layout.fillWidth: true
                }
            }
        }
    }
    // One of several options. The chosen one has an accent border and bold label;
    // assistive technology hears it as checked.
    component ChoiceButton: ReaderButton {
        property bool selected: false
        property string label
        chosen: selected
        text: label
        Accessible.name: label
        Accessible.checkable: true
        Accessible.checked: selected
    }
    // A small heading above a group of settings.
    component GroupLabel: Label {
        color: root.c.overlayMuted
        font.pointSize: Qt.application.font.pointSize * 0.9
        font.bold: true
        topPadding: 4
        Layout.fillWidth: true
    }
    component ReaderScrollBar: ScrollBar {
        orientation: Qt.Vertical
        policy: ScrollBar.AsNeeded
        implicitWidth: 24
        height: parent.height
        x: parent.width - width
        y: 0
        visible: size < 1
        contentItem: Rectangle { implicitWidth: 24; implicitHeight: 24; color: root.c.overlayBorder; radius: 4 }
        background: Rectangle { color: root.c.overlay }
    }
    component Note: Label {
        color: root.c.overlayMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    fullRepresentation: Rectangle {
        id: body
        color: root.c.overlay
        radius: root.n.radius
        border.width: 2
        border.color: root.c.overlayBorder
        implicitWidth: root.n.width
        implicitHeight: content.implicitHeight + root.n.padding * 2
        Layout.preferredWidth: root.n.width
        Layout.preferredHeight: Math.min(700, content.implicitHeight + root.n.padding * 2)
        Layout.minimumWidth: 320
        Layout.minimumHeight: 240
        onVisibleChanged: if (!visible) { windowColors.stop(); root.stop() }

        Connections {
            target: body.Window.window
            function onVisibleChanged() { if (!body.Window.window.visible) windowColors.stop() }
            function onVisibilityChanged() { if (body.Window.window.visibility === Window.Minimized) windowColors.stop() }
        }

        // Foot pedals and presentation clickers send these keys.
        Keys.onPressed: function(event) {
            if (root.pane === "edit")
                return
            switch (event.key) {
            case Qt.Key_Right: case Qt.Key_Down: case Qt.Key_PageDown:
                root.seek(root.tracker.position + 1); break
            case Qt.Key_Left: case Qt.Key_Up: case Qt.Key_PageUp:
                root.seek(root.tracker.position - 1); break
            case Qt.Key_Home:
                root.seek(-1); break
            case Qt.Key_Space: case Qt.Key_MediaTogglePlayPause:
                root.toggle(); break
            case Qt.Key_Plus: case Qt.Key_Equal:
                root.changeSpeed(10); break
            case Qt.Key_Minus:
                root.changeSpeed(-10); break
            default:
                return
            }
            event.accepted = true
        }
        Keys.onEscapePressed: function(event) { root.pane = ""; editToggle.forceActiveFocus(); event.accepted = true }

        ScrollView {
            id: outerScroll
            ScrollBar.vertical: ReaderScrollBar {}
            anchors.fill: parent
            anchors.margins: root.n.padding
            rightPadding: ScrollBar.vertical.visible ? 28 : 0
            contentWidth: availableWidth
            clip: true
            Connections {
                target: root
                function onRevealRequested(item) {
                    const top = item.mapToItem(content, 0, 0).y
                    const flick = outerScroll.contentItem
                    if (top < flick.contentY)
                        flick.contentY = top
                    else if (top + item.height > flick.contentY + outerScroll.availableHeight)
                        flick.contentY = Math.min(flick.contentHeight - outerScroll.availableHeight,
                            top + item.height - outerScroll.availableHeight)
                }
            }

            ColumnLayout {
                id: content
                width: outerScroll.availableWidth
                spacing: root.n.gap

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: root.scriptTitle
                        color: root.c.overlayMuted
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, 220)
                        height: root.n.controlHeight
                        verticalAlignment: Text.AlignVCenter
                    }
                    ReaderButton {
                        objectName: "sectionsButton"
                        quiet: true
                        iconName: "view-list-details"
                        text: i18n("Sections")
                        visible: root.sectionList.length > 0
                        onClicked: sectionMenu.popup(this, 0, height)
                        Menu {
                            id: sectionMenu
                            Instantiator {
                                model: root.sectionList
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData.title
                                    onTriggered: root.jumpTo(modelData.start)
                                }
                                onObjectAdded: (index, object) => sectionMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => sectionMenu.removeItem(object)
                            }
                        }
                    }
                    ReaderButton {
                        id: editToggle
                        objectName: "editToggle"
                        quiet: true
                        iconName: root.pane === "edit" ? "dialog-ok" : "document-edit"
                        text: root.pane === "edit" ? i18n("Done editing") : i18n("Edit script")
                        onClicked: root.showPane("edit")
                    }
                    ReaderButton {
                        objectName: "settingsToggle"
                        quiet: true
                        iconName: root.pane === "settings" ? "dialog-ok" : "configure"
                        text: root.pane === "settings" ? i18n("Done") : i18n("Settings")
                        onClicked: root.showPane("settings")
                    }
                }

                Rectangle {
                    objectName: "readingProgress"
                    Layout.fillWidth: true
                    implicitHeight: 3
                    color: root.c.overlayLine
                    Accessible.role: Accessible.ProgressBar
                    Accessible.name: i18n("Reading progress")
                    Rectangle {
                        width: parent.width * (root.wordCount ? root.readCount / root.wordCount : 0)
                        height: parent.height
                        color: root.c.reading
                    }
                }
                Label {
                    objectName: "reader"
                    text: root.markup
                    textFormat: Text.RichText
                    color: root.c.overlayText
                    font.pointSize: root.readerSize
                    font.family: root.readerFont || Qt.application.font.family
                    lineHeightMode: Text.ProportionalHeight
                    lineHeight: root.lineSpacing / 100
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.minimumHeight: font.pixelSize * 4.5
                    topPadding: 12
                    bottomPadding: 12
                    // Seconds left before steady scrolling starts, over the text.
                    Label {
                        objectName: "countdown"
                        visible: root.countdownLeft > 0
                        anchors.centerIn: parent
                        text: root.countdownLeft
                        color: root.c.overlayText
                        font.pointSize: root.readerSize * 2
                        font.bold: true
                        padding: 16
                        background: Rectangle { color: root.c.overlaySurface; border.color: root.c.overlayLine; radius: 12 }
                        Accessible.ignored: true
                    }
                    Accessible.name: i18n("Reading excerpt")
                    Accessible.description: i18n("Open Edit script to read or change the complete script.")
                    // Beam-splitter rigs show the reader through a mirror.
                    transform: Scale {
                        origin.x: width / 2
                        origin.y: height / 2
                        xScale: root.mirror === "horizontal" ? -1 : 1
                        yScale: root.mirror === "vertical" ? -1 : 1
                    }
                    MouseArea { anchors.fill: parent; onClicked: body.forceActiveFocus() }
                }

                Flow {
                    spacing: 8
                    Layout.fillWidth: true
                    ReaderButton {
                        objectName: "readAction"
                        primary: true
                        iconName: root.mode === "manual" ? "go-next"
                            : (steady.running || countdown.running || audio.active) ? "media-playback-pause" : "media-playback-start"
                        text: root.mode === "manual" ? i18n("Next words")
                            : root.mode === "steady" ? ((steady.running || countdown.running) ? i18n("Pause") : i18n("Start scrolling"))
                            : audio.active ? i18n("Pause") : audio.busy ? i18n("Cancel") : i18n("Start listening")
                        enabled: root.wordCount > 0 && (root.mode !== "manual" || !root.atEnd)
                        onClicked: root.toggle()
                    }
                    ReaderButton { quiet: true; iconName: "go-previous"; text: i18n("Previous"); enabled: root.readCount > 0; onClicked: root.seek(root.tracker.position - 4) }
                    ReaderButton {
                        objectName: "resetPosition"
                        quiet: true
                        iconName: "go-first"
                        text: i18n("Reset")
                        enabled: root.readCount > 0
                        onClicked: root.seek(-1)
                    }
                    Label {
                        text: root.readCount + " / " + root.wordCount
                        color: root.c.overlayMuted
                        height: root.n.controlHeight
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8
                    }
                }
                Flow {
                    visible: root.mode === "steady"
                    spacing: 8
                    Layout.fillWidth: true
                    ReaderButton { quiet: true; text: "−"; Accessible.name: i18n("Slower"); onClicked: root.changeSpeed(-10) }
                    Label { text: i18n("%1 words/min", root.speed); color: root.c.overlayText; height: root.n.controlHeight; verticalAlignment: Text.AlignVCenter }
                    ReaderButton { quiet: true; text: "+"; Accessible.name: i18n("Faster"); onClicked: root.changeSpeed(10) }
                }
                Label {
                    objectName: "readingState"
                    color: root.c.reading
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.StatusBar
                    text: root.mode === "manual" ? (root.atEnd ? i18n("End of script · Microphone off") : i18n("Manual · Microphone off"))
                        : root.mode === "steady" ? (countdown.running ? i18np("Starting in %1 second…", "Starting in %1 seconds…", root.countdownLeft)
                            : steady.running ? i18n("Scrolling · Microphone off")
                            : root.atEnd ? i18n("End of script · Microphone off") : i18n("Paused · Microphone off"))
                        : audio.active ? (root.lost ? i18n("Off script · Kalipso catches up when you return to the text")
                            : i18n("Listening · On this device"))
                        : audio.busy ? i18n("Starting local speech…")
                        : audio.failed ? i18n("Speech unavailable · Open Settings") : i18n("Paused · Microphone off")
                }

                Rectangle { visible: root.pane !== ""; Layout.fillWidth: true; implicitHeight: 1; color: root.c.overlayLine }

                ColumnLayout {
                    visible: root.pane === "edit"
                    Layout.fillWidth: true
                    spacing: root.n.gap
                    Label { text: i18n("Script"); font.capitalization: Font.AllUppercase; color: root.c.overlayMuted }
                    ScrollView {
                        ScrollBar.vertical: ReaderScrollBar {}
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.n.editorHeight
                        TextArea {
                            id: editor
                            objectName: "scriptEditor"
                            text: root.tracker.text
                            color: root.c.ink
                            font.pointSize: Qt.application.font.pointSize * 1.15
                            selectionColor: Design.tokens.palette.light.selection
                            selectedTextColor: Design.tokens.palette.light.selectionText
                            wrapMode: TextEdit.Wrap
                            padding: root.n.gap
                            selectByMouse: true
                            background: Rectangle {
                                color: root.c.surface
                                radius: root.n.buttonRadius
                                border.width: 2
                                border.color: editor.activeFocus ? Design.tokens.palette.light.focus : Design.tokens.palette.light.border
                            }
                            Accessible.name: i18n("Script")
                            onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { root.revealRequested(editor) })
                            Connections {
                                target: root
                                function onPaneChanged() { if (root.pane === "edit") Qt.callLater(function() { editor.forceActiveFocus() }) }
                            }
                            onTextChanged: {
                                if (text === root.tracker.text)
                                    return
                                root.tracker.setText(text)
                                root.sectionList = Tracking.sections(text)
                                saveScript.restart()
                                root.refresh()
                            }
                        }
                    }
                    Note { text: i18n("Saved automatically. [Bracketed cues] are shown but not read aloud. Lines starting with # are sections.") }
                }

                ColumnLayout {
                    visible: root.pane === "settings"
                    Layout.fillWidth: true
                    spacing: root.n.gap
                    GroupLabel { text: i18n("Reading mode") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ChoiceButton { objectName: "manualMode"; label: i18n("Manual"); selected: root.mode === "manual"; onClicked: root.setMode("manual") }
                        ChoiceButton { objectName: "steadyMode"; label: i18n("Steady speed"); selected: root.mode === "steady"; onClicked: root.setMode("steady") }
                        ChoiceButton { objectName: "voiceMode"; label: i18n("Follow my voice"); selected: root.mode === "voice"; onClicked: root.setMode("voice") }
                    }
                    GroupLabel { text: i18n("Speech engine") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ChoiceButton {
                            objectName: "moonshineChoice"; label: "Moonshine"; selected: audio.backend === "moonshine"
                            enabled: !audio.active && !audio.busy; onClicked: audio.selectBackend("moonshine")
                        }
                        ChoiceButton {
                            objectName: "voskChoice"; label: "Vosk"; selected: audio.backend === "vosk"
                            enabled: !audio.active && !audio.busy; onClicked: audio.selectBackend("vosk")
                        }
                    }
                    Note { objectName: "speechDetails"; text: audio.status }
                    GroupLabel { text: i18n("Reader colors") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ChoiceButton { objectName: "autoAppearance"; label: i18n("Desktop theme"); selected: root.appearance === "auto"; onClicked: root.chooseAppearance("auto") }
                        ChoiceButton { objectName: "darkAppearance"; label: i18n("Dark"); selected: root.appearance === "dark"; onClicked: root.chooseAppearance("dark") }
                        ChoiceButton { objectName: "lightAppearance"; label: i18n("Light"); selected: root.appearance === "light"; onClicked: root.chooseAppearance("light") }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ReaderButton {
                            objectName: "chooseBackgroundWindow"
                            text: i18n("Choose background window…")
                            enabled: !windowColors.active && !windowColors.busy
                            onClicked: { root.followWindow = true; windowColors.start() }
                        }
                        ReaderButton {
                            objectName: "stopWindowSharing"
                            text: i18n("Stop window sharing")
                            enabled: windowColors.active || windowColors.busy
                            onClicked: { windowColors.stop(); root.followWindow = false }
                        }
                    }
                    Note {
                        objectName: "windowColorsStatus"
                        text: windowColors.status
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                    Note { text: i18n("Window colors follow the window you select while this reader is visible. Images stay in memory and are never saved or sent. Requires the Kalipso companion, PipeWire and the desktop sharing portal. Every color mode keeps an opaque reading background.") }
                    GroupLabel { text: i18n("Mirror") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ChoiceButton { label: i18n("Off"); selected: root.mirror === "none"; onClicked: root.config.mirror = "none" }
                        ChoiceButton { objectName: "mirrorHorizontal"; label: i18n("Horizontal"); selected: root.mirror === "horizontal"; onClicked: root.config.mirror = "horizontal" }
                        ChoiceButton { label: i18n("Vertical"); selected: root.mirror === "vertical"; onClicked: root.config.mirror = "vertical" }
                    }
                    GroupLabel { text: i18n("Reading size") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ReaderButton { text: i18n("Smaller"); onClicked: root.config.readerSize = Math.max(18, root.readerSize - 2) }
                        ReaderButton { text: i18n("Larger"); onClicked: root.config.readerSize = Math.min(64, root.readerSize + 2) }
                    }
                    GroupLabel { text: i18n("Reader font") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ChoiceButton { objectName: "interfaceFont"; label: i18n("Interface font"); selected: root.readerFont === ""; onClicked: root.config.readerFont = "" }
                        Repeater {
                            model: root.fontChoices
                            ChoiceButton {
                                required property string modelData
                                label: modelData === "Serif" ? i18n("Serif") : modelData
                                selected: root.readerFont === modelData
                                onClicked: root.config.readerFont = modelData
                            }
                        }
                    }
                    GroupLabel { text: i18n("Line spacing") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [[100, i18n("Normal")], [125, i18n("Relaxed")], [150, i18n("Wide")]]
                            ChoiceButton {
                                required property var modelData
                                objectName: "lineSpacing" + modelData[0]
                                label: modelData[1]
                                selected: root.lineSpacing === modelData[0]
                                onClicked: root.config.lineSpacing = modelData[0]
                            }
                        }
                    }
                    GroupLabel { text: i18n("Countdown before steady scrolling") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [0, 3, 5]
                            ChoiceButton {
                                required property int modelData
                                objectName: "countdown" + modelData
                                label: modelData === 0 ? i18n("Off") : i18np("%1 second", "%1 seconds", modelData)
                                selected: root.config.countdown === modelData
                                onClicked: root.config.countdown = modelData
                            }
                        }
                    }
                    ReaderButton {
                        text: i18n("Local speech setup ↗")
                        Accessible.name: i18n("Local speech setup, opens in your browser")
                        onClicked: Qt.openUrlExternally("https://github.com/brianidavide/kalipso/blob/main/docs/local-engine.md")
                    }
                    Note { text: i18n("Keys: arrows or Page Up/Down step, Space starts or pauses, + and − change the steady speed. Screen sharing may include this widget; check what other participants see.") }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
