import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Podlord.Graphics 1.0

Pane {
    id: inspectorRoot
    readonly property var hostWindow: ApplicationWindow.window
    padding: 6
    implicitHeight: inspector.Layout.minimumHeight + topPadding + bottomPadding
    clip: true
    component InspectorPage: Loader {
        required property bool selected
        readonly property bool inspectorOpen: workspace.inspectorPath !== ""
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: item ? item.implicitHeight : 0
        active: false
        visible: selected
        onSelectedChanged: if (selected && inspectorOpen) active = true
        onInspectorOpenChanged: {
            if (!inspectorOpen) active = false
            else if (selected) active = true
        }
        Component.onCompleted: if (selected && inspectorOpen) active = true
    }

    component InspectorTab: WorkspaceTab {
        width: Math.max(44, Math.floor(inspector.width /
            (inspector.width < 600 ? Math.min(4, inspector.visibleTabCount) : inspector.visibleTabCount)))
    }
    background: Rectangle {
        color: workspace.appearanceColors.panel
        border.width: 1
        border.color: workspace.appearanceColors.glow
    }
    contentItem: ScrollView {
    id: inspectorScroll
    clip: true
    contentWidth: availableWidth
    contentHeight: inspector.height
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
    id: inspector
    width: inspectorScroll.availableWidth
    height: workspace.inspectorPage === "ai" ? implicitHeight : Math.max(implicitHeight, inspectorScroll.availableHeight)
    readonly property int visibleTabCount: 4 + (workspace.incidentAiSupported ? 1 : 0)
        + (workspace.valuesAvailable ? 1 : 0) + (workspace.podInspected ? 1 : 0)
        + (workspace.filesSupported ? 1 : 0)
        + (workspace.podInspected || workspace.containerTerminal !== null ? 1 : 0)
    property real yamlPosition: 0
    property real overviewPosition: 0
    property real valuesPosition: 0
    property string previousScope: ""
    property string previousPage: ""
    onWidthChanged: if (width > 0 && width < 600 && workspace.inspectorPage === "terminal") inspectorRoot.hostWindow.openContainerTerminal(true)
    function restorePosition() {
        const same = previousScope === workspace.inspectorScope
        const yamlPane = yamlPage.item ? yamlPage.item.scrollContent : null
        if (yamlPane)
            yamlPane.contentY = same ? Math.min(yamlPosition, Math.max(0, yamlPane.contentHeight - yamlPane.height)) : 0
        overviewScroll.contentY = same ? Math.min(overviewPosition, Math.max(0, overviewScroll.contentHeight - overviewScroll.height)) : 0
        const valuesPane = valuesPage.item
        if (valuesPane)
            valuesPane.contentY = same ? Math.min(valuesPosition, Math.max(0, valuesPane.contentHeight - valuesPane.viewportHeight)) : 0
    }
    Connections {
        target: workspace
        function onInspectorPresentationChanging() {
            inspector.previousScope = workspace.inspectorScope
            inspector.yamlPosition = (yamlPage.item ? yamlPage.item.scrollContent.contentY : 0)
            inspector.overviewPosition = overviewScroll.contentY
            inspector.valuesPosition = (valuesPage.item ? valuesPage.item.contentY : 0)
        }
        function onInspectorPresentationChanged() { Qt.callLater(inspector.restorePosition) }
        function onChanged() {
            const page = workspace.inspectorPage
            if (page === "terminal" && page !== inspector.previousPage && inspector.width < 600) inspectorRoot.hostWindow.openContainerTerminal(true)
            inspector.previousPage = page
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        IconButton { objectName: "inspectorBack"; glyph: "Previous"; text: workspace.uiText["tooltip.previousResource"]; enabled: workspace.canInspectBack; onClicked: workspace.navigateInspector(-1) }
        IconButton { objectName: "inspectorForward"; glyph: "Next"; text: workspace.uiText["tooltip.nextResource"]; enabled: workspace.canInspectForward; onClicked: workspace.navigateInspector(1) }
        SelectableText { objectName: "inspectorResourceName"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: (workspace.overviewFields.find(field => field.id === "kind")?.value || "") + "/" + workspace.inspectorName; textFormat: Text.PlainText; font.bold: true; wrapMode: TextEdit.NoWrap; clip: true; Accessible.name: "Inspecting " + text }
        IconButton { objectName: "refreshInspector"; glyph: "Reset"; text: "Refresh detail"; enabled: !workspace.authenticationRequired; onClicked: workspace.refreshInspector() }
        IconButton { objectName: "deleteResource"; glyph: "Trash"; destructive: true; text: "Review deletion of " + workspace.inspectorName; enabled: workspace.canDeleteResource; onClicked: workspace.previewDeletion() }
        IconButton { objectName: "preparePortForward"; glyph: "Service"; text: "Port forward " + workspace.inspectorName; visible: workspace.canPortForward; enabled: workspace.canPortForward; onClicked: workspace.preparePortForward() }
        IconButton { objectName: "closeInspector"; glyph: "Close"; text: "Close inspector"; onClicked: workspace.closeInspector() }
    }
    Flow {
        spacing: 0
        Layout.fillWidth: true
        InspectorTab { objectName: "overviewButton"; text: workspace.uiText["inspector.overview"]; checked: workspace.inspectorPage === "overview"; onClicked: workspace.setInspectorPage("overview") }
        InspectorTab { objectName: "inspectorAiButton"; text: workspace.uiText["settings.ai"]; checked: workspace.inspectorPage === "ai"; visible: workspace.incidentAiSupported; onClicked: workspace.setInspectorPage("ai") }
        InspectorTab { objectName: "yamlButton"; text: workspace.uiText["inspector.yaml"]; checked: workspace.inspectorPage === "yaml"; onClicked: workspace.setInspectorPage("yaml") }
        InspectorTab { objectName: "inspectorEventsButton"; text: workspace.uiText["inspector.events"]; checked: workspace.inspectorPage === "events"; onClicked: workspace.setInspectorPage("events") }
        InspectorTab { objectName: "inspectorLinksButton"; text: workspace.uiText["inspector.links"]; checked: workspace.inspectorPage === "links"; onClicked: workspace.setInspectorPage("links") }
        InspectorTab { objectName: "valuesButton"; text: workspace.uiText["inspector.values"]; checked: workspace.inspectorPage === "values"; visible: workspace.valuesAvailable; onClicked: workspace.setInspectorPage("values") }
        InspectorTab { objectName: "logsButton"; text: workspace.uiText["inspector.logs"]; checked: workspace.inspectorPage === "logs"; visible: workspace.podInspected; onClicked: workspace.setLogsVisible(true) }
        InspectorTab { objectName: "filesButton"; text: workspace.uiText["files.tab"]; checked: workspace.inspectorPage === "files"; visible: workspace.filesSupported; onClicked: workspace.setInspectorPage("files") }
        InspectorTab { objectName: "terminalButton"; text: "Terminal"; checked: workspace.inspectorPage === "terminal"; visible: workspace.podInspected || workspace.containerTerminal !== null; onClicked: workspace.setInspectorPage("terminal") }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: (workspace.yamlVisible && !workspace.yamlEditing) || workspace.inspectorPage === "terminal" || workspace.canReadBackDeletion
        IconButton { objectName: "copyYaml"; glyph: "ConfigMap"; text: "Copy visible YAML"; visible: workspace.yamlVisible && !workspace.yamlEditing; enabled: !workspace.yamlEditing && workspace.yamlText !== ""; onClicked: workspace.copyYaml() }
        IconButton { objectName: "expandTerminal"; glyph: "Sidebar"; text: "Expand terminal"; visible: workspace.inspectorPage === "terminal"; onClicked: inspectorRoot.hostWindow.openContainerTerminal(true) }
        IconButton { objectName: "readBackResourceDelete"; glyph: "Reset"; text: "Read back deletion target"; visible: workspace.canReadBackDeletion; onClicked: workspace.readBackDeletion() }
    }
    SelectableText {
        objectName: "inspectorReadStatus"
        Layout.fillWidth: true
        visible: workspace.inspectorPage === "overview" || workspace.inspectorPage === "yaml"
            || workspace.inspectorPage === "values"
        text: workspace.inspectorStatus
        color: workspace.appearanceColors.muted
    }
    SelectableText { objectName: "resourceDeleteStatus"; Layout.fillWidth: true; text: workspace.deletionStatus; visible: text !== "" }
    SelectableText { Layout.fillWidth: true; text: workspace.portForwardError; visible: text !== "" }
    ColumnLayout {
        id: aiPanel
        Layout.fillWidth: true
        visible: workspace.incidentAiSupported && workspace.inspectorPage === "ai" && workspace.inspectorPath !== ""
        spacing: 5
        onVisibleChanged: if (visible) workspace.ensureIncidentAiModels()
        Component.onCompleted: if (visible) workspace.ensureIncidentAiModels()
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "incidentAiReanalyze"
                glyph: "Reset"
                text: workspace.incidentAiResult.summary === undefined ? workspace.uiText["ai.analyze"] : workspace.uiText["ai.reanalyze"]
                enabled: workspace.incidentAiModel !== "" && workspace.incidentAiReady && workspace.incidentAiResult.pending !== true
                    && !workspace.authenticationRequired
                onClicked: workspace.refreshIncidentAiAnalysis()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: workspace.incidentAiModel === ""
            SelectableText { objectName: "incidentAiModelMissing"; text: workspace.uiText["ai.modelMissing"]; color: workspace.appearanceColors.warning }
            IconButton { objectName: "incidentAiOpenSettings"; glyph: "Settings"; text: workspace.uiText["ai.chooseModel"]; onClicked: inspectorRoot.hostWindow.openAiSettings() }
            Item { Layout.fillWidth: true }
        }
        SelectableText {
            objectName: "incidentAiStatus"
            Layout.fillWidth: true
            visible: workspace.incidentAiModel !== "" && workspace.incidentAiResult.pending !== true && text !== ""
            text: workspace.incidentAiStatus
            color: workspace.appearanceColors.muted
        }
        SelectableText {
            objectName: "incidentAiPending"
            Layout.fillWidth: true
            visible: workspace.incidentAiResult.pending === true
            text: workspace.uiText["ai.pending"]
            color: workspace.appearanceColors.muted
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            visible: workspace.incidentAiResult.summary !== undefined
            spacing: 4
            SelectableText { objectName: "incidentAiObservation"; Layout.fillWidth: true; text: workspace.uiText["ai.observation"]; font.bold: true; color: workspace.appearanceColors.accent }
            SelectableText { objectName: "incidentAiSummary"; Layout.fillWidth: true; text: workspace.incidentAiResult.summary || "" }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            visible: (workspace.incidentAiResult.cause || "") !== ""
            spacing: 4
            SelectableText { objectName: "incidentAiCauseHeading"; Layout.fillWidth: true; text: workspace.uiText["ai.possibleCause"]; font.bold: true; color: workspace.appearanceColors.accent }
            SelectableText { objectName: "incidentAiCause"; Layout.fillWidth: true; text: workspace.incidentAiResult.cause || "" }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            visible: workspace.incidentAiResult.action !== undefined
            spacing: 4
            SelectableText { objectName: "incidentAiNextCheck"; Layout.fillWidth: true; text: workspace.uiText["ai.nextCheck"]; font.bold: true; color: workspace.appearanceColors.accent }
            SelectableText { objectName: "incidentAiAction"; Layout.fillWidth: true; text: workspace.incidentAiResult.action || "" }
        }
    }
    ColumnLayout {
            Layout.fillWidth: true
            visible: workspace.inspectorPage === "overview" && workspace.resourceGuidance.length > 0
            FlatButton {
                id: guidanceToggle
                objectName: "resourceGuidanceToggle"
                Layout.fillWidth: true
                text: workspace.uiText["inspector.diagnostics"] + " (" + workspace.resourceGuidance.length + ")"
                checkable: true
                checked: false
                Accessible.name: "Expand or collapse known diagnostic guidance"
            }
            Loader {
                Layout.fillWidth: true
                Layout.preferredHeight: active ? Math.min(220, inspectorScroll.availableHeight * 0.35) : 0
                Layout.minimumHeight: 0
                active: guidanceToggle.checked && workspace.inspectorPage === "overview"
                visible: active
                sourceComponent: ScrollView {
                  id: guidanceScroll
                  contentWidth: availableWidth
                  clip: true
                  ColumnLayout {
                    width: guidanceScroll.availableWidth
                  Repeater {
                    model: workspace.resourceGuidance
                    ColumnLayout {
                        required property var modelData
                        required property int index
                        property string linkError: ""
                        Layout.fillWidth: true
                        SelectableText { objectName: "guidanceTitle_" + index; Layout.fillWidth: true; text: modelData.title; font.bold: true; color: workspace.appearanceColors[modelData.tone] }
                        SelectableText { objectName: "guidanceEvidence_" + index; Layout.fillWidth: true; text: modelData.scope + " / " + modelData.code; color: workspace.appearanceColors.muted }
                        SelectableText { objectName: "guidanceDetail_" + index; Layout.fillWidth: true; text: modelData.detail; Accessible.name: modelData.title + ": " + modelData.detail }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            IconButton { objectName: "guidanceEvents_" + index; glyph: "Event"; text: workspace.uiText["inspector.events"]; Accessible.name: "Open events for " + workspace.inspectorName; onClicked: workspace.setInspectorPage("events") }
                            IconButton { objectName: "guidanceYaml_" + index; glyph: "ConfigMap"; text: workspace.uiText["inspector.yaml"]; Accessible.name: "Open YAML for " + workspace.inspectorName; onClicked: workspace.setInspectorPage("yaml") }
                            IconButton { objectName: "guidanceLogs_" + index; glyph: "Menu"; text: workspace.uiText["inspector.logs"]; visible: workspace.podInspected; Accessible.name: "Open logs for " + workspace.inspectorName; onClicked: workspace.setLogsVisible(true) }
                        }
                        IconButton {
                            objectName: "guidanceDocs_" + index
                            glyph: "External"
                            text: "Kubernetes documentation"
                            Accessible.name: "Open official documentation for " + modelData.code
                            onClicked: parent.linkError = Qt.openUrlExternally(modelData.documentation) ? "" : "Could not open documentation. Check your browser and retry explicitly."
                        }
                        SelectableText { objectName: "guidanceLinkError_" + index; Layout.fillWidth: true; text: parent.linkError; visible: text !== ""; color: workspace.appearanceColors.danger }
                        Rectangle { Layout.fillWidth: true; height: 1; color: workspace.appearanceColors.border }
                    }
                  }
                  }
                }
            }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: inspector.width < 600 ? 44 : 38
        visible: workspace.inspectorPage === "overview"
        color: workspace.appearanceColors.panel
        border.color: workspace.appearanceColors.border
        RowLayout {
            anchors.fill: parent; anchors.margins: 6
            spacing: 12
                SelectableText { objectName: "overviewFieldHeader"; Layout.preferredWidth: inspector.width < 600 ? 100 : 260; text: "Field"; color: workspace.appearanceColors.accent; font.bold: true }
            SelectableText { Layout.fillWidth: true; text: "Value"; color: workspace.appearanceColors.accent; font.bold: true }
            SelectableText { objectName: "overviewMetricHeader"; visible: workspace.overviewFields.some(field => field.readiness !== undefined); Layout.preferredWidth: Math.min(150, overviewScroll.width * 0.28); text: "Metric"; color: workspace.appearanceColors.accent; font.bold: true }
        }
    }
    ListView {
        id: overviewScroll
        cacheBuffer: 0
        objectName: "overviewScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Math.min(250, contentHeight)
        visible: workspace.inspectorPage === "overview"
        clip: true
        reuseItems: false
        model: visible ? workspace.overviewFieldModel : null
        Accessible.name: "Resource metadata overview"
        Accessible.description: workspace.inspected
        ScrollBar.vertical: ScrollBar {}
        activeFocusOnTab: true
        onCurrentIndexChanged: if (activeFocus && currentItem) Qt.callLater(function() {
            if (!overviewScroll.visible || !overviewScroll.currentItem) return
            const rowTop = overviewScroll.currentItem.mapToItem(inspectorScroll, 0, 0).y
            const rowBottom = rowTop + overviewScroll.currentItem.height
            const scroll = inspectorScroll.contentItem
            const limit = Math.max(0, scroll.contentHeight - scroll.height)
            if (rowBottom > inspectorScroll.height)
                scroll.contentY = Math.min(limit, scroll.contentY + rowBottom - inspectorScroll.height)
            else if (rowTop < 0)
                scroll.contentY = Math.max(0, scroll.contentY + rowTop)
        })
        Keys.onPressed: function(event) { if (event.key===Qt.Key_End) { positionViewAtIndex(count-1,ListView.End); event.accepted=true } else if (event.key===Qt.Key_Home) { positionViewAtIndex(0,ListView.Beginning); event.accepted=true } }
        delegate: Rectangle {
            required property var modelData
            width: overviewScroll.width
            height: Math.max(32, overviewRow.implicitHeight + 8)
            color: workspace.appearanceColors.inset
            border.color: workspace.appearanceColors.border
            ColumnLayout {
            id: overviewRow
            anchors.fill: parent; anchors.margins: 4
            RowLayout {
                Layout.fillWidth: true; visible: modelData.metric===undefined; spacing: 12
                        SelectableText { Layout.preferredWidth: inspector.width < 600 ? 100 : 260; Layout.alignment: Qt.AlignTop; text: modelData.label; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; wrapMode: Text.Wrap }
                        SelectableText {
                            objectName: "overview_" + modelData.id
                            Layout.fillWidth: true
                            text: modelData.value
                            color: { const palette = workspace.appearanceColors; return workspace.inspectorFieldColor(modelData.id) }
                            Accessible.name: modelData.label + ": " + modelData.value
                        }
                        IconButton {
                            objectName: "overviewFilter_" + modelData.id
                            Layout.preferredHeight: inspector.width < 600 ? 44 : 24
                            visible: modelData.filterable === true
                            enabled: !workspace.busy
                            glyph: "Filters"
                            text: workspace.uiText["filters.title"] + ": " + modelData.label + " = " + modelData.value
                            onClicked: workspace.filterInspectorField(modelData.id)
                        }
                Loader {
                    Layout.preferredWidth: Math.min(150, overviewScroll.width * 0.28)
                    Layout.alignment: Qt.AlignVCenter
                    active: modelData.readiness !== undefined
                    visible: active
                    sourceComponent: ProgressBar {
                        id: readinessBar
                        objectName: "overviewReadinessBar"
                        from: 0; to: 1; value: modelData.readiness.fraction
                        implicitHeight: 8
                        Accessible.name: modelData.readiness.description
                        background: Rectangle { implicitHeight: 8; color: workspace.appearanceColors.inset; radius: 2 }
                        contentItem: Item {
                            implicitHeight: 8
                            Rectangle {
                                objectName: "overviewReadinessFill"
                                width: parent.width * readinessBar.visualPosition; height: parent.height; radius: 2
                                color: workspace.appearanceColors[modelData.readiness.tone]
                            }
                        }
                    }
                }
            }
            Loader { Layout.fillWidth: true; active: modelData.metric!==undefined; sourceComponent: MetricGauge { metric: modelData.metric } }
            }
        }
    }
    InspectorPage {
        id: eventsPage
        Layout.fillWidth: true; Layout.fillHeight: true
        selected: workspace.inspectorPage === "events"
        sourceComponent: ResourceGrid {
            id: events
            objectName: "inspectorEvents"
            prefix: "inspectorEvent"; tableModel: workspace.inspectorEventTable; columns: workspace.inspectorEventColumns
            sortColumn: workspace.inspectorEventSortColumn; sortDirection: workspace.inspectorEventSortDirection
            emptyObjectName: "emptyInspectorEvents"
            emptyText: workspace.uiText["inspector.noEvents"]
            onSortRequested: column => workspace.sortInspectorColumn("inspectorEvent", column)
            onInspectRequested: path => workspace.inspectEventPath(path)
            onCopyRequested: (row, column) => workspace.copyInspectorCell("inspectorEvent", events.pathAt(row), column)
            onCopyPathRequested: (path, column) => workspace.copyInspectorCell("inspectorEvent", path, column)
        }
    }
    InspectorPage {
        id: linksPage
        Layout.fillWidth: true; Layout.fillHeight: true
        selected: workspace.inspectorPage === "links"
        sourceComponent: ResourceGrid {
            id: links
            objectName: "inspectorLinks"
            prefix: "inspectorLink"; tableModel: workspace.inspectorLinkTable; columns: workspace.inspectorLinkColumns
            sortColumn: workspace.inspectorLinkSortColumn; sortDirection: workspace.inspectorLinkSortDirection
            emptyObjectName: "emptyInspectorLinks"
            emptyText: workspace.uiText["inspector.noLinks"]
            onSortRequested: column => workspace.sortInspectorColumn("inspectorLink", column)
            onInspectRequested: path => workspace.inspectPath(path)
            onCopyRequested: (row, column) => workspace.copyInspectorCell("inspectorLink", links.pathAt(row), column)
            onCopyPathRequested: (path, column) => workspace.copyInspectorCell("inspectorLink", path, column)
        }
    }
        InspectorPage {
            id: yamlPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 44
            Layout.preferredHeight: 44
            selected: workspace.yamlVisible
            sourceComponent: RowLayout {
                property alias scrollContent: yamlScroll.contentItem
                spacing: 0
                YamlPresentation {
                    id: yamlLines
                    objectName: "yamlLineNumbers"
                    Layout.preferredWidth: implicitWidth
                    Layout.fillHeight: true
                    editor: yamlEditor
                    document: workspace.yamlVisible ? yamlEditor.textDocument : null
                    font: yamlEditor.font
                    light: workspace.themeVariant === "light"
                    color: workspace.appearanceColors.muted
                    scrollOffset: yamlScroll.contentItem ? yamlScroll.contentItem.contentY : 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: "YAML line numbers"
                    Accessible.description: "First visible line: " + firstVisibleLine
                }
                ScrollView {
                    id: yamlScroll
                    objectName: "yamlScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    visible: workspace.yamlVisible
                    TextArea {
                        id: yamlEditor
                        objectName: "inspectorYaml"
                        readOnly: !workspace.yamlEditing || workspace.yamlApplyLocked
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        text: workspace.yamlText
                        onTextChanged: { if (!readOnly) workspace.setYamlDraft(text) }
                        font.family: workspace.monospaceFamily
                        wrapMode: TextArea.NoWrap
                        Accessible.name: readOnly ? "Resource YAML, read-only" : "Local resource YAML draft"
                    }
                }
            }
        }
    GridLayout {
        Layout.fillWidth: true
        visible: workspace.yamlVisible
        columns: inspector.width < 600 ? 1 : 2
        RowLayout {
            FlatButton { objectName: "editYaml"; text: "Edit YAML"; visible: !workspace.yamlEditing; enabled: workspace.canEditYaml && !workspace.busy && !workspace.authenticationRequired && !workspace.authenticationRunning && !workspace.deletionPending && !workspace.deletionRunning; onClicked: workspace.beginYamlEdit() }
            FlatButton { objectName: "leaveYamlEdit"; destructive: workspace.yamlDirty; text: workspace.yamlDirty ? "Discard draft" : "Leave editing"; visible: workspace.yamlEditing; onClicked: workspace.leaveYamlEdit() }
            FlatButton { objectName: "checkYaml"; text: workspace.yamlChecking ? "Checking..." : "Check YAML"; visible: workspace.yamlEditing; enabled: !workspace.busy && !workspace.discardPending && !workspace.yamlApplyLocked && !workspace.yamlChecking; Accessible.name: "Check local YAML syntax and resource identity"; onClicked: workspace.checkYamlDraft() }
        }
        SelectableText { objectName: "yamlDraftStatus"; Layout.fillWidth: true; visible: text !== ""; text: workspace.yamlDraftStatus }
    }
    SelectableText { objectName: "yamlCheckStatus"; Layout.fillWidth: true; visible: workspace.yamlVisible && text !== ""; text: workspace.yamlCheckStatus }
    RowLayout {
        visible: workspace.yamlVisible && workspace.yamlEditing
        FlatButton { objectName: "previewYaml"; text: "Preview apply"; enabled: workspace.yamlDraftCredentialsCurrent && !workspace.busy && !workspace.yamlApplyLocked && !workspace.yamlChecking && !workspace.discardPending && !workspace.authenticationRequired && !workspace.authenticationRunning; onClicked: workspace.previewYamlApply() }
        FlatButton { objectName: "readBackYaml"; text: "Read back"; visible: workspace.canReadBackYaml; enabled: !workspace.discardPending && !workspace.authenticationRequired; onClicked: workspace.readBackYaml() }
        FlatButton { objectName: "reconcileYaml"; text: "Compare / reconcile"; visible: workspace.canReconcileYaml; enabled: !workspace.discardPending; onClicked: workspace.reconcileYaml() }
    }
    SelectableText { objectName: "yamlApplyStatus"; Layout.fillWidth: true; visible: workspace.yamlVisible && text !== ""; text: workspace.yamlApplyStatus }
    InspectorPage {
        id: valuesPage
        Layout.fillWidth: true
        Layout.fillHeight: true
        selected: workspace.valuesVisible
        sourceComponent: ResourceValuesView {
            id: values
        }
    }
    InspectorPage {
        id: logsPage
        selected: workspace.logsVisible
        sourceComponent: PodLogsView { Layout.fillWidth: true; Layout.fillHeight: true; visible: workspace.logsVisible }
    }
    InspectorPage {
        id: filesPage
        selected: workspace.inspectorPage === "files"
        sourceComponent: ContainerFilesView { Layout.fillWidth: true; Layout.fillHeight: true }
    }
    Loader {
        Layout.fillWidth: true; Layout.fillHeight: true
        Layout.minimumHeight: 0
        active: workspace.inspectorPage === "terminal" && !inspectorRoot.hostWindow.terminalExpanded
        visible: active
        source: "ContainerTerminalView.qml"
        onLoaded: {
            item.inputPriority = Qt.binding(function() { return inspectorRoot.hostWindow?.inputPriority ?? false })
            if (item.terminal && item.terminal.connected) item.inputSurface.forceActiveFocus()
        }
    }

    }
    }
}
