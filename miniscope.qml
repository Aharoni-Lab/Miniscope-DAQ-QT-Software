import QtQuick 2.0
import QtQuick.Window 2.12
import QtMultimedia 5.12

Item {
    width: 640
    height: 360

    Camera {
        id: camera
    }
    //MyFilter {
    //    id: filter
    //    // set properties, they can also be animated
    //    onFinished: console.log("results of the computation: " + result)
    //}
    VideoOutput {
        source: camera
    //    filters: [ filter ]
        anchors.fill: parent
    }
}
