import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Miniscope.Theme 1.0

// The schema-driven user-config editor: a card-based form (General /
// Recording / Devices / feature blocks / Advanced) plus a raw-JSON tab.
//
// Data flow: every field reads from backend.userConfigJson (re-evaluated on
// each change) and writes through backend.setConfigValue(path, value), so the
// config object stays the single source of truth and keys the form doesn't
// know about — COMMENT_* annotations, window positions, retired settings —
// ride along untouched. Field help comes from userConfigProps.json (label
// tooltips); device control ranges/choices come from videoDevices.json.
Item {
    id: form

    // Ask the host view to open its Add-Device dialog.
    signal addDeviceRequested()

    // Live view of the config. Re-evaluates whenever the backend signals a change.
    readonly property var cfg: backend ? backend.userConfigJson : ({})
    // Editor metadata; constant per run.
    property var props: ({})
    property var catalog: ({})
    property var serialPorts: []

    Component.onCompleted: {
        props = backend.configPropsJson()
        catalog = backend.deviceCatalogJson()
        refreshSerialPorts()
        deviceRows = buildDeviceRows()
    }

    // --- Config access helpers -------------------------------------------------
    function val(path, fallback) {
        var node = cfg
        for (var i = 0; i < path.length; i++) {
            if (node === undefined || node === null)
                return fallback
            node = node[path[i]]
        }
        return (node === undefined || node === null) ? fallback : node
    }
    function set(path, value) { backend.setConfigValue(path, value) }

    function tip(path) {
        var node = props
        for (var i = 0; i < path.length; i++) {
            if (node === undefined || node === null)
                return ""
            node = node[path[i]]
        }
        return (node && node.tips) ? node.tips : ""
    }
    // Device keys live under a per-category template object in the props file.
    function devTip(category, path) {
        var templateKey = category === "miniscopes" ? "miniscopeDeviceName" : "cameraDeviceName"
        return tip(["devices", category, templateKey].concat(path))
    }

    // Control metadata (ranges, dropdown choices) for a catalog device type.
    function ctrl(deviceType, controlName) {
        var entry = catalog[deviceType]
        if (!entry || !entry.controlSettings)
            return undefined
        return entry.controlSettings[controlName]
    }
    function led0Max(deviceType, fineSteps) {
        var c = ctrl(deviceType, "led0")
        if (!c)
            return 100
        if (fineSteps && c.fineSteps && c.fineSteps.max !== undefined)
            return c.fineSteps.max
        return c.max !== undefined ? c.max : 100
    }
    // Codec dropdown model: the host-supported list, plus the configured value
    // if it isn't supported here (so the row shows what the file says).
    function codecModel(current) {
        var list = backend ? backend.availableCodecs : []
        if (current && current.length > 0 && list.indexOf(current) < 0)
            return [current].concat(list)
        return list
    }

    function refreshSerialPorts() {
        var ports = backend ? backend.availableSerialPorts() : []
        var current = val(["commutator", "port"], "")
        var found = false
        for (var i = 0; i < ports.length; i++)
            if (ports[i].name === current)
                found = true
        if (current.length > 0 && !found)
            ports = [{ name: current, label: current + " (not detected)" }].concat(ports)
        serialPorts = ports
    }

    // --- Device list -------------------------------------------------------------
    // The Repeater's model only rebuilds when the device STRUCTURE (names /
    // categories) changes, not on every value edit — otherwise each keystroke
    // inside an edit drawer would recreate the delegates and close the drawer.
    readonly property string deviceKey: {
        var parts = []
        var devs = cfg.devices || {}
        var cats = ["miniscopes", "cameras"]
        for (var c = 0; c < cats.length; c++) {
            var names = Object.keys(devs[cats[c]] || {})
            for (var i = 0; i < names.length; i++)
                parts.push(cats[c] + "\u001f" + names[i])
        }
        return parts.join("\u001e")
    }
    property var deviceRows: []
    onDeviceKeyChanged: deviceRows = buildDeviceRows()
    function buildDeviceRows() {
        var out = []
        var devs = cfg.devices || {}
        var cats = ["miniscopes", "cameras"]
        for (var c = 0; c < cats.length; c++) {
            var section = devs[cats[c]] || {}
            var names = Object.keys(section)
            for (var i = 0; i < names.length; i++)
                out.push({ category: cats[c], name: names[i] })
        }
        return out
    }

    // --- Shared dialogs ----------------------------------------------------------
    FolderDialog {
        id: folderDialog
        property var onPicked: null
        onAccepted: if (onPicked) onPicked(backend.urlToLocalFile(selectedFolder))
    }
    function pickFolder(title, callback) {
        folderDialog.title = title
        folderDialog.onPicked = callback
        folderDialog.open()
    }

    FileDialog {
        id: filePickDialog
        property var onPicked: null
        onAccepted: if (onPicked) onPicked(backend.urlToLocalFile(selectedFile))
    }
    function pickFile(title, callback) {
        filePickDialog.title = title
        filePickDialog.onPicked = callback
        filePickDialog.open()
    }

    Dialog {
        id: removeDeviceDialog
        property string category: ""
        property string deviceName: ""
        title: qsTr("Remove device")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Text {
            text: qsTr("Remove \"%1\" from this configuration?").arg(removeDeviceDialog.deviceName)
            font: Theme.fontBody
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.removeDevice(category, deviceName)
    }

    // --- Layout --------------------------------------------------------------
    component EditorTab: TabButton {
        id: tabBtn
        implicitHeight: Theme.controlHeight
        font: Theme.fontBody
        contentItem: Text {
            text: tabBtn.text
            font: tabBtn.font
            color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: tabBtn.checked ? Theme.surfaceAlt : "transparent"
            border.width: tabBtn.checked ? 1 : 0
            border.color: Theme.border
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        TabBar {
            id: tabs
            Layout.preferredWidth: 220
            background: Rectangle { color: "transparent" }
            EditorTab { text: qsTr("Form") }
            EditorTab { text: qsTr("JSON") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // =================== Form page ===================
            ScrollView {
                id: formScroll
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: formScroll.availableWidth
                    spacing: Theme.spacing

                    // --- General ---
                    FormCard {
                        title: qsTr("General")

                        FormRow {
                            label: qsTr("Researcher")
                            tip: form.tip(["researcherName"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["researcherName"], "")
                                onTextEdited: form.set(["researcherName"], text)
                            }
                        }
                        FormRow {
                            label: qsTr("Experiment")
                            tip: form.tip(["experimentName"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["experimentName"], "")
                                onTextEdited: form.set(["experimentName"], text)
                            }
                        }
                        FormRow {
                            label: qsTr("Animal")
                            tip: form.tip(["animalName"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["animalName"], "")
                                onTextEdited: form.set(["animalName"], text)
                            }
                        }
                        FormRow {
                            label: qsTr("Data directory")
                            tip: form.tip(["dataDirectory"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["dataDirectory"], "")
                                onTextEdited: form.set(["dataDirectory"], text)
                            }
                            UiButton {
                                text: qsTr("Browse…")
                                onClicked: form.pickFolder(qsTr("Choose the data directory"),
                                                           function(path) { form.set(["dataDirectory"], path) })
                            }
                        }
                        FormRow {
                            label: qsTr("Folder structure")
                            tip: form.tip(["directoryStructure"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["directoryStructure"], []).join(" / ")
                                onTextEdited: {
                                    var parts = text.split("/").map(function(s) { return s.trim() })
                                                     .filter(function(s) { return s.length > 0 })
                                    form.set(["directoryStructure"], parts)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                var parts = form.val(["directoryStructure"], [])
                                var mapped = []
                                for (var i = 0; i < parts.length; i++) {
                                    var t = parts[i]
                                    if (t === "Date") mapped.push("YYYY_MM_DD")
                                    else if (t === "Time") mapped.push("HH_MM_SS")
                                    else mapped.push(String(form.val([t], t)))
                                }
                                return qsTr("Recordings will be saved to:  %1")
                                    .arg(form.val(["dataDirectory"], "") + "/" + mapped.join("/"))
                            }
                            font: Theme.fontSmall
                            color: Theme.textSecondary
                            elide: Text.ElideMiddle
                        }
                    }

                    // --- Recording ---
                    FormCard {
                        title: qsTr("Recording")

                        FormRow {
                            label: qsTr("Recording length")
                            tip: form.tip(["recordLengthInSeconds"])
                            UiSpinBox {
                                from: 0
                                to: 360000
                                stepSize: 10
                                syncValue: form.val(["recordLengthInSeconds"], 0)
                                onValueModified: form.set(["recordLengthInSeconds"], value)
                            }
                            Text {
                                text: form.val(["recordLengthInSeconds"], 0) === 0
                                      ? qsTr("seconds — 0 records until manually stopped")
                                      : qsTr("seconds (≈ %1 min)").arg(
                                            (form.val(["recordLengthInSeconds"], 0) / 60).toFixed(1))
                                font: Theme.fontSmall
                                color: Theme.textSecondary
                            }
                        }
                    }

                    // --- Devices ---
                    FormCard {
                        title: qsTr("Devices")
                        subtitle: form.deviceRows.length === 0
                                  ? qsTr("Add at least one Miniscope or camera to run.") : ""

                        Repeater {
                            model: form.deviceRows

                            // One device: summary row + expandable edit drawer.
                            delegate: Rectangle {
                                id: deviceRow
                                required property var modelData

                                readonly property string cat: modelData.category
                                readonly property string name: modelData.name
                                readonly property bool isMiniscope: cat === "miniscopes"
                                readonly property string devType: form.val(["devices", cat, name, "deviceType"], "")
                                function dval(key, fallback) { return form.val(["devices", cat, name, key], fallback) }
                                function dset(key, value) { form.set(["devices", cat, name, key], value) }

                                property bool editOpen: false

                                Layout.fillWidth: true
                                implicitHeight: deviceColumn.implicitHeight + 2 * Theme.spacing
                                radius: Theme.radiusSmall
                                color: Theme.surfaceAlt
                                border.width: 1
                                border.color: Theme.border
                                clip: true
                                Behavior on implicitHeight { NumberAnimation { duration: Theme.animMs } }

                                ColumnLayout {
                                    id: deviceColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.spacing
                                    spacing: Theme.spacing

                                    // Summary row
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacing

                                        Rectangle {
                                            radius: Theme.radiusSmall
                                            color: "transparent"
                                            border.width: 1
                                            border.color: Theme.accent
                                            implicitWidth: badgeText.implicitWidth + 12
                                            implicitHeight: badgeText.implicitHeight + 6
                                            Text {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: deviceRow.isMiniscope ? qsTr("MINISCOPE") : qsTr("CAMERA")
                                                font: Theme.fontSmall
                                                color: Theme.accent
                                            }
                                        }

                                        ColumnLayout {
                                            spacing: 0
                                            Text {
                                                text: deviceRow.name
                                                font: Theme.fontBody
                                                color: Theme.textPrimary
                                            }
                                            Text {
                                                text: deviceRow.devType + qsTr("  ·  device ID %1").arg(deviceRow.dval("deviceID", 0))
                                                font: Theme.fontSmall
                                                color: Theme.textSecondary
                                            }
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            text: qsTr("Codec")
                                            font: Theme.fontSmall
                                            color: Theme.textSecondary
                                        }
                                        UiComboBox {
                                            implicitWidth: 110
                                            model: form.codecModel(deviceRow.dval("compression", ""))
                                            syncValue: deviceRow.dval("compression", "")
                                            onActivated: deviceRow.dset("compression", currentText)
                                        }

                                        UiButton {
                                            text: deviceRow.editOpen ? qsTr("Close") : qsTr("Edit")
                                            onClicked: deviceRow.editOpen = !deviceRow.editOpen
                                        }
                                        UiButton {
                                            text: qsTr("Delete")
                                            danger: true
                                            onClicked: {
                                                removeDeviceDialog.category = deviceRow.cat
                                                removeDeviceDialog.deviceName = deviceRow.name
                                                removeDeviceDialog.open()
                                            }
                                        }
                                    }

                                    // Edit drawer
                                    ColumnLayout {
                                        visible: deviceRow.editOpen
                                        Layout.fillWidth: true
                                        Layout.leftMargin: Theme.spacing
                                        spacing: Theme.spacing

                                        FormRow {
                                            label: qsTr("Device ID")
                                            tip: form.devTip(deviceRow.cat, ["deviceID"])
                                            UiSpinBox {
                                                from: 0; to: 63
                                                syncValue: deviceRow.dval("deviceID", 0)
                                                onValueModified: deviceRow.dset("deviceID", value)
                                            }
                                            Text {
                                                text: qsTr("Use Scan Devices to see which ID is which camera.")
                                                font: Theme.fontSmall
                                                color: Theme.textSecondary
                                            }
                                        }

                                        FormRow {
                                            visible: form.ctrl(deviceRow.devType, "gain") !== undefined
                                            label: qsTr("Gain")
                                            tip: form.devTip(deviceRow.cat, ["gain"])
                                            UiComboBox {
                                                implicitWidth: 140
                                                model: {
                                                    var c = form.ctrl(deviceRow.devType, "gain")
                                                    return c ? c.displaySpinBoxValues : []
                                                }
                                                syncValue: deviceRow.dval("gain", "")
                                                onActivated: deviceRow.dset("gain", currentText)
                                            }
                                        }

                                        FormRow {
                                            visible: form.ctrl(deviceRow.devType, "frameRate") !== undefined
                                            label: qsTr("Frame rate")
                                            tip: form.devTip(deviceRow.cat, ["frameRate"])
                                            UiComboBox {
                                                implicitWidth: 140
                                                model: {
                                                    var c = form.ctrl(deviceRow.devType, "frameRate")
                                                    return c ? c.displaySpinBoxValues : []
                                                }
                                                syncValue: deviceRow.dval("frameRate", "")
                                                onActivated: deviceRow.dset("frameRate", currentText)
                                            }
                                        }

                                        FormRow {
                                            visible: form.ctrl(deviceRow.devType, "led0") !== undefined
                                            label: qsTr("Excitation LED")
                                            tip: form.devTip(deviceRow.cat, ["led0"])
                                            UiSpinBox {
                                                from: 0
                                                to: form.led0Max(deviceRow.devType, deviceRow.dval("led0FineSteps", false))
                                                syncValue: deviceRow.dval("led0", 0)
                                                onValueModified: deviceRow.dset("led0", value)
                                            }
                                            UiSwitch {
                                                visible: {
                                                    var c = form.ctrl(deviceRow.devType, "led0")
                                                    return c !== undefined && c.fineSteps !== undefined
                                                }
                                                text: qsTr("Fine steps (0–255)")
                                                syncChecked: deviceRow.dval("led0FineSteps", false)
                                                onToggled: deviceRow.dset("led0FineSteps", checked)
                                            }
                                        }

                                        FormRow {
                                            visible: form.ctrl(deviceRow.devType, "ewl") !== undefined
                                            label: qsTr("EWL focus")
                                            tip: form.devTip(deviceRow.cat, ["ewl"])
                                            UiSpinBox {
                                                from: {
                                                    var c = form.ctrl(deviceRow.devType, "ewl")
                                                    return (c && c.min !== undefined) ? c.min : -127
                                                }
                                                to: {
                                                    var c = form.ctrl(deviceRow.devType, "ewl")
                                                    return (c && c.max !== undefined) ? c.max : 127
                                                }
                                                syncValue: deviceRow.dval("ewl", 0)
                                                onValueModified: deviceRow.dset("ewl", value)
                                            }
                                        }

                                        FormRow {
                                            visible: deviceRow.isMiniscope
                                            label: qsTr("Display colormap")
                                            tip: form.devTip(deviceRow.cat, ["lut"])
                                            UiComboBox {
                                                implicitWidth: 140
                                                model: backend ? backend.availableLUTs : []
                                                syncValue: deviceRow.dval("lut", "None")
                                                onActivated: deviceRow.dset("lut", currentText)
                                            }
                                            Text {
                                                text: qsTr("Display only — recordings stay grayscale.")
                                                font: Theme.fontSmall
                                                color: Theme.textSecondary
                                            }
                                        }

                                        FormRow {
                                            label: qsTr("Frames per file")
                                            tip: form.devTip(deviceRow.cat, ["framesPerFile"])
                                            UiSpinBox {
                                                from: 1; to: 100000
                                                stepSize: 100
                                                syncValue: deviceRow.dval("framesPerFile", 1000)
                                                onValueModified: deviceRow.dset("framesPerFile", value)
                                            }
                                        }

                                        FormRow {
                                            label: qsTr("Show saturation")
                                            tip: form.devTip(deviceRow.cat, ["showSaturation"])
                                            UiSwitch {
                                                syncChecked: deviceRow.dval("showSaturation", false)
                                                onToggled: deviceRow.dset("showSaturation", checked)
                                            }
                                        }

                                        FormRow {
                                            visible: deviceRow.isMiniscope
                                            label: qsTr("Head orientation")
                                            tip: form.devTip(deviceRow.cat, ["headOrientation", "enabled"])
                                            UiSwitch {
                                                text: qsTr("Enabled")
                                                syncChecked: form.val(["devices", deviceRow.cat, deviceRow.name,
                                                                       "headOrientation", "enabled"], false)
                                                onToggled: form.set(["devices", deviceRow.cat, deviceRow.name,
                                                                     "headOrientation", "enabled"], checked)
                                            }
                                            UiSwitch {
                                                visible: form.val(["devices", deviceRow.cat, deviceRow.name,
                                                                   "headOrientation", "enabled"], false)
                                                text: qsTr("Filter bad data")
                                                syncChecked: form.val(["devices", deviceRow.cat, deviceRow.name,
                                                                       "headOrientation", "filterBadData"], false)
                                                onToggled: form.set(["devices", deviceRow.cat, deviceRow.name,
                                                                     "headOrientation", "filterBadData"], checked)
                                            }
                                        }

                                        // ROI: present -> edit fields + remove; absent -> full sensor + add.
                                        FormRow {
                                            label: qsTr("ROI")
                                            tip: form.devTip(deviceRow.cat, ["ROI", "leftEdge"])

                                            RowLayout {
                                                visible: deviceRow.dval("ROI", undefined) !== undefined
                                                spacing: Theme.spacing
                                                Text { text: qsTr("x"); font: Theme.fontSmall; color: Theme.textSecondary }
                                                UiSpinBox {
                                                    implicitWidth: 110
                                                    from: 0; to: 10000
                                                    syncValue: form.val(["devices", deviceRow.cat, deviceRow.name, "ROI", "leftEdge"], 0)
                                                    onValueModified: form.set(["devices", deviceRow.cat, deviceRow.name, "ROI", "leftEdge"], value)
                                                }
                                                Text { text: qsTr("y"); font: Theme.fontSmall; color: Theme.textSecondary }
                                                UiSpinBox {
                                                    implicitWidth: 110
                                                    from: 0; to: 10000
                                                    syncValue: form.val(["devices", deviceRow.cat, deviceRow.name, "ROI", "topEdge"], 0)
                                                    onValueModified: form.set(["devices", deviceRow.cat, deviceRow.name, "ROI", "topEdge"], value)
                                                }
                                                Text { text: qsTr("w"); font: Theme.fontSmall; color: Theme.textSecondary }
                                                UiSpinBox {
                                                    implicitWidth: 110
                                                    from: 1; to: 10000
                                                    syncValue: form.val(["devices", deviceRow.cat, deviceRow.name, "ROI", "width"], 0)
                                                    onValueModified: form.set(["devices", deviceRow.cat, deviceRow.name, "ROI", "width"], value)
                                                }
                                                Text { text: qsTr("h"); font: Theme.fontSmall; color: Theme.textSecondary }
                                                UiSpinBox {
                                                    implicitWidth: 110
                                                    from: 1; to: 10000
                                                    syncValue: form.val(["devices", deviceRow.cat, deviceRow.name, "ROI", "height"], 0)
                                                    onValueModified: form.set(["devices", deviceRow.cat, deviceRow.name, "ROI", "height"], value)
                                                }
                                                UiButton {
                                                    text: qsTr("Remove")
                                                    onClicked: backend.removeConfigKey(
                                                                   ["devices", deviceRow.cat, deviceRow.name, "ROI"])
                                                }
                                            }

                                            RowLayout {
                                                visible: deviceRow.dval("ROI", undefined) === undefined
                                                spacing: Theme.spacing
                                                Text {
                                                    text: qsTr("Full sensor")
                                                    font: Theme.fontSmall
                                                    color: Theme.textSecondary
                                                }
                                                UiButton {
                                                    text: qsTr("Define ROI…")
                                                    onClicked: {
                                                        var entry = form.catalog[deviceRow.devType] || {}
                                                        form.set(["devices", deviceRow.cat, deviceRow.name, "ROI"],
                                                                 { leftEdge: 0, topEdge: 0,
                                                                   width: entry.width !== undefined ? entry.width : 600,
                                                                   height: entry.height !== undefined ? entry.height : 600 })
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            visible: form.deviceRows.length === 0
                            text: qsTr("No devices in this configuration yet.")
                            font: Theme.fontBody
                            color: Theme.textSecondary
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            UiButton {
                                text: qsTr("+ Add Device")
                                primary: true
                                onClicked: form.addDeviceRequested()
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Codecs available on this computer: %1")
                                  .arg(backend ? backend.availableCodecList : "")
                            font: Theme.fontSmall
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }

                    // --- Trace display ---
                    FormCard {
                        title: qsTr("Trace display")
                        subtitle: qsTr("Plots head orientation, fluorescence ROIs, and pose traces in a scrolling window during acquisition.")
                        showSwitch: true
                        switchChecked: form.val(["traceDisplay", "enabled"], false)
                        onSwitchToggled: checked => form.set(["traceDisplay", "enabled"], checked)
                    }

                    // --- Behavior tracker ---
                    FormCard {
                        title: qsTr("Behavior tracker")
                        subtitle: qsTr("Live pose tracking with DeepLabCut-Live. Requires a prepared Python environment.")
                        expanded: false
                        showSwitch: true
                        switchChecked: form.val(["behaviorTracker", "enabled"], false)
                        onSwitchToggled: checked => form.set(["behaviorTracker", "enabled"], checked)

                        FormRow {
                            label: qsTr("Python environment")
                            tip: form.tip(["behaviorTracker", "pyEnvPath"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["behaviorTracker", "pyEnvPath"], "")
                                onTextEdited: form.set(["behaviorTracker", "pyEnvPath"], text)
                            }
                            UiButton {
                                text: qsTr("Browse…")
                                onClicked: form.pickFolder(qsTr("Choose the Python environment folder"),
                                                           function(path) { form.set(["behaviorTracker", "pyEnvPath"], path) })
                            }
                        }
                        FormRow {
                            label: qsTr("DLC model")
                            tip: form.tip(["behaviorTracker", "modelPath"])
                            UiTextField {
                                Layout.fillWidth: true
                                syncText: form.val(["behaviorTracker", "modelPath"], "")
                                onTextEdited: form.set(["behaviorTracker", "modelPath"], text)
                            }
                            UiButton {
                                text: qsTr("Browse…")
                                onClicked: form.pickFolder(qsTr("Choose the DLC model folder"),
                                                           function(path) { form.set(["behaviorTracker", "modelPath"], path) })
                            }
                        }
                        FormRow {
                            label: qsTr("Resize factor")
                            tip: form.tip(["behaviorTracker", "resize"])
                            UiTextField {
                                implicitWidth: 100
                                syncText: form.val(["behaviorTracker", "resize"], 1)
                                validator: DoubleValidator { bottom: 0.05; top: 1.0 }
                                onEditingFinished: {
                                    var v = parseFloat(text)
                                    if (!isNaN(v))
                                        form.set(["behaviorTracker", "resize"], v)
                                }
                            }
                        }
                        FormRow {
                            label: qsTr("p cutoff (display)")
                            tip: form.tip(["behaviorTracker", "pCutoffDisplay"])
                            UiTextField {
                                implicitWidth: 100
                                syncText: form.val(["behaviorTracker", "pCutoffDisplay"], 0)
                                validator: DoubleValidator { bottom: 0.0; top: 1.0 }
                                onEditingFinished: {
                                    var v = parseFloat(text)
                                    if (!isNaN(v))
                                        form.set(["behaviorTracker", "pCutoffDisplay"], v)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Occupancy plot, pose overlay, and trace options are in the JSON tab.")
                            font: Theme.fontSmall
                            color: Theme.textSecondary
                        }
                    }

                    // --- Commutator ---
                    FormCard {
                        title: qsTr("Commutator")
                        subtitle: qsTr("Drives an Open Ephys commutator from the Miniscope's head orientation so the tether unwinds itself.")
                        expanded: false
                        showSwitch: true
                        switchChecked: form.val(["commutator", "enabled"], false)
                        onSwitchToggled: checked => form.set(["commutator", "enabled"], checked)

                        FormRow {
                            label: qsTr("Serial port")
                            tip: form.tip(["commutator", "port"])
                            UiComboBox {
                                Layout.fillWidth: true
                                model: form.serialPorts
                                textRole: "label"
                                valueRole: "name"
                                syncValue: form.val(["commutator", "port"], "")
                                onActivated: form.set(["commutator", "port"], currentValue)
                            }
                            UiButton {
                                text: qsTr("Rescan")
                                onClicked: form.refreshSerialPorts()
                            }
                        }
                        FormRow {
                            label: qsTr("Miniscope")
                            tip: form.tip(["commutator", "deviceName"])
                            UiTextField {
                                Layout.fillWidth: true
                                placeholderText: qsTr("blank = first Miniscope with head orientation")
                                syncText: form.val(["commutator", "deviceName"], "")
                                onTextEdited: form.set(["commutator", "deviceName"], text)
                            }
                        }
                        FormRow {
                            label: qsTr("Indicator LED")
                            tip: form.tip(["commutator", "led"])
                            UiSwitch {
                                syncChecked: form.val(["commutator", "led"], true)
                                onToggled: form.set(["commutator", "led"], checked)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Axis and fallback tuning are in the JSON tab.")
                            font: Theme.fontSmall
                            color: Theme.textSecondary
                        }
                    }

                    // --- Advanced ---
                    FormCard {
                        title: qsTr("Advanced")
                        subtitle: qsTr("Run an external program when recording starts or stops (e.g. to sync third-party hardware).")
                        expanded: false

                        Repeater {
                            model: [
                                { key: "executableOnStartRecording", label: qsTr("On record start") },
                                { key: "executableOnStopRecording",  label: qsTr("On record stop") }
                            ]
                            delegate: FormRow {
                                required property var modelData
                                label: modelData.label
                                tip: form.tip([modelData.key, "filePath"])
                                UiSwitch {
                                    syncChecked: form.val([modelData.key, "enabled"], false)
                                    onToggled: form.set([modelData.key, "enabled"], checked)
                                }
                                UiTextField {
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("path to executable")
                                    syncText: form.val([modelData.key, "filePath"], "")
                                    onTextEdited: form.set([modelData.key, "filePath"], text)
                                }
                                UiButton {
                                    text: qsTr("Browse…")
                                    onClicked: {
                                        var key = modelData.key
                                        form.pickFile(qsTr("Choose the executable"),
                                                      function(path) { form.set([key, "filePath"], path) })
                                    }
                                }
                                UiTextField {
                                    implicitWidth: 180
                                    placeholderText: qsTr("arguments, comma separated")
                                    syncText: form.val([modelData.key, "arguments"], []).join(", ")
                                    onTextEdited: {
                                        var parts = text.split(",").map(function(s) { return s.trim() })
                                                         .filter(function(s) { return s.length > 0 })
                                        form.set([modelData.key, "arguments"], parts)
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Window positions, COMMENT_* notes, and any custom keys are preserved as-is — edit them in the JSON tab.")
                            font: Theme.fontSmall
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // =================== JSON page ===================
            ColumnLayout {
                spacing: Theme.spacing
                // StackLayout drives this page's visibility; refresh from the
                // backend whenever the tab is brought forward, so it always
                // starts from the config the form last produced.
                onVisibleChanged: if (visible) jsonPage.revert()

                Item {
                    id: jsonPage
                    visible: false
                    property string lastError: ""
                    function revert() {
                        jsonArea.text = backend.rawConfigJson()
                        lastError = ""
                    }
                    function apply() {
                        lastError = backend.applyRawConfigJson(jsonArea.text)
                        if (lastError.length === 0)
                            jsonArea.text = backend.rawConfigJson()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Full config as JSON — every key, including ones the form doesn't show.")
                        font: Theme.fontSmall
                        color: Theme.textSecondary
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    UiButton {
                        text: qsTr("Revert")
                        onClicked: jsonPage.revert()
                    }
                    UiButton {
                        text: qsTr("Apply")
                        primary: true
                        onClicked: jsonPage.apply()
                    }
                }

                Text {
                    visible: jsonPage.lastError.length > 0
                    text: jsonPage.lastError
                    font: Theme.fontSmall
                    color: Theme.danger
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        id: jsonArea
                        font: Theme.fontMono
                        color: Theme.textPrimary
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.accentText
                        wrapMode: TextArea.NoWrap
                        tabStopDistance: 24
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.surfaceAlt
                            border.width: 1
                            border.color: Theme.border
                        }
                    }
                }
            }
        }
    }
}
