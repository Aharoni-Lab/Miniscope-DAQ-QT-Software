import QtQuick 2.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height

    property double currentRecordTime: 0
    property double ucRecordLength: 1

    signal submitNoteSignal(string note)

    Rectangle {
        id: rectangle
        color: "#cacaca"
        anchors.fill: parent
    }

    GridLayout {
        id: gridLayout
        anchors.fill: parent
        rows: 6
        columns: 2

        TextField {
            id: textNote
            objectName: "textNote"
            text: qsTr("Enter Note")
            font.pointSize: 10
            font.family: "Arial"
            Layout.fillWidth: true
            onFocusChanged:{
                      if(focus)
                          selectAll()
                 }
        }
        Button {
            id: bNoteSubmit
            objectName: "bNoteSubmit"
            text: qsTr("Submit Note")
            enabled: false
            checkable: false
            Layout.fillWidth: true
            onClicked: {

                root.submitNoteSignal(textNote.text);
                textNote.text = "Enter Note";
            }

        }
        Switch {
            id: element
            text: qsTr("Triggerable")
            enabled: false
        }

        Flickable {
            id: flick1
            flickableDirection: Flickable.VerticalFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Layout.columnSpan: 2
            TextArea.flickable: TextArea {
                 id: messageTextArea
                 objectName: "messageTextArea"
                 textFormat: TextEdit.RichText
                 text: "'Space Bar': screenshot of video stream.<br/>'h': hides/shows video stream controls.<br/>Messages:"
 //                wrapMode: Text.WrapAnywhere
                 anchors.fill: parent
                 font.pointSize: 12
                 readOnly: true
                 background: Rectangle {
 //                    radius: rbSelectUserConfig.radius
                     anchors.fill: parent
 //                    border.width: 1
                     color: "#ebebeb"
                 }
                 function log_color(msg, color){
                     return "<span style='color: " + color +  ";' >" + msg + "</span>";
                 }
                 function logMessage(time, msg){

                     var color = "darkgreen";
                     if(msg.toLowerCase().indexOf('error') >= 0){
                         color = "red";
                     } else if (msg.toLowerCase().indexOf('warning') >= 0){
                         color = "goldenrod";
                     }

                     var _time = log_color(time, "0xFFFFFF")
                     var _msg = log_color(msg, color);
                     messageTextArea.append(_time + ": " + _msg);

                     // scroll to bottom
                     flick1.contentY = (messageTextArea.height - flick1.height);
                 }
             }
            contentWidth: messageTextArea.width
            contentHeight: messageTextArea.height

            ScrollBar.vertical: ScrollBar {
                width: 20
            }
        }

        DelayButton {
            id: bRecord
            objectName: "bRecord"
            text: qsTr("Record")
//            autoExclusive: true
            font.family: "Arial"
            font.pointSize: 12
            Layout.fillWidth: true
            delay:1000
//            Layout.row: 0
//            Layout.column: 0
            Layout.rowSpan: 1
            Layout.columnSpan: 1
            onActivated: {

                bNoteSubmit.enabled = true;
                bStop.enabled = true;
                bRecord.enabled = false;
            }
        }

        DelayButton {
            id: bStop
            objectName: "bStop"
            text: qsTr("Stop")
            enabled: false
//            autoExclusive: true
            font.pointSize: 12
            font.family: "Arial"
//            enabled: false
            checkable: true
            Layout.fillWidth: true
            delay:1000
//            Layout.row: 0
//            Layout.column: 1
            Layout.rowSpan: 1
            Layout.columnSpan: 1
            onActivated: {

                bNoteSubmit.enabled = false;
                bStop.enabled = false;
                bRecord.enabled = true;
            }

        }

        ProgressBar {
            id: progressBar
            height: 40
            Layout.minimumHeight: 40
//            Layout.preferredHeight: 40
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.fillWidth: true
            value: (root.ucRecordLength > 0) ? root.currentRecordTime/root.ucRecordLength : 1
            from: 0
            to:1
            Layout.columnSpan: 1
        }

        Text {
            id: recordTimeText
            objectName: "recordTimeText"

            text: (root.ucRecordLength > 0) ? root.currentRecordTime.toString() + "/" + root.ucRecordLength.toString() + "s" : root.currentRecordTime.toString() + "s"
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            font.pointSize: 12
            font.family: "Arial"
        }
    }
    Connections{
        target: root
        onCurrentRecordTimeChanged: {
            if (root.currentRecordTime >= root.ucRecordLength) {
                bStop.enabled = false;
                bRecord.enabled = true;
            }
        }
    }
}




/*##^##
Designer {
    D{i:1;anchors_height:200;anchors_width:200}D{i:2;anchors_height:200;anchors_width:200}
}
##^##*/
