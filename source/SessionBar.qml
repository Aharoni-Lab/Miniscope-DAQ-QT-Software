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
    objectName: "sessionBar"

    property bool layoutLocked: false
    signal lockToggled(bool locked)

    // Pane grid controls (handled by AcquireView). 0 columns = automatic.
    property int gridColumns: 0
    signal gridColumnsPicked(int columns)
    signal resetLayoutRequested()

    readonly property var ctl: backend ? backend.sessionControl : null
    readonly property bool recording: ctl ? ctl.recording : false
    // Where this session's recording went; empty until the first record start.
    readonly property string recordDirectory: backend ? backend.recordDirectory : ""

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

    // --- Row 1: status / transport / grid / disk / End session -----------------
    // A Flow, not a RowLayout: this row's controls need ~1200 px, so on any
    // window narrower than that a RowLayout ran off the right edge and clipped
    // whatever didn't fit (End session included). Wrapping keeps every control
    // reachable at any window size.
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacing * 2

        // Status light + label + record clock stay welded together on one line.
        RowLayout {
            spacing: Theme.spacing
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

        // Reveal the recording's own folder (dataDirectory plus whatever
        // date/time/name folders directoryStructure produced) in the file
        // manager. Available as soon as the first recording of the session
        // starts, and stays pointed at it after the recording stops.
        UiButton {
            objectName: "openDataFolderButton"
            text: qsTr("📂 Data folder")
            enabled: bar.recordDirectory.length > 0
            onClicked: {
                if (!backend.openDirectory(bar.recordDirectory) && bar.ctl)
                    bar.ctl.receiveMessage(qsTr("Error: could not open %1")
                                               .arg(bar.recordDirectory))
            }
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: bar.recordDirectory.length > 0
                          ? qsTr("Open %1").arg(bar.recordDirectory)
                          : qsTr("Available once this session has recorded something")
        }

        // Disk space on the recording target
        Text {
            visible: bar.diskFreeBytes >= 0
            text: qsTr("%1 GB free").arg((bar.diskFreeBytes / 1e9).toFixed(1))
            font: Theme.fontSmall
            color: bar.diskFreeBytes < 5e9 ? Theme.danger
                 : bar.diskFreeBytes < 20e9 ? Theme.warning
                 : Theme.textSecondary
        }

        // --- Pane grid: the label and its two controls wrap as one unit ---
        RowLayout {
            spacing: Theme.spacing
            Text {
                text: qsTr("Columns")
                font: Theme.fontSmall
                color: Theme.textSecondary
            }
            UiComboBox {
                implicitWidth: 88
                enabled: !bar.layoutLocked
                model: [qsTr("Auto"), "1", "2", "3", "4"]
                syncValue: bar.gridColumns === 0 ? qsTr("Auto") : String(bar.gridColumns)
                onActivated: bar.gridColumnsPicked(currentIndex === 0 ? 0 : parseInt(currentText))
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("How many panes per row. Auto picks 1, 2, or 3 by device count.")
            }
            UiButton {
                text: qsTr("Reset")
                enabled: !bar.layoutLocked
                onClicked: bar.resetLayoutRequested()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Put every pane back in the automatic grid, evenly sized.")
            }
        }

        UiSwitch {
            text: qsTr("Lock layout")
            syncChecked: bar.layoutLocked
            onToggled: bar.lockToggled(checked)
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Freeze the arrangement: no dragging panes, resizing them, or popping them out.")
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
    // Also a Flow: one telemetry chip per device, so a four-device rig overran
    // the window here too.
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacing

        UiTextField {
            id: noteField
            width: 320
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

    // --- Messages -------------------------------------------------------------
    // Everything the session has to say lands here: a device that failed to
    // connect, a saver that could not write a file, a commutator nothing
    // answered. One elided line went unnoticed, so the collapsed card keeps the
    // most recent few messages on screen, colors them by severity, and carries
    // a running error/warning count so a problem from earlier in the session
    // stays visible after it scrolls out of the tail.
    readonly property var messages: ctl ? ctl.messageLog : []
    // Severity comes from the message text: every emitter prefixes "ERROR:" or
    // "Warning:" (backend / commutator / datasaver / behaviorTracker).
    function severityOf(line) {
        var l = line.toLowerCase()
        if (l.indexOf("error") >= 0 || l.indexOf("failed") >= 0)
            return 2
        if (l.indexOf("warn") >= 0)
            return 1
        return 0
    }
    function severityColor(severity) {
        return severity === 2 ? Theme.danger
             : severity === 1 ? Theme.warning
                              : Theme.textSecondary
    }
    readonly property int tailLines: 4
    readonly property var recentMessages: messages.slice(-tailLines)
    readonly property int errorCount: messages.filter(function (m) { return severityOf(m) === 2 }).length
    readonly property int warningCount: messages.filter(function (m) { return severityOf(m) === 1 }).length

    // The colored outline is an ALERT, not a state: it says "something just
    // happened", and it expires. A border driven straight off the cumulative
    // counts meant one benign warning at startup - a commutator reporting no
    // rotation from its first samples, say - left the card ringed for the rest
    // of the session, which reads as an unresolved fault. The counts in the
    // header stay for the whole session, so nothing is forgotten; only the
    // attention-grab is temporary. Errors hold the ring longer than warnings.
    readonly property int warningAlertMs: 15000
    readonly property int errorAlertMs: 60000
    property int alertSeverity: 0
    property int seenMessageCount: 0

    function raiseAlert(severity) {
        // A warning arriving mid-error-alert must not quietly downgrade the ring
        // (or extend it - the error is what still matters).
        if (severity < alertSeverity && alertTimer.running)
            return
        alertSeverity = severity
        alertTimer.interval = severity === 2 ? errorAlertMs : warningAlertMs
        alertTimer.restart()
    }
    // What alertTimer does when it fires; named so it can be driven directly.
    function expireAlert() {
        alertTimer.stop()
        alertSeverity = 0
    }

    onMessagesChanged: {
        // A new session installs a new controller, so the log restarts: drop the
        // previous session's alert rather than carry its ring over.
        if (messages.length < seenMessageCount) {
            seenMessageCount = messages.length
            expireAlert()
            return
        }
        var worst = 0
        for (var i = seenMessageCount; i < messages.length; i++)
            worst = Math.max(worst, severityOf(messages[i]))
        seenMessageCount = messages.length
        if (worst > 0)
            raiseAlert(worst)
    }

    Timer { id: alertTimer; onTriggered: bar.expireAlert() }

    Rectangle {
        id: logCard
        objectName: "sessionMessages"
        Layout.fillWidth: true
        implicitHeight: logHeader.implicitHeight + Theme.spacing
                        + (logOpen ? 220 : messageTail.implicitHeight)
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: 1
        // The card carries the severity of the newest message for as long as the
        // alert lasts, so a problem is visible from across the room without
        // reading the text - then it fades back out (see raiseAlert).
        border.color: bar.alertSeverity === 2 ? Theme.danger
                    : bar.alertSeverity === 1 ? Theme.warning
                                              : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.animMs * 4 } }
        clip: true

        property bool logOpen: false

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
                    text: qsTr("Messages")
                    font: Theme.fontSmall
                    color: Theme.textPrimary
                }
                Text {
                    visible: bar.errorCount > 0
                    text: qsTr("%n error(s)", "", bar.errorCount)
                    font: Theme.fontSmall
                    color: Theme.danger
                }
                Text {
                    visible: bar.warningCount > 0
                    text: qsTr("%n warning(s)", "", bar.warningCount)
                    font: Theme.fontSmall
                    color: Theme.warning
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: logCard.logOpen ? qsTr("%n message(s) — click to collapse", "", bar.messages.length)
                                          : qsTr("%n message(s) — click for the full log", "", bar.messages.length)
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                }
            }

            // Collapsed: the tail, oldest of the few at the top so it reads in
            // the same direction as the full log.
            ColumnLayout {
                id: messageTail
                visible: !logCard.logOpen
                Layout.fillWidth: true
                spacing: 0

                // Always tailLines rows: the blanks hold the card at a constant
                // height, so the pane grid below doesn't jump every time a
                // message arrives.
                Repeater {
                    model: bar.tailLines
                    delegate: Text {
                        required property int index
                        readonly property int pad: bar.tailLines - bar.recentMessages.length
                        readonly property string line:
                            index < pad ? "" : bar.recentMessages[index - pad]
                        Layout.fillWidth: true
                        text: line.length > 0 ? line
                            : (bar.messages.length === 0 && index === bar.tailLines - 1)
                              ? qsTr("No messages yet.") : " "
                        font: Theme.fontMono
                        color: bar.severityColor(bar.severityOf(line))
                        elide: Text.ElideRight
                    }
                }
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
                    text: bar.messages.join("\n")
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
