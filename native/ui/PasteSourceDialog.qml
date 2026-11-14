import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ModalDialog {
    id: surfaceDialog
    objectName: "pasteSourceDialog"
    parent: Overlay.overlay
    modal: true
    title: workspace.uiText["context.pasteTitle"]
    header: DialogHeader { dialog: surfaceDialog; closeObjectName: "cancelPastedSource"; closeEnabled: !surfaceDialog.awaitingImport }
    width: Math.min(640, Math.max(0, dialogArea.width - 24))
    height: Math.min(620, Math.max(0, dialogArea.height - 24))
    x: dialogArea.x + (dialogArea.width - width) / 2
    y: dialogArea.y + (dialogArea.height - height) / 2
    property bool awaitingImport: false
    property string errorText: ""
    closePolicy: awaitingImport ? Popup.NoAutoClose : Popup.CloseOnEscape
    onOpened: {
        origin.text = workspace.defaultSourceOrigin
        yaml.text = ""
        errorText = ""
        yaml.forceActiveFocus()
    }
    onClosed: { yaml.text = ""; origin.text = ""; errorText = "" }
    Connections {
        target: workspace
        function onSourceImportFinished(success) {
            if (!surfaceDialog.awaitingImport) return
            surfaceDialog.awaitingImport = false
            if (success) surfaceDialog.close()
            else surfaceDialog.errorText = workspace.sourceImportError
        }
    }
    contentItem: ColumnLayout {
        Label { Layout.fillWidth: true; visible: !workspace.appStore; text: workspace.uiText["context.pasteOrigin"]; wrapMode: Text.Wrap }
        TextField {
            id: origin
            objectName: "pasteSourceOrigin"
            visible: !workspace.appStore
            Layout.fillWidth: true
            enabled: !surfaceDialog.awaitingImport
            selectByMouse: true
            Accessible.name: "Kubeconfig origin path; source file is not written"
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: yaml
                objectName: "pasteSourceYaml"
                placeholderText: workspace.uiText["context.pasteTitle"]
                wrapMode: TextEdit.NoWrap
                textFormat: TextEdit.PlainText
                selectByMouse: true
                enabled: !surfaceDialog.awaitingImport
                Accessible.name: "Pasted kubeconfig YAML, may contain credentials"
            }
        }
        Label {
            objectName: "pasteSourceError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: surfaceDialog.errorText
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: workspace.appearanceColors.danger
            Accessible.name: text
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "importPastedSource"
            text: surfaceDialog.awaitingImport ? "Importing..." : workspace.uiText["action.import"]
            Accessible.description: "Imports privately without connecting or signing in. Maximum 16 MiB."
            enabled: !workspace.busy && !surfaceDialog.awaitingImport && yaml.text.trim().length > 0 && origin.text.trim().length > 0
            onClicked: {
                surfaceDialog.errorText = ""
                surfaceDialog.awaitingImport = workspace.importText(origin.text, yaml.text)
            }
        }
    }
}
