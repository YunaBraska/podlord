import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Pane {
    id: sidebar
    objectName: "workspaceSidebar"
    property bool collapsibleFilters: false
    property bool filtersExpanded: false
    property Item expandedRadarHost: null
    property bool radarVisible: true
    property bool suppressSmallRadar: false
    readonly property bool radarShown: radarVisible && (!suppressSmallRadar || expandedRadarHost !== null)
    readonly property bool compactFilterRail: collapsibleFilters && !filtersExpanded && (expandedRadarHost !== null || suppressSmallRadar)
    readonly property real collapsedFilterWidth: leftPadding + rightPadding + sidebarLayout.spacing + healthStrip.Layout.preferredWidth + filterToggle.implicitWidth
    readonly property Item mapNavigationHost: radar.navigationHost
    readonly property int count: radar.count
    readonly property int navigationIndex: radar.navigationIndex
    function navigateRows(offset) { radar.navigateRows(offset) }
    LayoutMirroring.enabled: workspace.uiRightToLeft
    LayoutMirroring.childrenInherit: true
    property string expandedField: ""
    property bool presetsExpanded: false
    property bool namingPreset: false
    readonly property string selectedSavedPreset: workspace.selectedFilterPreset !== "default" ? workspace.selectedFilterPreset : ""
    readonly property bool ownsInputFocus: activeFocus || renamePreset.visible
    function revealPresetInput() {
        const viewport=sidebarViewport.contentItem
        if (!presetName.activeFocus || !sidebar.visible || !viewport) return
        const top=presetName.mapToItem(viewport,0,0).y
        const delta=top<0 ? top : Math.max(0,top+presetName.height-viewport.height)
        if (delta!==0) viewport.contentY=Math.max(0,Math.min(viewport.contentHeight-viewport.height,viewport.contentY+delta))
    }
    onHeightChanged: if (presetName.activeFocus) Qt.callLater(revealPresetInput)
    readonly property string contextTab: workspace.currentSession
    onContextTabChanged: {
        presetName.text = ""
        presetsExpanded = false
        namingPreset = false
        expandedField = ""
        renamePreset.close()
    }
    readonly property string page: workspace.workspacePage
    onPageChanged: expandedField = ""
    onVisibleChanged: if (!visible) { expandedField = ""; presetsExpanded = false; namingPreset = false }
    function openField(field) {
        if (!workspace.prepareFilterPicker(field)) return
        filtersExpanded = true
        expandedField = field
    }
    function toggleField(field) {
        if (expandedField === field) expandedField = ""
        else openField(field)
    }
    Connections {
        target: workspace
        function onFilterPickerChanged() {
            if (workspace.filterPickerField !== sidebar.expandedField) sidebar.expandedField = ""
        }
        function onFilterPresetsChanged() {
            if (sidebar.namingPreset && !workspace.filterPresetsBusy && workspace.filterPresetsError === ""
                && workspace.selectedFilterPreset === presetName.text.trim()) {
                sidebar.namingPreset = false
                sidebar.presetsExpanded = false
            }
        }
    }
    readonly property var filterOrder: ["cluster", "namespace", "kind", "name", "status", "issue", "createdAt", "ready", "restarts", "cpu", "memory", "storage", "node", "image", "owner", "uid"]
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
    signal renameRequested()
    function focusPath(path, percent) { return radar.focusPath(path, percent) }
    padding: 6
    background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
    contentItem: RowLayout {
        id: sidebarLayout
        spacing: 6
        Rectangle {
            id: healthStrip
            objectName: "sessionHealthStrip"
            Layout.preferredWidth: 6; Layout.fillHeight: true
            color: workspace.appearanceColors.inset
            Column {
                objectName: "sessionHealthSegments"
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: parent.height * (workspace.initialSyncComplete ? 1 : Math.max(0, Math.min(0.99, workspace.loadingProgress)))
                Repeater {
                    model: [{name: "critical", count: workspace.healthSummary.critical || 0, color: "#FF5C5C"},
                        {name: "warning", count: workspace.healthSummary.warning || 0, color: "#FFE866"},
                        {name: "healthy", count: workspace.healthSummary.healthy || 0, color: "#7DFFC3"}]
                    Rectangle {
                        required property var modelData
                        objectName: "sessionHealth_" + modelData.name
                        width: parent.width
                        height: workspace.healthSummary.total > 0 ? parent.height * modelData.count / workspace.healthSummary.total : 0
                        color: modelData.color
                    }
                }
            }
            readonly property bool initiallyLoading: !workspace.initialSyncComplete && workspace.syncLoading
            Accessible.role: Accessible.StaticText
            Accessible.name: initiallyLoading
                ? workspace.uiText["status.loadingResources"] + " " + Math.round(workspace.loadingProgress * 100) + "%"
                : "Context health: " + (workspace.healthSummary.healthy || 0) + " healthy, "
                    + (workspace.healthSummary.warning || 0) + " warnings, " + (workspace.healthSummary.critical || 0) + " errors"
            ToolTip.visible: healthHover.hovered
            ToolTip.text: workspace.authenticationRunning
                ? "Authentication in progress."
                : Accessible.name
            HoverHandler { id: healthHover }
        }
        ScrollView {
            id: sidebarViewport
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; contentHeight: sidebarColumn.height
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Connections {
                target: sidebarViewport.contentItem
                function onContentYChanged() { radar.dismissInfo() }
            }
            Item {
                id: radarDetailsHost
                objectName: "radarDetailsOverlayHost"
                parent: sidebarViewport
                x: sidebarViewport.leftPadding
                y: Math.max(sidebarViewport.topPadding, sidebarViewport.topPadding + sidebarColumn.y + sidebarScroll.y - (sidebarViewport.contentItem.contentY || 0))
                width: sidebarViewport.availableWidth
                height: Math.max(0, Math.min(sidebarScroll.height, sidebarViewport.availableHeight - y + sidebarViewport.topPadding))
                visible: sidebar.visible && sidebarScroll.visible && width > 0 && height > 0
            }
        ColumnLayout {
            id: sidebarColumn
            width: sidebarViewport.availableWidth
            height: sidebar.collapsibleFilters && sidebar.filtersExpanded
                ? Math.max(implicitHeight, sidebarViewport.availableHeight) : sidebarViewport.availableHeight
            spacing: 4
            Item {
                id: radarSlot
                visible: sidebar.radarShown && sidebar.expandedRadarHost === null
                Layout.fillWidth: true
                Layout.fillHeight: sidebar.collapsibleFilters && !sidebar.filtersExpanded
                Layout.preferredHeight: Math.min(sidebar.collapsibleFilters ? 110 : 190, sidebar.height * 0.45)
                ResourceRadar {
                    id: radar
                    parent: sidebar.expandedRadarHost ?? radarSlot
                    anchors.fill: parent
                    expanded: sidebar.expandedRadarHost !== null
                    detailsHost: radarDetailsHost.visible ? radarDetailsHost : null
                    visible: sidebar.radarShown
                }
            }
            IconButton {
                id: filterToggle
                objectName: "toggleLandscapeFilters"
                visible: sidebar.collapsibleFilters
                Layout.fillWidth: true
                glyph: "Filters"; text: "Show or hide filters"
                checkable: true; checked: sidebar.filtersExpanded
                activeIndicator: workspace.filterText !== "" || workspace.problemsOnly || workspace.activityOnly || Object.keys(workspace.resourceFieldFilters).length > 0
                onClicked: sidebar.filtersExpanded = !sidebar.filtersExpanded
            }
            ScrollView {
                id: sidebarScroll
                objectName: "sidebarFilterScroll"
                visible: !sidebar.collapsibleFilters || sidebar.filtersExpanded
                Layout.fillWidth: true
                Layout.fillHeight: !sidebar.collapsibleFilters || !sidebar.filtersExpanded
                Layout.preferredHeight: sidebar.collapsibleFilters && sidebar.filtersExpanded ? sidebarControls.implicitHeight : -1
                contentWidth: availableWidth; contentHeight: sidebarControls.implicitHeight
                clip: true
                wheelEnabled: !sidebar.collapsibleFilters || !sidebar.filtersExpanded
                Binding {
                    target: sidebarScroll.contentItem
                    property: "interactive"
                    value: !sidebar.collapsibleFilters || !sidebar.filtersExpanded
                }
                ColumnLayout {
                    id: sidebarControls
                    width: sidebarScroll.availableWidth
                    spacing: 4
            RowLayout {
                Layout.fillWidth: true
                FlatButton {
                    id: sessionAction
                    objectName: "renameCurrentContext"
                    text: workspace.title
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
                    implicitHeight: Math.max(sidebar.Window.window && sidebar.Window.window.width < 900 ? 44 : 32, Math.ceil(workspace.textSizes.body * 1.3) + 12)
                    background: Rectangle {
                        color: sessionAction.hovered ? workspace.appearanceColors.raised : "transparent"
                        border.width: sessionAction.activeFocus ? 1 : 0
                        border.color: workspace.appearanceColors.accent
                    }
                    contentItem: Label { text: sessionAction.text; textFormat: Text.PlainText; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    enabled: workspace.currentSession !== "" && !workspace.busy
                    Accessible.name: workspace.uiText["context.rename"] + ": " + text
                    onClicked: renameRequested()
                }
                IconButton { objectName: "resetResourceFilters"; glyph: "Reset"; text: "Reset filters"; enabled: workspace.currentSession !== "" && (workspace.workspacePage === "events" ? workspace.eventFilterText !== "" : workspace.problemsOnly || workspace.activityOnly || workspace.filterText !== "" || Object.keys(workspace.resourceFieldFilters).length > 0); onClicked: workspace.workspacePage === "events" ? workspace.filterEvents("") : workspace.resetResourceFilters() }
                IconButton { objectName: "masterAudioMute"; glyph: workspace.alerts.muted ? "Mute" : "Volume"; text: workspace.uiText[workspace.alerts.muted ? "music.unmuteAlerts" : "music.muteAlerts"]; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(!workspace.alerts.muted, workspace.alerts.reducedMotion) }
            }
            RowLayout {
                Layout.fillWidth: true
                SelectableText { objectName: "filterTitle"; text: workspace.uiText["filters.title"]; font.bold: true; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                ScopeCheckBox { objectName: "problemsOnly"; text: workspace.uiText["filters.problems"]; Layout.fillWidth: true; checked: workspace.problemsOnly; enabled: workspace.currentSession !== ""; onClicked: workspace.setFilterMode(checked ? "problems" : "") }
                ScopeCheckBox { objectName: "activityOnly"; text: workspace.uiText["filters.activity"]; Layout.fillWidth: true; checked: workspace.activityOnly; enabled: workspace.currentSession !== ""; onClicked: workspace.setFilterMode(checked ? "activity" : "") }
            }
            RowLayout {
                Layout.fillWidth: true
                IconButton { objectName: "saveFilterPreset"; glyph: "Save"; text: workspace.uiText["action.save"]; enabled: !workspace.filterPresetsBusy && workspace.currentSession !== ""; onClicked: {
                    sidebar.presetsExpanded = true
                    if (!sidebar.namingPreset) {
                        sidebar.namingPreset = true
                        presetName.text = ""
                        Qt.callLater(function() { presetName.forceActiveFocus() })
                    } else if (presetName.text.trim() !== "") workspace.saveFilterPreset(presetName.text)
                } }
                IconButton {
                    id: preset; objectName: "filterPreset"
                    Layout.fillWidth: true; showText: true
                    visible: workspace.filterPresets.length > 1
                    glyph: sidebar.presetsExpanded ? "Expand" : "Next"; text: sidebar.presetsExpanded ? workspace.uiText["filters.savedFilters"] : sidebar.selectedSavedPreset || workspace.uiText["filters.savedFilters"]
                    checkable: true; checked: sidebar.presetsExpanded
                    background: Rectangle {
                        color: preset.hovered ? workspace.appearanceColors.raised : "transparent"
                        border.width: preset.activeFocus ? 1 : 0
                        border.color: workspace.appearanceColors.accent
                    }
                    Accessible.description: checked ? "Expanded saved filters" : "Collapsed saved filters"
                    enabled: !workspace.filterPresetsBusy && workspace.currentSession !== ""
                    onClicked: {
                        sidebar.presetsExpanded = !sidebar.presetsExpanded
                        if (sidebar.presetsExpanded) Qt.callLater(function() { if (sidebar.presetsExpanded && savedFilters.visible) savedFilters.forceActiveFocus() })
                    }
                }
            }
            ColumnLayout {
                objectName: "savedFilterPanel"
                Layout.fillWidth: true
                spacing: 4
                visible: sidebar.presetsExpanded
                enabled: visible
                TextField { id: presetName; objectName: "filterPresetName"; Layout.fillWidth: true; Layout.minimumWidth: 0; visible: sidebar.namingPreset; placeholderText: workspace.uiText["alert.field.name"]; maximumLength: 128; Accessible.name: "Saved filter name"; onAccepted: workspace.saveFilterPreset(text); onActiveFocusChanged: if (activeFocus) Qt.callLater(sidebar.revealPresetInput) }
            ListView {
                id: savedFilters
                objectName: "savedFilterOptions"
                readonly property int rowHeight: Math.max(sidebar.Window.window && sidebar.Window.window.width < 900 ? 44 : 32, Math.ceil(workspace.textSizes.body * 1.3) + 12)
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Math.min(180, count * rowHeight) : 0
                visible: sidebar.presetsExpanded
                clip: true
                interactive: contentHeight > height
                keyNavigationEnabled: true
                model: visible ? workspace.filterPresets.filter(name => name !== "default") : []
                currentIndex: Math.max(0, model.indexOf(workspace.selectedFilterPreset))
                ScrollBar.vertical: ScrollBar {}
                function activate(index) {
                    if (index < 0 || index >= count) return
                    const name = savedFilters.model[index]
                    if (workspace.loadFilterPreset(name)) {
                        presetName.text = ""
                        sidebar.namingPreset = false
                        sidebar.presetsExpanded = false
                        preset.forceActiveFocus()
                    }
                }
                Keys.onReturnPressed: activate(currentIndex)
                Keys.onEnterPressed: activate(currentIndex)
                Keys.onEscapePressed: { sidebar.presetsExpanded = false; preset.forceActiveFocus() }
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    objectName: "filterPresetOption_" + index
                    width: savedFilters.width
                    height: savedFilters.rowHeight
                    topPadding: 0; bottomPadding: 0; leftPadding: 6; rightPadding: 6
                    focusPolicy: Qt.StrongFocus
                    text: modelData
                    highlighted: modelData === workspace.selectedFilterPreset
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    contentItem: RowLayout {
                        spacing: 4
                        Label { text: modelData; Layout.fillWidth: true; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        IconButton { objectName: "renameFilterPreset"; glyph: "Pencil"; text: "Rename saved filter"; visible: modelData === sidebar.selectedSavedPreset; enabled: !workspace.filterPresetsBusy; onClicked: { renamePreset.original = modelData; renameText.text = modelData; renamePreset.open() } }
                        IconButton { objectName: "deleteFilterPreset"; glyph: "Trash"; destructive: true; text: "Delete saved filter"; visible: modelData === sidebar.selectedSavedPreset; enabled: !workspace.filterPresetsBusy; onClicked: workspace.deleteFilterPreset(modelData) }
                    }
                    onClicked: savedFilters.activate(index)
                }
            }
            }
            SelectableText { objectName: "filterPresetsError"; Layout.fillWidth: true; text: workspace.filterPresetsError; visible: text.length > 0; wrapMode: Text.Wrap; textFormat: Text.PlainText; Accessible.name: text }
            ColumnLayout {
                id: filters
                objectName: "sidebarFilters"
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                model: { const fields = workspace.filterFields; return sidebar.filterOrder.map(function(id) { return fields.find(function(field) { return field.id === id }) }).filter(function(field) { return field !== undefined }) }
                delegate: ColumnLayout {
                    id: fieldGroup
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    FlatButton {
                    id: fieldButton
                    readonly property var modelData: fieldGroup.modelData
                    objectName: "sidebarField_" + modelData.id
                    Layout.fillWidth: true; implicitHeight: Math.max(sidebar.Window.window && sidebar.Window.window.width < 900 ? 44 : 30, Math.ceil(workspace.textSizes.body * 1.3) + 12)
                    text: modelData.name + (workspace.resourceFieldFilters[modelData.id] ? ": " + workspace.resourceFieldFilters[modelData.id] : "")
                    leftPadding: 10; rightPadding: 10
                    font.bold: true
                    checkable: true
                    checked: sidebar.expandedField === modelData.id
                    Accessible.description: checked ? "Expanded filter values" : "Collapsed filter values"
                    background: Rectangle {
                        color: fieldButton.down || fieldButton.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.panel
                        border.width: fieldButton.activeFocus ? 1 : 0
                        border.color: workspace.appearanceColors.accent
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        KindGlyph {
                            Layout.preferredWidth: 16; Layout.preferredHeight: 16
                            kind: ({cluster: "Cluster", namespace: "Namespace", kind: "CustomResourceDefinition", name: "Pod", status: "Event", issue: "Event", createdAt: "CronJob", ready: "Service", restarts: "Event", cpu: "Node", memory: "ConfigMap", storage: "PersistentVolume", node: "Node", image: "ConfigMap", owner: "Deployment", uid: "Secret"})[fieldButton.modelData.id]
                            fill: workspace.appearanceColors.accent
                        }
                        Label { Layout.fillWidth: true; text: fieldButton.text; textFormat: Text.PlainText; font: fieldButton.font; elide: Text.ElideRight }
                        Label { text: fieldButton.checked ? "▾" : "▸"; color: workspace.appearanceColors.accent }
                    }
                    enabled: workspace.currentSession !== "" && !workspace.busy
                    Accessible.name: "Filter " + modelData.name
                    ToolTip.visible: hovered; ToolTip.text: text
                    onClicked: sidebar.toggleField(modelData.id)
                    }
                    Loader {
                        Layout.fillWidth: true
                        active: sidebar.expandedField === fieldGroup.modelData.id
                        visible: active
                        sourceComponent: FieldFilterEditor {
                            controller: workspace
                            onDismissed: { sidebar.expandedField = ""; fieldButton.forceActiveFocus() }
                        }
                    }
                }
                }
            }
                }
            }
        }
    }
        }
    ModalDialog {
        id: renamePreset
        objectName: "renameFilterPresetDialog"
        property string original
        readonly property bool keyboardEditing: sidebar.Window.window && sidebar.Window.window.inputPriority
        title: "Rename filter"
        modal: true
        parent: Overlay.overlay
        padding: keyboardEditing ? 0 : 12
        header: DialogHeader { dialog: renamePreset; visible: !renamePreset.keyboardEditing }
        width: sidebar.Window.window ? Math.min(360, sidebar.Window.window.width - 24) : 360
        x: parent ? (parent.width-width)/2 : 0
        y: Math.max(sidebar.Window.window ? sidebar.Window.window.topPadding : 0,
            ((sidebar.Window.window ? sidebar.Window.window.keyboardTop : parent.height)-height)/2)
        standardButtons: keyboardEditing ? Dialog.NoButton : Dialog.Ok | Dialog.Cancel
        TextField { id: renameText; objectName: "renameFilterPresetName"; width: parent.width; maximumLength: 128; Accessible.name: "New saved filter name"; onAccepted: renamePreset.accept() }
        onAccepted: workspace.renameFilterPreset(original, renameText.text)
    }
}
