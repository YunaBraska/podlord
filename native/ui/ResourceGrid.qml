import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Podlord.Graphics 1.0

ColumnLayout {
    id: grid
    Layout.minimumHeight: implicitHeight
    required property var tableModel
    required property int sortColumn
    required property string sortDirection
    property string prefix: "resource"
    required property string emptyText
    property string emptyObjectName: prefix === "resource" ? "emptyResources" : prefix === "event" ? "emptyEvents" : prefix + "Empty"
    property var columns: prefix === "resource" ? workspace.resourceColumns : workspace.eventColumns
    property var visualOrder: []
    property bool inspectable: true
    property int sortableColumns: -1
    property int rowHeight: 32
    property bool wrapCells: false
    property var actionColumns: []
    property var accessoryColumns: []
    property Component cellAccessory: null
    property var cellName: null
    property alias contentY: table.contentY
    readonly property real contentHeight: table.contentHeight
    readonly property real viewportHeight: table.height
    readonly property int count: table.rows
    readonly property int navigationIndex: selection.currentIndex.valid && selection.currentIndex.row >= 0 && selection.currentIndex.row < count ? selection.currentIndex.row : -1
    function navigateRows(offset) {
        if (count === 0) return
        const row = offset === 0 ? 0 : ((navigationIndex < 0 ? 0 : navigationIndex) + offset + count) % count
        const column = columns.find(column => column.visible).column
        selection.setCurrentIndex(tableModel.index(row, column), ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        if (visible) table.positionViewAtRow(row, TableView.Contain)
    }
    readonly property string contextScope: workspace.currentSession + (prefix === "value" ? "\n" + workspace.inspectorScope : "")
    onContextScopeChanged: { cellMenu.close(); valueTip.target = null; if (selection) selection.clear() }
    signal actionRequested(string identity, int column, string label)
    signal currentRequested(string identity)
    function focusCell(row, column, owner) {
        const index = tableModel.index(row, column)
        selection.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
        owner.positionViewAtIndex(index, TableView.Contain)
    }
    function selectIdentity(identity, column) {
        const current = selection.currentIndex
        if (current.valid && pathAt(current.row) === identity) column = current.column
        for (let row = 0; row < tableModel.rowCount(); ++row) {
            if (pathAt(row) !== identity) continue
            selection.setCurrentIndex(tableModel.index(row, column), ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
            if (visible) {
                table.forceLayout()
                table.positionViewAtIndex(selection.currentIndex, TableView.Contain)
            }
            return true
        }
        selection.clear()
        return false
    }
    function activateCell(row, column) {
        if (actionColumns.indexOf(column) >= 0) {
            const label = tableModel.data(tableModel.index(row, column), Qt.DisplayRole)
            if (label !== "") actionRequested(pathAt(row), column, label)
        } else if (inspectable) inspectRequested(endpointAt(row, column))
    }
    function handleCellKey(event, row, column, anchor) {
        const current = selection.currentIndex
        if (current.valid) { row = current.row; column = current.column }
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            const target = event.key === Qt.Key_Home ? 0 : table.rows - 1
            if (target >= 0) {
                selection.setCurrentIndex(tableModel.index(target, column), ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                table.positionViewAtRow(target, TableView.Contain)
                table.forceActiveFocus(Qt.TabFocusReason)
            }
        } else if (event.matches(StandardKey.Copy)) grid.copyRequested(row, column)
        else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
            cellMenu.path = grid.pathAt(row); cellMenu.endpoint = grid.endpointAt(row, column); cellMenu.column = column
            cellMenu.popup(anchor, 0, anchor.height)
        } else return false
        return true
    }
    function revealFocusedCell() {
        let control = grid.Window.window ? grid.Window.window.activeFocusItem : null
        while (control) {
            if (control === table || control === pinnedTable) {
                if (selection.currentIndex.valid) control.positionViewAtIndex(selection.currentIndex, TableView.Contain)
                return
            }
            control = control.parent
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            const column = selection.currentIndex.valid ? selection.currentIndex.column : columns.find(column => column.visible).column
            event.accepted = handleCellKey(event, 0, column, grid)
        } else if (selection.currentIndex.valid) {
            event.accepted = handleCellKey(event, selection.currentIndex.row, selection.currentIndex.column, grid.Window.window.activeFocusItem)
        }
    }
    property bool findOpen: false
    property bool findShortcutEnabled: true
    readonly property bool containsActiveFocus: {
        let target = grid.Window.window ? grid.Window.window.activeFocusItem : null
        while (target) { if (target === grid) return true; target = target.parent }
        return false
    }
    property int findCount: 0
    property int findIndex: -1
    property bool findValid: true
    function updateFindCount(reset) {
        findCount = findValid && findInput.text.trim() !== "" ? findMatches.rowCount() : 0
        findIndex = findCount === 0 ? -1 : reset ? 0 : Math.max(0, Math.min(findIndex, findCount - 1))
    }
    function selectFindMatch(offset) {
        if (findCount === 0) return
        findIndex = (findIndex + offset + findCount) % findCount
        const source = findMatches.mapToSource(findMatches.index(findIndex, 0))
        selection.setCurrentIndex(source, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        table.positionViewAtRow(source.row, TableView.Contain)
    }
    function openFind() { findOpen = true; findInput.forceActiveFocus(); findInput.selectAll() }
    readonly property real pinnedWidth: columns.filter(function(column) { return column.visible && column.pinned }).reduce(function(width, column) { return width + column.width }, 0)
    readonly property bool hasScrollingColumns: columns.some(function(column) { return column.visible && !column.pinned })
    signal sortRequested(int column)
    signal inspectRequested(string path)
    signal copyRequested(int row, int column)
    signal copyPathRequested(string path, int column)
    signal contextRequested(string path)
    property list<Action> contextActions
    function pathAt(row) { return tableModel.data(tableModel.index(row, 0), Qt.UserRole) || "" }
    function endpointAt(row, column) { return tableModel.data(tableModel.index(row, column), Qt.UserRole + 12) || pathAt(row) }
    function stateAt(visual) {
        const logical = visualOrder.length > visual ? visualOrder[visual] : visual
        return columns.find(function(column) { return column.column === logical })
    }
    function synchronizeColumns() {
        if (!visible || table.columns !== columns.length) return
        const wanted = columns.filter(function(column) { return column.visible && column.pinned }).concat(columns.filter(function(column) { return !column.visible || !column.pinned })).map(function(column) { return column.column })
        if (table.rows > 0 && wanted.every(function(column, position) { return table.columnAtIndex(tableModel.index(0, column)) === position })) {
            visualOrder = wanted
            table.forceLayout()
            pinnedTable.forceLayout()
            return
        }
        const current = columns.map(function(column) { return column.column }).sort(function(a, b) { return a - b })
        table.clearColumnReordering()
        visualOrder = current.slice()
        for (let position = 0; position < wanted.length; ++position) {
            const from = current.indexOf(wanted[position])
            if (from === position) continue
            current.splice(position, 0, current.splice(from, 1)[0])
            visualOrder = current.slice()
            table.moveColumn(from, position)
        }
        table.forceLayout()
        pinnedTable.forceLayout()
    }
    onColumnsChanged: Qt.callLater(synchronizeColumns)
    onVisibleChanged: {
        if (visible) Qt.callLater(synchronizeColumns)
        else { cellMenu.close(); valueTip.target = null }
    }
    Component.onCompleted: synchronizeColumns()
    spacing: 0
    ItemSelectionModel {
        id: selection; model: grid.tableModel
        onCurrentChanged: (current, previous) => { if (current.valid) grid.currentRequested(grid.pathAt(current.row)) }
    }
    ResourceFindFilter { id: findMatches; sourceModel: grid.findOpen && grid.visible ? grid.tableModel : null }
    Connections {
        target: grid.visible ? grid.tableModel : null
        // Column reordering also seeds Qt's row map; membership changes must not retain it.
        function onRowsInserted() { table.clearRowReordering() }
        function onRowsRemoved() { table.clearRowReordering() }
    }
    Connections {
        target: findMatches
        function onRowsInserted() { Qt.callLater(grid.updateFindCount, false) }
        function onRowsRemoved() { Qt.callLater(grid.updateFindCount, false) }
        function onModelReset() { Qt.callLater(grid.updateFindCount, false) }
        function onLayoutChanged() { Qt.callLater(grid.updateFindCount, false) }
    }
    Shortcut { sequences: [StandardKey.Find]; enabled: grid.visible && grid.findShortcutEnabled; onActivated: grid.openFind() }
    TableColumnsDialog { id: columnsDialog; tableType: grid.prefix; columns: grid.columns }
    PlainToolTip { id: valueTip; objectName: grid.prefix + "ValueTooltip" }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        IconButton { objectName: grid.prefix + "FindButton"; glyph: "Search"; text: workspace.uiText["nav.search"]; Accessible.name: "Find within cached " + grid.prefix + " rows"; onClicked: grid.openFind() }
        IconButton { objectName: grid.prefix + "ColumnsButton"; glyph: "Columns"; text: "Columns"; Accessible.name: "Columns for " + grid.prefix + " table"; onClicked: columnsDialog.open() }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: grid.findOpen
        TextField {
            id: findInput
            objectName: grid.prefix + "FindInput"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            placeholderText: "Find in this table; does not change filters"
            Accessible.name: "Find within cached " + grid.prefix + " rows"
            onTextChanged: { grid.findValid = findMatches.filter(text); grid.updateFindCount(true) }
            onAccepted: grid.selectFindMatch(1)
            Keys.onEscapePressed: { grid.findOpen = false; table.forceActiveFocus() }
        }
        Label { objectName: grid.prefix + "FindCount"; text: (grid.findIndex + 1) + "/" + grid.findCount; textFormat: Text.PlainText; Accessible.name: "Search match " + text }
        IconButton { objectName: grid.prefix + "FindPrevious"; glyph: "Previous"; text: workspace.uiText["tooltip.previousMatch"]; enabled: grid.findCount > 0; onClicked: grid.selectFindMatch(-1) }
        IconButton { objectName: grid.prefix + "FindNext"; glyph: "Next"; text: workspace.uiText["tooltip.nextMatch"]; enabled: grid.findCount > 0; onClicked: grid.selectFindMatch(1) }
        IconButton { objectName: grid.prefix + "FindClose"; glyph: "Close"; text: workspace.uiText["tooltip.closeSearch"]; onClicked: { grid.findOpen = false; table.forceActiveFocus() } }
    }
    Label { objectName: grid.prefix + "FindError"; Layout.fillWidth: true; visible: grid.findOpen && !grid.findValid; text: findMatches.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
    Label { Layout.fillWidth: true; visible: workspace.tableLayoutError !== "" && !columnsDialog.visible; text: workspace.tableLayoutError; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    Menu {
        id: cellMenu
        property string path: ""
        property int column: -1
        onAboutToShow: grid.contextRequested(path)
        property string endpoint: ""
        MenuItem { objectName: grid.prefix === "resource" ? "menuInspector" : grid.prefix + "MenuInspector"; text: workspace.uiText["ref.menuOpen"]; visible: grid.inspectable; enabled: visible; onTriggered: grid.inspectRequested(cellMenu.endpoint) }
        MenuItem { objectName: grid.prefix === "resource" ? "menuCopy" : grid.prefix + "MenuCopy"; text: workspace.uiText["copy.value"]; onTriggered: grid.copyPathRequested(cellMenu.path, cellMenu.column) }
        Instantiator {
            model: grid.contextActions
            delegate: MenuItem { required property var modelData; action: modelData; objectName: modelData.objectName }
            onObjectAdded: (index, object) => cellMenu.insertItem(index + 2, object)
            onObjectRemoved: (index, object) => cellMenu.removeItem(object)
        }
    }
    Component {
        id: headerDelegate
        Button {
            required property var model
            required property int column
            hoverEnabled: true
            readonly property bool pinned: TableView.view === pinnedHeader
            readonly property bool sortable: grid.sortableColumns < 0 || column < grid.sortableColumns
            objectName: (pinned ? "pinnedHeader_" : grid.prefix === "resource" ? "header_" : grid.prefix + "Header_") + column
            text: model.display
            implicitWidth: 170
            implicitHeight: 38
            font.pixelSize: 14
            font.bold: true
            leftPadding: 10; rightPadding: 10
            background: Rectangle {
                color: parent.down || parent.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.panel
                border.color: parent.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
            }
            contentItem: RowLayout {
                spacing: 6
                KindGlyph {
                    visible: ["alert", "diagnostic", "audit"].indexOf(grid.prefix) < 0
                    Layout.preferredWidth: 12; Layout.preferredHeight: 12
                    kind: ({Status:"Event",Kind:"CustomResourceDefinition",Name:"Pod",Namespace:"Namespace",Cluster:"Cluster",CPU:"Node",Memory:"ConfigMap",Storage:"PersistentVolume",Age:"CronJob",Ready:"Service",Restarts:"Event",Node:"Node",Image:"ConfigMap",Owner:"Deployment"})[model.display] || "Event"
                    fill: workspace.appearanceColors.accent
                }
                Label { Layout.fillWidth: true; text: model.display; textFormat: Text.PlainText; font: parent.parent.font; color: workspace.appearanceColors.accent; elide: Text.ElideRight }
                Label { visible: grid.sortColumn === column; text: grid.sortDirection.toLowerCase().indexOf("asc") === 0 ? "▲" : "▼"; color: workspace.appearanceColors.accent; font.pixelSize: 12 }
            }
            focusPolicy: sortable ? Qt.StrongFocus : Qt.NoFocus
            Accessible.name: model.display + (sortable ? grid.sortColumn===column ? ", " + grid.sortDirection : ", unsorted" : ", action column")
            onClicked: if (sortable) grid.sortRequested(column)
            ToolTip.visible: (hovered || activeFocus) && contentItem.implicitWidth > availableWidth
            ToolTip.text: text
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: columnsDialog.open() }
        }
    }
    Component {
        id: cellDelegate
        ItemDelegate {
            id: cell
            hoverEnabled: true
            activeFocusOnTab: true
            required property int row
            required property int column
            required property var model
            required property string resourcePath
            required property color identityColor
            required property bool selected
            readonly property bool current: selection.currentIndex.valid && selection.currentIndex.row === row && selection.currentIndex.column === column
            readonly property bool pinned: TableView.view === pinnedTable
            objectName: grid.cellName ? grid.cellName(row, column, resourcePath) : (pinned ? "pinnedCell_" : grid.prefix === "resource" ? "cell_" : grid.prefix + "Cell_") + row + "_" + column
            implicitWidth: 170
            implicitHeight: grid.rowHeight
            font.pixelSize: 13
            padding: grid.accessoryColumns.indexOf(column) >= 0 ? 0 : 6
            highlighted: selected
            onActiveFocusChanged: if (activeFocus) {
                grid.focusCell(row, column, TableView.view)
            }
            text: model.display
            Accessible.name: model.display
            contentItem: Label {
                id: cellText
                text: grid.accessoryColumns.indexOf(cell.column) < 0 ? model.display : ""
                textFormat: Text.PlainText
                font: cell.font
                color: identityColor.a > 0 ? identityColor : palette.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                wrapMode: grid.wrapCells ? Text.Wrap : Text.NoWrap
                maximumLineCount: grid.wrapCells ? 3 : 1
                Loader {
                    anchors.fill: parent
                    active: cell.width > 0 && grid.cellAccessory !== null && grid.accessoryColumns.indexOf(cell.column) >= 0
                    visible: active
                    sourceComponent: grid.cellAccessory
                    property string identity: cell.resourcePath
                    property string resourceName: grid.tableModel.data(grid.tableModel.index(cell.row,0),Qt.UserRole+1) || ""
                    property int row: cell.row
                    property int column: cell.column
                    property var tableView: cell.TableView.view
                    property bool encodedValue: cell.model.encodedValue
                }
            }
            background: Rectangle {
                color: cell.highlighted || cell.current ? workspace.appearanceColors.selection : cell.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.inset
                border.color: cell.activeFocus || (cell.current && cell.TableView.view.activeFocus) ? workspace.appearanceColors.accent : workspace.appearanceColors.border
                border.width: 1
            }
            onClicked: { forceActiveFocus(Qt.MouseFocusReason); grid.activateCell(row, column) }
            Keys.onReturnPressed: grid.activateCell(row, column)
            Keys.onEnterPressed: grid.activateCell(row, column)
            Keys.onPressed: function(event) {
                event.accepted = grid.handleCellKey(event, row, column, cell)
            }
            readonly property string tipText: grid.prefix === "value" && column === 2 ? model.valueDetail : model.display
            readonly property bool wantsTip: (hovered || activeFocus) && (cellText.truncated || tipText.length > model.display.length)
            onWantsTipChanged: if (wantsTip) valueTip.target=cell
            Component.onDestruction: if (valueTip.target===cell) valueTip.target=null
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: { cellMenu.path = grid.pathAt(row); cellMenu.endpoint = grid.endpointAt(row, column); cellMenu.column = column; cellMenu.popup() } }
        }
    }
    Item {
        implicitHeight: scrollingHeader.implicitHeight + grid.rowHeight + horizontalScroll.implicitHeight
        Layout.fillHeight: true
        Layout.fillWidth: true
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ColumnLayout {
                visible: grid.pinnedWidth > 0
                Layout.preferredWidth: Math.min(grid.pinnedWidth, Math.max(0, grid.width - (grid.hasScrollingColumns ? 120 : 0)))
                Layout.fillHeight: true
                spacing: 0
                HorizontalHeaderView { id: pinnedHeader; objectName: grid.prefix + "PinnedHeaderView"; Layout.fillWidth: true; syncView: pinnedTable; clip: true; delegate: headerDelegate }
                TableView {
                    id: pinnedTable
                    animate: false
                    activeFocusOnTab: true
                    objectName: grid.prefix + "PinnedTable"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    onWidthChanged: Qt.callLater(grid.revealFocusedCell)
                    clip: true
                    model: grid.visible && grid.pinnedWidth > 0 ? grid.tableModel : null
                    selectionModel: model ? selection : null
                    selectionBehavior: TableView.SelectRows
                    selectionMode: TableView.SingleSelection
                    syncView: grid.visible && grid.pinnedWidth > 0 ? table : null
                    syncDirection: Qt.Vertical
                    columnWidthProvider: function(column) { const state = grid.stateAt(column); return state && state.visible && state.pinned ? state.width : 0 }
                    rowHeightProvider: function() { return grid.rowHeight }
                    ScrollBar.horizontal: ScrollBar {}
                    delegate: cellDelegate
                    Keys.onReturnPressed: if (selection.currentIndex.valid) grid.activateCell(selection.currentIndex.row, selection.currentIndex.column)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 0
                HorizontalHeaderView { id: scrollingHeader; objectName: grid.prefix + "HeaderView"; Layout.fillWidth: true; syncView: table; clip: true; delegate: headerDelegate }
                TableView {
                    id: table
                    animate: false
                    activeFocusOnTab: true
                    objectName: grid.prefix + "Table"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    onWidthChanged: Qt.callLater(grid.revealFocusedCell)
                    clip: true
                    model: grid.visible ? grid.tableModel : null
                    selectionModel: model ? selection : null
                    onColumnsChanged: Qt.callLater(grid.synchronizeColumns)
                    selectionBehavior: TableView.SelectRows
                    selectionMode: TableView.SingleSelection
                    columnWidthProvider: function(column) { const state = grid.stateAt(column); return state && state.visible && !state.pinned ? state.width : 0 }
                    rowHeightProvider: function() { return grid.rowHeight }
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar { id: horizontalScroll }
                    delegate: cellDelegate
                    Keys.onReturnPressed: if (selection.currentIndex.valid) grid.activateCell(selection.currentIndex.row, selection.currentIndex.column)
                }
            }
        }
        ColumnLayout {
            anchors.centerIn: parent; width: Math.min(420, parent.width - 24)
            visible: table.rows === 0; spacing: 10
            Image {
                objectName: grid.prefix + "EmptyLogo"
                visible: grid.prefix === "resource"
                Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Math.min(220, parent.width)
                Layout.preferredHeight: Math.min(210, Math.max(0, grid.height - emptyTitle.implicitHeight - emptyMessage.implicitHeight - 110))
                source: "qrc:/podlord/brand-logo.png"; fillMode: Image.PreserveAspectFit
                sourceSize.width: 440; sourceSize.height: 420; asynchronous: true
                Accessible.ignored: true
            }
            Label {
                id: emptyTitle; visible: grid.prefix === "resource"
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                text: workspace.uiText[workspace.totalResourceCount > 0 ? "resource.noMatchingTitle" : workspace.loading ? "resource.loadingTitle" : "resource.emptyTitle"]
                textFormat: Text.PlainText; font.pixelSize: 24; font.bold: true
                color: workspace.appearanceColors.accent; wrapMode: Text.Wrap
            }
            Label { id: emptyMessage; objectName: grid.emptyObjectName; Layout.fillWidth: true; text: grid.emptyText; textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: workspace.appearanceColors.muted }
        }
    }
}
