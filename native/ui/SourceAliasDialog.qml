import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ModalDialog {
    id: sourceAliasDialog
    property string contextId: ""
    property string canonicalName: ""
    property string failure: ""
    title: workspace.uiText["context.rename"]
    modal: true
    Overlay.modal: Rectangle { color: "#99000000" }
    header: DialogHeader { dialog: sourceAliasDialog; closeObjectName: "cancelSourceAlias"; closeEnabled: !workspace.busy }
    focus: true
    parent: Overlay.overlay
    width: Math.min(520, parent.width - 24)
    x: (parent.width - width) / 2; y: Math.max(12, (parent.height - height) / 2)
    closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    contentItem: ColumnLayout {
        TextField { id: sourceAliasName; objectName: "sourceAliasName"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["context.name"]; placeholderText: sourceAliasDialog.canonicalName; onAccepted: sourceAliasDialog.save() }
        Label { objectName: "sourceAliasError"; Layout.fillWidth: true; visible: text !== ""; text: sourceAliasDialog.failure; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap; Accessible.name: text }
    }
    footer: DialogButtonBox {
        IconButton { glyph: "Save"; showText: true; font.bold: true; objectName: "saveSourceAlias"; text: workspace.busy ? workspace.uiText["action.saving"] : workspace.uiText["action.save"]; enabled: !workspace.busy; onClicked: sourceAliasDialog.save() }
    }
    function edit(context) {
        contextId = context.id; canonicalName = context.context; failure = ""
        sourceAliasName.text = context.name
        open(); sourceAliasName.forceActiveFocus(); sourceAliasName.selectAll()
    }
    function save() {
        failure = ""
        if (!workspace.renameSourceContext(contextId, sourceAliasName.text)) failure = "Another source operation is still running. Try again when it finishes."
    }
    onClosed: { contextId = ""; canonicalName = ""; sourceAliasName.text = ""; failure = "" }
    Connections {
        target: workspace
        function onSourceImportFinished(success) {
            if (!sourceAliasDialog.visible) return
            if (success) sourceAliasDialog.close()
            else sourceAliasDialog.failure = workspace.sourceImportError
        }
    }
}
