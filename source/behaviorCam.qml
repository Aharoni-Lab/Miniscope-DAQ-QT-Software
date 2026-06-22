import QtQuick 2.12
import VideoDisplay 1.0
import QtQuick.Window 2.12
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

    signal camPropsClicked()
    signal setRoiClicked()

    signal calibrateCameraClicked()
    signal calibrateCameraStart()
    signal calibrateCameraQuit()

    signal saturationSwitchChanged(bool value)

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

    Window {
        id: camCalibWindow
        width: 600
        height: 200
        visible: false
        title: "Camera Calibration"
        ColumnLayout {
            anchors.fill: parent

            TextArea {
                text: "Camera Calibration " + "<br/>" +
                      "Directions go here"
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.margins: 5
                Layout.fillWidth: true
                textFormat: Text.RichText
                onLinkActivated: Qt.openUrlExternally(link)
                font.pointSize: 12
                font.family: "Arial"
                wrapMode: Text.WordWrap
                MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton // we don't want to eat clicks on the Text
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
            }

            ProgressBar {
                id: camCalibProgressBar
                width: parent.width
                value: 0.5
            }

            RowLayout {
                id: camCalibRow
                objectName: "camCalibRow"
                width: parent.width
                height: 100
                spacing: 0
//                anchors.verticalCenter: parent.verticalCenter

                Button {
                    text: "Begin"
                    font.family: "Arial"
                    font.pointSize: 12
                    font.bold: true
                    font.weight: Font.Normal
                    background: Rectangle {
                        id: startRect
                        border.width: 1
                        color: "#a8a7fd"
                    }
                    Layout.margins: 5
                    Layout.fillWidth: true
                    onClicked: {
                        root.calibrateCameraStart()
                    }

                    onHoveredChanged: hovered ? startRect.color = "#f8a7fd" : startRect.color = "#a8a7fd"
                }

                Button {
                    text: "Quit"
                    font.family: "Arial"
                    font.pointSize: 12
                    font.bold: true
                    font.weight: Font.Normal
                    background: Rectangle {
                        id: quitRect
                        border.width: 1
                        color: "#a8a7fd"
                    }
                    Layout.margins: 5
                    Layout.fillWidth: true
                    onClicked: {
                        root.calibrateCameraQuit()
                        camCalibWindow.visible = false
                    }
                    onHoveredChanged: hovered ? quitRect.color = "#f8a7fd" : quitRect.color = "#a8a7fd"
                }
            }


        }
    }

    VideoDisplay {
        id: videoDisplay
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "vD"
        anchors.fill: parent

        property var sumAcqFPS: [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
//        property variant roiArray: [0,0,100,100,0]
        property var idx: 0
        onAcqFPSChanged: {

            sumAcqFPS[idx] = videoDisplay.acqFPS;
//            print(sumAcqFPS[idx]);
            idx++;
            if (idx >= 20)
                idx = 0;
        }
        onRoiChanged: {
            if (videoDisplay.ROI[4] === 1) {
                rectROI.visible = true;
                rectROI.color = "#40ffffff"; //"e38787";
                rectROI.x = videoDisplay.ROI[0];
                rectROI.y = videoDisplay.ROI[1];
                rectROI.width = videoDisplay.ROI[2];
                rectROI.height = videoDisplay.ROI[3];
            }
            else {
                rectROI.visible = true;
                rectROI.color = "#00000000";
                rectROI.x = videoDisplay.ROI[0];
                rectROI.y = videoDisplay.ROI[1];
                rectROI.width = videoDisplay.ROI[2];
                rectROI.height = videoDisplay.ROI[3];
            }
        }

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
                }

        Rectangle {
            id: rectROI
            x: 0
            y: 0
            width: videoDisplay.ROI[2]
            height: videoDisplay.ROI[3]
            visible: false

            border.color: "red"
            border.width: 2
//            radius: 10
        }
    }

    TopMenu{
        id: topMenu
        anchors.top: parent.top
        anchors.topMargin: 0
        anchors.horizontalCenter: parent.horizontalCenter

        GridLayout {
            id: gridLayout
            height: 64
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

            RoundButton {
                id: calibrateCamera
                objectName: "calibrateCamera"
                text: "Calib. Cam."
                font.family: "Arial"
                font.pointSize: 10
                font.bold: true
                font.weight: Font.Normal
                radius: 4
                enabled: false
                Layout.column: 0
                Layout.row: 0
                // TODO: Make hovering color change work.
                hoverEnabled: false
                background: Rectangle {
                    id: calibrateRect
                    radius: calibrateCamera.radius
                    border.width: 1
                    color: "#a8a7fd"
                }
                onHoveredChanged: hovered ? calibrateRect.color = "#f8a7fd" : calibrateRect.color = "#a8a7fd"
                onClicked: {

                    root.calibrateCameraClicked()
                    camCalibWindow.visible = true
                }
            }

            Switch {
                id: saturationSwitch
                objectName: "saturationSwitch"
                text: qsTr("Show Saturation")
                hoverEnabled: false

                font.bold: true
                font.family: "Arial"
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                Layout.column: 2
                Layout.row: 0
            }

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


        RoundButton {
            id: camProps
            objectName: "camProps"
            text: "Cam. Props."
            font.family: "Arial"
            font.pointSize: 10
            font.bold: true
            font.weight: Font.Normal
            radius: 4
            enabled: true
            visible: false
            background: Rectangle {
                id: camPropsRect
                radius: camProps.radius
                border.width: 1
                color: "#a8a7fd"
            }
            onHoveredChanged: hovered ? camPropsRect.color = "#f8a7fd" : camPropsRect.color = "#a8a7fd"
            onClicked: root.camPropsClicked()

        }

        RoundButton {
            id: setRoi
            objectName: "setRoi"
            text: "Set ROI"
            font.family: "Arial"
            font.pointSize: 10
            font.bold: true
            font.weight: Font.Normal
            radius: 4
            enabled: true
            background: Rectangle {
                id: setRoiRect
                radius: setRoi.radius
                border.width: 1
                color: "#a8a7fd"
            }
            onHoveredChanged: hovered ? setRoiRect.color = "#f8a7fd" : setRoiRect.color = "#a8a7fd"
            onClicked: root.setRoiClicked()

        }

        VideoSliderControl {
            id: led0
            objectName: "led0"
            max: 100
            startValue: 0
            iconPath: "img/icon/led.png"
            textColor: "black"
            visible: false
        }

        VideoSpinBoxControl{
            id: frameRate
            objectName: "frameRate"
            iconPath: "img/icon/fps.png"
            visible: false
        }
        VideoSpinBoxControl{
            id: gain
            objectName: "gain"
            iconPath: "img/icon/gain.png"
            visible: false
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
    Connections{
        target: saturationSwitch
        onClicked: saturationSwitchChanged(saturationSwitch.checked)
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






