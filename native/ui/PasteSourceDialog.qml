import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
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
            if (!dialog.awaitingImport) return
            dialog.awaitingImport = false
            if (success) dialog.close()
            else dialog.errorText = workspace.sourceImportError
        }
    }
    ColumnLayout {
        anchors.fill: parent
        Label {
            Layout.fillWidth: true
            text: "An owned snapshot is saved privately. Import does not connect to a cluster or run credential plugins. Maximum: 16 MiB."
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Label { text: "Origin path for relative certificate, token and plugin paths" }
        TextField {
            id: origin
            objectName: "pasteSourceOrigin"
            Layout.fillWidth: true
            enabled: !dialog.awaitingImport
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
                enabled: !dialog.awaitingImport
                Accessible.name: "Pasted kubeconfig YAML, may contain credentials"
            }
        }
        Label {
            objectName: "pasteSourceError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: dialog.errorText
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: "cancelPastedSource"
                text: "Cancel"
                enabled: !dialog.awaitingImport
                onClicked: dialog.close()
            }
            Button {
                objectName: "importPastedSource"
                text: dialog.awaitingImport ? "Importing..." : "Import snapshot"
                enabled: !workspace.busy && !dialog.awaitingImport && yaml.text.trim().length > 0 && origin.text.trim().length > 0
                onClicked: {
                    dialog.errorText = ""
                    dialog.awaitingImport = workspace.importText(origin.text, yaml.text)
                }
            }
        }
    }
}
