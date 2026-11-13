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
    Action { id: stop; objectName: "portContextStop"; text: view.selectedTerminal ? "Disconnect" : "Stop"; enabled: !!view.selectedForward || view.selectedTerminal; onTriggered: view.selectedTerminal ? workspace.stopContainerTerminal() : workspace.stopPortForward(view.selectedId) }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            visible: view.searchOpen
            TextField { id: search; objectName: "portFilter"; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText; Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: view.Window.window && view.Window.window.width < 900 ? 44 : 32; text: workspace.portFilterText; placeholderText: "Search connections"; Accessible.name: "Search connections"; onTextEdited: workspace.filterPorts(text); Keys.onEscapePressed: view.searchClosed() }
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            IconButton { glyph: "Service"; text: "Forward selected resource"; visible: workspace.canPortForward; onClicked: workspace.preparePortForward() }
        }
        Label { Layout.fillWidth: true; visible: workspace.portForwardError !== ""; text: workspace.portForwardError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger }
        Button { visible: workspace.portForwardError !== "" && workspace.authenticationRequired; text: "Sign in"; onClicked: authenticationRequested() }
        ResourceGrid {
            id: ports
            Layout.fillWidth: true; Layout.fillHeight: true
            prefix: "port"; tableModel: workspace.portTable; columns: workspace.portColumns
            rowHeight: view.Window.window ? view.Window.window.controlHeight : 44
            accessoryColumns: [0]
            accessibleAccessoryCells: true
            cellAccessory: Component {
                RowLayout {
                    id: connectionRow
                    readonly property string token: parent.identity
                    readonly property bool terminal: token === workspace.terminalConnection.id
                    readonly property bool truncated: endpointLabel.truncated
                    spacing: 4
                    Label {
                        id: endpointLabel
                        Accessible.ignored: true
                        objectName: "connectionEndpoint_" + connectionRow.token
                        Layout.leftMargin: 6
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: workspace.portTable.data(workspace.portTable.index(connectionRow.parent.row, 0)) || ""
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        font: ports.cellFont
                        color: workspace.appearanceColors.text
                    }
                    IconButton {
                        objectName: "stopConnection_" + connectionRow.token
                        glyph: "Close"
                        text: connectionRow.terminal ? "Disconnect terminal" : "Stop port forward"
                        Accessible.name: text + ": " + endpointLabel.text
                        Layout.preferredWidth: ports.rowHeight
                        Layout.preferredHeight: ports.rowHeight
                        onClicked: connectionRow.terminal ? workspace.stopContainerTerminal() : workspace.stopPortForward(connectionRow.token)
                    }
                }
            }
            sortColumn: workspace.portSortColumnIndex; sortDirection: workspace.portSortDirection
            emptyText: workspace.portForwards.length === 0 && !workspace.terminalConnection.active ? "No connections." : "No connections match this search."
            contextOpenText: view.selectedTerminal ? "Open terminal" : workspace.uiText["ref.menuOpen"]
            contextActions: view.selectedTerminal ? [stop] : [copyEndpoint, openHttp, openHttps, stop]
            onSortRequested: column => workspace.sortPortColumn(column)
            onInspectRequested: id => view.inspectConnection(id)
            onContextRequested: id => view.selectedId = id
            onCurrentRequested: id => view.selectedId = id
            onCopyRequested: (row, column) => workspace.copyPortValue(ports.pathAt(row), column)
            onCopyPathRequested: (id, column) => workspace.copyPortValue(id, column)
        }
    }
}
