import QtQuick

// Head-orientation indicator: the Miniscope logo, rotated by the BNO's
// quaternion so it mirrors how the headstage is oriented.
//
// C++ contract (Miniscope::sendNewFrame finds this by objectName "bno" and
// pushes properties): qw / qx / qy / qz / badData.
Item {
    id: root

    property double qw: 0
    property double qx: 0
    property double qy: 0
    property double qz: 0

    property bool badData: false

    // The logo's diagonal is what sweeps out as it rotates, so keep the logo
    // itself inside the item's box: the artwork is ~2.26:1, giving a diagonal
    // of ~1.09x its width.
    readonly property real logoSize: Math.min(width, height) * 0.85

    // The orientation matrix rotates about its target's own origin, so the
    // logo hangs off a zero-sized pivot at the item's centre. Rotating the
    // root Item instead orbited the logo around the item's TOP-LEFT corner,
    // which for a bottom-anchored widget swung it clean off the window.
    Item {
        id: pivot
        x: root.width / 2
        y: root.height / 2
        width: 0
        height: 0

        Image {
            // Square box, centred on the pivot; PreserveAspectFit letterboxes
            // the wide artwork inside it, so the drawn logo is centred too.
            width: root.logoSize
            height: root.logoSize
            x: -width / 2
            y: -height / 2
            fillMode: Image.PreserveAspectFit
            source: "img/Miniscope_Logo_BNO.png"
        }

        transform: Matrix4x4 {
            matrix: Qt.matrix4x4(1.0 - 2.0*root.qy*root.qy - 2.0*root.qz*root.qz,
                                 2.0*root.qx*root.qy - 2.0*root.qz*root.qw,
                                 2.0*root.qx*root.qz + 2.0*root.qy*root.qw, 0,

                                 2.0*root.qx*root.qy + 2.0*root.qz*root.qw,
                                 1.0 - 2.0*root.qx*root.qx - 2.0*root.qz*root.qz,
                                 2.0*root.qy*root.qz - 2.0*root.qx*root.qw, 0,

                                 2.0*root.qx*root.qz - 2.0*root.qy*root.qw,
                                 2.0*root.qy*root.qz + 2.0*root.qx*root.qw,
                                 1.0 - 2.0*root.qx*root.qx - 2.0*root.qy*root.qy, 0,

                                 0, 0, 0, 1.0)
        }
    }

    // Quaternion norm is off - the samples can't be trusted. Lives on the
    // un-rotated root so it stays upright and in a predictable corner.
    Text {
        visible: root.badData
        text: qsTr("!")
        color: "#ba0101"
        font.bold: true
        font.pixelSize: 15
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
