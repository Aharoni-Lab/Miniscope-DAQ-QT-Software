import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Compact video-overlay hardware control: the current value stays visible as
// a small chip (icon + value) that costs almost no screen space; hovering the
// chip slides out a slider to its left for adjustment (pressing keeps it
// open while dragging).
//
// C++ contract (videodevice.cpp control loop, keep stable): objectName set by
// the window shell; catalog-driven properties min/max/stepSize/startValue/
// displayValueScale/displayValueOffset/displayValueBitShift set via
// setProperty; valueChangedSignal(display, i2c, i2c2) consumed for hardware
// commands. Setting startValue moves the slider AND emits - initial config
// values reach the device through this path.
Item {
    id: root
    implicitWidth: chip.width
    implicitHeight: chip.height

    property color textColor: "#d8d8e0"
    property var iconPath: ""
    property double min: 0.0
    property double max: 100.0
    property double stepSize: 1.0
    property double startValue: 0
    property double displayValueScale: 1
    property double displayValueOffset: 0
    property double displayValueBitShift: 0
    property int decimalPrecision: 0

    signal valueChangedSignal(double displayValue, double i2cValue, double i2cValue2)

    function i2cValue(v) {
        return (v * displayValueScale - displayValueOffset) << displayValueBitShift
    }

    // Not a binding: a user drag must not break later C++ pushes (config
    // load, the external-trigger LED-off override). If the push lands on the
    // value the slider already shows, no change signal fires - emit anyway so
    // the initial config value still reaches the hardware.
    onStartValueChanged: {
        var pre = slider.value
        slider.value = startValue
        if (slider.value === pre)
            root.valueChangedSignal(slider.value, i2cValue(slider.value), 0)
    }

    readonly property bool expanded: chipHover.hovered || flyoutHover.hovered
                                     || slider.pressed

    Rectangle {
        id: chip
        anchors.right: parent.right
        width: chipRow.implicitWidth + 12
        height: 22
        radius: 4
        color: "#aa14141a"
        border.width: root.expanded ? 1 : 0
        border.color: "#5a5a68"

        HoverHandler { id: chipHover }

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            // The icon glyphs are black (drawn for the old light toolbar);
            // recolor them to the chip's text color.
            Item {
                height: 16
                width: 16
                anchors.verticalCenter: parent.verticalCenter
                Image {
                    id: chipIcon
                    anchors.fill: parent
                    visible: false
                    sourceSize.height: 24
                    sourceSize.width: 24
                    fillMode: Image.PreserveAspectFit
                    source: root.iconPath
                }
                MultiEffect {
                    anchors.fill: parent
                    source: chipIcon
                    brightness: 0.85 // black glyph -> near-white, alpha keeps shape
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: slider.value.toFixed(root.decimalPrecision)
                color: root.textColor
                font.pixelSize: 13
                font.bold: true
            }
        }
    }

    Rectangle {
        id: flyout
        visible: root.expanded
        anchors.right: chip.left
        anchors.rightMargin: 4
        anchors.verticalCenter: chip.verticalCenter
        width: 170
        height: 22
        radius: 4
        color: "#dd14141a"
        border.width: 1
        border.color: "#5a5a68"

        HoverHandler { id: flyoutHover }

        Slider {
            id: slider
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            from: root.min
            to: root.max
            stepSize: root.stepSize

            onValueChanged: root.valueChangedSignal(value, root.i2cValue(value), 0)

            background: Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.availableWidth
                height: 4
                radius: 2
                color: "#3a3a46"
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: "#7e7ef0"
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * slider.availableWidth - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: slider.pressed ? "#a8a7fd" : "#d8d8e0"
                border.width: 1
                border.color: "#5a5a68"
            }
        }
    }
}
