import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// ---------------------------------------------------------------------------
// User-config tree editor - Qt6 port.
//
// The original used Qt Quick Controls 1 (TreeView + TableViewColumn +
// Controls.Styles), all removed in Qt6. The backend still exposes the same
// QStandardItemModel (backend.jsonTreeModel); its column-0 index carries every
// field as a role - key / value / type / tips (the old design where each
// TableViewColumn just picked a different role of the same item). So this is a
// single logical column whose delegate lays out the row by hand:
//
//     [indent][expand arrow][key][value editor][type]
//
// NOTE: the Qt6 TreeView *attached* properties (TreeView.depth / hasChildren /
// expanded) come back undefined in this build, so we instead use the TreeView
// *methods* depth(row) / isExpanded(row) / toggleExpanded(row), and derive
// "has children" from the node type (Object/Array are the expandable parents).
// expandTick is bumped on expand/collapse so the isExpanded() bindings (which
// call a non-reactive method) re-evaluate.
//
// Bool values get a Switch, everything else a validated TextField. Edits are
// written back with backend.treeViewTextChanged(<modelIndex>, text); focusing a
// field publishes its "tips" to root.toolTipText (shown by the pane in
// main.qml).
// ---------------------------------------------------------------------------
TreeView {
    id: root
    property string toolTipText: ""
    property int expandTick: 0          // bump to re-evaluate isExpanded() bindings
    property var browseTarget: null     // QModelIndex currently being set via a dialog

    clip: true
    boundsBehavior: Flickable.StopAtBounds

    // Folder / file pickers for DirPath / FilePath entries, so the user can
    // browse to a location instead of typing it. The browse button stashes the
    // row's QModelIndex in browseTarget; on accept we write the chosen path back
    // through the same path as a manual edit.
    FolderDialog {
        id: folderDlg
        title: "Select a folder"
        onAccepted: if (root.browseTarget)
            backend.treeViewTextChanged(root.browseTarget, backend.urlToLocalFile(selectedFolder))
    }
    FileDialog {
        id: fileDlg
        title: "Select a file"
        onAccepted: if (root.browseTarget)
            backend.treeViewTextChanged(root.browseTarget, backend.urlToLocalFile(selectedFile))
    }

    onExpanded: root.expandTick++       // TreeView signal: a row was expanded
    onCollapsed: root.expandTick++

    // All data lives on column 0; the model's sibling columns 1 & 2 are empty
    // (legacy role-per-column design), so give column 0 the full content width
    // and collapse the others to zero.
    columnWidthProvider: function (column) { return column === 0 ? Math.max(root.width, 780) : 0 }
    onWidthChanged: Qt.callLater(root.forceLayout)

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

    // Background colour by data type (matches the original editor).
    function typeColor(t) {
        if (t === "Number" || t === "Integer" || t === "Double") return "#cfe2f3";
        if (t === "String") return "#d9d2e9";
        if (t === "DirPath" || t === "FilePath") return "#ead1dc";
        return "#f3f3f3";
    }
    // Object / Array(...) nodes are the expandable parents in this model.
    function isParent(t) { return t === "Object" || (t !== undefined && t.indexOf("Array") === 0); }

    delegate: Item {
        id: cell
        required property var model
        required property int row
        required property int column

        readonly property bool hasKids:    root.isParent(model.type)
        readonly property int  depthLevel: root.depth(cell.row)
        readonly property bool nodeOpen:   { root.expandTick; return root.isExpanded(cell.row); }

        implicitWidth: line.implicitWidth
        implicitHeight: 26
        visible: column === 0          // columns 1 & 2 carry no data

        Row {
            id: line
            height: parent.height

            // indentation by tree depth
            Item { width: cell.depthLevel * 18; height: 1 }

            // expand / collapse control
            Rectangle {
                width: 22; height: cell.height
                color: "transparent"
                Text {
                    anchors.centerIn: parent
                    visible: cell.hasKids
                    text: cell.nodeOpen ? "▾" : "▸"   // down / right triangle
                    font.pointSize: 11
                    color: "#333333"
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: cell.hasKids
                    onClicked: root.toggleExpanded(cell.row)
                }
            }

            // key
            Rectangle {
                width: 180; height: cell.height
                color: root.typeColor(cell.model.type)
                border.color: "black"; border.width: 1
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 4
                    text: cell.model.key === undefined ? "" : cell.model.key
                    font.pointSize: 10
                }
                TapHandler { onTapped: root.toolTipText = cell.model.tips }
            }

            // value editor: Switch for Bool, validated TextField otherwise,
            // plus a Browse button for DirPath / FilePath entries.
            Item {
                id: valueCell
                width: 360; height: cell.height
                readonly property bool isPath: cell.model.type === "DirPath" || cell.model.type === "FilePath"
                // The compression value is a video codec: offer the host-supported
                // codecs as a dropdown instead of a free-text field.
                readonly property bool isCompression: cell.model.key === "compression" && !cell.hasKids
                // The lut value is a display colormap: offer the available LUTs.
                readonly property bool isLut: cell.model.key === "lut" && !cell.hasKids
                // Either of the above renders a dropdown instead of a text field.
                readonly property bool isChoice: isCompression || isLut

                IntValidator    { id: intV; bottom: 0 }
                DoubleValidator { id: dblV; bottom: 0 }

                TextField {
                    id: tf
                    anchors.left: parent.left
                    anchors.right: valueCell.isPath ? browseBtn.left : parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    visible: cell.model.type !== "Bool" && !valueCell.isChoice
                    enabled: cell.model.type !== "Object" && cell.model.type !== "Array"
                    text: cell.model.value === undefined ? "" : cell.model.value
                    font.pointSize: 10
                    selectByMouse: true
                    color: "black"
                    validator: cell.model.type === "Integer" ? intV
                             : (cell.model.type === "Number" || cell.model.type === "Double") ? dblV
                             : null
                    background: Rectangle {
                        border.color: "black"; border.width: 1
                        color: root.typeColor(cell.model.type)
                    }
                    onActiveFocusChanged: if (activeFocus) root.toolTipText = cell.model.tips
                    onTextEdited: color = "red"          // unsaved edit
                    onEditingFinished: {
                        color = "green";                 // committed
                        backend.treeViewTextChanged(root.index(cell.row, 0), text);
                    }
                }

                Button {
                    id: browseBtn
                    visible: valueCell.isPath
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: visible ? 34 : 0
                    text: "…"
                    font.pointSize: 11
                    ToolTip.visible: hovered
                    ToolTip.text: cell.model.type === "DirPath" ? "Browse for a folder" : "Browse for a file"
                    onClicked: {
                        root.browseTarget = root.index(cell.row, 0);
                        if (cell.model.type === "DirPath")
                            folderDlg.open();
                        else
                            fileDlg.open();
                    }
                }

                Switch {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    visible: cell.model.type === "Bool"
                    checked: cell.model.value === true || cell.model.value === "true"
                    onToggled: backend.treeViewTextChanged(root.index(cell.row, 0), checked ? "true" : "false")
                    onActiveFocusChanged: if (activeFocus) root.toolTipText = cell.model.tips
                }

                ComboBox {
                    id: choiceBox
                    visible: valueCell.isChoice
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    font.pointSize: 10
                    // Options: codecs for "compression", colormaps for "lut".
                    readonly property var choices: valueCell.isLut ? backend.availableLUTs
                                                                    : backend.availableCodecs
                    // Current stored value; coerce to string in case the model hands
                    // back a non-string.
                    readonly property string curVal: cell.model.value === undefined ? "" : "" + cell.model.value
                    // Keep an existing-but-unlisted value visible so the user can change it.
                    model: {
                        var list = choiceBox.choices.slice();
                        if (curVal !== "" && list.indexOf(curVal) === -1)
                            list.unshift(curVal);
                        return list;
                    }
                    currentIndex: Math.max(0, model.indexOf(curVal))
                    onActivated: backend.treeViewTextChanged(root.index(cell.row, 0), currentText)
                    onActiveFocusChanged: if (activeFocus) root.toolTipText = cell.model.tips
                }
            }

            // type
            Rectangle {
                width: 90; height: cell.height
                color: root.typeColor(cell.model.type)
                border.color: "black"; border.width: 1
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 4
                    text: cell.model.type === undefined ? "" : cell.model.type
                    font.pointSize: 10
                }
            }
        }
    }
}
