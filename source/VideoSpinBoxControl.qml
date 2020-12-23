import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Controls.Styles 1.4

Item {
    id:root
    height:24
    x: 0
    y: 0
    width: spinBox.width + icon.width + textValue.width + 20
    state: "nothovered"
    property color textColor: "black"
    property var iconPath: "img/icon/ewl.ico"

    property var displaySpinBoxValues: ["1", "2", "3"]
    property var displayTextValues: [1.0,2.0,3.0]
    property var outputValues: [1, 2, 3]
    property var outputValues2: [0,0,0,0,0,0,0,0,0,0,0]
    property var startValue: -100.23948


    onStartValueChanged: {
        spinBox.value = displaySpinBoxValues.indexOf(startValue);
        spinBox.onValueChanged();
    }

    objectName: "default"

    signal valueChangedSignal(double displayValue, double i2cValue, double i2cValue2)

    Rectangle {
        id: rectangle
        height: parent.height
        color: "#e8e8e8"
        anchors.left: spinBox.left
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

    SpinBox {
        id: spinBox
        width: 200
        height: 24
        font.family: "Arial"
        font.pointSize: 12
        font.bold: true
        hoverEnabled: false
        anchors.right: textValue.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        opacity: 0.75
        background: Rectangle {
            color: "#e8e8e8"
            opacity: 0.75
            }

        from: 0
        to: root.displaySpinBoxValues.length - 1
        value: 100

        textFromValue: function(value) {
            return root.displaySpinBoxValues[value];
        }

        onValueChanged: root.valueChangedSignal(root.displayTextValues[value], root.outputValues[value],root.outputValues2[value])
    }

    Text {
        id: textValue
        color: root.textColor
        text: root.displayTextValues[spinBox.value]
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

