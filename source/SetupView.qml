import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Miniscope.Theme 1.0

// Setup mode: pick / create / edit a user config, then Run. The editor is
// the schema-driven card form (ConfigForm); all actions and dialogs from the
// old launcher window (main.qml) live on here.
Item {
    id: setupRoot

    // True once a config is open in the editor (selected, dropped, or new).
    // Replaces the old imperative treeView.visible / view.visible toggling.
    property bool configOpen: false

    // Save the config to its own file; a brand-new config (no path yet) routes
    // through the Save-As dialog. `andThen` (optional) runs after the save
    // completes — used by the unsaved-changes prompt to chain Run/Open/New.
    function saveInPlace(andThen) {
        if (backend.userConfigFileName.length > 0) {
            backend.saveConfigObjectAs(backend.userConfigFileName)
            if (andThen)
                andThen()
        } else {
            saveConfigDialog.pendingAction = andThen ? andThen : null
            saveConfigDialog.open()
        }
    }

    // Gate an action behind the unsaved-changes prompt. Runs it immediately
    // when the config is clean.
    function confirmIfDirty(actionLabel, action) {
        if (backend.configDirty) {
            unsavedDialog.actionLabel = actionLabel
            unsavedDialog.action = action
            unsavedDialog.open()
        } else {
            action()
        }
    }

    // --- Dialogs -----------------------------------------------------------------
    FileDialog {
        id: fileDialog
        title: qsTr("Please choose a user configuration file.")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        onAccepted: {
            backend.userConfigFileName = fileDialog.selectedFile
            setupRoot.configOpen = true
        }
    }

    FileDialog {
        id: saveConfigDialog
        title: qsTr("Save user configuration as…")
        fileMode: FileDialog.SaveFile
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        defaultSuffix: "json"
        // Set by saveInPlace() when a new config is saved on the way to
        // another action (Run/Open/New); null for a plain Save As.
        property var pendingAction: null
        onAccepted: {
            var path = backend.urlToLocalFile(saveConfigDialog.selectedFile)
            backend.saveConfigObjectAs(path)
            if (pendingAction) {
                var action = pendingAction
                pendingAction = null
                action()
            } else {
                saveMessageDialog.savedPath = path
                saveMessageDialog.open()
            }
        }
        onRejected: pendingAction = null
    }

    // Save-before-X prompt: editing is live in memory, but an experiment
    // should not run (or a config close) with changes that exist nowhere on
    // disk unless the user says so.
    Dialog {
        id: unsavedDialog
        property string actionLabel: ""
        property var action: null
        title: qsTr("Unsaved changes")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        closePolicy: Popup.CloseOnEscape

        // What to do once this dialog is actually gone. Run blocks the GUI thread
        // for seconds, so running it straight from a button handler left the
        // dialog painted on screen for the whole start-up - it looked like the
        // click had done nothing. `closed` fires after the exit transition, and
        // callLater gives the shell one more turn to paint its startup overlay.
        property var pendingAction: null
        onClosed: {
            if (!pendingAction)
                return
            var next = pendingAction
            pendingAction = null
            Qt.callLater(next)
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacing * 2
            Text {
                text: backend && backend.userConfigFileName.length > 0
                      ? qsTr("This configuration has changes that are not saved to\n%1").arg(backend.userConfigFileName)
                      : qsTr("This new configuration has not been saved to a file yet.")
                font: Theme.fontBody
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: Theme.spacing
                Item { Layout.fillWidth: true }
                UiButton {
                    text: qsTr("Cancel")
                    onClicked: {
                        unsavedDialog.pendingAction = null
                        unsavedDialog.close()
                    }
                }
                UiButton {
                    text: qsTr("%1 without saving").arg(unsavedDialog.actionLabel)
                    onClicked: {
                        unsavedDialog.pendingAction = unsavedDialog.action
                        unsavedDialog.close()
                    }
                }
                UiButton {
                    text: qsTr("Save and %1").arg(unsavedDialog.actionLabel)
                    primary: true
                    onClicked: {
                        var run = unsavedDialog.action
                        unsavedDialog.pendingAction = function () {
                            setupRoot.saveInPlace(run)
                        }
                        unsavedDialog.close()
                    }
                }
            }
        }
    }

    MessageDialog {
        id: errorMessageDialog
        title: qsTr("User Config File Error")
        text: qsTr("The selected user configuration file contains device name repeats. Please edit the file so each device name is unique.")
    }

    MessageDialog {
        id: errorMessageDialogCompression
        title: qsTr("User Config File Error")
        text: qsTr("The selected user configuration file contains video compression(s) that are not supported by your computer. Please edit the file so each 'compression' entry is a supported option from the following list: ")
              + (backend ? backend.availableCodecList : "")
    }

    MessageDialog {
        id: saveMessageDialog
        property string savedPath: ""
        title: qsTr("User Config File Saved")
        text: qsTr("The user config file has been saved to ") + savedPath
    }

    MessageDialog {
        id: deviceScanDialog
        title: qsTr("Connected Video Devices")
    }

    AddDeviceDialog {
        id: addDeviceDialog
        onAccepted: backend.addDevice(category, deviceType, deviceName, deviceID)
    }

    Connections {
        target: backend
        function onShowErrorMessage() { errorMessageDialog.open() }
        function onShowErrorMessageCompression() { errorMessageDialogCompression.open() }
    }

    Component.onCompleted: {
        // Default the open/save dialogs to the configured folder (set via
        // MINISCOPE_USERCONFIG_DIR, e.g. by the AppImage first-run prompt).
        var cfgFolder = backend.defaultUserConfigFolderUrl()
        if (cfgFolder && cfgFolder.toString().length > 0) {
            fileDialog.currentFolder = cfgFolder
            saveConfigDialog.currentFolder = cfgFolder
        }
    }

    // --- Content -----------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.padding
        spacing: Theme.spacing * 2

        // Action bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Text {
                text: qsTr("Configuration")
                font: Theme.fontTitle
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }

            UiButton {
                text: qsTr("Open…")
                onClicked: setupRoot.confirmIfDirty(qsTr("Open"), function() { fileDialog.open() })
            }
            UiButton {
                text: qsTr("New")
                onClicked: setupRoot.confirmIfDirty(qsTr("New"), function() {
                    backend.newUserConfig()
                    setupRoot.configOpen = true
                })
            }
            UiButton {
                text: qsTr("Save")
                visible: setupRoot.configOpen
                enabled: backend ? (backend.configDirty && backend.userConfigOK) : false
                onClicked: setupRoot.saveInPlace(null)
            }
            UiButton {
                text: qsTr("Save as…")
                enabled: backend ? backend.userConfigOK : false
                onClicked: {
                    saveConfigDialog.pendingAction = null
                    saveConfigDialog.selectedFile = backend.localFileToUrl(backend.userConfigFileName)
                    saveConfigDialog.open()
                }
            }
        }

        // Migration notes / schema warnings from the config load. Warnings only -
        // nothing here blocks Run.
        Rectangle {
            visible: setupRoot.configOpen && backend && backend.configCheckNotes.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(110, configCheckText.implicitHeight + Theme.padding)
            radius: Theme.radiusSmall
            color: Theme.surfaceAlt
            border.color: Theme.warning
            border.width: 1

            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spacing
                TextArea {
                    id: configCheckText
                    text: qsTr("Config check:\n") + (backend ? backend.configCheckNotes : "")
                    readOnly: true
                    wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                    font: Theme.fontSmall
                    color: Theme.textPrimary
                    background: null
                }
            }
        }

        // Welcome / drop target (shown until a config is opened)
        Rectangle {
            visible: !setupRoot.configOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            border.width: 1

            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.padding
                TextArea {
                    text: backend ? backend.userConfigDisplay : ""
                    readOnly: true
                    wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                    font: Theme.fontBody
                    color: Theme.textSecondary
                    background: null
                }
            }

            DropArea {
                anchors.fill: parent
                onDropped: drop => {
                    if (drop.hasUrls) {
                        backend.userConfigFileName = drop.urls[0]
                        setupRoot.configOpen = true
                    }
                }
            }
        }

        // Config editor: the schema-driven card form + raw-JSON tab.
        ConfigForm {
            visible: setupRoot.configOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            onAddDeviceRequested: addDeviceDialog.open()
            onScanDevicesRequested: {
                deviceScanDialog.text = backend.scanVideoDevices()
                deviceScanDialog.open()
            }
        }

        // Run
        UiButton {
            text: backend && backend.starting ? qsTr("Starting…") : qsTr("▶ Run")
            primary: true
            font: Theme.fontTitle
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget + 8
            enabled: backend ? (backend.userConfigOK && backend.hasDevices
                                && !backend.starting) : false
            onClicked: setupRoot.confirmIfDirty(qsTr("Run"), function() { backend.onRunClicked() })
        }
    }
}
