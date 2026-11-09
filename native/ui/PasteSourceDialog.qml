import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ModalDialog {
    id: surfaceDialog
    objectName: "pasteSourceDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: "Import pasted kubeconfig"
    width: Math.min(640, parent ? parent.width - 24 : 640)
    height: Math.min(620, parent ? parent.height - 24 : 620)
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
    ColumnLayout {
        anchors.fill: parent
        Label { visible: !workspace.appStore; text: "Origin path for relative certificate, token and plugin paths" }
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
                placeholderText: "Paste kubeconfig YAML"
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
            textFormat: Text.PlainText
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: "cancelPastedSource"
                text: workspace.uiText["action.cancel"]
                enabled: !surfaceDialog.awaitingImport
                onClicked: surfaceDialog.close()
            }
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
}
