import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

ModalDialog {
    id: surfaceDialog
    objectName: "awsProfileDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    title: "Import AWS profile"
    width: Math.min(540, parent ? parent.width - 24 : 540)
    height: Math.min(implicitHeight, parent ? parent.height - 24 : implicitHeight)
    property bool awaitingImport: false
    property string errorText: ""
    property bool choosingCredentials: false
    closePolicy: awaitingImport ? Popup.NoAutoClose : Popup.CloseOnEscape
    onOpened: { errorText = ""; config.text = ""; credentials.text = ""; context.currentIndex = 0 }
    onClosed: { config.text = ""; credentials.text = ""; errorText = "" }
    NativeDialogs.FileDialog {
        id: filePicker
        title: surfaceDialog.choosingCredentials ? "Select AWS credentials" : "Select AWS config"
        fileMode: NativeDialogs.FileDialog.OpenFile
        onAccepted: {
            if (surfaceDialog.choosingCredentials) credentials.text = selectedFile.toString()
            else config.text = selectedFile.toString()
        }
    }
    Connections {
        target: workspace
        function onSourceImportFinished(success) {
            if (!surfaceDialog.awaitingImport) return
            surfaceDialog.awaitingImport = false
            if (success) surfaceDialog.close()
            else surfaceDialog.errorText = workspace.sourceImportError
        }
    }
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        implicitHeight: importFields.implicitHeight
        ColumnLayout {
            id: importFields
            width: parent.width
            spacing: 8
            ComboBox {
                id: context
                objectName: "awsProfileContext"
                Layout.fillWidth: true
                model: workspace.contexts
                textRole: "name"
                valueRole: "id"
                enabled: !surfaceDialog.awaitingImport
                Accessible.name: "AWS EKS context"
            }
            Label { Layout.fillWidth: true; text: "Uses the profile selected by this kubeconfig. Import does not sign in or contact a cluster."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: config; objectName: "awsProfileConfig"; Layout.fillWidth: true; placeholderText: "AWS config (optional)"; readOnly: workspace.appStore || Qt.platform.os === "android" || Qt.platform.os === "ios"; enabled: !surfaceDialog.awaitingImport; Accessible.name: "Selected AWS config file" }
                IconButton { objectName: "browseAwsConfig"; glyph: ""; showText: true; text: "Browse"; Accessible.name: "Select AWS config"; enabled: !surfaceDialog.awaitingImport; onClicked: { surfaceDialog.choosingCredentials = false; filePicker.open() } }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: credentials; objectName: "awsProfileCredentials"; Layout.fillWidth: true; placeholderText: "AWS credentials (optional)"; readOnly: workspace.appStore || Qt.platform.os === "android" || Qt.platform.os === "ios"; enabled: !surfaceDialog.awaitingImport; Accessible.name: "Selected AWS credentials file" }
                IconButton { objectName: "browseAwsCredentials"; glyph: ""; showText: true; text: "Browse"; Accessible.name: "Select AWS credentials"; enabled: !surfaceDialog.awaitingImport; onClicked: { surfaceDialog.choosingCredentials = true; filePicker.open() } }
            }
            Label { Layout.fillWidth: true; visible: surfaceDialog.errorText !== ""; text: surfaceDialog.errorText; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
        }
    }
    footer: DialogButtonBox {
        IconButton { objectName: "cancelAwsProfile"; glyph: ""; showText: true; text: "Cancel"; enabled: !surfaceDialog.awaitingImport; onClicked: surfaceDialog.close() }
        IconButton {
            objectName: "confirmAwsProfile"
            glyph: ""; showText: true
            text: surfaceDialog.awaitingImport ? "Importing..." : "Import"
            enabled: !workspace.busy && !surfaceDialog.awaitingImport && context.currentIndex >= 0 && (config.text.trim() !== "" || credentials.text.trim() !== "")
            onClicked: {
                surfaceDialog.errorText = ""
                surfaceDialog.awaitingImport = workspace.importAwsProfile(context.currentValue, config.text, credentials.text)
                if (!surfaceDialog.awaitingImport) surfaceDialog.errorText = workspace.sourceImportError
            }
        }
    }
}
