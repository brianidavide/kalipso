// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import "tracker.mjs" as Tracking
import "Tokens.js" as Design
PlasmoidItem {
    id:root
    readonly property var c:Design.tokens.color
    readonly property var n:Design.tokens.native
    property string pane:""
    property string mode:"manual"
    property real readerSize:Design.tokens.native.readerSize
    property var tracker:new Tracking.Tracker("A little preparation makes room for a better conversation.\n\nTake your time. Leave space for a pause. Your next words will be here when you are ready.")
    property string markup:""
    property int readCount:0
    property int wordCount:0
    implicitWidth:n.width;implicitHeight:360
    preferredRepresentation:fullRepresentation
    function refresh() {
        markup=tracker.words.length?tracker.readingMarkup(c.overlayMuted,c.overlayText).replace(/<span foreground=/g,"<font color=").replace(/<\/span>/g,"</font>").replace(/\n/g,"<br>"):"Add your script to begin."
        readCount=tracker.position+1;wordCount=tracker.words.length
        if(Qt.application.name==="plasmawindowed")console.log("KALIPSO_TEST_STATE "+JSON.stringify({position:tracker.position,status:audio.status}))
    }
    function seek(index){tracker.seek(index);refresh()}
    function showPane(next){pane=pane===next?"":next;if(pane==="settings")audio.settings()}
    LocalEngine {id:audio;onTranscript:function(event){if(root.tracker.follow(event.text,event.final))root.refresh()}}
    Component.onCompleted:refresh()
    component ReaderButton:Button {
        id:control
        property bool primary:false
        font.pointSize:Qt.application.font.pointSize
        implicitHeight:Math.max(root.n.controlHeight,contentItem.implicitHeight+16)
        implicitWidth:Math.max(contentItem.implicitWidth+24,primary?Math.max(root.n.primaryWidth,metrics.advanceWidth("Start listening")+24):0)
        FontMetrics{id:metrics;font:control.font}
        padding:8;leftPadding:12;rightPadding:12
        background:Rectangle {
            radius:root.n.buttonRadius
            color:control.primary?(control.down?root.c.actionHover:root.c.action):(control.hovered||control.down?root.c.overlayLine:root.c.overlaySurface)
            border.width:control.activeFocus?2:0;border.color:root.c.focus
            opacity:control.enabled?1:0.45
        }
        contentItem:Text {text:control.text;color:root.c.overlayText;font:control.font;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;opacity:control.enabled?1:0.5}
    }
    fullRepresentation:Rectangle {
        id:body;color:root.c.overlay;radius:root.n.radius
        implicitWidth:root.n.width;implicitHeight:content.implicitHeight+root.n.padding*2
        Layout.preferredWidth:root.n.width;Layout.minimumWidth:420;Layout.minimumHeight:content.implicitHeight+root.n.padding*2
        border.width:1;border.color:root.c.overlayLine
        Keys.onEscapePressed:function(event){root.pane="";event.accepted=true}
        ColumnLayout {
            id:content;anchors.fill:parent;anchors.margins:root.n.padding;spacing:root.n.gap
            RowLayout {
                Layout.fillWidth:true;spacing:8
                Label{text:"Kalipso";color:root.c.overlayText;font.bold:true;font.pointSize:Qt.application.font.pointSize*1.15;Layout.fillWidth:true}
                ReaderButton{objectName:"editToggle";text:root.pane==="edit"?"Done editing":"Edit script";onClicked:root.showPane("edit")}
                ReaderButton{objectName:"settingsToggle";text:root.pane==="settings"?"Done":"Settings";onClicked:root.showPane("settings")}
            }
            Label {
                objectName:"reader";text:root.markup;textFormat:Text.RichText;color:root.c.overlayText
                font.pointSize:root.readerSize;wrapMode:Text.Wrap;Layout.fillWidth:true
                Layout.minimumHeight:font.pixelSize*4.5
                maximumLineCount:4;elide:Text.ElideRight
                topPadding:12;bottomPadding:12
            }
            RowLayout {
                spacing:8;Layout.fillWidth:true
                ReaderButton {
                    objectName:"readAction";primary:true
                    text:root.mode==="manual"?"Next words":audio.active?"Pause":audio.busy?"Cancel":"Start listening"
                    enabled:root.wordCount>0&&(root.mode!=="manual"||root.readCount<root.wordCount)
                    onClicked:{if(root.mode==="manual")root.seek(root.tracker.position+4);else if(audio.active||audio.busy)audio.stop();else audio.start()}
                }
                ReaderButton{text:"Previous";enabled:root.readCount>0;onClicked:root.seek(root.tracker.position-4)}
                ReaderButton{objectName:"resetPosition";text:"Reset";enabled:root.readCount>0;onClicked:root.seek(-1)}
                Label{text:root.readCount+" / "+root.wordCount;color:root.c.overlayMuted;horizontalAlignment:Text.AlignRight;Layout.fillWidth:true}
            }
            Label {
                objectName:"readingState";color:root.c.reading;wrapMode:Text.Wrap;Layout.fillWidth:true
                text:root.mode==="manual"?(root.wordCount>0&&root.readCount===root.wordCount?"End of script · Microphone off":"Manual · Microphone off"):audio.active?"Listening · On this device":audio.busy?"Starting local speech…":audio.failed?"Speech unavailable · Open Settings":"Paused · Microphone off"
            }
            Rectangle{visible:root.pane!=="";Layout.fillWidth:true;implicitHeight:1;color:root.c.overlayLine}
            ColumnLayout {
                visible:root.pane==="edit";Layout.fillWidth:true;spacing:root.n.gap
                Label{text:"SCRIPT";color:root.c.overlayMuted}
                ScrollView {
                    Layout.fillWidth:true;Layout.preferredHeight:root.n.editorHeight
                    TextArea {
                        id:editor;objectName:"scriptEditor";text:root.tracker.text;color:root.c.ink;font.pointSize:Qt.application.font.pointSize*1.15
                        wrapMode:TextEdit.Wrap;padding:root.n.gap;selectByMouse:true
                        background:Rectangle{color:root.c.surface;radius:root.n.buttonRadius;border.width:editor.activeFocus?2:0;border.color:root.c.focus}
                        Accessible.name:"Script"
                        Connections{target:root;function onPaneChanged(){if(root.pane==="edit")Qt.callLater(function(){editor.forceActiveFocus()})}}
                        onTextChanged:{if(text!==root.tracker.text){root.tracker.setText(text);root.refresh()}}
                    }
                }
                Label{text:"Changes appear immediately · This session only";color:root.c.overlayMuted;wrapMode:Text.Wrap;Layout.fillWidth:true}
            }
            ColumnLayout {
                visible:root.pane==="settings";Layout.fillWidth:true;spacing:root.n.gap
                RowLayout {
                    ReaderButton{objectName:"manualMode";text:(root.mode==="manual"?"✓ ":"")+"Manual";onClicked:{if(audio.active||audio.busy)audio.stop();root.mode="manual"}}
                    ReaderButton{objectName:"voiceMode";text:(root.mode==="voice"?"✓ ":"")+"Follow my voice";onClicked:{root.mode="voice";audio.settings()}}
                }
                RowLayout {
                    ReaderButton{objectName:"moonshineChoice";text:(audio.backend==="moonshine"?"✓ ":"")+"Moonshine";enabled:!audio.active&&!audio.busy;onClicked:audio.selectBackend("moonshine")}
                    ReaderButton{objectName:"voskChoice";text:(audio.backend==="vosk"?"✓ ":"")+"Vosk";enabled:!audio.active&&!audio.busy;onClicked:audio.selectBackend("vosk")}
                }
                Label{objectName:"speechDetails";text:audio.status;color:root.c.overlayMuted;wrapMode:Text.Wrap;Layout.fillWidth:true}
                RowLayout {
                    Label{text:"Reading size";color:root.c.overlayText;Layout.fillWidth:true}
                    ReaderButton{text:"Smaller";onClicked:root.readerSize=Math.max(18,root.readerSize-2)}
                    ReaderButton{text:"Larger";onClicked:root.readerSize=Math.min(40,root.readerSize+2)}
                }
                ReaderButton{text:"Local speech setup ↗";onClicked:Qt.openUrlExternally("https://github.com/brianidavide/kalipso/blob/main/docs/local-engine.md")}
                Label{text:"Place this widget near your webcam. It may appear in screen sharing; test your sharing setup before a call.";color:root.c.overlayMuted;wrapMode:Text.Wrap;Layout.fillWidth:true}
            }
            Item{Layout.fillHeight:true}
        }
    }
}
