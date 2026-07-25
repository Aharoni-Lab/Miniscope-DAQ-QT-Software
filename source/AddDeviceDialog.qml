import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// Modal dialog for the user-config generator's "Add Device" action. The user
// picks a category (Miniscope vs Camera), a device type from the catalog
// (backend.deviceTypes()), a deviceID, and a name. The deviceID dropdown only
// offers IDs not already used by another device (backend.availableDeviceIDs()),
// each labelled with the connected-device name when known, so two devices can't
// share an ID. A full scan of connected devices is also shown as a hint. On OK
// the chosen values are exposed via the category / deviceType / deviceID /
// deviceName properties and the built-in accepted() signal fires; SetupView.qml
// forwards them to backend.addDevice().
Dialog {
    id: control
    title: "Add a device"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    // Results read by the caller (SetupView.qml) on accept. deviceID is the
    // leading integer of the dropdown label (e.g. "0  (Asus Webcam)" -> 0).
    property string category:   catCombo.currentText === "Miniscope" ? "miniscopes" : "cameras"
    property string deviceType: typeCombo.currentText
    property int    deviceID:   idCombo.currentText.length > 0 ? parseInt(idCombo.currentText) : 0
    property string deviceName: nameField.text.trim()
    property string detectedDevices: ""

    // Reset the fields and refresh the live device info each time it opens.
    onAboutToShow: {
        catCombo.currentIndex = 0;
        typeCombo.currentIndex = 0;
        nameField.text = catCombo.currentText === "Miniscope" ? "My Miniscope" : "My Camera";
        idCombo.model = backend.availableDeviceIDs();   // unused IDs only
        idCombo.currentIndex = 0;
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

        Label { text: "Category"; font: Theme.fontBody; color: Theme.textPrimary }
        UiComboBox {
            id: catCombo
            Layout.fillWidth: true
            model: [ "Miniscope", "Camera" ]
            // Changing the category re-filters the device-type dropdown (via its
            // model binding), so reset it to the new category's first type. Also
            // refresh the suggested name unless the user already typed their own.
            onActivated: {
                typeCombo.currentIndex = 0;
                if (nameField.text === "My Miniscope" || nameField.text === "My Camera"
                        || nameField.text.trim() === "")
                    nameField.text = currentText === "Miniscope" ? "My Miniscope" : "My Camera";
            }
        }

        Label { text: "Device type"; font: Theme.fontBody; color: Theme.textPrimary }
        UiComboBox {
            id: typeCombo
            Layout.fillWidth: true
            // Only the types valid for the category chosen above, filtered by the
            // backend from deviceConfigs/videoDevices.json. Re-evaluates whenever
            // the category changes.
            model: backend ? backend.deviceTypesForCategory(
                       catCombo.currentText === "Miniscope" ? "miniscopes" : "cameras") : []
        }

        Label { text: "Device ID"; font: Theme.fontBody; color: Theme.textPrimary }
        UiComboBox {
            id: idCombo
            Layout.fillWidth: true
            // Populated in onAboutToShow with only the unused IDs.
        }

        Label { text: "Name"; font: Theme.fontBody; color: Theme.textPrimary }
        UiTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "e.g. My V4 Miniscope"
        }

        // Full connected-device scan (incl. already-used IDs), spanning both columns.
        Label {
            Layout.columnSpan: 2
            Layout.topMargin: 6
            text: "Connected devices:"
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
