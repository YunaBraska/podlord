import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Podlord.Graphics 1.0

Pane {
    id: root
    objectName: "sourceInspector"
    readonly property var hostWindow: ApplicationWindow.window
    readonly property bool inputPriority: hostWindow ? hostWindow.inputPriority : false
    readonly property string scope: (workspace.sourceInspection.snapshotId || "") + ":" + (workspace.sourceInspection.contextId || "")
    padding: 6
    clip: true
    component SourceTab: WorkspaceTab {}
    onScopeChanged: {
        if (tabs) tabs.currentIndex = 0
        if (yamlPage) yamlPage.active = false
    }
    background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.glow }
    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            visible: !root.inputPriority
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: workspace.sourceInspection.name || "Source"
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.bold: true
                Accessible.name: "Source: " + text
            }
            IconButton {
                objectName: "removeInspectedSource"
                glyph: "Close"
                text: "Remove"
                showText: true
                Accessible.name: "Remove imported context"
                enabled: !workspace.busy && !workspace.sourceEditing
                onClicked: workspace.requestSourceRemoval(workspace.sourceInspection.contextId)
            }
            IconButton {
                objectName: "closeSourceInspector"
                glyph: "Close"
                text: "Close source inspector"
                Accessible.name: "Close source inspector"
                enabled: !workspace.busy
                onClicked: workspace.closeInspector()
            }
        }
        TabBar {
            id: tabs
            objectName: "sourceInspectorTabs"
            spacing: 0
            Layout.fillWidth: true
            visible: !root.inputPriority
            SourceTab { objectName: "sourceOverviewTab"; text: "Overview" }
            SourceTab { objectName: "sourceYamlTab"; text: "YAML" }
        }
        Label {
            Layout.fillWidth: true
            visible: workspace.sourceInspectionLoading && !root.inputPriority
            text: "Loading owned kubeconfig..."
            Accessible.name: text
        }
        Label {
            objectName: "sourceInspectorError"
            Layout.fillWidth: true
            visible: text !== ""
            text: workspace.sourceInspectionError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: workspace.appearanceColors.danger
            Accessible.name: text
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tabs.currentIndex === 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: parent.width
                Repeater {
                    model: workspace.sourceInspection.fields || []
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        Label { text: modelData.label; font.bold: true; textFormat: Text.PlainText }
                        TextArea {
                            Layout.fillWidth: true
                            text: modelData.value
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            Accessible.name: modelData.label + ": " + modelData.value
                        }
                    }
                }
            }
        }
        Loader {
            id: yamlPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: false
            visible: tabs.currentIndex === 1
            onVisibleChanged: if (visible) active = true
            sourceComponent: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    visible: !root.inputPriority
                    Button {
                        objectName: "editSourceYaml"
                        text: "Edit YAML"
                        visible: !workspace.sourceEditing
                        enabled: !workspace.busy && !workspace.discardPending && workspace.canEditSourceYaml
                        onClicked: if (workspace.beginSourceEdit()) Qt.callLater(editor.forceActiveFocus)
                    }
                    Button {
                        objectName: "applySourceYaml"
                        text: "Apply"
                        visible: workspace.sourceEditing
                        enabled: !workspace.busy && !workspace.discardPending
                        Accessible.name: "Import edited kubeconfig as an immutable snapshot"
                        onClicked: workspace.applySourceDraft()
                    }
                    IconButton {
                        objectName: "resetSourceDraft"
                        glyph: "Reset"
                        text: workspace.yamlDirty ? "Discard source draft" : "Leave source editing"
                        visible: workspace.sourceEditing
                        enabled: !workspace.busy
                        Accessible.name: workspace.yamlDirty ? "Discard source draft" : "Leave source editing"
                        onClicked: workspace.leaveYamlEdit()
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    YamlPresentation {
                        Layout.preferredWidth: implicitWidth
                        Layout.fillHeight: true
                        editor: editor
                        document: root.visible && yamlPage.visible ? editor.textDocument : null
                        font: editor.font
                        light: workspace.themeVariant === "light"
                        color: workspace.appearanceColors.muted
                        scrollOffset: yamlScroll.contentItem ? yamlScroll.contentItem.contentY : 0
                        Accessible.role: Accessible.StaticText
                        Accessible.name: "Source YAML line numbers"
                    }
                    ScrollView {
                        id: yamlScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: editor
                            objectName: "sourceInspectorYaml"
                            text: workspace.sourceEditing ? workspace.sourceDraft : workspace.sourceYaml
                            readOnly: !workspace.sourceEditing || workspace.busy
                            textFormat: TextEdit.PlainText
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            font.family: workspace.monospaceFamily
                            onTextChanged: if (!readOnly && activeFocus && text !== workspace.sourceDraft) workspace.setSourceDraft(text)
                            Accessible.name: readOnly ? "Source YAML, read-only" : "Local kubeconfig draft"
                        }
                    }
                }
            }
        }
    }
}
