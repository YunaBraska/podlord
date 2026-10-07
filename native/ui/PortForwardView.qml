import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: view
    signal authenticationRequested()
    property string selectedId: ""
    readonly property var selectedForward: workspace.portForwards.find(row => row.id === selectedId)
    function focusSearch() { search.forceActiveFocus() }
    function inspectForward(id) {
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
    Action { id: stop; objectName: "portContextStop"; text: "Stop"; enabled: !!view.selectedForward; onTriggered: workspace.stopPortForward(view.selectedId) }
    Menu {
        id: endpointMenu
        MenuItem { objectName: "openPortForwardHttp"; action: openHttp }
        MenuItem { objectName: "openPortForwardHttps"; action: openHttps }
    }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            Label { text: workspace.uiText["nav.ports"]; font.bold: true }
            TextField { id: search; objectName: "portFilter"; Layout.fillWidth: true; text: workspace.portFilterText; placeholderText: "Search session port forwards"; Accessible.name: "Search session port forwards"; onTextEdited: workspace.filterPorts(text) }
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            Button { text: "Forward selected resource"; enabled: workspace.canPortForward; onClicked: workspace.preparePortForward() }
            Button { objectName: "copyPortForwardTask"; action: copyEndpoint }
            Button { objectName: "openPortForwardTask"; text: workspace.uiText["action.open"]; enabled: openHttp.enabled; Accessible.name: view.selectedForward ? "Open " + view.selectedForward.endpoint + " in browser" : "Open selected port forward in browser"; onClicked: endpointMenu.popup() }
            Button { objectName: "stopPortForwardTask"; action: stop }
        }
        Label { Layout.fillWidth: true; text: "Local endpoints belong to this session. Closing its tab stops them; switching views does not."; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; visible: workspace.portForwardError !== ""; text: workspace.portForwardError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger }
        Button { visible: workspace.portForwardError !== ""; text: "Review authentication..."; onClicked: authenticationRequested() }
        ResourceGrid {
            id: ports
            Layout.fillWidth: true; Layout.fillHeight: true
            prefix: "port"; tableModel: workspace.portTable; columns: workspace.portColumns
            sortColumn: workspace.portSortColumnIndex; sortDirection: workspace.portSortDirection
            emptyText: workspace.portForwards.length === 0 ? "No active port forwards in this session. Select a running Pod or Service in Resources to prepare one." : "No port forwards match this search."
            contextActions: [copyEndpoint, openHttp, openHttps, stop]
            onSortRequested: column => workspace.sortPortColumn(column)
            onInspectRequested: id => view.inspectForward(id)
            onContextRequested: id => view.selectedId = id
            onCopyRequested: (row, column) => workspace.copyPortValue(ports.pathAt(row), column)
            onCopyPathRequested: (id, column) => workspace.copyPortValue(id, column)
        }
    }
}
