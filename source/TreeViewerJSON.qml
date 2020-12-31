import QtQuick 2.0
import QtQuick.Controls 1.4
    import QtQuick.Controls 2.12 as QTQC2
import QtQuick.Layouts 1.3
import QtQuick.Controls.Styles 1.4

TreeView {
    property string toolTipText
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
            property string modelType: model.type
            property string modelTips: model.tips
            Rectangle {
                id: rectangle
                width: parent.width
                height: parent.height

                color: {
                    if (modelType === "Number") {"#cfe2f3"}
                    else if (modelType === "Integer") {"#cfe2f3"}
                    else if (modelType === "Double") {"#cfe2f3"}
                    else if (modelType === "String") {"#d9d2e9"}
                    else if (modelType === "DirPath") {"#ead1dc"}
                    else if (modelType === "FilePath") {"#ead1dc"}
                    else {"#f3f3f3"}
                }

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
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                            root.toolTipText = modelTips;

                    }
                }
            }
        }
    }

    TableViewColumn {
        title: "Value"
        role: "value"
        width: 400
        delegate: Loader {
            property string modelValue: model.value
            property string modelType: model.type
            property string modelTips: model.tips
            sourceComponent:
            {
                if (model.type === "Bool") {checkBoxDelegate}
                else {textFieldDelegate}
            }

//        delegate: Loader {
//            property var modelTwo: model.value
//            sourceComponent: stringDelegate
//            function updateValue(value) {
//                model.value = value;
//            }
        }
    }

    TableViewColumn {
        title: "Type"
        role: "type"
        width: 90
        delegate: Loader {
            property string modelType: model.type
            Rectangle {
                width: parent.width
                height: parent.height
                color: {
                    if (modelType === "Number") {"#cfe2f3"}
                    else if (modelType === "Integer") {"#cfe2f3"}
                    else if (modelType === "Double") {"#cfe2f3"}
                    else if (modelType === "String") {"#d9d2e9"}
                    else if (modelType === "DirPath") {"#ead1dc"}
                    else if (modelType === "FilePath") {"#ead1dc"}
                    else {"#f3f3f3"}
                }

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
        id: checkBoxDelegate
        QTQC2.Switch {
            width: 10
            height: 10
            checked: {
                if (modelValue === "true") {true}
                else {false}
            }
            onClicked: {
                if (checked)
                    backend.treeViewTextChanged(currentIndex, "true");
                else
                    backend.treeViewTextChanged(currentIndex, "false");
            }
            onFocusChanged: {
                if (focus) {
                    root.toolTipText = modelTips;
                }
            }
        }
    }

    Component {
        id: textFieldDelegate
        QTQC2.TextField {
            text: modelValue
            enabled: {
                if (modelType === "Object" || modelType === "Array") {false}
                else {true}
            }

            anchors.fill: parent
            font.pointSize: 10
            height: 25

            selectByMouse: true
//            canPaste: true

            background:
                Rectangle {
                    anchors.fill: parent
                    border.color: "black"
                    border.width: 1
                    color: {

                        if (modelType === "Number") {"#cfe2f3"}
                        else if (modelType === "Integer") {"#cfe2f3"}
                        else if (modelType === "Double") {"#cfe2f3"}
                        else if (modelType === "String") {"#d9d2e9"}
                        else if (modelType === "DirPath") {"#ead1dc"}
                        else if (modelType === "FilePath") {"#ead1dc"}
                        else {"#f3f3f3"}

//                        if (modelType === "Number") {"#cfe2f3"}
//                        else if (modelType === "String") {"#d9d2e9"}
//                        else if (modelType === "Bool") {"#ead1dc"}
//                        else {"#f3f3f3"}
                    }
                }
            property var validInt : IntValidator { bottom:0;}
            property var validDouble : DoubleValidator { bottom:0;}
            property var validPath : RegExpValidator{regExp: /^[^\\]+$/}
            property var validAll : RegExpValidator{}

            validator: {

                if (modelType === "Number") {validDouble}
                else if (modelType === "Integer") {validInt}
                else if (modelType === "Double") {validDouble}
                else if (modelType === "String") {validAll}
                else if (modelType === "DirPath") {validAll}
                else if (modelType === "FilePath") {validAll}
                else {validAll}
            }
            onFocusChanged: {
                if (focus) {
                    root.toolTipText = modelTips;
                }
            }

            onTextChanged: {
                if (focus)
                    color = "red"
            }

            onEditingFinished: {
                color = "green"
                backend.treeViewTextChanged(currentIndex, text);
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
