import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property string tableType
    required property var columns
    property bool pending: false
    ListModel { id: draft; dynamicRoles: true }
    objectName: tableType + "ColumnsDialog"
    title: "Columns"
    modal: true
    parent: Overlay.overlay
    width: Math.min(740, parent.width - 24)
    height: Math.min(600, parent.height - 24)
    anchors.centerIn: parent
    function snapshot() {
        draft.clear()
        for (const column of columns) draft.append(Object.assign({}, column, {widthText: String(column.width)}))
    }
    function change(index, field, value) {
        draft.setProperty(index, field, value)
        if (field === "pinned" && value) draft.setProperty(index, "visible", true)
        if (field === "visible" && !value) draft.setProperty(index, "pinned", false)
    }
    function resetDraft() {
        draft.clear()
        for (const column of workspace.defaultTableColumns(tableType))
            draft.append(Object.assign({}, column, {widthText: String(column.width)}))
    }
    function save() {
        const value = []
        for (let index = 0; index < draft.count; ++index) {
            const column = draft.get(index)
            value.push({id: column.id, visible: column.visible, pinned: column.pinned, width: column.widthText === "" ? 0 : Number(column.widthText)})
        }
        pending = true
        if (!workspace.saveTableLayout(tableType, value)) pending = false
    }
    onOpened: { pending = false; snapshot() }
    Connections {
        target: workspace
        function onTableLayoutStatusChanged() {
            if (dialog.pending && !workspace.tableLayoutSaving) {
                dialog.pending = false
                if (workspace.tableLayoutError === "") dialog.close()
            }
        }
    }
    contentItem: ColumnLayout {
        Label { Layout.fillWidth: true; text: "Saved for this table type in every session. Pin keeps a column visible while the other columns scroll."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 10
                Repeater {
                    model: draft
                    delegate: ColumnLayout {
                        required property var model
                        readonly property var modelData: model
                        required property int index
                        Layout.fillWidth: true
                        enabled: !workspace.tableLayoutSaving
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight }
                            CheckBox { objectName: dialog.tableType + "ColumnVisible_" + modelData.id; text: "Show"; checked: modelData.visible; Accessible.name: "Show " + modelData.title; onToggled: dialog.change(index, "visible", checked) }
                            CheckBox { objectName: dialog.tableType + "ColumnPinned_" + modelData.id; text: "Pin"; checked: modelData.pinned; Accessible.name: "Pin " + modelData.title; onToggled: dialog.change(index, "pinned", checked) }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Width" }
                            TextField {
                                objectName: dialog.tableType + "ColumnWidth_" + modelData.id
                                Layout.preferredWidth: 92
                                text: modelData.widthText
                                validator: IntValidator { bottom: 1; top: 2147483647 }
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: modelData.title + " width in pixels"
                                onTextEdited: dialog.change(index, "widthText", text)
                            }
                            Item { Layout.fillWidth: true }
                            Button { objectName: dialog.tableType + "ColumnEarlier_" + modelData.id; text: "Earlier"; enabled: index > 0; Accessible.name: "Move " + modelData.title + " earlier"; onClicked: draft.move(index, index - 1, 1) }
                            Button { objectName: dialog.tableType + "ColumnLater_" + modelData.id; text: "Later"; enabled: index + 1 < draft.count; Accessible.name: "Move " + modelData.title + " later"; onClicked: draft.move(index, index + 1, 1) }
                        }
                        ToolSeparator { orientation: Qt.Horizontal; Layout.fillWidth: true }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: workspace.tableLayoutSaving || workspace.tableLayoutError !== ""; text: workspace.tableLayoutSaving ? "Saving column layout..." : workspace.tableLayoutError; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        RowLayout {
            Button { objectName: dialog.tableType + "ResetColumns"; text: "Defaults"; enabled: !workspace.tableLayoutSaving; onClicked: dialog.resetDraft() }
            Button { text: "Reload saved"; enabled: !workspace.tableLayoutSaving; onClicked: if (workspace.reloadTableLayouts()) dialog.snapshot() }
        }
    }
    footer: DialogButtonBox {
        Button { objectName: dialog.tableType + "SaveColumns"; text: "Save"; enabled: !workspace.tableLayoutSaving; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; onClicked: dialog.save(); Keys.onReturnPressed: dialog.save() }
        Button { objectName: dialog.tableType + "CancelColumns"; text: "Cancel"; enabled: !workspace.tableLayoutSaving; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onRejected: dialog.close()
    }
}
