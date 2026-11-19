import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    property var files: workspace.containerFiles
    spacing: 6
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            id: pod
            objectName: "filesPod"
            Layout.fillWidth: true
            model: workspace.filePods
            textRole: "name"
            valueRole: "path"
            Accessible.name: "Pod"
            onActivated: if (container.currentText !== "") workspace.openContainerFiles(currentValue, container.currentText)
        }
        ComboBox {
            id: container
            objectName: "filesContainer"
            Layout.fillWidth: true
            model: workspace.fileContainers(pod.currentValue || "")
            Accessible.name: workspace.uiText["logs.container"]
            onActivated: workspace.openContainerFiles(pod.currentValue, currentText)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.files !== null
        IconButton { objectName: "filesUp"; glyph: "Previous"; text: workspace.uiText["files.up"]; enabled: root.files && !root.files.busy && root.files.directory !== "/"; onClicked: root.files.up() }
        SelectableText { Layout.fillWidth: true; text: root.files ? root.files.directory : "/"; wrapMode: TextEdit.NoWrap; clip: true }
        IconButton { objectName: "filesRefresh"; glyph: "Reset"; text: workspace.uiText["files.refresh"]; enabled: root.files && !root.files.busy; onClicked: root.files.refresh() }
    }
    SelectableText {
        Layout.fillWidth: true
        visible: workspace.filePods.length === 0 || (root.files && root.files.status !== "")
        text: workspace.filePods.length === 0 ? workspace.uiText["files.noPod"]
            : root.files && root.files.status !== "" ? (workspace.uiText[root.files.status] || "") : ""
        color: workspace.appearanceColors.muted
    }
    ListView {
        objectName: "filesList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 180
        clip: true
        model: root.files ? root.files.entries : []
        delegate: RowLayout {
            required property var modelData
            width: ListView.view.width
            height: 44
            spacing: 4
            IconButton {
                glyph: modelData.type === "directory" ? "Folder" : modelData.type === "file" ? "Download" : "Links"
                text: modelData.type === "directory" ? workspace.uiText["action.open"] : workspace.uiText["files.download"]
                enabled: root.files && !root.files.busy && (modelData.type === "directory" || modelData.type === "file")
                onClicked: modelData.type === "directory" ? root.files.browse(modelData.name) : root.files.download(modelData.name)
            }
            SelectableText { Layout.fillWidth: true; text: modelData.name; wrapMode: TextEdit.NoWrap; clip: true }
        }
    }
    Connections { target: root.files; function onDownloadReady() {
        if (root.files) {
            saveDialog.selectedFile = root.files.suggestedSaveUrl
            saveDialog.open()
        }
    } }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: workspace.uiText["files.download"]
        onAccepted: if (root.files) root.files.save(selectedFile)
        onRejected: if (root.files) root.files.discardDownload()
    }
}
