import QtQuick
import QtQuick.Controls
import Miniscope.Theme 1.0

// Themed combo box. Two-way config binding: bind `syncValue` to the backend
// value and handle `onActivated` to write back `currentValue` (or
// `currentText` when no valueRole is set). Selection changes never break the
// binding — syncValue is a plain property the control itself never writes.
ComboBox {
    id: control

    property var syncValue
    function resyncIndex() {
        if (syncValue === undefined || syncValue === null)
            return
        var idx = valueRole.length > 0 ? indexOfValue(syncValue) : find(String(syncValue))
        if (idx >= 0 && currentIndex !== idx)
            currentIndex = idx
    }
    onSyncValueChanged: resyncIndex()
    onModelChanged: resyncIndex()
    Component.onCompleted: resyncIndex()

    implicitHeight: Theme.controlHeight
    font: Theme.fontBody

    contentItem: Text {
        leftPadding: Theme.spacing
        rightPadding: control.indicator.width + Theme.spacing
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - Theme.spacing
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "▾"
        font: Theme.fontSmall
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }

    delegate: ItemDelegate {
        id: item
        required property var model
        required property int index
        width: ListView.view ? ListView.view.width : control.width
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textRole.length > 0 ? item.model[control.textRole] : item.model.modelData
            font: control.font
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: item.highlighted ? Theme.surfaceAlt : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
    }
}
