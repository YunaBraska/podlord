import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    padding: 6
    component InspectorTab: TabButton {
        id: tab
        width: visible ? implicitWidth : 0
        implicitHeight: inspector.width < 600 ? 44 : 32
        padding: 8
        font.bold: true
        contentItem: Label {
            text: tab.text; textFormat: Text.PlainText; font: tab.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: tab.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
        }
        background: Rectangle {
            color: tab.checked || tab.down ? workspace.appearanceColors.selection
                : tab.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.inset
            border.color: tab.checked || tab.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
        }
    }
    background: Rectangle {
        color: workspace.appearanceColors.panel
        border.width: 1
        border.color: workspace.appearanceColors.glow
    }
    contentItem: ColumnLayout {
    id: inspector
    property real yamlPosition: 0
    property real overviewPosition: 0
    property real valuesPosition: 0
    property string previousScope: ""
    property string previousPage: ""
    onWidthChanged: if (width > 0 && width < 600 && workspace.inspectorPage === "terminal") terminalDialog.open()
    function restorePosition() {
        const same = previousScope === workspace.inspectorScope
        const yamlPane = yamlScroll.contentItem
        const overviewPane = overviewScroll
        yamlPane.contentY = same ? Math.min(yamlPosition, Math.max(0, yamlPane.contentHeight - yamlPane.height)) : 0
        overviewPane.contentY = same ? Math.min(overviewPosition, Math.max(0, overviewPane.contentHeight - overviewPane.height)) : 0
        values.contentY = same ? Math.min(valuesPosition, Math.max(0, values.contentHeight - values.viewportHeight)) : 0
    }
    Connections {
        target: workspace
        function onInspectorPresentationChanging() {
            inspector.previousScope = workspace.inspectorScope
            inspector.yamlPosition = yamlScroll.contentItem.contentY
            inspector.overviewPosition = overviewScroll.contentY
            inspector.valuesPosition = values.contentY
        }
        function onInspectorPresentationChanged() { Qt.callLater(inspector.restorePosition) }
        function onChanged() {
            const page = workspace.inspectorPage
            if (page !== "terminal") terminalDialog.close()
            else if (page !== inspector.previousPage && inspector.width < 600) terminalDialog.open()
            inspector.previousPage = page
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        IconButton { objectName: "inspectorBack"; glyph: "Previous"; text: workspace.uiText["tooltip.previousResource"]; enabled: workspace.canInspectBack; onClicked: workspace.navigateInspector(-1) }
        IconButton { objectName: "inspectorForward"; glyph: "Next"; text: workspace.uiText["tooltip.nextResource"]; enabled: workspace.canInspectForward; onClicked: workspace.navigateInspector(1) }
        Label { objectName: "inspectorResourceName"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: (workspace.overviewFields.find(field => field.id === "kind")?.value || "") + "/" + workspace.inspectorName; textFormat: Text.PlainText; font.bold: true; elide: Text.ElideMiddle; Accessible.name: "Inspecting " + text }
        IconButton { objectName: "refreshInspector"; glyph: "Reset"; text: "Refresh detail"; enabled: !workspace.authenticationRequired; onClicked: workspace.refreshInspector() }
        IconButton { objectName: "deleteResource"; glyph: "Trash"; text: "Review deletion of " + workspace.inspectorName; enabled: workspace.canDeleteResource; onClicked: workspace.previewDeletion() }
        IconButton { objectName: "preparePortForward"; glyph: "Service"; text: "Port forward " + workspace.inspectorName; visible: workspace.canPortForward; enabled: workspace.canPortForward; onClicked: workspace.preparePortForward() }
        IconButton { objectName: "closeInspector"; glyph: "Close"; text: "Close inspector"; onClicked: workspace.closeInspector() }
    }
    TabBar {
        Layout.fillWidth: true
        currentIndex: ["overview", "yaml", "events", "links", "values", "logs", "terminal"].indexOf(workspace.inspectorPage)
        InspectorTab { objectName: "overviewButton"; text: workspace.uiText["inspector.overview"]; onClicked: workspace.setInspectorPage("overview") }
        InspectorTab { objectName: "yamlButton"; text: workspace.uiText["inspector.yaml"]; onClicked: workspace.setInspectorPage("yaml") }
        InspectorTab { objectName: "inspectorEventsButton"; text: workspace.uiText["inspector.events"]; onClicked: workspace.setInspectorPage("events") }
        InspectorTab { objectName: "inspectorLinksButton"; text: workspace.uiText["inspector.links"]; onClicked: workspace.setInspectorPage("links") }
        InspectorTab { objectName: "valuesButton"; text: workspace.uiText["inspector.values"]; visible: workspace.valuesAvailable; onClicked: workspace.setInspectorPage("values") }
        InspectorTab { objectName: "logsButton"; text: workspace.uiText["inspector.logs"]; visible: workspace.podInspected; onClicked: workspace.setLogsVisible(true) }
        InspectorTab { objectName: "terminalButton"; text: "Terminal"; visible: workspace.podInspected || workspace.containerTerminal !== null; onClicked: workspace.setInspectorPage("terminal") }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: workspace.yamlVisible || workspace.inspectorPage === "terminal" || workspace.canReadBackDeletion
        IconButton { objectName: "copyYaml"; glyph: "ConfigMap"; text: "Copy visible YAML"; visible: workspace.yamlVisible; enabled: !workspace.yamlEditing && workspace.yamlText !== ""; onClicked: workspace.copyYaml() }
        IconButton { objectName: "expandTerminal"; glyph: "Sidebar"; text: "Expand terminal"; visible: workspace.inspectorPage === "terminal"; onClicked: terminalDialog.open() }
        IconButton { objectName: "readBackResourceDelete"; glyph: "Reset"; text: "Read back deletion target"; visible: workspace.canReadBackDeletion; onClicked: workspace.readBackDeletion() }
    }
    Label {
        objectName: "inspectorReadStatus"
        Layout.fillWidth: true
        text: workspace.inspectorStatus
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        Accessible.name: text
        color: workspace.appearanceColors.muted
    }
    Label { objectName: "resourceDeleteStatus"; Layout.fillWidth: true; text: workspace.deletionStatus; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
    Label { Layout.fillWidth: true; text: workspace.portForwardError; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
    ColumnLayout {
            Layout.fillWidth: true
            visible: workspace.inspectorPage === "overview" && workspace.resourceGuidance.length > 0
            ToolButton {
                id: guidanceToggle
                objectName: "resourceGuidanceToggle"
                Layout.fillWidth: true
                text: (checked ? "Hide" : "Show") + " cached diagnostics (" + workspace.resourceGuidance.length + ")"
                checkable: true
                checked: false
                Accessible.name: "Expand or collapse known diagnostic guidance"
            }
            Loader {
                Layout.fillWidth: true
                Layout.preferredHeight: active ? Math.min(220, inspector.height * 0.35) : 0
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
                        Label { objectName: "guidanceTitle_" + index; Layout.fillWidth: true; text: modelData.title; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.bold: true; color: workspace.appearanceColors[modelData.tone] }
                        Label { objectName: "guidanceEvidence_" + index; Layout.fillWidth: true; text: modelData.scope + " / " + modelData.code; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
                        TextArea { objectName: "guidanceDetail_" + index; Layout.fillWidth: true; text: modelData.detail; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; padding: 0; background: null; Accessible.name: modelData.title + ": " + modelData.detail }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Button { objectName: "guidanceEvents_" + index; text: workspace.uiText["inspector.events"]; Accessible.name: "Open cached events for " + workspace.inspectorName; onClicked: workspace.setInspectorPage("events") }
                            Button { objectName: "guidanceYaml_" + index; text: workspace.uiText["inspector.yaml"]; Accessible.name: "Open YAML for " + workspace.inspectorName; onClicked: workspace.setInspectorPage("yaml") }
                            Button { objectName: "guidanceLogs_" + index; text: workspace.uiText["inspector.logs"]; visible: workspace.podInspected; Accessible.name: "Open logs for " + workspace.inspectorName; onClicked: workspace.setLogsVisible(true) }
                        }
                        Button {
                            objectName: "guidanceDocs_" + index
                            text: "Kubernetes documentation"
                            Accessible.name: "Open official documentation for " + modelData.code
                            onClicked: parent.linkError = Qt.openUrlExternally(modelData.documentation) ? "" : "Could not open documentation. Check your browser and retry explicitly."
                        }
                        Label { objectName: "guidanceLinkError_" + index; Layout.fillWidth: true; text: parent.linkError; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
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
            Label { objectName: "overviewFieldHeader"; Layout.preferredWidth: 100; text: "Field"; color: workspace.appearanceColors.accent; font.bold: true }
            Label { Layout.fillWidth: true; text: "Value"; color: workspace.appearanceColors.accent; font.bold: true }
            Label { Layout.preferredWidth: Math.min(150, overviewScroll.width * 0.28); text: "Metric"; color: workspace.appearanceColors.accent; font.bold: true }
        }
    }
    ListView {
        id: overviewScroll
        objectName: "overviewScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: workspace.inspectorPage === "overview"
        clip: true
        reuseItems: true
        model: visible ? workspace.overviewFields : []
        Accessible.name: "Resource metadata overview"
        Accessible.description: workspace.inspected
        ScrollBar.vertical: ScrollBar {}
        activeFocusOnTab: true
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
                Label { Layout.preferredWidth: 100; Layout.alignment: Qt.AlignTop; text: modelData.label; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; wrapMode: Text.Wrap }
                TextArea { objectName: "overview_" + modelData.id; Layout.fillWidth: true; readOnly: true; selectByMouse: true; text: modelData.value; textFormat: TextEdit.PlainText; wrapMode: TextArea.Wrap; padding: 0; background: null; Accessible.name: modelData.label + ": " + modelData.value }
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
    ResourceGrid {
        id: events
        objectName: "inspectorEvents"
        Layout.fillWidth: true; Layout.fillHeight: true
        visible: workspace.inspectorPage === "events"
        prefix: "inspectorEvent"; tableModel: workspace.inspectorEventTable; columns: workspace.inspectorEventColumns
        sortColumn: workspace.inspectorEventSortColumn; sortDirection: workspace.inspectorEventSortDirection
        emptyObjectName: "emptyInspectorEvents"
        emptyText: "No related Events in this cache. Only Events for this resource UID are included. Check synchronization for unavailable data."
        onSortRequested: column => workspace.sortInspectorColumn("inspectorEvent", column)
        onInspectRequested: path => workspace.inspectEventPath(path)
        onCopyRequested: (row, column) => workspace.copyInspectorCell("inspectorEvent", events.pathAt(row), column)
        onCopyPathRequested: (path, column) => workspace.copyInspectorCell("inspectorEvent", path, column)
    }
    ResourceGrid {
        id: links
        objectName: "inspectorLinks"
        Layout.fillWidth: true; Layout.fillHeight: true
        visible: workspace.inspectorPage === "links"
        prefix: "inspectorLink"; tableModel: workspace.inspectorLinkTable; columns: workspace.inspectorLinkColumns
        sortColumn: workspace.inspectorLinkSortColumn; sortDirection: workspace.inspectorLinkSortDirection
        emptyObjectName: "emptyInspectorLinks"
        emptyText: "No related resources in this cache. Owner references are resolved by UID; node and namespace links use their explicit scope."
        onSortRequested: column => workspace.sortInspectorColumn("inspectorLink", column)
        onInspectRequested: path => workspace.inspectPath(path)
        onCopyRequested: (row, column) => workspace.copyInspectorCell("inspectorLink", links.pathAt(row), column)
        onCopyPathRequested: (path, column) => workspace.copyInspectorCell("inspectorLink", path, column)
    }
    ScrollView {
        id: yamlScroll
        objectName: "yamlScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: workspace.yamlVisible
        TextArea {
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
    RowLayout {
        Layout.fillWidth: true
        visible: workspace.yamlVisible
        Button { objectName: "editYaml"; text: "Edit YAML"; visible: !workspace.yamlEditing; enabled: workspace.canEditYaml && !workspace.busy && !workspace.authenticationRequired && !workspace.authenticationRunning && !workspace.deletionPending && !workspace.deletionRunning; onClicked: workspace.beginYamlEdit() }
        Button { objectName: "leaveYamlEdit"; text: workspace.yamlDirty ? "Discard draft" : "Leave editing"; visible: workspace.yamlEditing; onClicked: workspace.leaveYamlEdit() }
        Button { objectName: "checkYaml"; text: workspace.yamlChecking ? "Checking..." : "Check YAML"; visible: workspace.yamlEditing; enabled: !workspace.busy && !workspace.discardPending && !workspace.yamlApplyLocked && !workspace.yamlChecking; Accessible.name: "Check local YAML syntax and resource identity"; onClicked: workspace.checkYamlDraft() }
        Label { objectName: "yamlDraftStatus"; Layout.fillWidth: true; text: workspace.yamlDraftStatus; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
    }
    Label { objectName: "yamlCheckStatus"; Layout.fillWidth: true; visible: workspace.yamlVisible && text !== ""; text: workspace.yamlCheckStatus; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
    RowLayout {
        visible: workspace.yamlVisible && workspace.yamlEditing
        Button { objectName: "previewYaml"; text: "Preview apply"; enabled: !workspace.busy && !workspace.yamlApplyLocked && !workspace.yamlChecking && !workspace.discardPending && !workspace.authenticationRequired; onClicked: workspace.previewYamlApply() }
        Button { objectName: "readBackYaml"; text: "Read back"; visible: workspace.canReadBackYaml; enabled: !workspace.discardPending && !workspace.authenticationRequired; onClicked: workspace.readBackYaml() }
        Button { objectName: "reconcileYaml"; text: "Compare / reconcile"; visible: workspace.canReconcileYaml; enabled: !workspace.discardPending; onClicked: workspace.reconcileYaml() }
    }
    Label { objectName: "yamlApplyStatus"; Layout.fillWidth: true; visible: workspace.yamlVisible && text !== ""; text: workspace.yamlApplyStatus; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
    ResourceValuesView {
        id: values
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: workspace.valuesVisible
    }
    PodLogsView { Layout.fillWidth: true; Layout.fillHeight: true; visible: workspace.logsVisible }
    Loader {
        Layout.fillWidth: true; Layout.fillHeight: true
        Layout.minimumHeight: 0
        active: workspace.inspectorPage === "terminal" && !terminalDialog.visible
        visible: active
        source: "ContainerTerminalView.qml"
        onLoaded: if (item.terminal && item.terminal.connected) item.inputSurface.forceActiveFocus()
    }
    Dialog {
        id: terminalDialog
        objectName: "expandedTerminal"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 16)
        height: Math.max(0, parent.height - 16)
        title: "Container terminal"
        modal: true
        closePolicy: Popup.NoAutoClose
        footer: DialogButtonBox {
            Button { objectName: "collapseTerminal"; text: "Back to inspector"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onRejected: terminalDialog.reject()
        }
        contentItem: Loader {
            active: terminalDialog.visible && workspace.inspectorPage === "terminal"
            source: "ContainerTerminalView.qml"
            onLoaded: if (item.terminal && item.terminal.connected) item.inputSurface.forceActiveFocus()
        }
    }
    }
}
