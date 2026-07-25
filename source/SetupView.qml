import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Miniscope.Theme 1.0

// Setup mode: pick / create / edit a user config, then Run.
// V1 of the ui-v3 shell keeps the JSON tree editor; the schema-driven form
// editor replaces it in a later step. All actions and dialogs from the old
// launcher window (main.qml) live on here.
Item {
    id: setupRoot

    // True once a config is open in the editor (selected, dropped, or new).
    // Replaces the old imperative treeView.visible / view.visible toggling.
    property bool configOpen: false

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
        onAccepted: {
            var path = backend.urlToLocalFile(saveConfigDialog.selectedFile)
            backend.saveConfigObjectAs(path)
            saveMessageDialog.savedPath = path
            saveMessageDialog.open()
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
                onClicked: fileDialog.open()
            }
            UiButton {
                text: qsTr("New")
                onClicked: {
                    backend.newUserConfig()
                    setupRoot.configOpen = true
                }
            }
            UiButton {
                text: qsTr("Save As…")
                enabled: backend ? backend.userConfigOK : false
                onClicked: {
                    saveConfigDialog.selectedFile = backend.localFileToUrl(backend.userConfigFileName)
                    saveConfigDialog.open()
                }
            }
            UiButton {
                text: qsTr("Scan Devices")
                onClicked: {
                    deviceScanDialog.text = backend.scanVideoDevices()
                    deviceScanDialog.open()
                }
            }
            UiButton {
                text: qsTr("+ Add Device")
                primary: true
                visible: setupRoot.configOpen
                onClicked: addDeviceDialog.open()
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

        // Config editor (the JSON tree, until the form editor replaces it)
        Rectangle {
            visible: setupRoot.configOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacing

                TreeViewerJSON {
                    id: treeView
                    model: backend ? backend.jsonTreeModel : null
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                // Help text for the focused config field
                Text {
                    visible: treeView.toolTipText.length > 0
                    text: treeView.toolTipText
                    wrapMode: Text.WordWrap
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                    Layout.maximumHeight: 80
                }
            }
        }

        // Run
        UiButton {
            text: qsTr("▶  Run")
            primary: true
            font: Theme.fontTitle
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget + 8
            enabled: backend ? (backend.userConfigOK && backend.hasDevices) : false
            onClicked: backend.onRunClicked()
        }
    }
}
