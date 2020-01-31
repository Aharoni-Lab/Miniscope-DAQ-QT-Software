import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id:root
    height:24
    x: 0
    y: 0
    width: slider.width + icon.width + textValue.width + 20
    state: "nothovered"
    property color textColor: "black"
    property var iconPath: "img/icon/ewl.ico"
    property double min: 0.0
    property double max: 100.0
    property double stepSize: 1.0
    property double startValue: -1000.23948 // just start with a value that will definitely be different than initialize value. This way onValueChanged will be called
    property double displayValueScale: 1
    property double displayValueOffset: 0
    property double displayValueBitShift: 0
    property double displayRotation: 0
    property int decimalPrecision: 0
    objectName: "default"

//    onStartValueChanged: {
//        slider.valueChanged()
//    }
    signal valueChangedSignal(double displayValue, double i2cValue, double i2cValue2)

    Rectangle {
        id: rectangle
        height: parent.height
        color: "#e8e8e8"
        anchors.left: slider.left
        anchors.leftMargin: 0
        opacity: 0.5
        anchors.right: icon.right
        anchors.rightMargin: 0
//        radius: 16
    }

    MouseArea {
        id: mouseArea
        width: parent.width
        height: parent.height
        anchors.right: icon.right
        anchors.rightMargin: 0

        propagateComposedEvents: true
        onClicked: mouse.accepted = false;
        onPressed: mouse.accepted = false;
        onReleased: mouse.accepted = false;
        hoverEnabled: true
        onEntered: root.state = "hovered"
        onExited: root.state = "nothovered"

    }

    Slider {
        id: slider
        rotation: root.displayRotation
        hoverEnabled: false
//        mirrored:
        anchors.right: textValue.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        opacity: 0.75
        stepSize: root.stepSize
        from: root.min
        to: root.max
        value: root.startValue

        onValueChanged: root.valueChangedSignal(value, (value * root.displayValueScale  - root.displayValueOffset)<<root.displayValueBitShift, 0)

//        function valueChanged(){
//            root.valueChangedSignal(value, value * root.displayValueScale  - root.displayValueOffset);
//        }
    }

    Text {
        id: textValue
        color: root.textColor
        text: (slider.value).toFixed(decimalPrecision)
        x: 10
        width:30
        anchors.verticalCenter: root.verticalCenter
        horizontalAlignment: Text.AlignCenter
        verticalAlignment: Text.AlignVCenter
        font.bold: true
        font.family: "Arial"
        font.pixelSize: 20

    }

    Image {
        id: icon
        height: root.height
        sourceSize.height: 24
        sourceSize.width: 24
        anchors.left: textValue.right
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width:24

        fillMode: Image.PreserveAspectFit
        source: root.iconPath

    }

    states: [
        State{
            name:"hovered"
            PropertyChanges {target: textValue; x:210   }

        },
        State{
            name:"nothovered"
            PropertyChanges {target:textValue; x: 10}


        }
    ]
    transitions: [
        Transition {
            from: "nothovered"
            to: "hovered"

            NumberAnimation {
                target: textValue
                property: "x"
                duration: 500
                easing.type: Easing.OutQuad
            }
        },
        Transition {
            from: "hovered"
            to: "nothovered"

            NumberAnimation {
                target: textValue
                property: "x"
                duration: 200
                easing.type: Easing.OutQuad
            }
        }
    ]
}

