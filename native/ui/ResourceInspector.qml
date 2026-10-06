import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    padding: 6
    background: Rectangle {
        color: workspace.appearanceColors.panel
        border.width: 1
        border.color: workspace.appearanceColors.glow
    }
    contentItem: ColumnLayout {
    id: inspector
    SystemPalette { id: systemPalette }
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
        values.contentY = same ? Math.min(valuesPosition, Math.max(0, values.contentHeight - values.height)) : 0
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
        ToolButton { objectName: "inspectorBack"; text: "Back"; enabled: workspace.canInspectBack; Accessible.name: "Previous inspected resource"; onClicked: workspace.navigateInspector(-1) }
        ToolButton { objectName: "inspectorForward"; text: "Forward"; enabled: workspace.canInspectForward; Accessible.name: "Next inspected resource"; onClicked: workspace.navigateInspector(1) }
        Label { objectName: "inspectorResourceName"; Layout.fillWidth: true; text: workspace.inspectorName; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap; Accessible.name: "Inspecting " + text }
    }
    TabBar {
        Layout.fillWidth: true
        currentIndex: ["overview", "yaml", "events", "links", "values", "logs", "terminal"].indexOf(workspace.inspectorPage)
        TabButton { objectName: "overviewButton"; width: implicitWidth; text: "Overview"; onClicked: workspace.setInspectorPage("overview") }
        TabButton { objectName: "yamlButton"; width: implicitWidth; text: "YAML"; onClicked: workspace.setInspectorPage("yaml") }
        TabButton { objectName: "inspectorEventsButton"; width: implicitWidth; text: "Events"; onClicked: workspace.setInspectorPage("events") }
        TabButton { objectName: "inspectorLinksButton"; width: implicitWidth; text: "Links"; onClicked: workspace.setInspectorPage("links") }
        TabButton { objectName: "valuesButton"; text: "Values"; visible: workspace.valuesAvailable; width: visible ? implicitWidth : 0; onClicked: workspace.setInspectorPage("values") }
        TabButton { objectName: "logsButton"; text: "Logs"; visible: workspace.podInspected; width: visible ? implicitWidth : 0; onClicked: workspace.setLogsVisible(true) }
        TabButton { objectName: "terminalButton"; text: "Terminal"; visible: workspace.podInspected || workspace.containerTerminal !== null; width: visible ? implicitWidth : 0; onClicked: workspace.setInspectorPage("terminal") }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Button { objectName: "refreshInspector"; text: "Refresh detail"; enabled: !workspace.authenticationRequired; onClicked: workspace.refreshInspector() }
        Button { objectName: "copyYaml"; text: "Copy visible YAML"; visible: workspace.yamlVisible; enabled: !workspace.yamlEditing && workspace.yamlText !== ""; onClicked: workspace.copyYaml() }
        Button { objectName: "deleteResource"; text: "Delete..."; enabled: workspace.canDeleteResource; Accessible.name: "Review deletion of " + workspace.inspectorName; onClicked: workspace.previewDeletion() }
        Button { objectName: "preparePortForward"; text: "Port forward..."; enabled: workspace.canPortForward; Accessible.name: "Port forward " + workspace.inspectorName; onClicked: workspace.preparePortForward() }
        Button { objectName: "closeInspector"; text: "Close"; Accessible.name: "Close inspector"; onClicked: workspace.closeInspector() }
        Button { objectName: "expandTerminal"; text: "Expand terminal"; visible: workspace.inspectorPage === "terminal"; onClicked: terminalDialog.open() }
        Button { objectName: "readBackResourceDelete"; text: "Read back deletion target"; visible: workspace.canReadBackDeletion; onClicked: workspace.readBackDeletion() }
    }
    Label {
        objectName: "inspectorReadStatus"
        Layout.fillWidth: true
        text: workspace.inspectorStatus
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        Accessible.name: text
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
                            Button { objectName: "guidanceEvents_" + index; text: "Events"; Accessible.name: "Open cached events for " + workspace.inspectorName; onClicked: workspace.setInspectorPage("events") }
                            Button { objectName: "guidanceYaml_" + index; text: "YAML"; Accessible.name: "Open YAML for " + workspace.inspectorName; onClicked: workspace.setInspectorPage("yaml") }
                            Button { objectName: "guidanceLogs_" + index; text: "Pod logs"; visible: workspace.podInspected; Accessible.name: "Open logs for " + workspace.inspectorName; onClicked: workspace.setLogsVisible(true) }
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
    ListView {
        id: overviewScroll
        objectName: "overviewScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: workspace.inspectorPage === "overview"
        clip: true
        reuseItems: true
        model: workspace.overviewFields
        Accessible.name: "Resource metadata overview"
        Accessible.description: workspace.inspected
        ScrollBar.vertical: ScrollBar {}
        activeFocusOnTab: true
        Keys.onPressed: function(event) { if (event.key===Qt.Key_End) { positionViewAtIndex(count-1,ListView.End); event.accepted=true } else if (event.key===Qt.Key_Home) { positionViewAtIndex(0,ListView.Beginning); event.accepted=true } }
        delegate: ColumnLayout {
            required property var modelData
            width: overviewScroll.width
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
    ListView {
        id: values
        objectName: "resourceValues"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: workspace.valuesVisible
        clip: true
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Home) { values.positionViewAtIndex(0,ListView.Beginning); event.accepted = true }
            else if (event.key === Qt.Key_End) { values.positionViewAtIndex(values.count-1,ListView.End); event.accepted = true }
        }
        model: workspace.resourceValues
        spacing: 8
        ScrollBar.vertical: ScrollBar {}
        delegate: ColumnLayout {
            id: valueRow
            required property var modelData
            property string copyError: ""
            function copy(representation) {
                copyError = workspace.copyValue(modelData.id, representation) ? "" : "Cannot decode this value as text. Copy the raw value instead."
            }
            width: values.width
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight }
                Button { objectName: "reveal_" + modelData.id; visible: modelData.secret; text: modelData.revealed ? "Hide" : "Reveal"; Accessible.name: text + " " + modelData.name; onClicked: workspace.revealValue(modelData.id, !modelData.revealed) }
                Button { objectName: "copy_" + modelData.id; text: "Copy"; Accessible.name: "Copy " + modelData.name; onClicked: valueRow.copy("preferred") }
                Button {
                    id: copyOptions
                    objectName: "copyOptions_" + modelData.id
                    text: "Copy options"
                    Accessible.name: "Copy options for " + modelData.name
                    onClicked: copyMenu.popup(copyOptions, 0, copyOptions.height)
                    Menu {
                        id: copyMenu
                        MenuItem { objectName: "copyKey_" + modelData.id; text: "Copy key"; onTriggered: valueRow.copy("key") }
                        MenuItem { objectName: "copyRaw_" + modelData.id; text: "Copy raw value"; onTriggered: valueRow.copy("raw") }
                        MenuItem { objectName: "copyDecoded_" + modelData.id; text: "Copy decoded text"; onTriggered: valueRow.copy("decoded") }
                    }
                }
            }
            Label { objectName: "copyValueError_" + modelData.id; Layout.fillWidth: true; visible: text !== ""; text: valueRow.copyError; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            Label { text: modelData.field + " - " + modelData.encoding; textFormat: Text.PlainText }
            TextArea {
                objectName: "value_" + modelData.id
                Layout.fillWidth: true
                readOnly: true
                textFormat: TextEdit.PlainText
                text: modelData.preview
                wrapMode: TextArea.Wrap
                Accessible.name: modelData.name + ": " + modelData.preview
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.tooltip
            }
        }
        Label { anchors.centerIn: parent; width: parent.width - 16; visible: values.count === 0; text: "No values cached. Refresh detail to request current data."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        Rectangle { anchors.fill: parent; z: 1; visible: values.activeFocus; color: "transparent"; border.width: 2; border.color: systemPalette.highlight }
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
