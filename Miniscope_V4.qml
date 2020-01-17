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
    signal vidPropChangedSignal(string name, double displayValue, double i2cValue)
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


//        FpsItem {
//            id: fpsItem
//            anchors.horizontalCenter: parent.horizontalCenter
//            anchors.bottom: parent.bottom
//            anchors.bottomMargin: 0
//        }
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
            iconPath: "img/icon/exposure.ico"
            textColor: "black"
        }
        VideoSpinBoxControl{
            id: gain
            objectName: "gain"
            iconPath: "img/icon/gain.ico"
        }
        ToolSeparator {
            id: toolSeparator
        }
        VideoSliderControl{
            id: alpha
            objectName: "alpha"
            iconPath: "img/icon/alpha.ico"
            startValue: 1
            min: 0
            max: 1
            stepSize: .01
            decimalPrecision: 2
        }
        VideoSliderControl{
            id: beta
            objectName: "beta"
            iconPath: "img/icon/beta.ico"
            startValue: 0
            min: 0
            max: 1
            stepSize: .01
            decimalPrecision: 2
        }


    }

    BNODisplay {
        id: bno
        objectName: "bno"
        qx: parent.width - 120
        qy: parent.height - 70
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
    Connections{
        target: alpha
        onValueChangedSignal: vidPropChangedSignal(alpha.objectName, displayValue, i2cValue)
    }
    Connections{
        target: beta
        onValueChangedSignal: vidPropChangedSignal(beta.objectName, displayValue, i2cValue)
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






