import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Pane {
    id: view
    signal authenticationRequested()
    signal searchClosed()
    signal terminalRequested()
    property bool searchOpen: false
    property string selectedId: ""
    readonly property var selectedForward: workspace.portForwards.find(row => row.id === selectedId)
    readonly property bool selectedTerminal: selectedId !== "" && selectedId === workspace.terminalConnection.id
    function focusSearch() { search.forceActiveFocus() }
    function inspectConnection(id) {
        if (id === workspace.terminalConnection.id) {
            selectedId = id
            terminalRequested()
            return true
        }
        const forward = workspace.portForwards.find(row => row.id === id)
        if (!forward) return false
        selectedId = id
        return workspace.inspectPath(forward.path)
    }
    objectName: "portForwardTasks"
    padding: 8
    background: Rectangle { color: workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border }
    Action { id: copyEndpoint; objectName: "portContextCopyEndpoint"; text: "Copy endpoint"; enabled: !!view.selectedForward; onTriggered: workspace.copyPortForwardEndpoint(view.selectedId) }
    Action { id: openHttp; objectName: "portContextOpenHttp"; text: "Open HTTP"; enabled: !!view.selectedForward && view.selectedForward.status === "Listening"; onTriggered: workspace.openPortForwardEndpoint(view.selectedId, false) }
    Action { id: openHttps; objectName: "portContextOpenHttps"; text: "Open HTTPS"; enabled: openHttp.enabled; onTriggered: workspace.openPortForwardEndpoint(view.selectedId, true) }
    Action { id: openShell; objectName: "connectionOpenTerminal"; text: "Open terminal"; enabled: view.selectedTerminal; onTriggered: view.terminalRequested() }
    Action { id: stop; objectName: "portContextStop"; text: view.selectedTerminal ? (workspace.terminalConnection.active ? "Disconnect" : "Dismiss") : "Stop"; enabled: !!view.selectedForward || view.selectedTerminal; onTriggered: view.selectedTerminal ? workspace.stopContainerTerminal() : workspace.stopPortForward(view.selectedId) }
    Menu {
        id: endpointMenu
        MenuItem { objectName: "openPortForwardHttp"; action: openHttp }
        MenuItem { objectName: "openPortForwardHttps"; action: openHttps }
    }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            visible: view.searchOpen
            TextField { id: search; objectName: "portFilter"; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText; Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: view.Window.window && view.Window.window.width < 900 ? 44 : 32; text: workspace.portFilterText; placeholderText: "Search session connections"; Accessible.name: "Search session connections"; onTextEdited: workspace.filterPorts(text); Keys.onEscapePressed: view.searchClosed() }
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            IconButton { glyph: "Service"; text: "Forward selected resource"; visible: workspace.canPortForward; onClicked: workspace.preparePortForward() }
            IconButton { objectName: "openTerminalConnection"; glyph: "Next"; action: openShell; visible: view.selectedTerminal }
            IconButton { objectName: "copyPortForwardTask"; glyph: "ConfigMap"; action: copyEndpoint; visible: !view.selectedTerminal }
            IconButton { id: openPortForwardTask; objectName: "openPortForwardTask"; glyph: "Next"; text: workspace.uiText["action.open"]; visible: !view.selectedTerminal; enabled: openHttp.enabled; Accessible.name: view.selectedForward ? "Open " + view.selectedForward.endpoint + " in browser" : "Open selected port forward in browser"; onClicked: endpointMenu.popup(openPortForwardTask, 0, openPortForwardTask.height) }
            IconButton { objectName: "stopPortForwardTask"; glyph: "Close"; action: stop }
        }
        Label { Layout.fillWidth: true; visible: workspace.portForwardError !== ""; text: workspace.portForwardError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger }
        Button { visible: workspace.portForwardError !== ""; text: "Review authentication..."; onClicked: authenticationRequested() }
        ResourceGrid {
            id: ports
            Layout.fillWidth: true; Layout.fillHeight: true
            prefix: "port"; tableModel: workspace.portTable; columns: workspace.portColumns
            sortColumn: workspace.portSortColumnIndex; sortDirection: workspace.portSortDirection
            emptyText: workspace.portForwards.length === 0 && !workspace.containerTerminal ? "No connections in this session." : "No connections match this search."
            contextOpenText: view.selectedTerminal ? "Open terminal" : workspace.uiText["ref.menuOpen"]
            contextActions: view.selectedTerminal ? [stop] : [copyEndpoint, openHttp, openHttps, stop]
            onSortRequested: column => workspace.sortPortColumn(column)
            onInspectRequested: id => view.inspectConnection(id)
            onContextRequested: id => view.selectedId = id
            onCopyRequested: (row, column) => workspace.copyPortValue(ports.pathAt(row), column)
            onCopyPathRequested: (id, column) => workspace.copyPortValue(id, column)
        }
    }
}
