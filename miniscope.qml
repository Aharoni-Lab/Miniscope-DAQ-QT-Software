import QtQuick 2.12
import VideoDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height

    signal vidPropChangedSignal(string type, double value)

    FpsItem {
        id: fpsItem
        x: 189
        y: 262
    }

    VideoDisplay {
        id: videoDisplay
        //            Layout.fillHeight: true
        //            Layout.fillWidth: true
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "vD"
        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

    ColumnLayout {
        id: rowLayout
        width: parent.width
        height: 100
        spacing: 0
        anchors.verticalCenter: parent.verticalCenter

        VideoPropertyControl {
            id: led1
            objectName: "LED1"
            sliderMax: 200
            sliderStartValue: 100
            iconPath: "img/icon/led.ico"
            textColor: "blue"



        }
        VideoPropertyControl {
            id: ewl
            objectName: "EWL"
            sliderMax: 100
            sliderStartValue: 10
            iconPath: "img/icon/ewl.ico"
            textColor: "black"
        }
        VideoPropertyControl {
            id: gain
            objectName: "GAIN"
            sliderMax: 100
            sliderStartValue: 10
            iconPath: "img/icon/ewl.ico"
            textColor: "black"
        }
    }

    Connections{
        target: led1
        onValueChangedSignal: vidPropChangedSignal(led1.objectName,value)
    }
    Connections{
        target: ewl
        onValueChangedSignal: vidPropChangedSignal(ewl.objectName,value)
    }
    Connections{
        target: gain
        onValueChangedSignal: vidPropChangedSignal(gain.objectName,value)
    }


}




