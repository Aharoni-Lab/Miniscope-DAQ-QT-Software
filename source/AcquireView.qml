import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// Acquire mode, v1: the session is running in the floating device/control
// windows (as before); this page anchors the session in the main window and
// provides the way back to Setup. The embedded pane grid and session bar
// replace this placeholder in later steps.
Item {
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 2 * Theme.padding)
        spacing: Theme.spacing * 2

        Rectangle { // status card
            Layout.fillWidth: true
            implicitHeight: statusCol.implicitHeight + 2 * Theme.padding
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                id: statusCol
                anchors.fill: parent
                anchors.margins: Theme.padding
                spacing: Theme.spacing

                RowLayout {
                    spacing: Theme.spacing
                    Rectangle { width: 10; height: 10; radius: 5; color: Theme.success }
                    Text {
                        text: qsTr("Acquisition session running")
                        font: Theme.fontTitle
                        color: Theme.textPrimary
                    }
                }
                Text {
                    text: backend ? backend.userConfigFileName : ""
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                    elide: Text.ElideLeft
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Device streams, the control panel, and any trace/tracker "
                               + "views are open as separate windows. Recording controls "
                               + "are on the control panel.")
                    wrapMode: Text.WordWrap
                    font: Theme.fontBody
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }
            }
        }

        UiButton {
            text: qsTr("End Session")
            danger: true
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            // Tears down all session windows/threads and returns to Setup -
            // load a different config (or the same one) and Run again.
            onClicked: backend.endSession()
        }
    }
}
