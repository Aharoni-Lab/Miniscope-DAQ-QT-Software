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
// hardware controls (gain / frame rate / LEDs / EWL - the recorded device
// settings) live in a left-edge dock whose value + icon are always visible,
// and which expands on hover (or when pinned) to reveal every control's
// slider/stepper at once (values glanceable at all times, adjustment UI only
// when needed); display-only controls (contrast / brightness / saturation /
// LUT / dFF) and actions sit in an auto-hiding right rail.
//
// Both panels overlay the video, so both are sized as a fraction of the pane
// (root.panelWidth) rather than in fixed pixels, only as tall as they need, and
// never open at the same time - a pane in the Acquire grid is often ~450 px wide,
// and these controls are adjusted while watching the live image.
//
// C++ contract (videodevice/miniscope/behaviorcam.cpp attach by objectName
// and root signal - keep these stable):
//   root signals: takeScreenShotSignal, vidPropChangedSignal, dFFSwitchChanged,
//     saturationSwitchChanged, lutSwitchChanged, setRoiClicked,
//     addTraceRoiClicked, camPropsClicked
//   objectNames: vD (VideoDisplay), gain / frameRate / led0 / led1 /
//     optoPeriod / optoDuration / ewl (control rows; C++ makes the
//     catalog-defined ones visible and configures their ranges),
//     saturationSwitch, lutSwitch, addTraceRoi, camProps, bno
//   root properties: recording (pushed by VideoDevice on record start/stop)
Item {
    id: root
    width: 640
    height: 480

    property bool miniscope: false
    property bool recording: false
    // Driven from C++ (VideoDevice) on device connection loss/recovery. Defaults
    // true so the disconnect chip only appears once a drop actually happens.
    property bool connected: true

    signal takeScreenShotSignal()
    signal vidPropChangedSignal(string name, double displayValue, double i2cValue, double i2cValue2)
    signal dFFSwitchChanged(bool checked)
    signal saturationSwitchChanged(bool checked)
    signal lutSwitchChanged(bool checked)
    signal setRoiClicked()
    signal addTraceRoiClicked()
    signal camPropsClicked()

    // gnuplot-style colormap for trace-ROI outlines; matches the trace
    // display's coloring so an outline pairs visually with its trace.
    function colormapGnuPlot(x) {
        return Qt.rgba(Math.abs(2.0 * x - 0.5),
                       Math.sin(3.141592 * x),
                       Math.cos(3.141592 / 2.0 * x), 1.0)
    }

    focus: true
    Keys.onSpacePressed: root.takeScreenShotSignal()

    // Width of the two overlay panels (hardware dock, display rail). Panes in the
    // Acquire grid are routinely 450-630 px wide, where a fixed 250 px panel
    // covered 40-56% of the video - and most of these controls (EWL focus, LED,
    // gain, contrast) are adjusted *while watching* the live image. So the panels
    // are a fraction of the pane instead of a constant, floored at the width a
    // slider row still needs to be usable and capped at the old 250.
    readonly property int panelWidth:
        Math.round(Math.min(250, Math.max(150, width * 0.34)))

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

    // Inline slider row for the rail's display controls (0-1 range).
    component RailSlider: RowLayout {
        id: railSlider
        property alias iconPath: railSliderIcon.source
        property alias value: railSliderSlider.value
        // These rows are icon-only to keep the rail narrow, and the icons
        // (alpha / beta) name the shader term rather than what it does.
        property string tip: ""
        signal moved(double value)
        spacing: 8
        RecoloredIcon {
            id: railSliderIcon
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            ToolTip.text: railSlider.tip
            ToolTip.visible: railSlider.tip.length > 0 && iconHover.hovered
            ToolTip.delay: 400
            HoverHandler { id: iconHover }
        }
        Slider {
            id: railSliderSlider
            Layout.fillWidth: true
            from: 0
            to: 1
            stepSize: 0.01
            onValueChanged: railSlider.moved(value)
            background: Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.availableWidth
                height: 4
                radius: 2
                color: "#3a3a46"
                Rectangle {
                    width: railSliderSlider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: "#7e7ef0"
                }
            }
            handle: Rectangle {
                x: railSliderSlider.leftPadding + railSliderSlider.visualPosition
                   * railSliderSlider.availableWidth - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: railSliderSlider.pressed ? "#a8a7fd" : "#d8d8e0"
                border.width: 1
                border.color: "#5a5a68"
            }
        }
        Text {
            text: railSliderSlider.value.toFixed(2)
            font: Theme.fontSmall
            color: "#d8d8e0"
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
            visible: !root.connected
            text: qsTr("⚠ DISCONNECTED")
            textColor: Theme.warning
        }
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
        // Bottom-right corner, clear of the left-edge hardware dock. Kept
        // inside the pane at any orientation - the widget's box bounds the
        // rotating logo (see BNODisplay.qml), and shrinks with small panes.
        width: Math.min(84, root.width / 4, root.height / 4)
        height: width
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        opacity: 0.9
    }

    // --- Hardware control dock (left edge) -------------------------------------
    // The recorded device settings (gain / frame rate / LEDs / EWL). Value +
    // icon are always visible so they stay glanceable during a recording;
    // hovering the dock (or pinning it) expands it and reveals every control's
    // slider/stepper at once - no per-control flyout floating over the video.
    // The catalog (videoDevices.json) defines which controls exist per device
    // type; C++ shows (visible) and configures each by objectName.
    //
    // Sizing is window-relative: video panes in the Acquire grid can get small,
    // so the expanded width clamps to the pane and the rows scroll if a very
    // short pane can't fit them all.
    Rectangle {
        id: hwDock
        objectName: "hardwareDock"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacing

        // Collapses while the display rail is out: two panels this wide left
        // nothing of a small pane's video visible at once, and the rail is what
        // the pointer is on. One-directional on purpose - the rail must not
        // depend on the dock in turn, or the two bindings form a cycle.
        readonly property bool hwExpanded: (hwHover.hovered || hwPinned) && !rail.railOpen
        property bool hwPinned: false
        readonly property int collapsedWidth: 78
        readonly property int expandedWidth: root.panelWidth

        // Whether the catalog gave this device any hardware controls. C++ calls
        // setVisible(true) on each control AFTER the window loads. We must NOT
        // gate this Rectangle's own `visible` on the children's visibility:
        // QML `item.visible` is EFFECTIVE visibility, so a hidden dock keeps its
        // children effectively hidden, their visibleChanged never fires, and the
        // dock stays hidden forever (chicken-and-egg). Keeping the dock itself
        // visible lets the children's effective visibility track C++, so this
        // binding resolves correctly; we hide the empty dock by collapsing its
        // width and making it transparent instead.
        readonly property bool hasControls:
            gainCtl.visible || frameRateCtl.visible || led0Ctl.visible
            || led1Ctl.visible || optoPeriodCtl.visible || optoDurationCtl.visible
            || ewlCtl.visible

        width: hasControls ? (hwExpanded ? expandedWidth : collapsedWidth) : 0
        // Never taller than the pane (minus a margin); the ScrollView handles
        // the overflow if it still doesn't fit.
        height: Math.min(hwContent.implicitHeight + 2 * Theme.spacing,
                         root.height - 2 * Theme.spacing)
        radius: Theme.radiusSmall
        color: !hasControls ? "transparent"
                            : (hwExpanded ? "#dd14141a" : "#aa14141a")

        Behavior on width { NumberAnimation { duration: Theme.animMs } }

        HoverHandler { id: hwHover }

        ScrollView {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: hwContent
                width: hwDock.width - 2 * Theme.spacing
                spacing: 4

                // Pin (glove-friendly), shown only once expanded.
                Text {
                    Layout.alignment: Qt.AlignRight
                    visible: hwDock.hwExpanded
                    text: hwDock.hwPinned ? "📌" : "📍"
                    font: Theme.fontSmall
                    color: "#8a8a96"
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: hwDock.hwPinned = !hwDock.hwPinned
                    }
                }

                VideoSpinBoxControl {
                    id: gainCtl
                    objectName: "gain"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/gain.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("gain", displayValue, i2cValue, i2cValue2)
                }
                VideoSpinBoxControl {
                    id: frameRateCtl
                    objectName: "frameRate"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/fps.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("frameRate", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    id: led0Ctl
                    objectName: "led0"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/led.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("led0", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    id: led1Ctl
                    objectName: "led1"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/led.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("led1", displayValue, i2cValue, i2cValue2)
                }
                // Optogenetic burst timing. Wired up ahead of the catalog entry
                // that will use them - no shipped device declares these yet, so
                // both stay hidden. Raw byte values straight to the scope's MCU;
                // 0 means stimulation off, so a catalog startValue of 0 disarms
                // opto at connect.
                VideoSliderControl {
                    id: optoPeriodCtl
                    objectName: "optoPeriod"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/led.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("optoPeriod", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    id: optoDurationCtl
                    objectName: "optoDuration"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/led.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("optoDuration", displayValue, i2cValue, i2cValue2)
                }
                VideoSliderControl {
                    id: ewlCtl
                    objectName: "ewl"
                    visible: false
                    Layout.fillWidth: true
                    expanded: hwDock.hwExpanded
                    iconPath: "img/icon/ewl.png"
                    onValueChangedSignal: (displayValue, i2cValue, i2cValue2) =>
                        root.vidPropChangedSignal("ewl", displayValue, i2cValue, i2cValue2)
                }
            }
        }
    }

    // --- Control rail (auto-hide, right edge) ----------------------------------
    // Fixed dark styling: the rail floats over live video, independent of the
    // app theme.
    Rectangle {
        id: rail
        objectName: "displayRail"
        width: root.panelWidth
        // Only as tall as its contents (capped to the pane), not the full pane
        // height: the rail's controls come to ~400 px, so a full-height panel was
        // an opaque slab over video for no reason. Centred, like the dock.
        height: Math.min(railContent.implicitHeight + 2 * Theme.spacing, root.height)
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: railOpen ? Theme.spacing : -(width + Theme.spacing)
        radius: Theme.radiusSmall
        // Slightly more see-through than the old #dd: what is behind the rail is
        // the video being adjusted.
        color: "#cc14141a"

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
                id: railContent
                width: rail.width - 2 * Theme.spacing
                spacing: Theme.spacing

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        // Everything under this heading changes the on-screen
                        // image only. The rail is narrow, so the label is short
                        // and the caveat lives in the tooltip.
                        text: qsTr("Display only")
                        font: Theme.fontSmall
                        color: "#8a8a96"
                        ToolTip.text: qsTr("Affects the live view only — never the recorded video.")
                        ToolTip.visible: headingHover.hovered
                        ToolTip.delay: 400
                        HoverHandler { id: headingHover }
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

                // Contrast/brightness of the on-screen image only; the C++
                // side routes these straight to the display shader.
                RailSlider {
                    Layout.fillWidth: true
                    iconPath: "img/icon/alpha.png"
                    tip: qsTr("Contrast")
                    value: 1
                    onMoved: v => root.vidPropChangedSignal("alpha", v, v, 0)
                }
                RailSlider {
                    Layout.fillWidth: true
                    iconPath: "img/icon/beta.png"
                    tip: qsTr("Brightness")
                    value: 0
                    onMoved: v => root.vidPropChangedSignal("beta", v, v, 0)
                }

                UiSwitch {
                    objectName: "saturationSwitch"
                    text: qsTr("Saturation")
                    textColor: "#d8d8e0"
                    ToolTip.text: qsTr("Highlight saturated pixels in the live view.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onToggled: root.saturationSwitchChanged(checked)
                }
                UiSwitch {
                    objectName: "lutSwitch"
                    visible: root.miniscope
                    text: qsTr("Colormap")
                    textColor: "#d8d8e0"
                    ToolTip.text: qsTr("Apply the config's LUT colormap to the live view.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onToggled: root.lutSwitchChanged(checked)
                }
                UiSwitch {
                    visible: root.miniscope
                    text: qsTr("ΔF/F")
                    textColor: "#d8d8e0"
                    ToolTip.text: qsTr("Show each pixel's change from its running mean.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
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
                    ToolTip.text: qsTr("Save a PNG of the current frame (or press Space).")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onClicked: root.takeScreenShotSignal()
                }
                UiButton {
                    Layout.fillWidth: true
                    text: qsTr("Recording ROI…")
                    ToolTip.text: qsTr("Drag a region on the video; only that region is recorded.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onClicked: root.setRoiClicked()
                }
                UiButton {
                    objectName: "addTraceRoi"
                    Layout.fillWidth: true
                    visible: root.miniscope
                    enabled: false // enabled by C++ when the trace display runs
                    text: qsTr("+ Trace ROI…")
                    ToolTip.text: qsTr("Drag a region to plot its mean in the trace display.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onClicked: root.addTraceRoiClicked()
                }
                UiButton {
                    objectName: "camProps"
                    Layout.fillWidth: true
                    visible: false // shown by C++ for cameras with a props dialog
                    text: qsTr("Properties…")
                    ToolTip.text: qsTr("Open the camera driver's own settings dialog.")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
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
