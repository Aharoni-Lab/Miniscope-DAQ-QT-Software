import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Item {
    id: root
//    height:50
    property double heading: 0
    property double roll: 0
    property double pitch: 0

    property double qw: 0
    property double qx: 0
    property double qy: 0
    property double qz: 0

    property bool badData: false



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

        Text {
            id: badDataNotice
            color: "#ba0101"
            text: qsTr("!")
            visible: root.badData
            anchors.bottomMargin: -3
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            font.bold: true
            font.pixelSize: 15
        }

    }



    transform: Matrix4x4 {
//        property real a: Math.PI / 180

//        property real c3: Math.cos(root.heading*a)
//        property real c1: Math.cos(root.roll*a)
//        property real c2: Math.cos(root.pitch*a)

//        property real s3: Math.sin(root.heading*a)
//        property real s1: Math.sin(root.roll*a)
//        property real s2: Math.sin(root.pitch*a)

//        matrix: Qt.matrix4x4(c2*c3,-c2*s3, s2,  0,
//                              c1*s3 + c3*s1*s2, c1*c3 - s1*s2*s3,    -c2*s1, 0,
//                             s1*s3 - c1*c3*s2, c3*s1 + c1*s2*s3,  c1*c2,  0,
//                             0,           0,            0, 1)

//        property real n: 1.0/Math.sqrt(qw*qw + qx*qx + qy*qy + qz*qz)

        matrix: Qt.matrix4x4(1.0 - 2.0*qy*qy - 2.0*qz*qz, 2.0*qx*qy - 2.0*qz*qw, 2.0*qx*qz + 2.0*qy*qw, 0,
                             2.0*qx*qy + 2.0*qz*qw, 1 - 2.0*qx*qx - 2.0*qz*qz, 2.0*qy*qz - 2.0*qx*qw, 0,
                             2.0*qx*qz - 2.0*qy*qw, 2.0*qy*qz + 2.0*qx*qw, 1.0 - 2.0*qx*qx - 2.0*qy*qy, 0,
                             0, 0, 0, 1.0)
//        matrix: Qt.matrix4x4(1,0,0,0,
//                             0,1,0,0,
//                             0,0,1,0,
//                             0,0,0,1)


//        matrix: Qt.matrix4x4(qw*qw + qx*qx - qy*qy - qz*qz, 2.0*qx*qy - 2.0*qz*qw, 2.0*qx*qz + 2.0*qy*qw, 0,
//                             2.0*qx*qy + 2.0*qz*qw, qw*qw - qx*qx + qy*qy - qz*qz, 2.0*qy*qz - 2.0*qx*qw, 0,
//                             2.0*qx*qz - 2.0*qy*qw, 2.0*qy*qz + 2.0*qx*qw, qw*qw - qx*qx - qy*qy + qz*qz, 0,
//                             0, 0, 0, 1.0)


    }
}

/*##^##
Designer {
    D{i:0;autoSize:true;height:480;width:640}
}
##^##*/
