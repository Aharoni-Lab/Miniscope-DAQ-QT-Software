import QtQuick 2.12
import VideoDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3


Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height

    signal vidPropChangedSignal(string name, double displayValue, double i2cValue)

    VideoDisplay {
        id: videoDisplay
        //            Layout.fillHeight: true
        //            Layout.fillWidth: true
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "vD"
//        SequentialAnimation on t {
//            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
//            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
//            loops: Animation.Infinite
//            running: true
        //        }
    }
    TopMenu{
        id: topMenu
        anchors.top: parent.top
        anchors.topMargin: 0
        anchors.horizontalCenter: parent.horizontalCenter

        FpsItem {
            id: fpsItem
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
//            x: 189
            //            y: 262
        }
    }

    ColumnLayout {
        id: rowLayout
        objectName: "controlColumn"
        width: parent.width
        height: 100
        spacing: 0
        anchors.verticalCenter: parent.verticalCenter

        VideoSliderControl {
            id: led0
            objectName: "led0"
            max: 200
            startValue: 100
            iconPath: "img/icon/led.ico"
            textColor: "blue"
        }

        VideoSliderControl {
            id: ewl
            objectName: "ewl"
            max: 100
            startValue: 10
            iconPath: "img/icon/ewl.ico"
            textColor: "black"
        }

        VideoSliderControl {
            id: exposure
            objectName: "exposure"
            max: 100
            startValue: 10
            iconPath: "img/icon/ewl.ico"
            textColor: "black"
        }
        VideoSpinBoxControl{
            id: gain
            objectName: "gain"
        }
    }

    Connections{
        target: led0
        onValueChangedSignal: vidPropChangedSignal(led0.objectName, displayValue, i2cValue)
    }
    Connections{
        target: ewl
        onValueChangedSignal: vidPropChangedSignal(ewl.objectName, displayValue, i2cValue)
    }
    Connections{
        target: gain
        onValueChangedSignal: vidPropChangedSignal(gain.objectName, displayValue, i2cValue)
    }


}




