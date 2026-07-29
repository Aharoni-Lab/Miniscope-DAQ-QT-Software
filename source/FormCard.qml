import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// One card of the config form: a titled surface with an optional
// enable-switch in the header and a collapsible body. Children declared
// inside a FormCard land in the body column.
Rectangle {
    id: card

    property string title: ""
    property string subtitle: ""
    property bool expanded: true
    // Optional header switch (for "enabled" feature blocks). When showSwitch
    // is true, bind `switchChecked` to the config value and handle
    // `onSwitchToggled` to write it back.
    property bool showSwitch: false
    property var switchChecked
    signal switchToggled(bool checked)

    default property alias body: bodyColumn.data
    // > 1, not > 0: bodyColumn's first child is the disabled-state hint FormCard
    // declares itself (see below), so a card whose user declared no rows still
    // has one child and must not grow a caret over an empty body.
    readonly property bool hasBody: bodyColumn.children.length > 1

    Layout.fillWidth: true
    implicitHeight: headerRow.implicitHeight + 2 * Theme.padding
                    + (expanded && hasBody ? bodyColumn.implicitHeight + Theme.spacing : 0)
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    clip: true

    Behavior on implicitHeight { NumberAnimation { duration: Theme.animMs } }

    // The enable switch sits immediately after the title, not out at the right
    // edge: with the heading on the left and the switch a card-width away, it
    // read as unrelated furniture and got missed - the classic result being a
    // commutator with its serial port filled in and the feature still off. The
    // subtitle moves to its own row underneath so nothing pushes the switch away
    // from the title it belongs to.
    ColumnLayout {
        id: headerRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.padding
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            // Expand / collapse caret (hidden when the card has no body)
            Text {
                visible: card.hasBody
                text: "▸"
                font: Theme.fontBody
                color: Theme.textSecondary
                rotation: card.expanded ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: Theme.animMs } }
            }

            Text {
                objectName: "cardTitle"
                text: card.title
                font: Theme.fontTitle
                color: Theme.textPrimary
            }

            UiSwitch {
                objectName: "enableSwitch"
                visible: card.showSwitch
                syncChecked: card.switchChecked
                onToggled: card.switchToggled(checked)
            }
            // The state in words, and a second (larger) hit target for the
            // switch - reading a small pill's position is the hard part.
            Text {
                objectName: "enableStateText"
                visible: card.showSwitch
                text: card.switchChecked === true ? qsTr("Enabled") : qsTr("Disabled")
                font: Theme.fontSmall
                color: card.switchChecked === true ? Theme.accent : Theme.textSecondary
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.switchToggled(card.switchChecked !== true)
                }
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            visible: card.subtitle.length > 0
            text: card.subtitle
            font: Theme.fontSmall
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // Click anywhere on the header (except the switch, which sits above) to
    // expand/collapse.
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: headerRow.height + Theme.padding
        z: -1
        enabled: card.hasBody
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: card.expanded = !card.expanded
    }

    ColumnLayout {
        id: bodyColumn
        visible: card.expanded && card.hasBody
        anchors.top: headerRow.bottom
        anchors.topMargin: Theme.spacing
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.padding
        spacing: Theme.spacing

        // Declared here so it is the FIRST row of the body: the card's own
        // children are appended after it via the `body` alias. Someone with this
        // card open is configuring the feature, which is exactly when a
        // still-disabled switch is worth saying out loud - the fields stay
        // editable, since the port has to be typed in before enabling makes
        // sense.
        Rectangle {
            objectName: "disabledHint"
            visible: card.showSwitch && card.switchChecked !== true
            Layout.fillWidth: true
            implicitHeight: offHint.implicitHeight + Theme.spacing
            radius: Theme.radiusSmall
            color: Theme.surfaceAlt
            RowLayout {
                id: offHint
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                spacing: Theme.spacing
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Disabled — nothing here takes effect until you turn it on.")
                    font: Theme.fontSmall
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }
                UiButton {
                    text: qsTr("Enable")
                    onClicked: card.switchToggled(true)
                }
            }
        }
    }
}
