import QtQuick 2.0
import QtQuick.Controls 1.4
    import QtQuick.Controls 2.12 as QTQC2
import QtQuick.Layouts 1.3
import QtQuick.Controls.Styles 1.4

TreeView {
    id:root
    rowDelegate: Rectangle {
            width: parent.width
            height: 25

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
                QTQC2.TextField {
                    text: model.value
                    enabled: {
                        if (model.type === "Object" || model.type === "Array") {false}
                        else {true}
                    }

                    anchors.fill: parent
                    font.pointSize: 10
                    height: 25
                    background:
                        Rectangle {
                            anchors.fill: parent
                            border.color: "black"
                            border.width: 1
                            color: if (model.type === "Number") {"#cfe2f3"} else if (model.type === "String") {"#d9d2e9"} else if (model.type === "Bool") {"#ead1dc"} else {"#f3f3f3"}
                        }

                    property var validNumber : IntValidator { bottom:0;}
//                    property var validNone : RegExpValidator{regExp: /$^/}
                    property var validAll : RegExpValidator{}

                    validator: {

                        if (model.type === "Number") {validNumber}
//                        else if (model.type === "Object") {validNone}
//                        else if (model.type === "Array") {validNone}
                        else {validAll}
                    }

                    onTextChanged: {
                        if (focus)
                            color = "red"
                    }

                    onEditingFinished: {
                        color = "green"
                        backend.treeViewTextChanged(currentIndex, text);
                    }

//                    onTextChanged: backend.treeViewTextChanged(currentIndex, text);
                }
//                Text {
//                    text: model.value
//                    anchors.left: parent.left
//                    anchors.leftMargin: 3
//                    font.pointSize: 10
////                    color:  if (model.type === "Number") {"black"} else {"black"}
//                }
//            }
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

//    Component {
//        id: stringDelegate
//        TextEdit {
//            text: modelTwo
//            font.family: Arial
//            font.pointSize: 12
//            onTextChanged: updateValue(text)
//        }
//    }

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
