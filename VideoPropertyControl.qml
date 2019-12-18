import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id:root
    height:32
    x: 0
    y: 0
    width: slider.width + icon.width + textValue.width + 20
    state: "nothovered"
    onStateChanged: console.log("State = ", state)
    property color textColor: "blue"
    property var iconPath: "img/icon/ewl.ico"
    property double sliderMin: 0.0
    property double sliderMax: 100.0
    property double sliderStepSize: 1.0
    property double sliderStartValue: 0.0
    objectName: "default"

    signal valueChangedSignal(double value)

    Rectangle {
        id: rectangle
        height: parent.height
        color: "#e8e8e8"
        anchors.left: slider.left
        anchors.leftMargin: 0
        opacity: 0.5
        anchors.right: icon.right
        anchors.rightMargin: 0
        radius: 16
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
        hoverEnabled: false
        anchors.right: textValue.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        opacity: 0.75
        stepSize: root.sliderStepSize
        from: root.sliderMin
        to: root.sliderMax
        value: root.sliderStartValue
        onValueChanged: root.valueChangedSignal(slider.value)
    }

    Text {
        id: textValue
        color: root.textColor
        text: slider.value
        x: 10
        width:30
        anchors.verticalCenter: root.verticalCenter
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        font.bold: true
        font.family: "Arial"
        font.pixelSize: 20

    }

    Image {
        id: icon
        height: root.height
        sourceSize.height: 32
        sourceSize.width: 32
        anchors.left: textValue.right
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width:32

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
                easing.type: Easing.InOutQuad
            }
        },
        Transition {
            from: "hovered"
            to: "nothovered"

            NumberAnimation {
                target: textValue
                property: "x"
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }
    ]
}

