import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modal dialog for the user-config generator's "Add Device" action. The user
// picks a category (Miniscope vs Camera), a device type from the catalog
// (backend.deviceTypes()), and a name. On OK the chosen values are exposed via the
// category / deviceType / deviceName properties and the built-in accepted() signal
// fires; main.qml forwards them to backend.addDevice().
Dialog {
    id: control
    title: "Add a device"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 440
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    // Results read by the caller (main.qml) on accept.
    property string category:   catCombo.currentText === "Miniscope" ? "miniscopes" : "cameras"
    property string deviceType: typeCombo.currentText
    property string deviceName: nameField.text.trim()

    // Reset the fields each time the dialog is shown.
    onAboutToShow: {
        catCombo.currentIndex = 0;
        typeCombo.currentIndex = 0;
        nameField.text = catCombo.currentText === "Miniscope" ? "My Miniscope" : "My Camera";
    }

    // Keep OK disabled until a name is entered (backend.addDevice also guards).
    Component.onCompleted: {
        var ok = control.standardButton(Dialog.Ok);
        if (ok)
            ok.enabled = Qt.binding(function () { return nameField.text.trim().length > 0; });
    }

    GridLayout {
        columns: 2
        columnSpacing: 10
        rowSpacing: 12
        width: parent.width

        Label { text: "Category"; font.pointSize: 12 }
        ComboBox {
            id: catCombo
            Layout.fillWidth: true
            font.pointSize: 12
            model: [ "Miniscope", "Camera" ]
            // Refresh the suggested name to match the category unless the user
            // already typed their own.
            onActivated: {
                if (nameField.text === "My Miniscope" || nameField.text === "My Camera"
                        || nameField.text.trim() === "")
                    nameField.text = currentText === "Miniscope" ? "My Miniscope" : "My Camera";
            }
        }

        Label { text: "Device type"; font.pointSize: 12 }
        ComboBox {
            id: typeCombo
            Layout.fillWidth: true
            font.pointSize: 12
            // All supported types from deviceConfigs/videoDevices.json; the user
            // picks the category explicitly above.
            model: backend.deviceTypes()
        }

        Label { text: "Name"; font.pointSize: 12 }
        TextField {
            id: nameField
            Layout.fillWidth: true
            font.pointSize: 12
            selectByMouse: true
            placeholderText: "e.g. My V4 Miniscope"
        }
    }
}
