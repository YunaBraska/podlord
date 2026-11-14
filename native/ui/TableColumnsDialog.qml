import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ModalDialog {
    id: surfaceDialog
    required property string tableType
    required property var columns
    property bool pending: false
    ListModel { id: draft; dynamicRoles: true }
    objectName: tableType + "ColumnsDialog"
    title: workspace.uiText["action.columns"]
    modal: true
    parent: Overlay.overlay
    width: Math.min(580, Math.max(0, dialogArea.width - 24))
    height: Math.min(600, Math.max(0, dialogArea.height - 24))
    x: dialogArea.x + (dialogArea.width - width) / 2
    y: dialogArea.y + (dialogArea.height - height) / 2
    Overlay.modal: Rectangle { color: "#99000000" }
    header: DialogHeader { dialog: surfaceDialog; closeObjectName: surfaceDialog.tableType + "CancelColumns"; closeEnabled: !workspace.tableLayoutSaving }
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
            if (surfaceDialog.pending && !workspace.tableLayoutSaving) {
                surfaceDialog.pending = false
                if (workspace.tableLayoutError === "") surfaceDialog.close()
            }
        }
    }
    contentItem: ColumnLayout {
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 4
                Repeater {
                    model: draft
                    delegate: GridLayout {
                        required property var model
                        readonly property var modelData: model
                        required property int index
                        readonly property int layoutColumns: width >= 480 ? 6 : 3
                        Layout.fillWidth: true
                        columns: layoutColumns
                        columnSpacing: 8
                        rowSpacing: 4
                        enabled: !workspace.tableLayoutSaving
                        Label { Layout.fillWidth: true; text: modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight }
                        IconButton { objectName: surfaceDialog.tableType + "ColumnVisible_" + modelData.id; glyph: modelData.visible ? "Visible" : "Hidden"; checkable: true; checked: modelData.visible; text: "Show " + modelData.title; Accessible.name: text; onClicked: surfaceDialog.change(index, "visible", checked) }
                        IconButton { objectName: surfaceDialog.tableType + "ColumnPinned_" + modelData.id; glyph: "Pin"; checkable: true; checked: modelData.pinned; text: "Pin " + modelData.title; Accessible.name: text; onClicked: surfaceDialog.change(index, "pinned", checked) }
                        TextField {
                            objectName: surfaceDialog.tableType + "ColumnWidth_" + modelData.id
                            Layout.preferredWidth: 92
                            implicitHeight: Window.window ? Window.window.controlHeight : 44
                            Layout.minimumHeight: implicitHeight
                            text: modelData.widthText
                            validator: IntValidator { bottom: 1; top: 2147483647 }
                            inputMethodHints: Qt.ImhDigitsOnly
                            Accessible.name: modelData.title + " width in pixels"
                            ToolTip.text: Accessible.name
                            ToolTip.visible: hovered || activeFocus
                            onTextEdited: surfaceDialog.change(index, "widthText", text)
                        }
                            IconButton { objectName: surfaceDialog.tableType + "ColumnEarlier_" + modelData.id; glyph: ""; contentItem: Label { text: "\u2191"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } text: "Move up"; enabled: index > 0; Accessible.name: "Move " + modelData.title + " earlier"; onClicked: draft.move(index, index - 1, 1) }
                            IconButton { objectName: surfaceDialog.tableType + "ColumnLater_" + modelData.id; glyph: ""; contentItem: Label { text: "\u2193"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } text: "Move down"; enabled: index + 1 < draft.count; Accessible.name: "Move " + modelData.title + " later"; onClicked: draft.move(index, index + 1, 1) }
                        ToolSeparator { orientation: Qt.Horizontal; Layout.fillWidth: true; Layout.columnSpan: layoutColumns; topPadding: 0; bottomPadding: 0 }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: workspace.tableLayoutSaving || workspace.tableLayoutError !== ""; text: workspace.tableLayoutSaving ? "Saving column layout..." : workspace.tableLayoutError; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
    }
    footer: DialogButtonBox {
        implicitHeight: Math.max(defaultsButton.implicitHeight, saveButton.implicitHeight) + topPadding + bottomPadding
        Button {
            id: defaultsButton
            objectName: surfaceDialog.tableType + "ResetColumns"
            text: "Defaults"
            enabled: !workspace.tableLayoutSaving
            implicitHeight: Window.window ? Window.window.controlHeight : 44
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
            onClicked: surfaceDialog.resetDraft()
        }
        IconButton { id: saveButton; glyph: "Save"; showText: true; objectName: surfaceDialog.tableType + "SaveColumns"; text: workspace.uiText["action.save"]; enabled: !workspace.tableLayoutSaving; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; onClicked: surfaceDialog.save(); Keys.onReturnPressed: surfaceDialog.save() }
        onRejected: surfaceDialog.close()
    }
}
