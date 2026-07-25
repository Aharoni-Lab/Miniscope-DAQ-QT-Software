import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// The Acquire view's session bar: recording transport (Record, hold-to-Stop,
// record clock with the timed-stop limit), session notes, the external
// trigger, disk-space and per-device telemetry chips, the layout lock, and
// End Session. Backed by backend.sessionControl (the windowless session
// controller) plus a 1 Hz poll of backend.sessionTelemetry().
ColumnLayout {
    id: bar

    property bool layoutLocked: false
    signal lockToggled(bool locked)

    readonly property var ctl: backend ? backend.sessionControl : null
    readonly property bool recording: ctl ? ctl.recording : false

    spacing: Theme.spacing

    // --- Telemetry poll -----------------------------------------------------
    property var telemetryDevices: []
    property double diskFreeBytes: -1
    property var fpsByName: ({})
    property var prevFrames: ({})

    function formatClock(totalSeconds) {
        var h = Math.floor(totalSeconds / 3600)
        var m = Math.floor((totalSeconds % 3600) / 60)
        var s = totalSeconds % 60
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return (h > 0 ? h + ":" + pad(m) : m) + ":" + pad(s)
    }

    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: backend ? backend.sessionActive : false
        onTriggered: {
            var t = backend.sessionTelemetry()
            var fps = {}
            var prev = bar.prevFrames
            for (var i = 0; i < t.devices.length; i++) {
                var d = t.devices[i]
                fps[d.name] = prev[d.name] !== undefined ? Math.max(0, d.frames - prev[d.name]) : 0
                prev[d.name] = d.frames
            }
            bar.prevFrames = prev
            bar.fpsByName = fps
            bar.telemetryDevices = t.devices
            bar.diskFreeBytes = t.diskFreeBytes
        }
    }

    // --- Row 1: status / transport / disk / layout ---------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing * 2

        Rectangle {
            width: 12; height: 12; radius: 6
            color: bar.recording ? Theme.recording : Theme.success
        }
        Text {
            text: bar.recording ? qsTr("Recording") : qsTr("Session running")
            font: Theme.fontTitle
            color: bar.recording ? Theme.recording : Theme.textPrimary
        }

        // Record clock (mono so digits don't jitter)
        Text {
            text: {
                if (!bar.ctl)
                    return ""
                var clock = bar.formatClock(bar.ctl.currentRecordTime)
                if (bar.ctl.recordLengthInSeconds > 0)
                    clock += "  /  " + bar.formatClock(bar.ctl.recordLengthInSeconds)
                return bar.recording ? clock : ""
            }
            font: Theme.fontMono
            color: bar.recording ? Theme.recording : Theme.textSecondary
        }

        UiButton {
            text: qsTr("● Record")
            primary: true
            enabled: bar.ctl !== null && !bar.recording && !(bar.ctl && bar.ctl.extTriggerEnabled)
            onClicked: bar.ctl.startRecording()
        }

        // Hold-to-confirm stop: a stray click must not end an experiment.
        UiButton {
            id: stopButton
            danger: true
            enabled: bar.recording
            text: stopHold.running ? qsTr("keep holding…") : qsTr("■ Stop (hold)")
            onPressedChanged: pressed ? stopHold.start() : stopHold.stop()
            Timer {
                id: stopHold
                interval: 800
                onTriggered: bar.ctl.stopRecording()
            }
        }

        Item { Layout.fillWidth: true }

        // Disk space on the recording target
        Text {
            visible: bar.diskFreeBytes >= 0
            text: qsTr("%1 GB free").arg((bar.diskFreeBytes / 1e9).toFixed(1))
            font: Theme.fontSmall
            color: bar.diskFreeBytes < 5e9 ? Theme.danger
                 : bar.diskFreeBytes < 20e9 ? Theme.warning
                 : Theme.textSecondary
        }

        UiSwitch {
            text: qsTr("Lock layout")
            syncChecked: bar.layoutLocked
            onToggled: bar.lockToggled(checked)
        }

        UiButton {
            text: qsTr("End session")
            danger: true
            // The backend refuses mid-recording too: ending the session would
            // end the experiment by accident.
            enabled: backend ? !backend.recording : false
            onClicked: backend.endSession()
        }
    }

    // --- Row 2: notes / trigger / device chips --------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        UiTextField {
            id: noteField
            Layout.preferredWidth: 320
            enabled: bar.recording
            placeholderText: bar.recording ? qsTr("Note for the recording…")
                                           : qsTr("Notes can be logged while recording")
            onAccepted: {
                bar.ctl.submitNote(text)
                clear()
            }
        }
        UiButton {
            text: qsTr("Log note")
            enabled: bar.recording && noteField.text.trim().length > 0
            onClicked: {
                bar.ctl.submitNote(noteField.text)
                noteField.clear()
            }
        }

        UiSwitch {
            text: qsTr("External trigger")
            syncChecked: bar.ctl ? bar.ctl.extTriggerEnabled : false
            onToggled: if (bar.ctl) bar.ctl.extTriggerEnabled = checked
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Recording starts and stops with the Miniscope's trigger input; the timed stop is disabled.")
        }

        Item { Layout.fillWidth: true }

        // Per-device telemetry chips
        Repeater {
            model: bar.telemetryDevices
            delegate: Rectangle {
                required property var modelData
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: 1
                border.color: modelData.bufferUsed > modelData.bufferSize * 0.8
                              ? Theme.warning : Theme.border
                implicitWidth: chipText.implicitWidth + Theme.spacing * 2
                implicitHeight: 24
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                    text: {
                        var fps = bar.fpsByName[modelData.name]
                        var parts = [modelData.name,
                                     (fps === undefined ? "–" : fps) + " FPS"]
                        parts.push(qsTr("drop %1").arg(modelData.dropped < 0 ? "n/a" : modelData.dropped))
                        parts.push(qsTr("buf %1/%2").arg(modelData.bufferUsed).arg(modelData.bufferSize))
                        return parts.join("  ·  ")
                    }
                }
            }
        }
    }

    // --- Session log (collapsible) --------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: logHeader.implicitHeight + (logOpen ? 150 : 0) + Theme.spacing
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        clip: true

        property bool logOpen: false
        id: logCard

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing / 2
            spacing: Theme.spacing / 2

            RowLayout {
                id: logHeader
                Layout.fillWidth: true
                spacing: Theme.spacing
                Text {
                    text: "▸"
                    rotation: logCard.logOpen ? 90 : 0
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                    Behavior on rotation { NumberAnimation { duration: Theme.animMs } }
                }
                Text {
                    text: qsTr("Session log")
                    font: Theme.fontSmall
                    color: Theme.textPrimary
                }
                Text {
                    // Latest message inline while the log is collapsed.
                    visible: !logCard.logOpen && bar.ctl && bar.ctl.messageLog.length > 0
                    text: bar.ctl && bar.ctl.messageLog.length > 0
                          ? bar.ctl.messageLog[bar.ctl.messageLog.length - 1] : ""
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Item { visible: logCard.logOpen; Layout.fillWidth: true }
            }

            ScrollView {
                visible: logCard.logOpen
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    id: logArea
                    readOnly: true
                    wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                    font: Theme.fontMono
                    color: Theme.textPrimary
                    background: null
                    text: bar.ctl ? bar.ctl.messageLog.join("\n") : ""
                    onTextChanged: cursorPosition = length // keep scrolled to newest
                }
            }
        }

        MouseArea {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: logHeader.implicitHeight + Theme.spacing
            cursorShape: Qt.PointingHandCursor
            onClicked: logCard.logOpen = !logCard.logOpen
        }

        Behavior on implicitHeight { NumberAnimation { duration: Theme.animMs } }
    }
}
