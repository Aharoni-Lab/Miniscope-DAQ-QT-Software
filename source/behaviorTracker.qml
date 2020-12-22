import QtQuick 2.12
import TrackerDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height
    focus: true

    TrackerDisplay {
//        property float occX: occRect.x
//        property float occY: occRect.y
        id: trackerDisplay
        //            Layout.fillHeight: true
        //            Layout.fillWidth: true
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "trackerDisplay"
        anchors.fill: parent
        property var sumAcqFPS: [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
        property var idx: 0

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
                }
    }
        Rectangle {
            id: occRect
            x: 0.75/1 * root.width
            y: 0.05/1 * root.height
            width: root.width * 0.4/2
            height: root.height * 0.4/2
            color: "transparent"
            border.color: "red"
            onXChanged: {
                    if (occMA.drag.active) {
                        trackerDisplay.occRectMoved(2 * (occRect.x )/root.width - 1, 1 - 2 * (occRect.y + occRect.height)/root.height);
                }
            }
            onYChanged: {
                    if (occMA.drag.active) {
                        trackerDisplay.occRectMoved(2 * (occRect.x)/root.width - 1, 1 - 2 * (occRect.y + occRect.height)/root.height);
                    }
                }

            MouseArea {
                id: occMA
                anchors.fill: parent
                drag.target: parent
                drag.axis: Drag.XAndYAxis
                drag.minimumX: 0
                drag.maximumX: root.width - occRect.width
                drag.minimumY: 0
                drag.maximumY: root.height - occRect.height
                cursorShape: Qt.OpenHandCursor
                onPressedChanged: {
                    if (pressed === true)
                        cursorShape = Qt.ClosedHandCursor;
                    else
                        cursorShape = Qt.OpenHandCursor;
                }

            }
        }
//    Rectangle {
////        visible: trackerDisplay.
//        width: root.width * .5/2
//        height: root.height * .5/2
//        anchors.top: parent.top
//        anchors.topMargin: 0
//        anchors.right: parent.right
//        anchors.rightMargin: 0
//        color: "transparent"
//        border.color: "#000000"

//    }

}
