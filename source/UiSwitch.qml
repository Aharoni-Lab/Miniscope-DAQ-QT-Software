import QtQuick
import QtQuick.Controls
import Miniscope.Theme 1.0

// Themed switch. Two-way config binding: bind `syncChecked` to the backend
// value and handle `onToggled` to write it back (a user click never breaks
// the binding — syncChecked is a plain property the control never writes).
Switch {
    id: control

    // Override on fixed-dark surfaces (the video windows' control rail):
    // theme colors follow the app theme, which makes the label dark-on-dark
    // there in light mode.
    property color textColor: Theme.textPrimary

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
        color: control.enabled ? control.textColor : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        // The indicator alone is 40 px, so in a narrow container (the video
        // windows' control rail is as little as 150 px wide) the label had well
        // under half the width and was cut mid-word. Elide like UiButton does.
        elide: Text.ElideRight
        leftPadding: control.indicator.width + (control.text.length > 0 ? Theme.spacing : 0)
    }
}
