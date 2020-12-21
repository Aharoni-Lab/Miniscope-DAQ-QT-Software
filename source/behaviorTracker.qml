import QtQuick 2.12
import TrackerDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height
//    state: "controlsShown"
    focus: true

    TrackerDisplay {
        id: trackerDisplay
        //            Layout.fillHeight: true
        //            Layout.fillWidth: true
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "trackerDisplay"
        anchors.fill: parent

        property var sumAcqFPS: [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
        property var idx: 0
//        onAcqFPSChanged: {

//            sumAcqFPS[idx] = videoDisplay.acqFPS;
////            print(sumAcqFPS[idx]);
//            idx++;
//            if (idx >= 20)
//                idx = 0;
//        }

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
                }
    }

}
