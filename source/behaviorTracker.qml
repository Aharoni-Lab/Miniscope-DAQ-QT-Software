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
            objectName: "occRect"
            visible: false
            x: 0.75/1 * root.width
            y: 0.05/1 * root.height
            width: root.width * 0.4/2
            height: root.height * 0.4/2
            color: "transparent"
            border.color: "steelblue"
            onXChanged: {
//                    if (occMA.drag.active) {
                        trackerDisplay.occRectChanged(2 * (occRect.x )/root.width - 1, 1 - 2 * (occRect.y + occRect.height)/root.height, occRect.width/root.width * 2, occRect.height/root.height * 2);
//                }
            }
            onYChanged: {
//                if (occMA.drag.active) {
                    trackerDisplay.occRectChanged(2 * (occRect.x)/root.width - 1, 1 - 2 * (occRect.y + occRect.height)/root.height, occRect.width/root.width * 2, occRect.height/root.height * 2);
//                }
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
            Rectangle {
                id: resizeCorner
                width: 10
                height: 10
                color: "steelblue"
                anchors.verticalCenter:parent.bottom
                anchors.horizontalCenter: parent.right



                MouseArea {
                    id: resizeMA
                    anchors.fill: parent
                    drag{ target: parent; axis: Drag.XAndYAxis }
                    cursorShape: Qt.SizeFDiagCursor

                    property var initX: 0
                    property var initY: 0

                    onPressed: {
                        initX = mouseX;
                        initY = mouseY;
                    }

                    onPositionChanged: {
                       if(drag.active){
                           var newWidth;
                           var newHeight;
                           var dx = (mouseX - initX)
                           var dy = (mouseY - initY)
                           var delta = Math.min(Math.abs(), Math.abs(dy))
                           if (dx * occRect.height/occRect.width >= dy) {
                               newWidth = occRect.width + dy * occRect.width/occRect.height
                               newHeight = occRect.height + dy;
                           }
                           else {
                               newWidth = occRect.width + dx
                               newHeight = occRect.height + dx * occRect.height/occRect.width;
                           }

//                           if (newWidth < width || newHeight < height)
//                               return

                           occRect.width = newWidth
//                           occRect.x = selComp.x + delta

                           occRect.height = newHeight
//                           occRect.y = selComp.y + delta
                           occRect.onXChanged();
                       }
                    }
                }

            }
        }
}
