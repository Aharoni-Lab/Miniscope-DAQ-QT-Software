import QtQuick 2.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height

    Rectangle {
        id: rectangle
        color: "#ffffff"
        anchors.fill: parent
    }



    GridLayout {
        id: gridLayout
        anchors.fill: parent
        rows: 6
        columns: 2

        DelayButton {
            id: bRecord
            text: qsTr("Record")
            delay:1000
            Layout.row: 0
            Layout.column: 0
            Layout.rowSpan: 1
            Layout.columnSpan: 1
        }
        DelayButton {
            id: bStop
            text: qsTr("Stop")
            delay:1000
            Layout.row: 0
            Layout.column: 1
            Layout.rowSpan: 1
            Layout.columnSpan: 1
        }

        TextField {
            id: textField2
            text: qsTr("Current Recorded Length")
        }

        TextField {
            id: textField
            text: qsTr("Record Length")

        }
        ProgressBar {
            id: progressBar
            value: 0.5
        }

        Switch {
            id: element
            text: qsTr("Triggerable")
        }

        TextField {
            id: textField1
            text: qsTr("Enter Notes")
        }
        Button {
            id: bNoteSubmit
            text: qsTr("Submit Note")
        }

        ScrollView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.row:5
            Layout.column: 0
            Layout.columnSpan: 2

            TextArea {
                id: messageTextArea
                objectName: "messageTextArea"
                text: "Messages:\n"
                wrapMode: Text.WrapAnywhere
                anchors.fill: parent
                font.pointSize: 12
                readOnly: true
//                background: Rectangle {
//                    radius: rbSelectUserConfig.radius
//                    anchors.fill: parent
//                    border.width: 1
//                    color: "#ebebeb"
                //                }
            }
        }
    }
}




/*##^##
Designer {
    D{i:1;anchors_height:200;anchors_width:200}D{i:2;anchors_height:200;anchors_width:200}
}
##^##*/
