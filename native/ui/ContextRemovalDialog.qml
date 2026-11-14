import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ModalDialog {
    id: removal
    objectName: "sourceRemovalDialog"
    title: workspace.uiText["settings.removeContextTitle"]
    modal: true
    focus: true
    parent: Overlay.overlay
    width: Math.min(520, Math.max(0, dialogArea.width - 24))
    height: Math.min(implicitHeight, Math.max(0, dialogArea.height - 24))
    x: dialogArea.x + (dialogArea.width - width) / 2
    y: dialogArea.y + (dialogArea.height - height) / 2
    header: DialogHeader { dialog: removal; closeObjectName: "cancelSourceRemoval"; closeEnabled: !workspace.busy }
    closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    contentItem: ColumnLayout {
        Label { Layout.fillWidth: true; text: workspace.sourceRemoval.name || ""; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap }
        Label { Layout.fillWidth: true; text: workspace.uiText["settings.removeContextHelp"]; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        Label { objectName: "sourceRemovalError"; Layout.fillWidth: true; visible: text !== ""; text: workspace.sourceImportError; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap; Accessible.name: text }
    }
    footer: DialogButtonBox {
        IconButton { glyph: ""; showText: true; objectName: "confirmSourceRemoval"; text: workspace.busy ? workspace.uiText["settings.removing"] : workspace.uiText["settings.remove"]; enabled: !workspace.busy; Accessible.name: removal.title; onClicked: workspace.confirmSourceRemoval() }
    }
    onAboutToHide: workspace.cancelSourceRemoval()
    Connections {
        target: workspace
        function onSourceRemovalChanged() {
            if (workspace.sourceRemoval.contextId) removal.open()
            else if (removal.visible) removal.close()
        }
    }
}
