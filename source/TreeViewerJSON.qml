import QtQuick 2.0
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.3

TreeView {
    rowDelegate: Rectangle {
            width: parent.width
            height: 20

        }
    TableViewColumn {
        title: "Key"
        role: "key"
        width: 200

        delegate: Loader {
            Rectangle {
                id: rectangle
                width: parent.width
                height: parent.height
                color: if (model.type === "Number") {"#cfe2f3"} else if (model.type === "String") {"#d9d2e9"} else if (model.type === "Bool") {"#ead1dc"} else {"#f3f3f3"}
                border.color: "black"
                border.width: 1
                anchors.fill: parent
                Text {
                    text: model.key
                    anchors.left: parent.left
                    anchors.leftMargin: 3
                    font.pointSize: 10
//                    color:  if (model.type === "Number") {"black"} else {"black"}
                }
            }
        }
    }

    TableViewColumn {
        title: "Value"
        role: "value"
        width: 400
        delegate: Loader {
            Rectangle {
                width: parent.width
                height: parent.height
                color: if (model.type === "Number") {"#cfe2f3"} else if (model.type === "String") {"#d9d2e9"} else if (model.type === "Bool") {"#ead1dc"} else {"#f3f3f3"}
                border.color: "black"
                border.width: 1
                anchors.fill: parent
                Text {
                    text: model.value
                    anchors.left: parent.left
                    anchors.leftMargin: 3
                    font.pointSize: 10
//                    color:  if (model.type === "Number") {"black"} else {"black"}
                }
            }
        }
//        delegate: Loader {
//            property var modelTwo: model.value
//            sourceComponent: stringDelegate
//            function updateValue(value) {
//                model.value = value;
//            }
//        }
    }

    TableViewColumn {
        title: "Type"
        role: "type"
        width: 100
        delegate: Loader {
            Rectangle {
                width: parent.width
                height: parent.height
                color: if (model.type === "Number") {"#cfe2f3"} else if (model.type === "String") {"#d9d2e9"} else if (model.type === "Bool") {"#ead1dc"} else {"#f3f3f3"}
                border.color: "black"
                border.width: 1
//                anchors.fill: parent
                Text {
                    text: model.type
                    anchors.left: parent.left
                    anchors.leftMargin: 3
                    font.pointSize: 10
//                    color:  if (model.type === "Number") {"black"} else {"black"}
                }
            }
        }
    }

    Component {
        id: stringDelegate
        TextEdit {
            text: modelTwo
            font.family: Arial
            font.pointSize: 12
            onTextChanged: updateValue(text)
        }
    }

    DropArea {
        id: drop
        anchors.fill: parent
        onDropped: {
            // Send file name to c++ backend
            if (drop.hasUrls) {
                backend.userConfigFileName = drop.urls[0];

            }
        }
    }
}
