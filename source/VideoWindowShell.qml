import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import VideoDisplay 1.0
import Miniscope.Theme 1.0

// One window shell for every video device - Miniscopes and behavior cameras.
// behaviorCam.qml and Miniscope_V4_BNO.qml are thin wrappers that set
// `miniscope`, so the deviceConfigs catalog's qmlFile entries keep working.
//
// Design: telemetry lives in always-visible status chips over the video (REC
// state above all - the old windows showed nothing during a recording);
// hardware controls sit in an auto-hiding right rail, with display-only
// toggles (saturation / LUT / dFF) separated from sensor controls.
//
// C++ contract (videodevice/miniscope/behaviorcam.cpp attach by objectName
// and root signal - keep these stable):
//   root signals: takeScreenShotSignal, vidPropChangedSignal, dFFSwitchChanged,
//     saturationSwitchChanged, lutSwitchChanged, setRoiClicked,
//     addTraceRoiClicked, camPropsClicked, calibrateCamera{Clicked,Start,Quit}
//   objectNames: vD (VideoDisplay), gain / frameRate / led0 / ewl (control
//     rows; C++ makes the catalog-defined ones visible and configures their
//     ranges), saturationSwitch, lutSwitch, addTraceRoi, camProps, bno
//   root properties: recording (pushed by VideoDevice on record start/stop)
Item {
    id: root
    width: 640
    height: 480

    property bool miniscope: false
    property bool recording: false

    signal takeScreenShotSignal()
    signal vidPropChangedSignal(string name, double displayValue, double i2cValue, double i2cValue2)
    signal dFFSwitchChanged(bool checked)
    signal saturationSwitchChanged(bool checked)
    signal lutSwitchChanged(bool checked)
    signal setRoiClicked()
    signal addTraceRoiClicked()
    signal camPropsClicked()
    signal calibrateCameraClicked()
    signal calibrateCameraStart()
    signal calibrateCameraQuit()

    // gnuplot-style colormap for trace-ROI outlines; matches the trace
    // display's coloring so an outline pairs visually with its trace.
    function colormapGnuPlot(x) {
        return Qt.rgba(Math.abs(2.0 * x - 0.5),
                       Math.sin(3.141592 * x),
                       Math.cos(3.141592 / 2.0 * x), 1.0)
    }

    focus: true
    Keys.onSpacePressed: root.takeScreenShotSignal()

    // --- Video ---------------------------------------------------------------
    VideoDisplay {
        id: videoDisplay
        objectName: "vD"
        anchors.fill: parent

        // Committed trace ROIs: Miniscope::handleAddNewTraceROI appends to
        // these arrays via setProperty, then numTraceROIs (incremented below
        // when a selection commits) tells the Repeater to draw them.
        property var traceROIx: []
        property var traceROIy: []
        property var traceROIw: []
        property var traceROIh: []
        property var traceColor: []
        property int numTraceROIs: 0

        // Rolling average FPS from the inter-frame intervals (acqFPS is the
        // interval in ms), like the old windows' 20-sample window.
        property var fpsWindow: []
        property double aveFps: 0
        onAcqFPSChanged: {
            fpsWindow.push(acqFPS)
            if (fpsWindow.length > 20)
                fpsWindow.shift()
            var sum = 0
            for (var i = 0; i < fpsWindow.length; i++)
                sum += fpsWindow[i]
            aveFps = sum > 0 ? 1000.0 * fpsWindow.length / sum : 0
        }

        // Recording-ROI overlay. C++ drives videoDisplay.ROI in display
        // pixels: [x, y, w, h, selecting] while the user drags, then
        // selecting flips to 0 and the committed region stays outlined
        // (VideoDevice::handleDisplayResized keeps it placed on resize).
        // roiSeen: the ROI property starts with a placeholder value, so the
        // overlay only appears once a selection actually happens.
        property bool roiSeen: false
        onRoiChanged: roiSeen = true
        Rectangle {
            visible: videoDisplay.roiSeen && videoDisplay.ROI.length === 5
            x: videoDisplay.ROI[0]
            y: videoDisplay.ROI[1]
            width: videoDisplay.ROI[2]
            height: videoDisplay.ROI[3]
            color: videoDisplay.ROI[4] === 1 ? "#40ffffff" : "transparent"
            border.color: "red"
            border.width: 2
        }

        // Trace-ROI selection overlay, visible during the drag. When the
        // selection commits ([4] flips to 0), C++ has already appended the
        // region to the arrays above - bumping the count reveals it in the
        // committed Repeater below.
        onAddTraceROIChanged: {
            if (addTraceROI.length === 5 && addTraceROI[4] !== 1)
                numTraceROIs++
        }
        Rectangle {
            visible: videoDisplay.addTraceROI.length === 5
                     && videoDisplay.addTraceROI[4] === 1
            x: videoDisplay.addTraceROI[0]
            y: videoDisplay.addTraceROI[1]
            width: videoDisplay.addTraceROI[2]
            height: videoDisplay.addTraceROI[3]
            color: "#40ffffff"
            border.color: "yellow"
            border.width: 2
        }

        // Committed trace ROIs, numbered and colored to match their traces.
        Repeater {
            model: videoDisplay.numTraceROIs
            Rectangle {
                x: videoDisplay.traceROIx[index]
                y: videoDisplay.traceROIy[index]
                width: videoDisplay.traceROIw[index]
                height: videoDisplay.traceROIh[index]
                color: "transparent"
                border.color: root.colormapGnuPlot(videoDisplay.traceColor[index])
                border.width: 1
                Text {
                    text: index
                    color: parent.border.color
                    anchors.right: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // --- Status chips (always visible; fixed dark styling over the video) -----
    component StatusChip: Rectangle {
        property alias text: chipText.text
        property alias textColor: chipText.color
        radius: Theme.radiusSmall
        color: "#aa14141a"
        implicitWidth: chipText.implicitWidth + 12
        implicitHeight: 22
        Text {
            id: chipText
            anchors.centerIn: parent
            font: Theme.fontSmall
            color: "#d8d8e0"
        }
    }

    Row {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.spacing
        spacing: 6

        StatusChip {
            visible: root.recording
            text: qsTr("● REC")
            textColor: Theme.recording
        }
        StatusChip { text: root.Window.window ? root.Window.window.title : "" }
        StatusChip { text: videoDisplay.aveFps.toFixed(1) + qsTr(" FPS") }
        StatusChip {
            text: qsTr("drop ") + (videoDisplay.droppedFrameCount < 0
                                   ? qsTr("n/a") : videoDisplay.droppedFrameCount)
        }
        StatusChip {
            text: qsTr("buf %1/%2").arg(videoDisplay.bufferUsed).arg(videoDisplay.maxBuffer)
            textColor: videoDisplay.maxBuffer > 0
                       && videoDisplay.bufferUsed > 0.8 * videoDisplay.maxBuffer
                       ? Theme.warning : "#d8d8e0"
        }
    }

    // --- Head-orientation widget (Miniscopes with the BNO stream) -------------
    BNODisplay {
        id: bno
        objectName: "bno"
        // Idle until the first quaternion sample arrives.
        visible: root.miniscope && (qw !== 0 || qx !== 0 || qy !== 0 || qz !== 0 || badData)
        width: 90
        height: 90
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        opacity: 0.9
    }

    // --- Control rail (auto-hide, right edge) ----------------------------------
    // Fixed dark styling: the rail floats over live video, independent of the
    // app theme.
    Rectangle {
        id: rail
        width: 250
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: railOpen ? 0 : -width
        color: "#dd14141a"

        readonly property bool railOpen: railHover.hovered || edgeHover.hovered || railPinned
        property bool railPinned: false

        Behavior on anchors.rightMargin { NumberAnimation { duration: Theme.animMs } }

        HoverHandler { id: railHover }

        ScrollView {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: rail.width - 2 * Theme.spacing
                spacing: Theme.spacing

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Sensor")
                        font: Theme.fontSmall
                        color: "#8a8a96"
                    }
                    Item { Layout.fillWidth: true }
                    // Pin keeps the rail open (glove-friendly)
                    Text {
                        text: rail.railPinned ? "📌" : "📍"
                        font: Theme.fontSmall
                        color: "#8a8a96"
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: rail.railPinned = !rail.railPinned
                        }
                    }
                }

                // Hardware controls: the catalog (videoDevices.json) defines
                // which exist per device type; C++ shows and configures them.
                VideoSpinBoxControl {
                    objectName: "gain"
                    visible: false
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/gain.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("gain", displayValue, i2cValue, i2cValue2)
                }
                VideoSpinBoxControl {
                    objectName: "frameRate"
                    visible: false
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/fps.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("frameRate", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    objectName: "led0"
                    visible: false
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/led.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("led0", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    objectName: "led1"
                    visible: false
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/led.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("led1", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    objectName: "ewl"
                    visible: false
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/ewl.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("ewl", displayValue, i2cValue, i2cValue2)
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#33333d" }
                Text {
                    text: qsTr("Display (does not affect recordings)")
                    font: Theme.fontSmall
                    color: "#8a8a96"
                }

                // Contrast/brightness of the on-screen image only; the C++
                // side routes these straight to the display shader.
                VideoSliderControl {
                    id: alphaControl
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/alpha.png"
                    startValue: 1
                    min: 0
                    max: 1
                    stepSize: 0.01
                    decimalPrecision: 2
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("alpha", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    id: betaControl
                    textColor: "#d8d8e0"
                    iconPath: "img/icon/beta.png"
                    startValue: 0
                    min: 0
                    max: 1
                    stepSize: 0.01
                    decimalPrecision: 2
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("beta", displayValue, i2cValue, i2cValue2)
                }

                UiSwitch {
                    objectName: "saturationSwitch"
                    text: qsTr("Show saturation")
                    onToggled: root.saturationSwitchChanged(checked)
                }
                UiSwitch {
                    objectName: "lutSwitch"
                    visible: root.miniscope
                    text: qsTr("Apply LUT colormap")
                    onToggled: root.lutSwitchChanged(checked)
                }
                UiSwitch {
                    visible: root.miniscope
                    text: qsTr("ΔF/F display")
                    onToggled: root.dFFSwitchChanged(checked)
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#33333d" }
                Text {
                    text: qsTr("Actions")
                    font: Theme.fontSmall
                    color: "#8a8a96"
                }

                UiButton {
                    Layout.fillWidth: true
                    text: qsTr("Screenshot")
                    onClicked: root.takeScreenShotSignal()
                }
                UiButton {
                    Layout.fillWidth: true
                    text: qsTr("Select recording ROI…")
                    onClicked: root.setRoiClicked()
                }
                UiButton {
                    objectName: "addTraceRoi"
                    Layout.fillWidth: true
                    visible: root.miniscope
                    enabled: false // enabled by C++ when the trace display runs
                    text: qsTr("Add trace ROI…")
                    onClicked: root.addTraceRoiClicked()
                }
                UiButton {
                    objectName: "camProps"
                    Layout.fillWidth: true
                    visible: false // shown by C++ for cameras with a props dialog
                    text: qsTr("Camera properties…")
                    onClicked: root.camPropsClicked()
                }
            }
        }
    }

    // Thin reveal tab so the rail is discoverable when closed.
    Rectangle {
        id: railTab
        visible: !rail.railOpen
        width: 22
        height: 64
        radius: Theme.radiusSmall
        color: "#aa14141a"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        Text {
            anchors.centerIn: parent
            text: "◂"
            color: "#d8d8e0"
            font: Theme.fontSmall
        }
    }
    // Hovering the right edge (or the tab) opens the rail.
    Item {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 26
        HoverHandler { id: edgeHover }
    }
}
