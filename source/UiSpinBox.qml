import QtQuick
import QtQuick.Controls
import Miniscope.Theme 1.0

// Themed integer spin box. Two-way config binding: bind `syncValue` to the
// backend value and handle `onValueModified` to write it back (user edits
// never break the binding — syncValue is a plain property the control itself
// never writes).
SpinBox {
    id: control

    property var syncValue
    onSyncValueChanged: {
        var v = Number(syncValue)
        if (!isNaN(v) && value !== v)
            value = v
    }

    editable: true
    font: Theme.fontBody
    implicitHeight: Theme.controlHeight
    implicitWidth: 140

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
    }

    up.indicator: Rectangle {
        x: parent.width - width
        height: parent.height
        implicitWidth: Theme.controlHeight
        color: control.up.pressed ? Theme.surfaceAlt : "transparent"
        Text {
            text: "+"
            font: Theme.fontBody
            color: control.enabled ? Theme.textPrimary : Theme.textDisabled
            anchors.centerIn: parent
        }
    }

    down.indicator: Rectangle {
        height: parent.height
        implicitWidth: Theme.controlHeight
        color: control.down.pressed ? Theme.surfaceAlt : "transparent"
        Text {
            text: "−"
            font: Theme.fontBody
            color: control.enabled ? Theme.textPrimary : Theme.textDisabled
            anchors.centerIn: parent
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
