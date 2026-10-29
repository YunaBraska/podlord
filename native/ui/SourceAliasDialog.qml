import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: sourceAliasDialog
    property string contextId: ""
    property string canonicalName: ""
    property string failure: ""
    title: "Rename source context"
    modal: true
    focus: true
    parent: Overlay.overlay
    width: Math.min(520, parent.width - 24)
    x: (parent.width - width) / 2; y: Math.max(12, (parent.height - height) / 2)
    closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    contentItem: ColumnLayout {
        Label { Layout.fillWidth: true; text: "Canonical context: " + sourceAliasDialog.canonicalName; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        TextField { id: sourceAliasName; objectName: "sourceAliasName"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: "Source display name"; placeholderText: "Empty restores the canonical name"; onAccepted: sourceAliasDialog.save() }
        Label { Layout.fillWidth: true; text: "Only the display label changes. Credentials, context identity and sessions remain unchanged."; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; wrapMode: Text.Wrap }
        Label { objectName: "sourceAliasError"; Layout.fillWidth: true; visible: text !== ""; text: sourceAliasDialog.failure; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap; Accessible.name: text }
    }
    footer: DialogButtonBox {
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "cancelSourceAlias"; text: "Cancel"; enabled: !workspace.busy; onClicked: sourceAliasDialog.close() }
        IconButton { glyph: ""; showText: true; font.bold: true; objectName: "saveSourceAlias"; text: workspace.busy ? "Saving..." : "Save"; enabled: !workspace.busy; onClicked: sourceAliasDialog.save() }
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
