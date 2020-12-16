import QtQuick 2.12
import TraceDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3


Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height
    focus: true

    TraceDisplay {
        id: traceDisplay
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "traceDisplay"
        anchors.fill: parent

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

    RowLayout {
        id: bottomText
        objectName: "bottomText"
        width: parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom


        Text {
            id: xLabel0
            objectName: "xLabel0"
            color: "#969696"
            text: traceDisplay.xLabel[0]
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            font.family: "Helvetica"
            font.pointSize: 10
        }

        Text {
            objectName: "xLabel1"
            text: traceDisplay.xLabel[1]
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            font: xLabel0.font
            color: xLabel0.color
        }

        Text {
            objectName: "xLabel2"
            text: traceDisplay.xLabel[2]
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            font: xLabel0.font
            color: xLabel0.color
        }

        Text {
            objectName: "xLabel3"
            text: traceDisplay.xLabel[3]
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            font: xLabel0.font
            color: xLabel0.color
        }

        Text {
            objectName: "xLabel4"
            text: traceDisplay.xLabel[4]
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            font: xLabel0.font
            color: xLabel0.color
        }
    }


}

