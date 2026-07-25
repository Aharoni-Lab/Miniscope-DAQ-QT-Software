import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Miniscope.Theme 1.0

// The ui-v3 application shell: ONE main window with two modes.
//   Setup   - load/create/edit a user config, then Run.
//   Acquire - the running session; End Session returns to Setup, so configs
//             can be switched without restarting the app.
// The mode is not local UI state: it follows backend.sessionActive, so the
// shell can never disagree with the backend about whether a session exists.
ApplicationWindow {
    id: root
    visible: true
    // Wide enough for a 2-column pane grid in Acquire; Setup centers its form.
    width: 1280
    height: 940
    minimumWidth: 720
    minimumHeight: 560
    color: Theme.background
    title: qsTr("Miniscope DAQ")

    // Closing the main window ends the session (if any) and quits.
    onClosing: backend.exitClicked()

    Settings {
        id: uiSettings
        category: "ui"
        property bool darkTheme: true
    }
    Component.onCompleted: ThemeState.dark = uiSettings.darkTheme

    // --- Header bar -----------------------------------------------------------
    header: Rectangle {
        height: 56
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.padding
            anchors.rightMargin: Theme.padding
            spacing: Theme.spacing * 2

            ColumnLayout {
                spacing: 0
                Text {
                    text: "Miniscope DAQ"
                    font: Theme.fontTitle
                    color: Theme.textPrimary
                }
                RowLayout {
                    spacing: Theme.spacing
                    Text {
                        // The loaded config, so the operator always knows which rig
                        // definition is active - in both modes.
                        text: backend && backend.userConfigFileName.length > 0
                              ? backend.userConfigFileName
                              : (backend && backend.configDirty ? qsTr("unsaved new config")
                                                                : qsTr("no config loaded"))
                        font: Theme.fontSmall
                        color: Theme.textSecondary
                        elide: Text.ElideLeft
                        Layout.maximumWidth: 420
                    }
                    Text {
                        // Unsaved-changes marker; the Save button in Setup clears it.
                        visible: backend ? backend.configDirty : false
                        text: qsTr("· edited")
                        font: Theme.fontSmall
                        color: Theme.warning
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Session state chip: quiet in Setup, unmissable in Acquire.
            Rectangle {
                visible: backend ? backend.sessionActive : false
                radius: Theme.radiusSmall
                color: Theme.surfaceAlt
                implicitHeight: 26
                implicitWidth: sessionChipRow.implicitWidth + Theme.spacing * 2
                RowLayout {
                    id: sessionChipRow
                    anchors.centerIn: parent
                    spacing: Theme.spacing
                    Rectangle { width: 8; height: 8; radius: 4; color: Theme.success }
                    Text {
                        text: qsTr("session running")
                        font: Theme.fontSmall
                        color: Theme.textPrimary
                    }
                }
            }

            RowLayout {
                spacing: Theme.spacing
                Text {
                    text: Theme.dark ? qsTr("Dark") : qsTr("Light")
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                }
                Switch {
                    checked: !Theme.dark
                    onToggled: {
                        // Shared across every window's engine (control panel etc.).
                        ThemeState.dark = !checked
                        uiSettings.darkTheme = !checked
                    }
                }
            }

            UiButton {
                text: qsTr("Help")
                onClicked: helpDialog.open()
            }
        }

        Rectangle { // bottom hairline
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // --- Mode host --------------------------------------------------------------
    StackLayout {
        anchors.fill: parent
        currentIndex: backend && backend.sessionActive ? 1 : 0

        SetupView { }
        AcquireView { }
    }

    // --- Help ---------------------------------------------------------------------
    Dialog {
        id: helpDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(620, root.width - 2 * Theme.padding)
        padding: Theme.padding

        // Fully explicit styling: the Basic-style title/button areas follow the
        // OS palette, which is pinned light for the legacy windows' sake.
        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius
            border.color: Theme.border
            border.width: 1
        }
        header: null
        footer: null

        contentItem: Column {
            spacing: Theme.spacing * 2

            Text {
                text: qsTr("Miniscope DAQ Help")
                font: Theme.fontTitle
                color: Theme.textPrimary
            }

            Text {
                id: helpText
                width: helpDialog.availableWidth
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                font: Theme.fontBody
                color: Theme.textPrimary
                linkColor: Theme.accent
                text: "Miniscope DAQ Software version " + (backend ? backend.versionNumber : "") + "<br/>" +
                  (backend ? backend.buildInfo : "") + "<br/><br/>" +
                  "Developed by the <a href='https://aharoni-lab.github.io/'>Aharoni Lab</a>, UCLA<br/>" +
                  "Overview of the UCLA Miniscope project: <a href='http://www.miniscope.org'>miniscope.org</a><br/>" +
                  "Miniscope Wiki: <a href='https://github.com/Aharoni-Lab/Miniscope-v4/wiki'>github.com/Aharoni-Lab/Miniscope-v4/wiki</a><br/>" +
                  "Discussion board: <a href='https://groups.google.com/d/forum/miniscope'>groups.google.com/d/forum/miniscope</a><br/>" +
                  "Issues and suggestions: <a href='https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software'>github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software</a><br/><br/>" +
                  "Icons from <a href='https://icons8.com/'>icons8</a>"
                onLinkActivated: link => Qt.openUrlExternally(link)
                HoverHandler {
                    cursorShape: helpText.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }

            UiButton {
                text: qsTr("Close")
                width: parent.width
                onClicked: helpDialog.close()
            }
        }
    }
}
