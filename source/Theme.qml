pragma Singleton
import QtQuick

// Design tokens for the ui-v3 frontend. Every color, size, and font in the
// QML comes from here — no inline hex values or per-control font settings.
// Registered as the "Theme" singleton in main.cpp (qmlRegisterSingletonType).
//
// Two palettes: dark (default — rig rooms are dim and live video reads best
// on a neutral dark canvas) and light (matches the original config-screen
// mockup). `dark` is the switch; all color tokens derive from it.
QtObject {
    id: root

    // Persisted/toggled by the shell's theme switch.
    property bool dark: true

    // --- Palette -------------------------------------------------------------
    // Accent stays in the app's historical lavender family so the app remains
    // recognizable, tuned per scheme for contrast.
    readonly property color accent: dark ? "#8b8aee" : "#7c7bdd"
    readonly property color accentHover: dark ? "#a3a2f4" : "#918fe8"
    readonly property color accentText: dark ? "#0e0e14" : "#ffffff"

    readonly property color background: dark ? "#131318" : "#eeedf4"
    readonly property color surface: dark ? "#1d1d25" : "#f7f7fb"
    readonly property color surfaceAlt: dark ? "#26262f" : "#e9e8f2"
    readonly property color border: dark ? "#34343f" : "#d8d7e4"

    readonly property color textPrimary: dark ? "#e8e8ef" : "#26262e"
    readonly property color textSecondary: dark ? "#a3a3b0" : "#6b6b78"
    readonly property color textDisabled: dark ? "#5c5c68" : "#b0afbd"

    readonly property color success: dark ? "#5dbb7a" : "#2e8b4d"
    readonly property color warning: dark ? "#e0b34c" : "#b07d18"
    readonly property color danger: dark ? "#e06060" : "#c03535"
    readonly property color recording: "#e53935" // one red in both schemes

    // Video panes sit on pure black so the imagery, not the chrome, dominates.
    readonly property color videoCanvas: "#000000"

    // --- Typography ------------------------------------------------------------
    // System UI font (no more hardcoded Arial); mono for numeric telemetry so
    // FPS / clock / disk readouts don't jitter as digits change.
    readonly property font fontBody: Qt.font({ pointSize: 12 })
    readonly property font fontSmall: Qt.font({ pointSize: 10 })
    readonly property font fontTitle: Qt.font({ pointSize: 15, weight: Font.DemiBold })
    readonly property font fontDisplay: Qt.font({ pointSize: 22, weight: Font.DemiBold })
    readonly property font fontMono: Qt.font({ family: "Menlo, Consolas, monospace", pointSize: 11 })

    // --- Metrics ---------------------------------------------------------------
    readonly property int spacing: 8        // base unit; use multiples
    readonly property int padding: 16       // card / page padding
    readonly property int radius: 8         // cards, buttons
    readonly property int radiusSmall: 5    // fields, chips
    readonly property int controlHeight: 36 // buttons, fields
    readonly property int touchTarget: 40   // minimum interactive size

    // Motion: one standard duration; no decorative animation.
    readonly property int animMs: 120
}
