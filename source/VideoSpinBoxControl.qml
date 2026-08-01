import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One stepped hardware-control row for the left dock (gain, frame rate). The
// current value is always visible (icon + number); the dock reveals a -/+
// stepper in-row when it expands, matching VideoSliderControl's behaviour.
//
// C++ contract (videodevice.cpp control loop, keep stable): objectName set by
// the window shell; catalog-driven properties displaySpinBoxValues/
// displayTextValues/outputValues/outputValues2/startValue set via
// setProperty; valueChangedSignal(display, i2c, i2c2) consumed for hardware
// commands. Setting startValue selects the entry AND emits - initial config
// values reach the device through this path.
Item {
    id: root
    implicitHeight: 30

    property bool expanded: false
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

    RowLayout {
        anchors.fill: parent
        spacing: 6

        RecoloredIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            source: root.iconPath
        }

        // Collapsed: a plain value label. Expanded: the -/+ stepper (which
        // shows the same value). Only one is present at a time so the row
        // stays narrow when collapsed.
        Text {
            visible: !root.expanded
            Layout.fillWidth: true
            text: root.displayTextValues[spinBox.value] !== undefined
                  ? "" + root.displayTextValues[spinBox.value] : ""
            color: root.textColor
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignRight
        }

        SpinBox {
            id: spinBox
            visible: root.expanded
            opacity: root.expanded ? 1 : 0
            Layout.fillWidth: true
            Layout.preferredHeight: root.implicitHeight
            from: 0
            to: Math.max(0, root.displaySpinBoxValues.length - 1)
            value: 0
            font.pixelSize: 13
            font.bold: true

            Behavior on opacity { NumberAnimation { duration: 120 } }

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
