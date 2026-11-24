import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: overview
    property var contexts: []
    readonly property var groups: {
        const result = []
        const byId = {}
        for (const context of contexts) {
            if (!context.groupId) continue
            if (!byId[context.groupId]) {
                byId[context.groupId] = {id: context.groupId, name: context.groupName, contexts: []}
                result.push(byId[context.groupId])
            }
            byId[context.groupId].contexts.push(context)
        }
        return result
    }
    readonly property var ungroupedContexts: contexts.filter(context => !context.groupId)
    readonly property real cardWidth: width / Math.max(1, Math.floor(width / 240))
    readonly property real cardHeight: Math.max(1, Math.min(224, height))
    readonly property var groupRows: {
        const rows = []
        const columns = Math.max(1, Math.floor(width / 240))
        function appendCards(contexts) {
            for (let index = 0; index < contexts.length; index += columns)
                rows.push({heading: false, contexts: contexts.slice(index, index + columns)})
        }
        for (const group of groups) {
            rows.push({heading: true, id: group.id, name: group.name, contexts: []})
            appendCards(group.contexts.filter(context => context.groupRole === "management"))
            appendCards(group.contexts.filter(context => context.groupRole === "workload"))
        }
        appendCards(ungroupedContexts)
        return rows
    }
    padding: 4
    background: Rectangle { color: workspace.appearanceColors.inset }
    SourceAliasDialog { id: aliases }
    Menu {
        id: contextOpenMenu
        objectName: "contextOpenMenu"
        property string contextId: ""
        MenuItem { objectName: "openMapContextInTab"; enabled: !workspace.busy && overview.contexts.some(context => context.id === contextOpenMenu.contextId); text: workspace.uiText["action.openInTab"]; onTriggered: workspace.openContext(contextOpenMenu.contextId, false, true) }
        MenuItem { objectName: "openMapContextInWindow"; enabled: !workspace.busy && overview.contexts.some(context => context.id === contextOpenMenu.contextId); text: workspace.uiText["action.openInWindow"]; visible: workspace.windowActionsAvailable; height: visible ? implicitHeight : 0; onTriggered: workspace.openContext(contextOpenMenu.contextId, true) }
        MenuItem { objectName: "renameMapContext_" + contextOpenMenu.contextId; text: workspace.uiText["context.rename"]; onTriggered: { const context = overview.contexts.find(entry => entry.id === contextOpenMenu.contextId); if (context) aliases.edit(context) } }
        MenuItem {
            objectName: "removeMapContext_" + contextOpenMenu.contextId
            text: workspace.uiText["settings.remove"]
            contentItem: Label { text: parent.text; color: workspace.appearanceColors.danger; font: parent.font; verticalAlignment: Text.AlignVCenter }
            onTriggered: workspace.requestSourceRemoval(contextOpenMenu.contextId)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        SelectableText {
            objectName: "mapPreviewError"
            Layout.fillWidth: true
            text: workspace.mapPreviewError
            visible: text !== ""; color: workspace.appearanceColors.warning
        }
        GridView {
            id: cards
            objectName: visible ? "contextMapCards" : ""
            visible: overview.groups.length === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; cacheBuffer: 0
            cellWidth: overview.cardWidth
            cellHeight: overview.cardHeight
            model: overview.groups.length === 0 ? overview.contexts : []
            ScrollBar.vertical: ScrollBar {}
            delegate: contextCard
        }
        ListView {
            id: groupedCards
            objectName: visible ? "contextMapCards" : ""
            visible: overview.groups.length > 0
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; cacheBuffer: 0; spacing: 8
            model: visible ? overview.groupRows : []
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: groupRow
                required property var modelData
                width: groupedCards.width
                height: modelData.heading ? 28 : overview.cardHeight
                SelectableText {
                    objectName: groupRow.modelData.heading ? "contextGroup_" + groupRow.modelData.id : ""
                    visible: groupRow.modelData.heading
                    width: parent.width
                    text: groupRow.modelData.name || ""
                    font.bold: true
                    color: workspace.appearanceColors.accent
                }
                Row {
                    visible: !groupRow.modelData.heading
                    Repeater { model: groupRow.modelData.contexts; delegate: contextCard }
                }
            }
        }
        Component {
            id: contextCard
            ItemDelegate {
                id: card
                required property var modelData
                objectName: "contextMapCard_" + modelData.id
                width: overview.cardWidth - 8; height: Math.max(0, overview.cardHeight - 8)
                padding: 8
                enabled: !workspace.busy
                readonly property string openSessionId: modelData.openSessionId || ""
                Accessible.name: modelData.name + ", cluster " + modelData.cluster
                Accessible.description: workspace.uiText["action.openInTab"]
                onClicked: workspace.openContext(modelData.id)
                background: Rectangle {
                    color: card.hovered || card.activeFocus ? workspace.appearanceColors.raised
                        : card.openSessionId ? workspace.appearanceColors.selection : workspace.appearanceColors.panel
                    border.width: card.openSessionId ? 2 : 1
                    border.color: card.openSessionId || card.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
                }
                contentItem: GridLayout {
                    readonly property bool horizontal: overview.cardHeight < 160
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
                            Accessible.name: workspace.uiText["context.noPreview"]
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: parent.horizontal ? card.availableWidth * 0.6 : -1
                        Label {
                            visible: card.modelData.groupRole === "workload"
                            text: "↳"
                            color: workspace.appearanceColors.accent
                            Accessible.ignored: true
                        }
                        SelectableText {
                            objectName: "contextMapName_" + card.modelData.id
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: card.modelData.name; font.bold: true
                            wrapMode: TextEdit.NoWrap; clip: true
                            TapHandler { gesturePolicy: TapHandler.DragThreshold; onTapped: card.clicked() }
                        }
                        IconButton {
                            id: openContextOptions
                            objectName: "openMapContextOptions_" + card.modelData.id
                            glyph: "Expand"; text: workspace.uiText["action.more"] + ": " + card.modelData.name
                            onClicked: { contextOpenMenu.contextId = card.modelData.id; contextOpenMenu.popup(openContextOptions, 0, openContextOptions.height) }
                        }
                        IconButton {
                            objectName: "closeMapContext_" + card.modelData.id
                            visible: card.openSessionId !== ""
                            glyph: "Close"; text: workspace.uiText["action.close"] + " " + card.modelData.name
                            onClicked: workspace.close(card.openSessionId)
                        }
                    }
                    SelectableText {
                        objectName: "contextMapCluster_" + card.modelData.id
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.preferredWidth: parent.horizontal ? card.availableWidth * 0.6 : -1
                        text: "Cluster: " + card.modelData.cluster
                        wrapMode: TextEdit.NoWrap; clip: true; color: workspace.appearanceColors.muted
                        TapHandler { gesturePolicy: TapHandler.DragThreshold; onTapped: card.clicked() }
                    }
                }
            }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent; width: Math.min(360, parent.width - 24)
        visible: overview.contexts.length === 0 && !workspace.busy
        Image { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 140; Layout.preferredHeight: 120; source: "qrc:/podlord/brand-logo.png"; fillMode: Image.PreserveAspectFit; asynchronous: true }
        SelectableText { Layout.fillWidth: true; text: workspace.uiText["context.emptyTitle"]; font.pixelSize: workspace.textSizes.emptyTitle; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
    }
    BusyIndicator { anchors.centerIn: parent; running: visible; visible: overview.contexts.length === 0 && workspace.busy }
}
