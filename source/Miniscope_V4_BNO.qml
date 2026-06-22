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
    signal dFFSwitchChanged(bool value)
    signal saturationSwitchChanged(bool value)

    signal setRoiClicked()
    signal addTraceRoiClicked()

    // colormap functions
    function f1(type, x){
        if (type === 33)
            return Math.abs(2.0 * x - 0.5);
        if (type === 13)
            return Math.sin(3.141592 * x);
        if (type === 10)
            return Math.cos(3.141592/2.0 * x);
    }
    function colormap_gnu_plot (x) {
//        console.debug(Qt.rgba(f1(33,x),f1(13,x),f1(10,x),1.0));
        return Qt.rgba(f1(33,x),f1(13,x),f1(10,x),1.0);
    }

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
        Layout.minimumHeight: 480
        Layout.minimumWidth: 640
        objectName: "vD"
        anchors.fill: parent

        // For Trace ROIs
        property var traceROIx: []
        property var traceROIy: []
        property var traceROIw: []
        property var traceROIh: []
        property var traceColor: []
        property int numTraceROIs: 0


        property var sumAcqFPS: [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
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
        Repeater{
            id: repeater0
            model: videoDisplay.numTraceROIs
            Rectangle {
                x: videoDisplay.traceROIx[index]
                y: videoDisplay.traceROIy[index]
                width: videoDisplay.traceROIw[index]
                height: videoDisplay.traceROIh[index]
                visible: true

                border.color: root.colormap_gnu_plot(videoDisplay.traceColor[index])
                color: "transparent"
                border.width: 1
                Text {
                    text: index
                    color: parent.border.color
                    anchors.right: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

        }
        onAddTraceROIChanged: {
            if (videoDisplay.addTraceROI[4] === 1) {
                rectAddTraceROI.visible = true;
//                rectAddTraceROI.color = "#40ffffff"; //"e38787";
                rectAddTraceROI.x = videoDisplay.addTraceROI[0];
                rectAddTraceROI.y = videoDisplay.addTraceROI[1];
                rectAddTraceROI.width = videoDisplay.addTraceROI[2];
                rectAddTraceROI.height = videoDisplay.addTraceROI[3];
            }
            else {
                // New trace ROI has been selected
                rectAddTraceROI.visible = false;
//                videoDisplay.traceROIx.push(videoDisplay.addTraceROI[0]);
//                videoDisplay.traceROIy.push(videoDisplay.addTraceROI[1]);
//                videoDisplay.traceROIw.push(videoDisplay.addTraceROI[2]);
//                videoDisplay.traceROIh.push(videoDisplay.addTraceROI[3]);
                videoDisplay.numTraceROIs++;
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
        Rectangle {
            id: rectAddTraceROI
            x: 0
            y: 0
            width: videoDisplay.addTraceROI[2]
            height: videoDisplay.addTraceROI[3]
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


            Switch {
                id: dFFSwitch
                objectName: "dFFSwitch"
                text: qsTr("dF/F Display")
                hoverEnabled: false

                font.bold: true
                font.family: "Arial"
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                Layout.column: 0
                Layout.row: 0
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
            id: addTraceRoi
            objectName: "addTraceRoi"
            text: "Add Neuron ROI"
            font.family: "Arial"
            font.pointSize: 10
            font.bold: true
            font.weight: Font.Normal
            radius: 4
            enabled: true
            background: Rectangle {
                id: addTraceRoiRect
                radius: addTraceRoi.radius
                border.width: 1
                color: "#a8a7fd"
            }
            onHoveredChanged: hovered ? addTraceRoiRect.color = "#f8a7fd" : addTraceRoiRect.color = "#a8a7fd"
            onClicked: root.addTraceRoiClicked()

        }

        RoundButton {
            id: setRoi
            objectName: "setRoi"
            text: "Set Frame ROI"
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
        }

        VideoSliderControl {
            id: led1
            objectName: "led1"
            max: 100
            startValue: 0
            iconPath: "img/icon/led.png"
            textColor: "black"
            visible: false
        }

        VideoSliderControl {
            id: ewl
            objectName: "ewl"
            max: 100
            startValue: 10
            iconPath: "img/icon/ewl.png"
            textColor: "black"
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

    BNODisplay {
        id: bno
        objectName: "bno"
        x: parent.width - 120
        y: parent.height - 70
    }

    Connections{
        target: led0
        onValueChangedSignal: vidPropChangedSignal(led0.objectName, displayValue, i2cValue, i2cValue2)
    }
    Connections{
        target: led1
        onValueChangedSignal: vidPropChangedSignal(led1.objectName, displayValue, i2cValue, i2cValue2)
    }
    Connections{
        target: ewl
        onValueChangedSignal: vidPropChangedSignal(ewl.objectName, displayValue, i2cValue, i2cValue2)
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
        target: dFFSwitch
        onClicked: dFFSwitchChanged(dFFSwitch.checked)
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






