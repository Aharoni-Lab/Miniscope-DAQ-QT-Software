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
    readonly property bool hasBody: bodyColumn.children.length > 0

    Layout.fillWidth: true
    implicitHeight: headerRow.implicitHeight + 2 * Theme.padding
                    + (expanded && hasBody ? bodyColumn.implicitHeight + Theme.spacing : 0)
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    clip: true

    Behavior on implicitHeight { NumberAnimation { duration: Theme.animMs } }

    RowLayout {
        id: headerRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.padding
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

        ColumnLayout {
            spacing: 2
            Text {
                text: card.title
                font: Theme.fontTitle
                color: Theme.textPrimary
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

        Item { Layout.fillWidth: true }

        UiSwitch {
            visible: card.showSwitch
            syncChecked: card.switchChecked
            onToggled: card.switchToggled(checked)
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
    }
}
