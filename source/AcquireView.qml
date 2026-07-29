import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Miniscope.Theme 1.0

// Acquire mode: the session's windows (device streams, control panel, trace
// display) embedded as panes the operator can arrange, with pop-out to a
// floating window. Closing a floating pane docks it back — streams never stop
// from window management.
//
// The grid is rows of panes, nested SplitViews: drag a horizontal divider to
// change a row's height, a vertical one to change a pane's width, and drag a
// pane's header onto another pane to swap their places. "Columns" picks how
// many panes per row (Auto = 1/2/3 by device count) and "Reset" returns to an
// even automatic grid. Everything — the arrangement, the divider positions,
// which panes float and where — persists per config file (backend.paneLayout /
// savePaneLayout), so a rig's operator gets their setup back on every run, and
// Lock layout freezes the whole lot.
Item {
    id: acquireRoot
    objectName: "acquireView" // main.cpp's dev hooks drive setFloating via this

    readonly property var panes: backend ? backend.sessionPanes : []
    // Per-pane UI state keyed by pane name ({floating: bool}); reassigned
    // wholesale so bindings re-evaluate.
    property var paneStates: ({})
    property bool layoutLocked: false

    // The arrangement: one array of pane names per row, e.g.
    // [["Miniscope", "BehaviorCam"], ["Traces"]].
    property var rows: []
    // Panes per row. 0 = automatic (by pane count), otherwise the user's pick.
    property int columnCount: 0

    // True while a pane header is being dragged, so the drop targets light up.
    readonly property bool dragActive: paneGhost.Drag.active

    // Stacking order of the overlapping drop targets. Their RELATIVE order is
    // the contract, so they are declared together rather than as literals at
    // each site: a divider beats the pane beneath it, a side edge beats the
    // divider, a top/bottom edge beats a side edge (so a corner reads as "new
    // row"), and the drag ghost floats above the lot.
    readonly property int zHandle: 20
    readonly property int zEdge: 30
    readonly property int zCorner: 31
    readonly property int zGhost: 999

    // Keep the drag ghost centred on the cursor. Its own hotSpot is its centre,
    // so the drop point the DropAreas see is exactly the cursor.
    function moveGhost(pointInView) {
        paneGhost.x = pointInView.x - paneGhost.width / 2
        paneGhost.y = pointInView.y - paneGhost.height / 2
    }

    function isFloating(name) {
        var st = paneStates[name]
        return st !== undefined && st.floating === true
    }
    function paneByName(name) {
        for (var i = 0; i < panes.length; i++)
            if (panes[i].name === name)
                return panes[i]
        return undefined
    }

    // --- Arrangement ----------------------------------------------------------
    function effectiveColumns() {
        if (columnCount > 0)
            return columnCount
        return panes.length <= 1 ? 1 : panes.length <= 4 ? 2 : 3
    }

    // Chunk a flat list of pane names into rows of effectiveColumns().
    function rowsFromOrder(order) {
        var cols = effectiveColumns()
        var out = [], row = []
        for (var i = 0; i < order.length; i++) {
            row.push(order[i])
            if (row.length === cols) {
                out.push(row)
                row = []
            }
        }
        if (row.length > 0)
            out.push(row)
        return out
    }
    function paneOrder() {
        var out = []
        for (var r = 0; r < rows.length; r++)
            for (var c = 0; c < rows[r].length; c++)
                out.push(rows[r][c])
        return out
    }

    // Accept a stored arrangement only for the panes this session actually has:
    // a config edited between runs (device added, removed, renamed) must not
    // leave a hole in the grid or drop a pane out of it entirely.
    function rowsForPanes(stored) {
        var live = []
        for (var i = 0; i < panes.length; i++)
            live.push(panes[i].name)
        if (!stored || stored.length === 0)
            return rowsFromOrder(live)

        var out = [], seen = {}
        for (var r = 0; r < stored.length; r++) {
            var row = []
            for (var c = 0; c < stored[r].length; c++) {
                var name = stored[r][c]
                if (live.indexOf(name) >= 0 && seen[name] !== true) {
                    row.push(name)
                    seen[name] = true
                }
            }
            if (row.length > 0)
                out.push(row)
        }
        // Panes the stored layout never heard of go in a row of their own.
        var extra = live.filter(function (n) { return seen[n] !== true })
        if (extra.length > 0)
            out = out.concat(rowsFromOrder(extra))
        return out.length > 0 ? out : rowsFromOrder(live)
    }

    // Swap two panes' places. Swapping (rather than inserting) keeps the row
    // shape the operator chose, so the divider positions stay meaningful.
    function swapPanes(a, b) {
        if (a === b || a.length === 0 || b.length === 0)
            return
        var next = []
        for (var r = 0; r < rows.length; r++) {
            var row = []
            for (var c = 0; c < rows[r].length; c++) {
                var name = rows[r][c]
                row.push(name === a ? b : name === b ? a : name)
            }
            next.push(row)
        }
        rows = next
        saveArrangement()
    }

    // Which gap a divider sits in: the number of items ahead of it. Handles
    // carry no index of their own, but SplitView has already positioned both
    // them and the items, so their coordinates say it.
    function gapIndexAt(split, pos, vertical) {
        var n = 0
        for (var i = 0; i < split.count; i++) {
            var item = split.itemAt(i)
            if (item && (vertical ? item.y : item.x) < pos)
                n++
        }
        return n
    }

    // Drop on a divider: put the pane in that slot rather than trading places
    // with whatever is there. `position` is a gap index within row `row`, or -
    // with asNewRow - `row` is where a new one-pane row goes.
    function movePaneTo(name, row, position, asNewRow) {
        if (!name || name.length === 0)
            return
        var next = []
        for (var r = 0; r < rows.length; r++)
            next.push(rows[r].slice())

        var fromRow = -1, fromCol = -1
        for (r = 0; r < next.length; r++) {
            var c = next[r].indexOf(name)
            if (c >= 0) {
                fromRow = r
                fromCol = c
                break
            }
        }
        if (fromRow < 0)
            return

        // Dropping onto a divider it already borders changes nothing.
        if (asNewRow) {
            if (next[fromRow].length === 1 && (row === fromRow || row === fromRow + 1))
                return
        } else if (fromRow === row && (position === fromCol || position === fromCol + 1)) {
            return
        }

        next[fromRow].splice(fromCol, 1)
        // Taking it out shifts everything after it in that row...
        if (!asNewRow && fromRow === row && fromCol < position)
            position--
        // ...and empties the row entirely if it was the only pane in it.
        var vacated = false
        if (next[fromRow].length === 0) {
            next.splice(fromRow, 1)
            vacated = true
            if (row > fromRow)
                row--
        }

        if (asNewRow || (vacated && fromRow === row))
            next.splice(Math.max(0, Math.min(row, next.length)), 0, [name])
        else if (row < 0 || row >= next.length)
            next.push([name])
        else
            next[row].splice(Math.max(0, Math.min(position, next[row].length)), 0, name)

        rows = next
        // Panes per row changed, so the stored divider positions describe a
        // different grid.
        saveArrangement(true)
    }

    function setColumns(cols) {
        columnCount = cols
        rows = rowsFromOrder(paneOrder())
        saveArrangement(true)
    }

    // Auto columns (0) and an even grid is exactly what setColumns(0) produces.
    function resetLayout() {
        setColumns(0)
    }

    // --- Persistence ----------------------------------------------------------
    // Pass resetSplits when the ROW SHAPE changed: the stored divider positions
    // then describe a different grid and must go. The two are written together
    // in one settings batch so a caller cannot save a new shape while leaving
    // stale divider positions behind it.
    function saveArrangement(resetSplits) {
        if (!backend || panes.length === 0)
            return
        var state = { locked: layoutLocked,
                      columns: columnCount,
                      rows: JSON.stringify(rows) }
        if (resetSplits === true) {
            state.split = ""
            state.rowSplits = ""
        }
        backend.savePaneLayout("__layout", state)
    }

    // SplitView.saveState() captures the preferred sizes its handles wrote, for
    // the outer (row heights) and each row (pane widths).
    function saveSplitState() {
        if (!backend || panes.length === 0)
            return
        var rowStates = []
        for (var i = 0; i < rowRepeater.count; i++) {
            var row = rowRepeater.itemAt(i)
            rowStates.push(row ? row.saveState() : null)
        }
        backend.savePaneLayout("__layout", {
            split: JSON.stringify(outerSplit.saveState()),
            rowSplits: JSON.stringify(rowStates) })
    }
    function restoreSplitState() {
        if (!backend || panes.length === 0)
            return
        var meta = backend.paneLayout("__layout")
        // Stored state that no longer parses (or was written by an older
        // version) just means the grid opens evenly sized.
        try {
            if (meta.split !== undefined && String(meta.split).length > 0)
                outerSplit.restoreState(JSON.parse(meta.split))
            if (meta.rowSplits !== undefined && String(meta.rowSplits).length > 0) {
                var states = JSON.parse(meta.rowSplits)
                for (var i = 0; i < states.length && i < rowRepeater.count; i++) {
                    var row = rowRepeater.itemAt(i)
                    if (row && states[i])
                        row.restoreState(states[i])
                }
            }
        } catch (e) {
            // Malformed stored layout: keep the even default.
        }
    }

    // Apply the saved layout when a session's panes appear (and reset state
    // when they clear at session end). Also on creation, in case the session
    // was already running when this view instantiated.
    onPanesChanged: restoreLayout()
    Component.onCompleted: restoreLayout()
    function restoreLayout() {
        var st = {}
        if (panes.length > 0) {
            var meta = backend.paneLayout("__layout")
            layoutLocked = meta.locked === true || meta.locked === "true"
            columnCount = meta.columns !== undefined ? Number(meta.columns) : 0
            var stored = null
            try {
                if (meta.rows !== undefined && String(meta.rows).length > 0)
                    stored = JSON.parse(meta.rows)
            } catch (e) {
                stored = null
            }
            rows = rowsForPanes(stored)
        } else {
            rows = []
        }
        for (var i = 0; i < panes.length; i++) {
            var p = panes[i]
            var saved = backend.paneLayout(p.name)
            var floating = saved.floating === true || saved.floating === "true"
            st[p.name] = { floating: floating }
            if (floating) {
                backend.setPaneEmbedded(p.window, false, p.aspect)
                if (saved.width !== undefined) {
                    p.window.x = Number(saved.x)
                    p.window.y = Number(saved.y)
                    p.window.width = Number(saved.width)
                    p.window.height = Number(saved.height)
                }
            } else {
                // Clear the window's own aspect lock / minimum size so it
                // follows its container; the pane letterboxes instead.
                backend.setPaneEmbedded(p.window, true, p.aspect)
            }
        }
        paneStates = st
        // The SplitViews' children only exist after this rows assignment has
        // been processed.
        Qt.callLater(restoreSplitState)
    }

    function setFloating(pane, floating) {
        // Build a FRESH object: reassigning the same JS object reference back
        // to a var property emits no change signal, so no binding updates -
        // the WindowContainer then never releases/reattaches the window and
        // keeps overriding its state (the pop-out-snaps-back-on-drag bug).
        var st = {}
        for (var name in paneStates)
            st[name] = paneStates[name]
        st[pane.name] = { floating: floating }
        paneStates = st // containers attach/detach via their window binding
        backend.setPaneEmbedded(pane.window, !floating, pane.aspect)
        if (!floating)
            pane.window.visible = true // it may have been closed while floating
        saveFloatingState(pane)
    }

    function saveFloatingState(pane) {
        backend.savePaneLayout(pane.name, isFloating(pane.name)
            ? { floating: true, x: pane.window.x, y: pane.window.y,
                width: pane.window.width, height: pane.window.height }
            : { floating: false })
    }

    // --- Reusable drop targets --------------------------------------------------
    // A pane-edge drop zone: an invisible catch area that tints itself while a
    // dragged pane hovers it. The four edges differ only in where they sit and
    // what they do on drop, so the highlight is defined once here.
    //
    // Use `available` rather than `enabled` to gate a zone: the lock check then
    // cannot be dropped by a call site that only meant to add a condition.
    component EdgeDropZone: DropArea {
        id: edgeZone
        property bool available: true
        enabled: available && !acquireRoot.layoutLocked
        Rectangle {
            anchors.fill: parent
            color: Theme.accent
            opacity: edgeZone.containsDrag ? 0.55 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.animMs } }
        }
    }

    // A SplitView divider that doubles as a drop target: dropping a pane on it
    // inserts the pane at that slot rather than trading places with a
    // neighbour. Shared by the row dividers (outer, vertical) and the pane
    // dividers within a row, which previously carried two copies of this.
    component GridDivider: Rectangle {
        id: divider
        signal paneDropped(string paneName)

        // A locked layout collapses the handle to nothing, so there is nothing
        // to grab. (Disabling the SplitView instead would disable every control
        // inside the panes along with it.)
        implicitWidth: acquireRoot.layoutLocked ? 0 : Theme.spacing
        implicitHeight: acquireRoot.layoutLocked ? 0 : Theme.spacing
        color: dividerDrop.containsDrag ? Theme.accent
             : divider.SplitHandle.pressed ? Theme.accent
             : divider.SplitHandle.hovered ? Theme.accentHover
                                           : "transparent"
        radius: Theme.radiusSmall
        // Above the panes so this divider's drop zone wins over theirs.
        z: acquireRoot.zHandle

        // The negative margins widen the catch area past the thin visible
        // divider, which is only a few pixels wide.
        DropArea {
            id: dividerDrop
            anchors.fill: parent
            anchors.margins: -6
            enabled: !acquireRoot.layoutLocked
            onDropped: drop => divider.paneDropped(drop.source.paneName)
        }
    }

    // The dragged pane's stand-in. It lives at the top of this view rather than
    // inside the pane being dragged, because a pane clips its own content (the
    // embedded video window must not bleed out of its cell) - a ghost parented
    // there would vanish the moment it left the cell.
    Rectangle {
        id: paneGhost
        property string paneName: ""
        // Drag.active is driven imperatively by the header drags below, NOT by a
        // binding: releasing has to call Drag.drop() to deliver the drop event,
        // and setting Drag.active back to false CANCELS the drag instead (which
        // is why a bound version dragged fine and then dropped nowhere).
        visible: Drag.active
        opacity: 0.75
        z: acquireRoot.zGhost
        width: 150
        height: 26
        radius: Theme.radiusSmall
        color: Theme.accent
        Drag.source: paneGhost
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        Text {
            anchors.centerIn: parent
            text: paneGhost.paneName
            font: Theme.fontSmall
            color: Theme.accentText
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.padding
        spacing: Theme.spacing

        // --- Session bar: transport, telemetry, grid controls, End Session ------
        SessionBar {
            Layout.fillWidth: true
            layoutLocked: acquireRoot.layoutLocked
            gridColumns: acquireRoot.columnCount
            onLockToggled: locked => {
                acquireRoot.layoutLocked = locked
                acquireRoot.saveArrangement()
            }
            onGridColumnsPicked: cols => acquireRoot.setColumns(cols)
            onResetLayoutRequested: acquireRoot.resetLayout()
        }

        // --- Pane grid: rows of resizable panes ---------------------------------
        SplitView {
            id: outerSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            // Dropping a pane between two rows puts it in a row of its own
            // right here.
            handle: GridDivider {
                id: rowHandle
                onPaneDropped: paneName => acquireRoot.movePaneTo(
                                   paneName,
                                   acquireRoot.gapIndexAt(outerSplit, rowHandle.y, true),
                                   0, true)
            }
            onResizingChanged: if (!resizing) acquireRoot.saveSplitState()

            Repeater {
                id: rowRepeater
                model: acquireRoot.rows

                delegate: SplitView {
                    id: rowSplit
                    required property var modelData   // this row's pane names
                    required property int index
                    readonly property int paneCount: modelData.length

                    orientation: Qt.Horizontal
                    // The last row takes the slack; the others start with an
                    // even share. SplitView writes preferredHeight when a
                    // handle is dragged, which replaces this binding - so a
                    // row the operator sized keeps that size.
                    SplitView.fillHeight: index === acquireRoot.rows.length - 1
                    SplitView.preferredHeight: outerSplit.height
                                               / Math.max(1, acquireRoot.rows.length)
                    SplitView.minimumHeight: 80
                    // Dropping a pane on a divider inside a row inserts it at
                    // that spot instead of swapping with a neighbour.
                    handle: GridDivider {
                        id: paneHandle
                        onPaneDropped: paneName => acquireRoot.movePaneTo(
                                           paneName, rowSplit.index,
                                           acquireRoot.gapIndexAt(rowSplit, paneHandle.x, false),
                                           false)
                    }
                    onResizingChanged: if (!resizing) acquireRoot.saveSplitState()

                    Repeater {
                        model: rowSplit.modelData

                        delegate: Rectangle {
                            id: paneFrame
                            required property var modelData   // the pane's name
                            required property int index

                            // Stable handles for the pane-grid test (and the
                            // dev hooks in main.cpp), which drive a real header
                            // drag through the window.
                            objectName: "paneFrame"

                            readonly property var pane: acquireRoot.paneByName(modelData)
                            readonly property bool floating:
                                acquireRoot.isFloating(modelData)
                            // The header is the drag grip AND the swap target, so
                            // the edge drop zones below start beneath it.
                            readonly property int headerHeight: 26

                            // A floating pane keeps its grid cell as a placeholder
                            // with an explicit Dock button. Window signals (closing /
                            // visibleChanged) must NOT drive dock state: macOS
                            // synthesizes both while re-establishing a dragged native
                            // window that was just reparented out of a container, so
                            // any signal-based dock trigger yanks the window back the
                            // moment the user drags it.
                            SplitView.fillWidth: index === rowSplit.paneCount - 1
                            SplitView.preferredWidth: rowSplit.width
                                                      / Math.max(1, rowSplit.paneCount)
                            SplitView.minimumWidth: 120
                            radius: Theme.radiusSmall
                            color: Theme.surface
                            border.width: 1
                            border.color: dropTarget.containsDrag ? Theme.accent : Theme.border
                            clip: true

                            // Geometry changes while floating are saved, debounced.
                            Connections {
                                target: paneFrame.pane ? paneFrame.pane.window : null
                                enabled: paneFrame.floating
                                function onXChanged() { saveTimer.restart() }
                                function onYChanged() { saveTimer.restart() }
                                function onWidthChanged() { saveTimer.restart() }
                                function onHeightChanged() { saveTimer.restart() }
                            }
                            Timer {
                                id: saveTimer
                                interval: 400
                                onTriggered: if (paneFrame.floating && paneFrame.pane)
                                                 acquireRoot.saveFloatingState(paneFrame.pane)
                            }

                            // Drop the dragged pane here: the two trade places.
                            DropArea {
                                id: dropTarget
                                anchors.fill: parent
                                enabled: !acquireRoot.layoutLocked
                                onDropped: drop => acquireRoot.swapPanes(drop.source.paneName,
                                                                        paneFrame.modelData)
                            }

                            // The row's outer edges. Every boundary between two
                            // panes is a divider with its own drop zone, but the
                            // first pane's left edge and the last pane's right edge
                            // have no divider - without these two zones a pane
                            // could not be dropped at either END of a row. Both sit
                            // above the swap zone (z) so an edge beats the middle.
                            EdgeDropZone {
                                width: 18
                                z: acquireRoot.zEdge
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.topMargin: paneFrame.headerHeight
                                anchors.bottom: parent.bottom
                                available: paneFrame.index === 0
                                onDropped: drop => acquireRoot.movePaneTo(
                                               drop.source.paneName, rowSplit.index, 0, false)
                            }
                            EdgeDropZone {
                                width: 18
                                z: acquireRoot.zEdge
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: paneFrame.headerHeight
                                anchors.bottom: parent.bottom
                                available: paneFrame.index === rowSplit.paneCount - 1
                                onDropped: drop => acquireRoot.movePaneTo(
                                               drop.source.paneName, rowSplit.index,
                                               rowSplit.paneCount, false)
                            }

                            // Top and bottom edges make a NEW row above or below
                            // this one. Without them the only way from one row to
                            // two was the Columns control, since a single-row grid
                            // has no divider between rows to drop on. Declared
                            // after the side zones so a corner reads as top/bottom.
                            EdgeDropZone {
                                height: 18
                                z: acquireRoot.zCorner
                                anchors.top: parent.top
                                anchors.topMargin: paneFrame.headerHeight
                                anchors.left: parent.left
                                anchors.right: parent.right
                                onDropped: drop => acquireRoot.movePaneTo(
                                               drop.source.paneName, rowSplit.index, 0, true)
                            }
                            EdgeDropZone {
                                height: 18
                                z: acquireRoot.zCorner
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                onDropped: drop => acquireRoot.movePaneTo(
                                               drop.source.paneName, rowSplit.index + 1, 0, true)
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 1
                                spacing: 0

                                // Pane header: grip + name + pop-out. Dragging it
                                // moves the pane within the grid.
                                Rectangle {
                                    id: paneHeader
                                    objectName: "paneHeader"
                                    Layout.fillWidth: true
                                    implicitHeight: paneFrame.headerHeight
                                    color: dragHandleArea.containsMouse || dropTarget.containsDrag
                                           ? Theme.surface : Theme.surfaceAlt
                                    radius: Theme.radiusSmall

                                    // Header drag: moves the ghost by hand and
                                    // drops it explicitly. MouseArea's own
                                    // drag.target was no use here - it moves the
                                    // target relative to where it already sat, not
                                    // to the cursor, and it has no way to end a
                                    // drag with a drop.
                                    MouseArea {
                                        id: dragHandleArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: !acquireRoot.layoutLocked
                                        cursorShape: enabled
                                                     ? (acquireRoot.dragActive
                                                        ? Qt.ClosedHandCursor
                                                        : Qt.OpenHandCursor)
                                                     : Qt.ArrowCursor

                                        property point pressPoint

                                        onPressed: mouse => {
                                            pressPoint = mapToItem(acquireRoot, mouse.x, mouse.y)
                                            paneGhost.paneName = paneFrame.modelData
                                            acquireRoot.moveGhost(pressPoint)
                                        }
                                        onPositionChanged: mouse => {
                                            if (!pressed)
                                                return
                                            var p = mapToItem(acquireRoot, mouse.x, mouse.y)
                                            // Don't turn a plain click on the header
                                            // into a drag.
                                            if (!paneGhost.Drag.active) {
                                                if (Math.abs(p.x - pressPoint.x) < 8
                                                        && Math.abs(p.y - pressPoint.y) < 8)
                                                    return
                                                paneGhost.Drag.active = true
                                            }
                                            acquireRoot.moveGhost(p)
                                        }
                                        onReleased: {
                                            if (paneGhost.Drag.active)
                                                paneGhost.Drag.drop() // -> DropArea.onDropped
                                        }
                                        onCanceled: paneGhost.Drag.active = false

                                        ToolTip.visible: containsMouse && !acquireRoot.dragActive
                                        ToolTip.delay: 800
                                        ToolTip.text: qsTr("Drag onto a pane to swap places, onto a "
                                                           + "divider or side edge to drop it there, "
                                                           + "or onto a top/bottom edge for a new row")
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.spacing
                                        anchors.rightMargin: 2
                                        spacing: Theme.spacing

                                        Text {
                                            visible: !acquireRoot.layoutLocked
                                            text: "⠿"
                                            font: Theme.fontSmall
                                            color: Theme.textSecondary
                                        }

                                        Text {
                                            text: paneFrame.modelData
                                            font: Theme.fontSmall
                                            color: Theme.textPrimary
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        // Pop out / dock back
                                        Rectangle {
                                            visible: !acquireRoot.layoutLocked
                                            width: paneButtonText.implicitWidth + 16
                                            height: 22
                                            radius: Theme.radiusSmall
                                            color: popOutArea.containsMouse ? Theme.surfaceAlt
                                                                            : "transparent"
                                            Text {
                                                id: paneButtonText
                                                anchors.centerIn: parent
                                                text: paneFrame.floating ? qsTr("⇲ Dock") : "↗"
                                                font: Theme.fontSmall
                                                color: Theme.textSecondary
                                            }
                                            MouseArea {
                                                id: popOutArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: if (paneFrame.pane)
                                                               acquireRoot.setFloating(
                                                                   paneFrame.pane,
                                                                   !paneFrame.floating)
                                            }
                                            ToolTip.visible: popOutArea.containsMouse
                                            ToolTip.delay: 600
                                            ToolTip.text: paneFrame.floating
                                                          ? qsTr("Bring the window back into this pane")
                                                          : qsTr("Pop out into a floating window")
                                        }
                                    }
                                }

                                // Pane body: the device/panel window, letterboxed to
                                // its native aspect for video panes - or, while the
                                // window floats, a placeholder with the dock controls.
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true

                                    WindowContainer {
                                        window: (paneFrame.floating || !paneFrame.pane)
                                                ? null : paneFrame.pane.window
                                        visible: !paneFrame.floating
                                        anchors.centerIn: parent
                                        readonly property double aspect:
                                            paneFrame.pane ? paneFrame.pane.aspect : 0
                                        width: aspect > 0
                                               ? Math.min(parent.width, parent.height * aspect)
                                               : parent.width
                                        height: aspect > 0 ? width / aspect : parent.height
                                    }

                                    ColumnLayout {
                                        visible: paneFrame.floating
                                        anchors.centerIn: parent
                                        spacing: Theme.spacing

                                        Text {
                                            text: qsTr("Floating in its own window")
                                            font: Theme.fontBody
                                            color: Theme.textSecondary
                                            Layout.alignment: Qt.AlignHCenter
                                        }
                                        RowLayout {
                                            Layout.alignment: Qt.AlignHCenter
                                            spacing: Theme.spacing
                                            UiButton {
                                                text: qsTr("Show window")
                                                onClicked: {
                                                    paneFrame.pane.window.visible = true
                                                    paneFrame.pane.window.requestActivate()
                                                }
                                            }
                                            UiButton {
                                                text: qsTr("Dock here")
                                                primary: true
                                                enabled: !acquireRoot.layoutLocked
                                                onClicked: acquireRoot.setFloating(
                                                               paneFrame.pane, false)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
