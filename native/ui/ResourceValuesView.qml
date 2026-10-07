import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ResourceGrid {
    id: values
    objectName: "resourceValues"
    prefix: "value"
    tableModel: workspace.valuesTable
    columns: workspace.valuesColumns
    sortColumn: workspace.valuesSortColumn
    sortDirection: workspace.valuesSortDirection
    sortableColumns: 3
    inspectable: false
    rowHeight: 64
    wrapCells: true
    actionColumns: [4]
    accessoryColumns: [3]
    emptyText: "No values cached. Refresh detail to request current data."
    property string copyError: ""
    function copy(identity, representation) {
        copyError = workspace.copyValue(identity, representation) ? "" : "Cannot decode this value as text. Copy the raw value instead."
    }
    cellName: function(row, column, identity) {
        return column === 2 ? "value_" + identity : column === 4 ? "reveal_" + identity : "valueCell_" + row + "_" + column
    }
    onSortRequested: column => workspace.sortInspectorColumn("value", column)
    onActionRequested: (identity, column, label) => {
        workspace.revealValue(identity, label === "Reveal")
    }
    onCopyRequested: (row, column) => copy(pathAt(row), column === 0 ? "key" : "preferred")
    onCopyPathRequested: (identity, column) => copy(identity, column === 0 ? "key" : "preferred")
    Connections {
        target: workspace
        function onInspectorPresentationChanged() { values.copyError = "" }
    }
    Label {
        objectName: "copyValueError"
        Layout.fillWidth: true
        text: values.copyError
        textFormat: Text.PlainText
        visible: text !== ""
        wrapMode: Text.Wrap
        color: workspace.appearanceColors.danger
        Accessible.name: text
    }
    cellAccessory: Component {
        RowLayout {
            id: copyActions
            readonly property string identity: parent.identity
            readonly property bool encoded: parent.encodedValue
            spacing: 2
            ToolButton { objectName: "copyKey_" + copyActions.identity; text: "KEY"; padding: 2; Accessible.name: workspace.uiText["copy.key"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "key") }
            ToolButton { objectName: "copy_" + copyActions.identity; text: "VALUE"; padding: 2; Accessible.name: workspace.uiText["copy.value"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "preferred") }
            ToolButton { objectName: "copyRaw_" + copyActions.identity; text: "RAW"; padding: 2; visible: copyActions.encoded; Accessible.name: workspace.uiText["copy.rawBase64"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "raw") }
            ToolButton { objectName: "copyDecoded_" + copyActions.identity; text: "DEC"; padding: 2; visible: copyActions.encoded; Accessible.name: workspace.uiText["copy.decodedValue"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "decoded") }
        }
    }
}
