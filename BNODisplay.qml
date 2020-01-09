import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id: root
//    height:50
    property double heading: 0
    property double roll: 0
    property double pitch: 0

    Image {
        id: bno
        height: 50
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
//        transformOrigin: Item.Center
        //        sourceSize.height: 32
        //        sourceSize.width: 32

//        height:32

        fillMode: Image.PreserveAspectFit
        source: "img/Miniscope_Logo_BNO.png"

        }

    transform: Matrix4x4 {
        property real a: Math.PI / 180

        property real c3: Math.cos(root.heading*a)
        property real c1: Math.cos(root.roll*a)
        property real c2: Math.cos(root.pitch*a)

        property real s3: Math.sin(root.heading*a)
        property real s1: Math.sin(root.roll*a)
        property real s2: Math.sin(root.pitch*a)

//        matrix: Qt.matrix4x4(c1*c3, -c1*s3*c2 + s1*s2, c1*s3*s2 + s1*c2, 0,
//                             s3,    c3*c2, -c3*s2, 0,
//                             -s1*c3,  s1*s3*c2 + c1*s2, -s1*s3*s2 + c1*c2, 0,
//                             0,           0,            0, 1)
        matrix: Qt.matrix4x4(c2*c3,-c2*s3, s2,  0,
                              c1*s3 + c3*s1*s2, c1*c3 - s1*s2*s3,    -c2*s1, 0,
                             s1*s3 - c1*c3*s2, c3*s1 + c1*s2*s3,  c1*c2,  0,
                             0,           0,            0, 1)

    }
}

/*##^##
Designer {
    D{i:0;autoSize:true;height:480;width:640}
}
##^##*/
