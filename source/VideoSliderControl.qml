import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One hardware-control row for the left dock (LED, EWL, ...). The current value
// is always visible (icon + number); the dock reveals the slider in-row when it
// expands, so every control's slider comes out together - no per-control hover
// flyout floating over the video.
//
// The dock drives `expanded`; when collapsed the slider is hidden and the row
// is just icon + value, so the whole dock stays narrow.
//
// C++ contract (videodevice.cpp control loop, keep stable): objectName set by
// the window shell; catalog-driven properties min/max/stepSize/startValue/
// displayValueScale/displayValueOffset/displayValueBitShift set via
// setProperty; valueChangedSignal(display, i2c, i2c2) consumed for hardware
// commands. Setting startValue moves the slider AND emits - initial config
// values reach the device through this path.
Item {
    id: root
    implicitHeight: 30

    property bool expanded: false
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

    RowLayout {
        anchors.fill: parent
        spacing: 6

        RecoloredIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            source: root.iconPath
        }

        Slider {
            id: slider
            visible: root.expanded
            opacity: root.expanded ? 1 : 0
            Layout.fillWidth: true
            Layout.preferredHeight: root.implicitHeight
            from: root.min
            to: root.max
            stepSize: root.stepSize

            Behavior on opacity { NumberAnimation { duration: 120 } }

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

        Text {
            Layout.preferredWidth: 34
            text: slider.value.toFixed(root.decimalPrecision)
            color: root.textColor
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignRight
        }
    }
}
