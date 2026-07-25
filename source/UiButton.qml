import QtQuick
import QtQuick.Controls
import Miniscope.Theme 1.0

// The app's standard button. `primary: true` for the one main action on a
// screen (accent-filled); default is a quiet surface button. All colors and
// metrics come from the Theme singleton.
Button {
    id: control

    property bool primary: false
    // Destructive/attention actions (End Session, Stop) tint the text.
    property bool danger: false

    implicitHeight: Theme.controlHeight
    font: Theme.fontBody
    hoverEnabled: true

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled
             : control.primary ? Theme.accentText
             : control.danger ? Theme.danger
             : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.primary
               ? (control.hovered && control.enabled ? Theme.accentHover : Theme.accent)
               : (control.hovered && control.enabled ? Theme.surfaceAlt : Theme.surface)
        border.width: control.primary ? 0 : 1
        border.color: Theme.border
        opacity: control.enabled ? 1.0 : 0.55
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
    }
}
