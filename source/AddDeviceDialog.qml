import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modal dialog for the user-config generator's "Add Device" action. The user
// picks a category (Miniscope vs Camera), a device type from the catalog
// (backend.deviceTypes()), a deviceID, and a name. A live scan of the connected
// video devices (backend.scanVideoDevices()) is shown as a hint so the deviceID
// can be matched to the right camera. On OK the chosen values are exposed via the
// category / deviceType / deviceID / deviceName properties and the built-in
// accepted() signal fires; main.qml forwards them to backend.addDevice().
Dialog {
    id: control
    title: "Add a device"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    // Results read by the caller (main.qml) on accept.
    property string category:   catCombo.currentText === "Miniscope" ? "miniscopes" : "cameras"
    property string deviceType: typeCombo.currentText
    property int    deviceID:   deviceIdSpin.value
    property string deviceName: nameField.text.trim()
    property string detectedDevices: ""

    // Reset the fields and refresh the connected-device scan each time it opens.
    onAboutToShow: {
        catCombo.currentIndex = 0;
        typeCombo.currentIndex = 0;
        deviceIdSpin.value = 0;
        nameField.text = catCombo.currentText === "Miniscope" ? "My Miniscope" : "My Camera";
        detectedDevices = backend.scanVideoDevices();
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

        Label { text: "Device ID"; font.pointSize: 12 }
        SpinBox {
            id: deviceIdSpin
            from: 0
            to: 63
            value: 0
            editable: true
            font.pointSize: 12
        }

        Label { text: "Name"; font.pointSize: 12 }
        TextField {
            id: nameField
            Layout.fillWidth: true
            font.pointSize: 12
            selectByMouse: true
            placeholderText: "e.g. My V4 Miniscope"
        }

        // Connected-device hint (deviceID -> name), spanning both columns.
        Label {
            Layout.columnSpan: 2
            Layout.topMargin: 6
            text: "Connected devices (use these IDs above):"
            font.pointSize: 11
            font.bold: true
        }
        Frame {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            ScrollView {
                anchors.fill: parent
                clip: true
                TextArea {
                    readOnly: true
                    wrapMode: TextArea.WordWrap
                    font.pointSize: 10
                    text: control.detectedDevices === "" ? "(no devices detected)"
                                                         : control.detectedDevices
                }
            }
        }
    }
}
