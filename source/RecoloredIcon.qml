import QtQuick
import QtQuick.Effects

// The legacy toolbar icon glyphs are black (drawn for the old light toolbar).
// This recolors them to near-white for the fixed-dark video overlays, keeping
// the glyph's alpha shape. Set width/height on the instance; sourceSize stays
// at 24 to match the source art.
Item {
    property alias source: glyph.source

    Image {
        id: glyph
        anchors.fill: parent
        visible: false
        sourceSize.width: 24
        sourceSize.height: 24
        fillMode: Image.PreserveAspectFit
    }
    MultiEffect {
        anchors.fill: parent
        source: glyph
        brightness: 0.85 // black glyph -> near-white, alpha keeps shape
    }
}
