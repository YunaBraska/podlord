import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ResourceGrid {
    id: values
    objectName: "resourceValues"
    prefix: "value"
    tableModel: workspace.valuesTable
    property bool compact: false
    inlineTools: !compact
    showHeader: !compact
    readonly property int copyColumnWidth: 64
    readonly property int keyColumnWidth: Math.max(72, Math.min(220, Math.floor((width - copyColumnWidth) / 3)))
    columns: compact ? workspace.valuesColumns.map(column => {
        const visible = column.column === 0 || column.column === 2 || column.column === 3
        const width = column.column === 0 ? keyColumnWidth : column.column === 3 ? copyColumnWidth
            : Math.max(96, Math.floor(values.width) - keyColumnWidth - copyColumnWidth)
        return Object.assign({}, column, {visible: visible, width: width})
    }) : workspace.valuesColumns
    sortColumn: workspace.valuesSortColumn
    sortDirection: workspace.valuesSortDirection
    sortableColumns: 3
    inspectable: false
    rowHeight: compact ? Math.max(44, Math.ceil(workspace.textSizes.cell * 1.3) + 12) : 64 * workspace.textScale
    wrapCells: true
    actionColumns: [4]
    accessoryColumns: [3]
    emptyText: workspace.uiText["inspector.noValues"]
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
    SelectableText {
        objectName: "copyValueError"
        Layout.fillWidth: true
        text: values.copyError
        visible: text !== ""
        color: workspace.appearanceColors.danger
    }
    cellAccessory: Component {
        RowLayout {
            id: copyActions
            readonly property string identity: parent.identity
            readonly property bool encoded: parent.encodedValue
            spacing: 2
            FlatButton { objectName: "copyKey_" + copyActions.identity; text: "KEY"; padding: 2; visible: !values.compact; Accessible.name: workspace.uiText["copy.key"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "key") }
            IconButton {
                objectName: "copy_" + copyActions.identity
                Layout.alignment: Qt.AlignHCenter
                glyph: values.compact ? "Copy" : ""
                showText: !values.compact
                padding: values.compact ? 6 : 2
                text: values.compact ? workspace.uiText["copy.value"] : "VALUE"
                Accessible.name: workspace.uiText["copy.value"] + " " + copyActions.identity
                onClicked: values.copy(copyActions.identity, "preferred")
            }
            FlatButton { objectName: "copyRaw_" + copyActions.identity; text: "RAW"; padding: 2; visible: !values.compact && copyActions.encoded; Accessible.name: workspace.uiText["copy.rawBase64"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "raw") }
            FlatButton { objectName: "copyDecoded_" + copyActions.identity; text: "DEC"; padding: 2; visible: !values.compact && copyActions.encoded; Accessible.name: workspace.uiText["copy.decodedValue"] + " " + copyActions.identity; onClicked: values.copy(copyActions.identity, "decoded") }
        }
    }
}
