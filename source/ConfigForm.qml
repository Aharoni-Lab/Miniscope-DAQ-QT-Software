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
    // Ask the host view to show the connected-video-device scan. It belongs
    // with the Devices card (it's how you find a device's ID) rather than in
    // the config action bar, where it read as a peer of Open / New.
    signal scanDevicesRequested()

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
    //
    // Each category leads with the codecs that should actually be used for it,
    // rather than offering whatever the host happened to enumerate first:
    // Miniscope imaging stays lossless (GREY / FFV1), while behavior video is a
    // natural scene that lossy codecs handle well (MJPG / XVID).
    readonly property var losslessCodecs: ["GREY", "FFV1"]
    readonly property var behaviorCodecs: ["MJPG", "XVID"]
    function preferredCodecs(category) {
        return category === "miniscopes" ? losslessCodecs : behaviorCodecs
    }
    function codecModel(current, category) {
        var list = backend ? backend.availableCodecs.slice() : []
        var preferred = preferredCodecs(category)
        var lead = preferred.filter(function (c) { return list.indexOf(c) >= 0 })
        var rest = list.filter(function (c) { return preferred.indexOf(c) < 0 })
        list = lead.concat(rest)
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
            Layout.preferredWidth: 330
            background: Rectangle { color: "transparent" }
            EditorTab { text: qsTr("Form") }
            // Rarely-used settings (behavior tracking, external programs) live
            // here so the Form page stays about running an experiment.
            EditorTab { text: qsTr("Advanced") }
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
                        // Live folder-structure feedback: the exact path a
                        // recording will build, plus whether every entry
                        // resolves. Mirrors DataSaver::setupBaseDirectory so the
                        // preview matches what actually gets written to disk.
                        ColumnLayout {
                            id: dirPreview
                            Layout.fillWidth: true
                            spacing: 2

                            property var parts: form.val(["directoryStructure"], [])
                            property string dataDir: form.val(["dataDirectory"], "")
                            // Entries that are neither date/time nor a non-empty
                            // key in this config; the DAQ saves these as literal
                            // "<name>Missing" folders.
                            property var missing: {
                                var bad = []
                                for (var i = 0; i < parts.length; i++) {
                                    var t = String(parts[i]); var tl = t.toLowerCase()
                                    if (tl === "date" || tl === "time") continue
                                    if (String(form.val([t], "")).length === 0) bad.push(t)
                                }
                                return bad
                            }
                            property bool valid: missing.length === 0 && dataDir.length > 0
                            property string builtPath: {
                                var mapped = []
                                for (var i = 0; i < parts.length; i++) {
                                    var t = String(parts[i]); var tl = t.toLowerCase()
                                    if (tl === "date") mapped.push("YYYY_MM_DD")
                                    else if (tl === "time") mapped.push("HH_MM_SS")
                                    else {
                                        var v = String(form.val([t], ""))
                                        mapped.push(v.length ? v.replace(/ /g, "_") : t + "Missing")
                                    }
                                }
                                var base = dataDir.length ? dataDir : qsTr("‹set a data directory›")
                                return base + "/" + mapped.join("/")
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Recordings will be saved to:")
                                font: Theme.fontSmall
                                color: Theme.textSecondary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: dirPreview.builtPath
                                font: Theme.fontMono
                                color: Theme.textPrimary
                                wrapMode: Text.WrapAnywhere
                            }
                            Text {
                                Layout.fillWidth: true
                                font: Theme.fontSmall
                                wrapMode: Text.WordWrap
                                color: dirPreview.valid ? Theme.success : Theme.warning
                                text: {
                                    if (dirPreview.valid)
                                        return qsTr("✓  Every folder resolves.")
                                    var issues = []
                                    if (dirPreview.dataDir.length === 0)
                                        issues.push(qsTr("no data directory set"))
                                    if (dirPreview.missing.length === 1)
                                        issues.push(qsTr("\"%1\" has no value in this config, so it saves as \"%1Missing\"")
                                                    .arg(dirPreview.missing[0]))
                                    else if (dirPreview.missing.length > 1)
                                        issues.push(qsTr("%1 have no values in this config, so they save as \"…Missing\" folders")
                                                    .arg(dirPreview.missing.join(", ")))
                                    return "⚠  " + issues.join(";  ")
                                }
                            }
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

                        // Actions first: scanning is how you find a device's ID,
                        // so it has to be reachable before the device list, not
                        // buried under it.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacing
                            UiButton {
                                text: qsTr("+ Add Device")
                                primary: true
                                onClicked: form.addDeviceRequested()
                            }
                            UiButton {
                                objectName: "scanDevicesButton"
                                text: qsTr("Scan devices…")
                                onClicked: form.scanDevicesRequested()
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("…lists the cameras connected right now, and the device ID to use for each.")
                                font: Theme.fontSmall
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }

                        Repeater {
                            model: form.deviceRows

                            // One device: summary row + expandable edit drawer.
                            delegate: Rectangle {
                                id: deviceRow
                                required property var modelData

                                readonly property string cat: modelData.category
                                readonly property string name: modelData.name
                                readonly property bool isMiniscope: cat === "miniscopes"
                                readonly property bool codecLossless:
                                    form.losslessCodecs.indexOf(dval("compression", "")) >= 0
                                // Does the catalog give this device type a fine-step
                                // led0 mapping, and is the config using it?
                                readonly property bool hasFineSteps: {
                                    var c = form.ctrl(devType, "led0")
                                    return c !== undefined && c.fineSteps !== undefined
                                }
                                readonly property bool fineSteps: dval("led0FineSteps", false) === true
                                readonly property int led0Max: form.led0Max(devType, fineSteps)
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
                                            model: form.codecModel(deviceRow.dval("compression", ""),
                                                                   deviceRow.cat)
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

                                    // Codec guidance. Miniscope imaging must stay
                                    // lossless, and picking between the two
                                    // lossless codecs is a storage/CPU trade-off
                                    // that depends on the machine; behavior video
                                    // is a natural scene, where a lossy codec is
                                    // the right default.
                                    Text {
                                        objectName: "codecHint"
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        font: Theme.fontSmall
                                        color: deviceRow.isMiniscope && !deviceRow.codecLossless
                                               ? Theme.warning : Theme.textSecondary
                                        text: deviceRow.isMiniscope
                                              ? ((deviceRow.codecLossless ? ""
                                                    : qsTr("⚠ %1 is lossy — Miniscope data should use GREY or FFV1. ")
                                                          .arg(deviceRow.dval("compression", "")))
                                                 + qsTr("GREY is fully uncompressed: no CPU cost, largest files — use it when storage isn't an issue. ")
                                                 + qsTr("FFV1 is lossless too (identical pixels, smaller files) but uses CPU, so some computers struggle and start dropping frames."))
                                              : qsTr("Lossy compression is fine here: these codecs work well on normal video, unlike the fine spatial structure of Miniscope imaging. ")
                                                + qsTr("MJPG is the safe default; XVID gives smaller files. GREY and FFV1 stay lossless but make far larger files than behavior video needs.")
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
                                                to: deviceRow.led0Max
                                                syncValue: deviceRow.dval("led0", 0)
                                                onValueModified: deviceRow.dset("led0", value)
                                            }
                                            UiSwitch {
                                                visible: deviceRow.hasFineSteps
                                                text: qsTr("Fine steps (0–%1)")
                                                          .arg(form.led0Max(deviceRow.devType, true))
                                                syncChecked: deviceRow.fineSteps
                                                onToggled: deviceRow.dset("led0FineSteps", checked)
                                            }
                                        }

                                        // What the fine-steps switch actually does. The
                                        // switch label only has room for the range, and
                                        // the userConfigProps tip is only surfaced on
                                        // FormRow LABELS - this control sits in a row
                                        // slot, so it had no explanation at all.
                                        Text {
                                            objectName: "led0FineStepsHint"
                                            visible: deviceRow.hasFineSteps
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 170 + Theme.spacing
                                            wrapMode: Text.WordWrap
                                            font: Theme.fontSmall
                                            color: Theme.textSecondary
                                            text: qsTr("Fine steps (new in v2.0) address each of the LED driver's %1 hardware steps directly instead of 0–100%, giving %2× finer control over the same brightness range — for preps that need very dim illumination.")
                                                      .arg(form.led0Max(deviceRow.devType, true))
                                                      .arg((form.led0Max(deviceRow.devType, true)
                                                            / form.led0Max(deviceRow.devType, false)).toFixed(2))
                                        }

                                        // The stored led0 number is reinterpreted in the
                                        // new units, so an LED value carried over from a
                                        // config written before v2.0 drives a different
                                        // brightness than it used to.
                                        Text {
                                            objectName: "led0FineStepsUnitsHint"
                                            visible: deviceRow.hasFineSteps && deviceRow.fineSteps
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 170 + Theme.spacing
                                            wrapMode: Text.WordWrap
                                            font: Theme.fontSmall
                                            color: Theme.warning
                                            text: qsTr("Units change with this switch: led0 = %1 is %2% of full LED power on the 0–%3 scale. ")
                                                      .arg(deviceRow.dval("led0", 0))
                                                      .arg((100 * deviceRow.dval("led0", 0)
                                                            / form.led0Max(deviceRow.devType, true)).toFixed(0))
                                                      .arg(form.led0Max(deviceRow.devType, true))
                                                  + qsTr("LED values saved before v2.0 are on the 0–100 scale, where the same number is a percentage — so they run dimmer here until you scale them up.")
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
                        id: traceCard
                        title: qsTr("Trace display")
                        subtitle: qsTr("Plots head orientation, fluorescence ROIs, and pose traces in a scrolling window during acquisition.")
                        showSwitch: true
                        switchChecked: form.val(["traceDisplay", "enabled"], false)
                        onSwitchToggled: checked => form.set(["traceDisplay", "enabled"], checked)

                        // Mirrors the backend gating: traces are only fed by
                        // miniscopes or the behavior tracker, so the window is
                        // suppressed for configs with neither (webcam-only).
                        readonly property bool hasTraceSource: {
                            var ms = form.val(["devices", "miniscopes"], null)
                            if (ms !== null && Object.keys(ms).length > 0)
                                return true
                            var bt = form.val(["behaviorTracker"], null)
                            var cams = form.val(["devices", "cameras"], null)
                            return bt !== null && Object.keys(bt).length > 0
                                   && bt.enabled !== false
                                   && cams !== null && Object.keys(cams).length > 0
                        }
                        Text {
                            objectName: "traceSourceHint"
                            visible: traceCard.switchChecked === true
                                     && !traceCard.hasTraceSource
                            Layout.fillWidth: true
                            text: qsTr("This configuration has no trace sources, so the trace window will not open. Traces come from Miniscopes (head orientation, fluorescence ROIs) or the behavior tracker.")
                            font: Theme.fontSmall
                            color: Theme.warning
                            wrapMode: Text.WordWrap
                        }
                    }

                    // --- Commutator ---
                    FormCard {
                        objectName: "commutatorCard"
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

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Behavior tracking and record-start/stop programs are in the Advanced tab.")
                        font: Theme.fontSmall
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // =================== Advanced page ===================
            // Settings almost no run needs. They stay out of the Form page so
            // that page reads as "what this experiment records"; nothing here
            // is required for a normal Miniscope + camera session.
            ScrollView {
                id: advancedScroll
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: advancedScroll.availableWidth
                    spacing: Theme.spacing

                    // --- Behavior tracker ---
                    FormCard {
                        title: qsTr("Behavior tracker")
                        subtitle: qsTr("Live pose tracking with DeepLabCut-Live. Requires a prepared Python environment.")
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

                    // --- External programs ---
                    FormCard {
                        title: qsTr("External programs")
                        subtitle: qsTr("Run an external program when recording starts or stops (e.g. to sync third-party hardware).")

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
