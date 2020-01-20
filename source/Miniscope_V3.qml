import QtQuick 2.12
import VideoDisplay 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3


Item {
    id: root
    objectName: "root"
    width: parent.width
    height: parent.height
    state: "controlsShown"
    focus: true
    signal vidPropChangedSignal(string name, double displayValue, double i2cValue, double i2cValue2)
    signal takeScreenShotSignal()

    Keys.onPressed: {
        if (event.key === Qt.Key_H) {
            if (root.state == "controlsShown")
                root.state = "controlsHidden";
            else
                root.state = "controlsShown";
        }
        if (event.key === Qt.Key_Space) {
            // Take screenshot of window
            takeScreenShotSignal();
        }
    }

    VideoDisplay {
        id: videoDisplay
        //            Layout.fillHeight: true
        //            Layout.fillWidth: true
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "vD"

        property var sumAcqFPS: [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
        property var idx: 0
        onAcqFPSChanged: {

            sumAcqFPS[idx] = videoDisplay.acqFPS;
//            print(sumAcqFPS[idx]);
            idx++;
            if (idx >= 20)
                idx = 0;
        }

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
                }
    }
    TopMenu{
        id: topMenu
        anchors.top: parent.top
        anchors.topMargin: 0
        anchors.horizontalCenter: parent.horizontalCenter

        GridLayout {
            id: gridLayout
            height: 32
            columnSpacing: 0
            rowSpacing: 0
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            anchors.right: parent.right
            anchors.rightMargin: 0
            anchors.left: parent.left
            anchors.leftMargin: 0
            columns: 3
            rows: 2


            Text{
                id: droppedFrameCount
                objectName: "droppedFrameCount"
                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                text: "--"
                font.pointSize: 10
                font.family: "Arial"
                Layout.column: 0
                Layout.row: 1
                Timer{
                    interval: 100
                    repeat: true
                    running: true
                    onTriggered: {
                        droppedFrameCount.text = "Dropped Frames: " + videoDisplay.droppedFrameCount;
                      }
                }


            }

            Text{
                id: acqFPS
                objectName: "acqFPS"
                property double aveFPS: 0
                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                text: "--"
                font.pointSize: 10
                font.family: "Arial"
                Layout.column: 1
                Layout.row: 1
                Timer{
                    interval: 100
                    repeat: true
                    running: true
                    onTriggered: {
                        acqFPS.aveFPS = 0;
                        for (var i = 0; i < 20; i++) {

                            acqFPS.aveFPS += videoDisplay.sumAcqFPS[i];
                        }
                        acqFPS.aveFPS = 20000.0/(acqFPS.aveFPS);
                        acqFPS.text = "Inst. FPS: " + (1000.0/videoDisplay.acqFPS).toFixed(1) + " | Ave. FPS: " + acqFPS.aveFPS.toFixed(1);

                    }
                }
            }

            Text{
                id: bufferCount
                objectName: "bufferCount"
                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                text: "--"
                font.pointSize: 10
                font.family: "Arial"
                Layout.column: 2
                Layout.row: 1
                Timer{
                    interval: 100
                    repeat: true
                    running: true
                    onTriggered: {
                        bufferCount.text = "Buffer Used: " + videoDisplay.bufferUsed + "/" + videoDisplay.maxBuffer;
                        bufferCount.color.g = 1 - videoDisplay.bufferUsed/videoDisplay.maxBuffer;
                        bufferCount.color.r = videoDisplay.bufferUsed/videoDisplay.maxBuffer;

                    }
                }
            }


        }
    }

    ColumnLayout {
        id: controlColumn
        objectName: "controlColumn"
        width: parent.width
        height: 100
        spacing: 0
        anchors.verticalCenter: parent.verticalCenter

        VideoSliderControl {
            id: led0
            objectName: "led0"
            max: 100
            startValue: 0
            iconPath: "img/icon/led.png"
            textColor: "blue"
        }

        VideoSpinBoxControl{
            id: frameRate
            objectName: "frameRate"
            iconPath: "img/icon/fps.png"
        }
        VideoSpinBoxControl{
            id: gain
            objectName: "gain"
            iconPath: "img/icon/gain.png"
        }
        ToolSeparator {
            id: toolSeparator
        }
        VideoSliderControl{
            id: alpha
            objectName: "alpha"
            iconPath: "img/icon/alpha.png"
            startValue: 1
            min: 0
            max: 1
            stepSize: .01
            decimalPrecision: 2
        }
        VideoSliderControl{
            id: beta
            objectName: "beta"
            iconPath: "img/icon/beta.png"
            startValue: 0
            min: 0
            max: 1
            stepSize: .01
            decimalPrecision: 2
        }


    }

    Connections{
        target: led0
        onValueChangedSignal: vidPropChangedSignal(led0.objectName, displayValue, i2cValue, i2cValue2)
    }
    Connections{
        target: gain
        onValueChangedSignal: vidPropChangedSignal(gain.objectName, displayValue, i2cValue, i2cValue2)
    }
    Connections{
        target: frameRate
        onValueChangedSignal: vidPropChangedSignal(frameRate.objectName, displayValue, i2cValue, i2cValue2)
    }
    Connections{
        target: alpha
        onValueChangedSignal: vidPropChangedSignal(alpha.objectName, displayValue, i2cValue, i2cValue2)
    }
    Connections{
        target: beta
        onValueChangedSignal: vidPropChangedSignal(beta.objectName, displayValue, i2cValue, i2cValue2)
    }

    states: [
        State{
            name: "controlsShown"

            PropertyChanges {
                target: controlColumn
                x: 0
            }
        },
        State{
            name: "controlsHidden"
            PropertyChanges {
                target: controlColumn
                x: -100
            }
        }
    ]
    transitions: [
        Transition {
            from: "controlsShown"
            to: "controlsHidden"
            NumberAnimation {
                target: controlColumn
                property: "x"
                duration: 500
                easing.type: Easing.OutQuad
            }
        },
        Transition{
            from: "controlsHidden"
            to: "controlsShown"
            NumberAnimation {
                target: controlColumn
                property: "x"
                duration: 500
                easing.type: Easing.OutQuad
            }

        }

    ]


}






