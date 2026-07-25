import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Compact video-overlay stepped control (gain, frame rate): the current
// value stays visible as a small chip; hovering slides out a -/+ stepper to
// its left.
//
// C++ contract (videodevice.cpp control loop, keep stable): objectName set by
// the window shell; catalog-driven properties displaySpinBoxValues/
// displayTextValues/outputValues/outputValues2/startValue set via
// setProperty; valueChangedSignal(display, i2c, i2c2) consumed for hardware
// commands. Setting startValue selects the entry AND emits - initial config
// values reach the device through this path.
Item {
    id: root
    implicitWidth: chip.width
    implicitHeight: chip.height

    property color textColor: "#d8d8e0"
    property var iconPath: ""
    property var displaySpinBoxValues: []
    property var displayTextValues: []
    property var outputValues: []
    property var outputValues2: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    property var startValue: undefined

    signal valueChangedSignal(double displayValue, double i2cValue, double i2cValue2)

    // Loose-equality lookup: catalog entries are strings while user-config
    // values may be numbers (the old strict indexOf silently fell back to
    // the first entry in that case). If the push lands on the entry already
    // selected, no change signal fires - emit anyway so the initial config
    // value still reaches the hardware.
    onStartValueChanged: {
        var idx = -1
        for (var i = 0; i < displaySpinBoxValues.length; i++) {
            if (displaySpinBoxValues[i] == startValue) {
                idx = i
                break
            }
        }
        if (idx < 0)
            return
        if (spinBox.value === idx)
            root.valueChangedSignal(displayTextValues[idx], outputValues[idx],
                                    outputValues2[idx])
        else
            spinBox.value = idx
    }

    readonly property bool expanded: chipHover.hovered || flyoutHover.hovered

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
                text: root.displayTextValues[spinBox.value] !== undefined
                      ? "" + root.displayTextValues[spinBox.value] : ""
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
        width: 110
        height: 22
        radius: 4
        color: "#dd14141a"
        border.width: 1
        border.color: "#5a5a68"

        HoverHandler { id: flyoutHover }

        SpinBox {
            id: spinBox
            anchors.fill: parent
            from: 0
            to: Math.max(0, root.displaySpinBoxValues.length - 1)
            value: 0
            font.pixelSize: 13
            font.bold: true

            textFromValue: function(value) {
                return root.displaySpinBoxValues[value] !== undefined
                       ? "" + root.displaySpinBoxValues[value] : ""
            }

            onValueChanged: root.valueChangedSignal(root.displayTextValues[value],
                                                    root.outputValues[value],
                                                    root.outputValues2[value])

            background: Rectangle { color: "transparent" }
            contentItem: Text {
                text: spinBox.textFromValue(spinBox.value, spinBox.locale)
                color: root.textColor
                font: spinBox.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            up.indicator: Rectangle {
                x: spinBox.width - width
                height: spinBox.height
                width: 24
                radius: 4
                color: spinBox.up.pressed ? "#3a3a46" : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: root.textColor
                    font.pixelSize: 15
                    font.bold: true
                }
            }
            down.indicator: Rectangle {
                x: 0
                height: spinBox.height
                width: 24
                radius: 4
                color: spinBox.down.pressed ? "#3a3a46" : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "−"
                    color: root.textColor
                    font.pixelSize: 15
                    font.bold: true
                }
            }
        }
    }
}
