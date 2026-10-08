import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Pane {
    id: sidebar
    LayoutMirroring.enabled: workspace.uiRightToLeft
    LayoutMirroring.childrenInherit: true
    SessionManager { id: sessionManager }
    readonly property var filterOrder: ["cluster", "namespace", "kind", "name", "status", "issue", "createdAt", "ready", "restarts", "cpu", "memory", "storage", "node", "image", "owner", "uid"]
    signal fieldRequested(string field, var anchor)
    component ScopeCheckBox: CheckBox {
        id: scopeControl
        indicator: Rectangle {
            x: scopeControl.mirrored ? scopeControl.width - width - scopeControl.rightPadding : scopeControl.leftPadding
            y: (scopeControl.height - height) / 2
            width: 18; height: 18
            color: scopeControl.checked ? workspace.appearanceColors.accent : workspace.appearanceColors.inset
            border.color: scopeControl.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
            Label { anchors.centerIn: parent; text: scopeControl.checked ? "✓" : ""; color: workspace.appearanceColors.inset }
        }
    }
    signal sourcesRequested()
    signal renameRequested()
    signal filtersRequested()
    signal filtersScrolled()
    function focusPath(path, percent) { return radar.focusPath(path, percent) }
    padding: 6
    background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
    contentItem: RowLayout {
        spacing: 6
        Rectangle {
            objectName: "sessionHealthStrip"
            Layout.preferredWidth: 6; Layout.fillHeight: true
            color: workspace.appearanceColors.inset
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: parent.height * workspace.loadingProgress; color: sidebar.palette.text; opacity: 0.15 }
            Column {
                objectName: "sessionHealthSegments"
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: parent.height * workspace.loadingProgress
                Behavior on height { enabled: !workspace.alerts.reducedMotion; NumberAnimation { duration: 180 } }
                Repeater {
                    model: [{name: "critical", count: workspace.healthSummary.critical || 0, color: "#FF5C5C"},
                        {count: workspace.healthSummary.warning || 0, color: "#FFE866"},
                        {count: workspace.healthSummary.healthy || 0, color: "#7DFFC3"}]
                    Rectangle { required property var modelData; width: parent.width; height: workspace.healthSummary.total > 0 ? parent.height * modelData.count / workspace.healthSummary.total : 0; color: modelData.color }
                }
            }
            Accessible.name: (workspace.syncLoading ? "Loading session. " : "Session health: ") + (workspace.healthSummary.healthy || 0) + " healthy, " + (workspace.healthSummary.warning || 0) + " warnings, " + (workspace.healthSummary.critical || 0) + " errors"
            ToolTip.visible: healthHover.hovered
            ToolTip.text: workspace.authenticationRunning
                ? (workspace.loadingProgress < 1 ? "Authentication in progress; loading percentage is not yet known." : "Authentication in progress; cached health is retained.")
                : workspace.syncLoading
                    ? (workspace.loadingProgress < 1 ? "Loading collections; progress grows as requests finish." : "Updating the cached session; health is retained.")
                    : Accessible.name
            HoverHandler { id: healthHover }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
            ResourceRadar { id: radar; compact: true; Layout.fillWidth: true; Layout.preferredHeight: Math.min(190, sidebar.height * 0.45) }
            ScrollView {
                id: sidebarScroll
                objectName: "sidebarFilterScroll"
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth; contentHeight: sidebarControls.implicitHeight
                clip: true
                Connections {
                    target: sidebarScroll.contentItem
                    function onContentYChanged() { sidebar.filtersScrolled() }
                }
                ColumnLayout {
                    id: sidebarControls
                    width: sidebarScroll.availableWidth
                    spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Button {
                    id: sourceAction
                    objectName: "sourcesButton"
                    text: workspace.title || "Sources"
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
                    implicitHeight: sidebar.Window.window && sidebar.Window.window.width < 900 ? 44 : 32
                    contentItem: Label { text: sourceAction.text; textFormat: Text.PlainText; elide: Text.ElideRight }
                    Accessible.name: "Show or hide kubeconfig sources"
                    onClicked: sourcesRequested()
                }
                IconButton { objectName: "masterAudioMute"; glyph: workspace.alerts.muted ? "Mute" : "Volume"; text: workspace.alerts.muted ? "Unmute" : "Mute"; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(!workspace.alerts.muted, workspace.alerts.reducedMotion) }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { objectName: "filterTitle"; text: workspace.uiText["filters.title"]; font.bold: true; Layout.fillWidth: true }
                IconButton { objectName: "resourceFieldFilters"; glyph: "Filters"; text: "Resource field filters"; enabled: workspace.currentSession !== "" && !workspace.busy; onClicked: filtersRequested() }
                IconButton { objectName: "resetResourceFilters"; glyph: "Reset"; text: "Reset filters"; enabled: workspace.workspacePage === "events" ? workspace.eventFilterText !== "" : workspace.problemsOnly || workspace.activityOnly || workspace.filterText !== "" || Object.keys(workspace.resourceFieldFilters).length > 0; onClicked: workspace.workspacePage === "events" ? workspace.filterEvents("") : workspace.resetResourceFilters() }
            }
            RowLayout {
                ScopeCheckBox { objectName: "problemsOnly"; text: workspace.uiText["filters.problems"]; checked: workspace.problemsOnly; enabled: workspace.currentSession !== ""; onClicked: workspace.setFilterMode(checked ? "problems" : "") }
                ScopeCheckBox { objectName: "activityOnly"; text: workspace.uiText["filters.activity"]; checked: workspace.activityOnly; enabled: workspace.currentSession !== ""; onClicked: workspace.setFilterMode(checked ? "activity" : "") }
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox { id: preset; objectName: "filterPreset"; Layout.fillWidth: true; Layout.minimumWidth: 0; model: workspace.filterPresets; currentIndex: model.indexOf(workspace.selectedFilterPreset); displayText: currentIndex < 0 ? "Custom filter" : currentText; Accessible.name: "Load saved filter"; enabled: !workspace.filterPresetsBusy && workspace.currentSession !== ""; onActivated: { workspace.loadFilterPreset(currentText); presetName.text = currentText === "default" ? "" : currentText } }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: presetName; objectName: "filterPresetName"; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: workspace.uiText["filters.namePlaceholder"]; maximumLength: 128; Accessible.name: "Saved filter name"; onAccepted: workspace.saveFilterPreset(text) }
                Button { objectName: "saveFilterPreset"; text: workspace.uiText["action.save"]; implicitHeight: 28; enabled: !workspace.filterPresetsBusy && presetName.text.trim().length > 0 && workspace.currentSession !== ""; onClicked: workspace.saveFilterPreset(presetName.text) }
                IconButton { objectName: "filterPresetActions"; glyph: "Menu"; text: "Saved filter actions"; enabled: !workspace.filterPresetsBusy; onClicked: presetActions.open() }
                Menu { id: presetActions
                    MenuItem { objectName: "reloadFilterPresets"; text: "Reload saved filters"; enabled: !workspace.filterPresetsBusy; onTriggered: workspace.reloadFilterPresets() }
                    MenuItem { objectName: "importFilterPresets"; text: "Import saved filters..."; onTriggered: importPresets.open() }
                    MenuItem { objectName: "renameFilterPreset"; text: "Rename"; enabled: presetName.text !== "default" && workspace.filterPresets.indexOf(presetName.text) >= 0; onTriggered: { renamePreset.original = presetName.text; renameText.text = presetName.text; renamePreset.open() } }
                    MenuItem { objectName: "deleteFilterPreset"; text: "Delete"; enabled: presetName.text !== "default" && workspace.filterPresets.indexOf(presetName.text) >= 0; onTriggered: workspace.deleteFilterPreset(presetName.text) }
                }
            }
            Label { objectName: "filterPresetsError"; Layout.fillWidth: true; text: workspace.filterPresetsError; visible: text.length > 0; wrapMode: Text.Wrap; textFormat: Text.PlainText; Accessible.name: text }
            FileDialog {
                id: importPresets
                title: "Import saved filters"
                nameFilters: ["Saved filters (*.json)"]
                fileMode: FileDialog.OpenFile
                onAccepted: workspace.importFilterPresets(selectedFile)
            }
            ListView {
                id: filters
                objectName: "sidebarFilters"
                Layout.fillWidth: true; Layout.preferredHeight: count * (sidebar.Window.window && sidebar.Window.window.width < 900 ? 44 : 30)
                model: { const fields = workspace.filterFields; return sidebar.filterOrder.map(function(id) { return fields.find(function(field) { return field.id === id }) }).filter(function(field) { return field !== undefined }) }
                clip: true; reuseItems: true; cacheBuffer: 0
                interactive: false
                delegate: Button {
                    id: fieldButton
                    required property var modelData
                    objectName: "sidebarField_" + modelData.id
                    width: filters.width; height: sidebar.Window.window && sidebar.Window.window.width < 900 ? 44 : 30
                    text: modelData.name + (workspace.resourceFieldFilters[modelData.id] ? ": " + workspace.resourceFieldFilters[modelData.id] : "")
                    leftPadding: 10; rightPadding: 10
                    font.bold: true
                    background: Rectangle {
                        color: fieldButton.down || fieldButton.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.panel
                        border.color: fieldButton.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        KindGlyph {
                            Layout.preferredWidth: 16; Layout.preferredHeight: 16
                            kind: ({cluster: "Cluster", namespace: "Namespace", kind: "CustomResourceDefinition", name: "Pod", status: "Event", issue: "Event", createdAt: "CronJob", ready: "Service", restarts: "Event", cpu: "Node", memory: "ConfigMap", storage: "PersistentVolume", node: "Node", image: "ConfigMap", owner: "Deployment", uid: "Secret"})[fieldButton.modelData.id]
                            fill: workspace.appearanceColors.accent
                        }
                        Label { Layout.fillWidth: true; text: fieldButton.text; textFormat: Text.PlainText; font: fieldButton.font; elide: Text.ElideRight }
                        Label { text: "▾"; color: workspace.appearanceColors.accent }
                    }
                    enabled: workspace.currentSession !== "" && !workspace.busy
                    Accessible.name: "Filter " + modelData.name
                    ToolTip.visible: hovered; ToolTip.text: text
                    onClicked: fieldRequested(modelData.id, fieldButton)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox { Layout.fillWidth: true; Layout.minimumWidth: 0; model: workspace.sessions; textRole: "name"; valueRole: "id"; displayText: count > 0 ? currentText : "No saved sessions"; Accessible.name: "Saved sessions, ordered by usage"; onActivated: workspace.activate(currentValue) }
                IconButton { objectName: "renameCurrentSession"; glyph: "Pencil"; text: "Rename session"; enabled: workspace.currentSession !== "" && !workspace.busy; onClicked: renameRequested() }
                IconButton { objectName: "manageSidebarSessions"; glyph: "Menu"; text: "Manage sessions"; enabled: !workspace.busy && workspace.sessions.length > 0; onClicked: sessionManager.open() }
            }
                }
            }
        }
    }
    Dialog { id: renamePreset; property string original; title: "Rename filter"; modal: true; anchors.centerIn: Overlay.overlay; width: sidebar.Window.window ? Math.min(360, sidebar.Window.window.width - 24) : 360; standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: renameText; objectName: "renameFilterPresetName"; width: parent.width; maximumLength: 128; Accessible.name: "New saved filter name" }
        onAccepted: { if (workspace.renameFilterPreset(original, renameText.text)) presetName.text = renameText.text.trim() }
    }
}
