import QtQuick

// ---------------------------------------------------------------------------
// TEMPORARY STUB (Qt6 migration)
//
// The original config-tree editor used Qt Quick Controls 1
// (TreeView / TableViewColumn / Controls.Styles), all removed in Qt6.
// This placeholder lets the application launch while the editor is ported to
// Qt6 (TreeView + DelegateChooser, or a C++ QAbstractItemModel-backed view).
//
// The original implementation is preserved in git history on the commit
// before this stub (branch: qt6-migration). Restore & port it from there.
// ---------------------------------------------------------------------------
Item {
    id: root

    // Properties bound by main.qml — keep these so existing bindings resolve.
    property string toolTipText: ""
    property var model

    Rectangle {
        anchors.fill: parent
        color: "#f3f3f3"
        border.color: "#999999"
        border.width: 1

        Text {
            anchors.centerIn: parent
            width: parent.width - 20
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#555555"
            font.pointSize: 11
            text: "Config editor temporarily unavailable.\n" +
                  "(Qt6 port of the tree-view editor is pending.)\n\n" +
                  "You can still edit the user-config .json file directly."
        }
    }
}
