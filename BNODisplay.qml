import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id: root
    height:32
    property double heading: 0
    property double roll: 0
    property double pitch: 0

    Image {
        id: bno
        height: root.height
        sourceSize.height: 32
        sourceSize.width: 32

        anchors.left: textValue.right
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width:32

        fillMode: Image.PreserveAspectFit
        source: "img/icon/ewl.ico"
        transform: Matrix4x4 {
            property real a: Math.PI / 180

            property real c1: Math.cos(root.heading*a)
            property real c2: Math.cos(root.roll*a)
            property real c3: Math.cos(root.pitch*a)

            property real s1: Math.sin(root.heading*a)
            property real s2: Math.sin(root.roll*a)
            property real s3: Math.sin(root.pitch*a)



            matrix: Qt.matrix4x4(c2*c3, c2*s3, -s2, 0,
                                 s1*s2*c3 - c1*s3,  s1*s2*s3 + c1*c3, s1*c2, 0,
                                 c1*s2*c3 + s1*s3,  c1*s2*s3 - s1*c3, c1*c2, 0,
                                 0,           0,            0, 1)
        }
//        transform: [

//            Rotation { origin.x: 16; origin.y: 16; axis { x: 0; y: 0; z: 1 } angle: root.heading },
//            Rotation { origin.x: 16; origin.y: 16; axis { x: Math.cos(root.heading/57.296); y: Math.sin(root.heading/57.296); z: 0 } angle: root.roll },
//            Rotation { origin.x: 16; origin.y: 16; axis { x: 0; y: Math.cos(root.heading/57.296); z: Math.sin(root.heading/57.296) } angle: root.pitch }
//        ]

    }
}
