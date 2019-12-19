import QtQuick 2.0
import QtQuick.Controls 2.12

Item {
    id: root
    height: 20
    x:0
    y:0
    width: parent.width - 20
    state:"nothovered"
    onStateChanged: print(state)

    Rectangle {
        id: rectangle
        color: "#e8e8e8"
//        radius: 16
        opacity: 0.5
        anchors.fill: parent

    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent

        propagateComposedEvents: true
        onClicked: mouse.accepted = false;
        onPressed: mouse.accepted = false;
        onReleased: mouse.accepted = false;
        hoverEnabled: true
        onEntered: root.state = "hovered"
        onExited: root.state = "nothovered"

    }
    states: [
        State{
            name:"hovered"
            PropertyChanges {target: root; height: 64   }


        },
        State{
            name:"nothovered"
            PropertyChanges {target:root; height: 20}


        }
    ]
    transitions: [
        Transition {
            from: "nothovered"
            to: "hovered"

            NumberAnimation {
                target: root
                property: "height"
                duration: 500
                easing.type: Easing.OutQuad
            }
        },
        Transition {
            from: "hovered"
            to: "nothovered"

            NumberAnimation {
                target: root
                property: "height"
                duration: 200
                easing.type: Easing.OutQuad
            }
        }
    ]
}


