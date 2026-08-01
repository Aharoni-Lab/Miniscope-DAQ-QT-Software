import QtQuick
import QtQuick.Controls
import Miniscope.Theme 1.0

// Themed single-line text field. Two-way config binding: bind `syncText` to
// the backend value and handle `onTextEdited` to write it back — user typing
// never breaks the binding (syncText is a plain property the control itself
// never writes), and backend echoes of the same string leave the cursor alone.
TextField {
    id: control

    property var syncText
    onSyncTextChanged: {
        var s = (syncText === undefined || syncText === null) ? "" : String(syncText)
        if (text !== s)
            text = s
    }

    implicitHeight: Theme.controlHeight
    font: Theme.fontBody
    color: enabled ? Theme.textPrimary : Theme.textDisabled
    placeholderTextColor: Theme.textDisabled
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.animMs } }
    }
}
