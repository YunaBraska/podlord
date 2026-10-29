import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: overview
    signal sourcesRequested()
    property var contexts: []
    padding: 4
    background: Rectangle { color: workspace.appearanceColors.inset }
    SourceAliasDialog { id: aliases }
    ColumnLayout {
        anchors.fill: parent
        Label {
            objectName: "mapPreviewError"
            Layout.fillWidth: true
            text: workspace.mapPreviewError; textFormat: Text.PlainText; wrapMode: Text.Wrap
            visible: text !== ""; color: workspace.appearanceColors.warning
        }
        GridView {
            id: cards
            objectName: "contextMapCards"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; cacheBuffer: 0
            cellWidth: width / Math.max(1, Math.floor(width / 240))
            cellHeight: Math.max(1, Math.min(224, height))
            model: overview.contexts
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: card
                required property var modelData
                objectName: "contextMapCard_" + modelData.id
                width: GridView.view.cellWidth - 8; height: Math.max(0, GridView.view.cellHeight - 8)
                padding: 8
                enabled: !workspace.busy
                Accessible.name: modelData.name + ", cluster " + modelData.cluster
                Accessible.description: "Open this context using the existing session loader"
                onClicked: workspace.openContext(modelData.id)
                background: Rectangle {
                    color: card.hovered || card.activeFocus ? workspace.appearanceColors.raised : workspace.appearanceColors.panel
                    border.color: card.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
                }
                contentItem: GridLayout {
                    readonly property bool horizontal: cards.cellHeight < 160
                    columns: horizontal ? 2 : 1
                    columnSpacing: 8; rowSpacing: 4
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Layout.rowSpan: parent.horizontal ? 2 : 1
                        Layout.preferredWidth: parent.horizontal ? card.availableWidth * 0.4 : -1
                        Image {
                            id: preview
                            objectName: "contextMapPreview_" + card.modelData.id
                            anchors.fill: parent
                            source: { workspace.mapPreviewRevision; return workspace.mapPreviewUrl(card.modelData.id) }
                            asynchronous: true; cache: false; fillMode: Image.PreserveAspectFit
                        }
                        Label {
                            objectName: "contextMapUnknown_" + card.modelData.id
                            anchors.centerIn: parent
                            visible: preview.source.toString() === "" || preview.status === Image.Error
                            text: "?"; font.pixelSize: 64; font.bold: true
                            color: workspace.appearanceColors.muted
                            Accessible.name: "No saved map preview"
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: parent.horizontal ? card.availableWidth * 0.6 : -1
                        Label { objectName: "contextMapName_" + card.modelData.id; Layout.fillWidth: true; Layout.minimumWidth: 0; text: card.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; maximumLineCount: 1; wrapMode: Text.NoWrap; font.bold: true }
                        IconButton {
                            objectName: "renameMapContext_" + card.modelData.id
                            glyph: "Pencil"; text: "Rename context locally"
                            onClicked: aliases.edit(card.modelData)
                        }
                    }
                    Label { objectName: "contextMapCluster_" + card.modelData.id; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: parent.horizontal ? card.availableWidth * 0.6 : -1; text: "Cluster: " + card.modelData.cluster; textFormat: Text.PlainText; elide: Text.ElideRight; maximumLineCount: 1; wrapMode: Text.NoWrap; color: workspace.appearanceColors.muted }
                }
            }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent; width: Math.min(360, parent.width - 24)
        visible: cards.count === 0
        Image { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 140; Layout.preferredHeight: 120; source: "qrc:/podlord/brand-logo.png"; fillMode: Image.PreserveAspectFit; asynchronous: true }
        Label { Layout.fillWidth: true; text: "Import a kubeconfig"; font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        Button { objectName: "mapImportSource"; Layout.alignment: Qt.AlignHCenter; text: "Open kubeconfig"; onClicked: overview.sourcesRequested() }
    }
}
