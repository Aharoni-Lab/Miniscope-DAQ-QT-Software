import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// Acquire mode: the session's windows (device streams, control panel, trace
// display) embedded as panes in a grid, each with pop-out to a floating
// window. Closing a floating pane docks it back — streams never stop from
// window management. Which panes float, and their floating geometry, persist
// per config file (backend.paneLayout / savePaneLayout), so a rig's operator
// gets their arrangement back on every run.
Item {
    id: acquireRoot

    readonly property var panes: backend ? backend.sessionPanes : []
    // Per-pane UI state keyed by pane name ({floating: bool}); reassigned
    // wholesale so bindings re-evaluate.
    property var paneStates: ({})
    property bool layoutLocked: false

    readonly property int embeddedCount: {
        var n = 0
        for (var i = 0; i < panes.length; i++)
            if (!isFloating(panes[i].name))
                n++
        return n
    }

    function isFloating(name) {
        var st = paneStates[name]
        return st !== undefined && st.floating === true
    }

    // Apply the saved layout when a session's panes appear (and reset state
    // when they clear at session end). Also on creation, in case the session
    // was already running when this view instantiated.
    onPanesChanged: restoreLayout()
    Component.onCompleted: restoreLayout()
    function restoreLayout() {
        var st = {}
        if (panes.length > 0) {
            var meta = backend.paneLayout("__layout")
            layoutLocked = meta.locked === true || meta.locked === "true"
        }
        for (var i = 0; i < panes.length; i++) {
            var p = panes[i]
            var saved = backend.paneLayout(p.name)
            var floating = saved.floating === true || saved.floating === "true"
            st[p.name] = { floating: floating }
            if (floating) {
                backend.setPaneEmbedded(p.window, false, p.aspect)
                if (saved.width !== undefined) {
                    p.window.x = Number(saved.x)
                    p.window.y = Number(saved.y)
                    p.window.width = Number(saved.width)
                    p.window.height = Number(saved.height)
                }
            } else {
                // Clear the window's own aspect lock / minimum size so it
                // follows its container; the pane letterboxes instead.
                backend.setPaneEmbedded(p.window, true, p.aspect)
            }
        }
        paneStates = st
    }

    function setFloating(pane, floating) {
        var st = paneStates
        if (!st[pane.name])
            st[pane.name] = {}
        st[pane.name].floating = floating
        paneStates = st // containers attach/detach via their window binding
        backend.setPaneEmbedded(pane.window, !floating, pane.aspect)
        saveFloatingState(pane)
    }

    function saveFloatingState(pane) {
        backend.savePaneLayout(pane.name, isFloating(pane.name)
            ? { floating: true, x: pane.window.x, y: pane.window.y,
                width: pane.window.width, height: pane.window.height }
            : { floating: false })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.padding
        spacing: Theme.spacing

        // --- Session bar: transport, telemetry, layout lock, End Session --------
        SessionBar {
            Layout.fillWidth: true
            layoutLocked: acquireRoot.layoutLocked
            onLockToggled: locked => {
                acquireRoot.layoutLocked = locked
                backend.savePaneLayout("__layout", { locked: locked })
            }
        }

        // --- Pane grid -----------------------------------------------------------
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: acquireRoot.embeddedCount <= 1 ? 1
                   : acquireRoot.embeddedCount <= 4 ? 2 : 3
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacing

            Repeater {
                model: acquireRoot.panes

                delegate: Rectangle {
                    id: paneFrame
                    required property var modelData
                    readonly property bool floating: acquireRoot.isFloating(modelData.name)

                    visible: !floating // floating panes leave the grid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    // Closing a floating pane window (title-bar X) docks it
                    // back. QQuickWindow::closing fires only on a real user
                    // close - visibility-based triggers dock the pane the
                    // moment a drag starts, because macOS flickers the window's
                    // visible state while re-establishing the dragged native
                    // window. Geometry changes while floating are saved,
                    // debounced.
                    Connections {
                        target: paneFrame.modelData.window
                        enabled: paneFrame.floating
                        function onClosing(close) {
                            if (backend.sessionActive)
                                acquireRoot.setFloating(paneFrame.modelData, false)
                        }
                        function onXChanged() { saveTimer.restart() }
                        function onYChanged() { saveTimer.restart() }
                        function onWidthChanged() { saveTimer.restart() }
                        function onHeightChanged() { saveTimer.restart() }
                    }
                    Timer {
                        id: saveTimer
                        interval: 400
                        onTriggered: if (paneFrame.floating) acquireRoot.saveFloatingState(paneFrame.modelData)
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0

                        // Pane header: name + pop-out
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 26
                            color: Theme.surfaceAlt
                            radius: Theme.radiusSmall

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacing
                                anchors.rightMargin: 2
                                spacing: Theme.spacing

                                Text {
                                    text: paneFrame.modelData.name
                                    font: Theme.fontSmall
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                // Pop out to a floating window
                                Rectangle {
                                    visible: !acquireRoot.layoutLocked
                                    width: 22; height: 22
                                    radius: Theme.radiusSmall
                                    color: popOutArea.containsMouse ? Theme.surface : "transparent"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "↗"
                                        font: Theme.fontSmall
                                        color: Theme.textSecondary
                                    }
                                    MouseArea {
                                        id: popOutArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: acquireRoot.setFloating(paneFrame.modelData, true)
                                    }
                                    ToolTip.visible: popOutArea.containsMouse
                                    ToolTip.delay: 600
                                    ToolTip.text: qsTr("Pop out into a floating window (close it to dock it back)")
                                }
                            }
                        }

                        // Pane body: the device/panel window, letterboxed to its
                        // native aspect for video panes.
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            WindowContainer {
                                window: paneFrame.floating ? null : paneFrame.modelData.window
                                anchors.centerIn: parent
                                width: paneFrame.modelData.aspect > 0
                                       ? Math.min(parent.width, parent.height * paneFrame.modelData.aspect)
                                       : parent.width
                                height: paneFrame.modelData.aspect > 0
                                        ? width / paneFrame.modelData.aspect
                                        : parent.height
                            }
                        }
                    }
                }
            }

            // All panes floating: keep a hint where the grid would be.
            Text {
                visible: acquireRoot.embeddedCount === 0 && acquireRoot.panes.length > 0
                text: qsTr("All panes are floating. Close a floating window to dock it back here.")
                font: Theme.fontBody
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
