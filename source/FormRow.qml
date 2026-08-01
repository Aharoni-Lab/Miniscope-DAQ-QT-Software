import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// One labeled row of the config form: fixed-width label on the left (hover
// for the userConfigProps tip), controls on the right. Children declared
// inside a FormRow land in the control slot, laid out horizontally.
RowLayout {
    id: row

    property string label: ""
    // Help text from userConfigProps.json, shown as a tooltip on the label.
    property string tip: ""

    default property alias content: slot.data

    Layout.fillWidth: true
    spacing: Theme.spacing

    Text {
        id: labelText
        text: row.label
        font: Theme.fontBody
        color: Theme.textSecondary
        Layout.preferredWidth: 170
        Layout.alignment: Qt.AlignVCenter
        elide: Text.ElideRight

        // Dotted underline marks rows that have help text.
        Rectangle {
            visible: row.tip.length > 0
            anchors.top: parent.baseline
            anchors.topMargin: 3
            width: Math.min(labelText.implicitWidth, labelText.width)
            height: 1
            color: Theme.border
        }

        HoverHandler { id: labelHover; enabled: row.tip.length > 0 }
        ToolTip.visible: labelHover.hovered
        ToolTip.delay: 400
        ToolTip.text: row.tip
    }

    RowLayout {
        id: slot
        Layout.fillWidth: true
        spacing: Theme.spacing
    }
}
