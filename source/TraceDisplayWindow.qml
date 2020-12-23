import QtQuick 2.12
import TraceDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3


Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height
//    focus: true
//    Keys.onPressed: {
//        if (event.key == Qt.Key_Left) {
//            console.log("move left");
//            event.accepted = true;
//        }
//    }

    TraceDisplay {
        id: traceDisplay
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "traceDisplay"
        anchors.fill: parent
        Keys.enabled: true
        focus:true

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
        }
    }
    ColumnLayout {
        id: columnL
        height: parent.height * (traceDisplay.traceNames.length - 1)/traceDisplay.traceNames.length
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        Repeater { id: repeater
            model: traceDisplay.traceNames.length
            Rectangle {
                id: rectangle
                width: 100
                height:15
                color: "transparent"
                Text { text: traceDisplay.traceNames[index]
                    anchors.verticalCenterOffset: -7
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 5
                    font.family: "Helvetica"
                    font.pointSize: 10
                    color: "#969696"
                   }
            }

        }
    }

    ColumnLayout {
        id: columnSelected
        height: parent.height * (traceDisplay.ySelectedLabel.length - 1)/traceDisplay.ySelectedLabel.length
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: 100
        Repeater { id: repeaterSelected
            model: traceDisplay.ySelectedLabel.length
            Rectangle {
                id: rectangleSelected
                width: 100
                height:15
                color: "transparent"
                anchors.horizontalCenter: root.horizontalCenter
                Text { text: traceDisplay.ySelectedLabel[index]
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignHCenter
                    font.family: "Helvetica"
                    font.pointSize: 10
                    color: "#969696"
                   }
            }

        }
    }

    RowLayout {
        id: bottomText
        objectName: "bottomText"
        width: parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom


        Repeater { id: repeaterXLabel
            model: traceDisplay.xLabel.length
            Rectangle {
                id: rectangleXLabel
                width:50
                height:15
                color: "transparent"
                Text { text: traceDisplay.xLabel[index]
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                    font.family: "Helvetica"
                    font.pointSize: 10
                    color: "#969696"
                   }
            }

        }

    }


}




/*##^##
Designer {
    D{i:2;anchors_height:480}
}
##^##*/
