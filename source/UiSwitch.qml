import QtQuick
import QtQuick.Controls
import Miniscope.Theme 1.0

// Themed switch. Two-way config binding: bind `syncChecked` to the backend
// value and handle `onToggled` to write it back (a user click never breaks
// the binding — syncChecked is a plain property the control never writes).
Switch {
    id: control

    property var syncChecked
    onSyncCheckedChanged: {
        var b = syncChecked === true
        if (checked !== b)
            checked = b
    }

    font: Theme.fontBody

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.surfaceAlt
        border.width: 1
        border.color: control.checked ? Theme.accent : Theme.border
        opacity: control.enabled ? 1.0 : 0.55
        Behavior on color { ColorAnimation { duration: Theme.animMs } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: parent.height - 6
            height: parent.height - 6
            radius: height / 2
            color: control.checked ? Theme.accentText : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: Theme.animMs } }
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + (control.text.length > 0 ? Theme.spacing : 0)
    }
}
