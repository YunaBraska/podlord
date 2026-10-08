import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: settings
    LayoutMirroring.enabled: workspace.uiRightToLeft
    LayoutMirroring.childrenInherit: true
    PasteSourceDialog { id: pasteSourceDialog }
    SessionManager { id: sessionManager }
    Dialog {
        id: sourceRemovalDialog
        objectName: "sourceRemovalDialog"
        title: "Remove imported context"
        modal: true
        parent: Overlay.overlay
        width: Math.min(520, parent.width - 24)
        x: (parent.width - width) / 2; y: Math.max(12, (parent.height - height) / 2)
        closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: workspace.sourceRemoval.name || ""; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: "This removes the context and the saved sessions below. Their tabs and port-forwards will close. Original kubeconfig files and other contexts stay unchanged."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(180, count * 28)
                clip: true; reuseItems: true; model: workspace.sourceRemoval.sessions || []
                ScrollBar.vertical: ScrollBar {}
                delegate: Label { required property string modelData; width: ListView.view.width; height: 28; text: modelData; textFormat: Text.PlainText; elide: Text.ElideRight; Accessible.name: modelData }
            }
            Label { visible: !(workspace.sourceRemoval.sessions || []).length; text: "No saved sessions use this context."; textFormat: Text.PlainText }
            Label { objectName: "sourceRemovalError"; Layout.fillWidth: true; visible: text !== ""; text: workspace.sourceImportError; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap; Accessible.name: text }
        }
        footer: DialogButtonBox {
            Button { objectName: "cancelSourceRemoval"; text: "Cancel"; enabled: !workspace.busy; onClicked: sourceRemovalDialog.close() }
            Button { objectName: "confirmSourceRemoval"; text: workspace.busy ? "Removing..." : "Remove"; enabled: !workspace.busy; Accessible.name: "Remove imported context and its listed sessions"; onClicked: workspace.confirmSourceRemoval() }
        }
        onClosed: workspace.cancelSourceRemoval()
        Connections {
            target: workspace
            function onSourceRemovalChanged() {
                if (workspace.sourceRemoval.contextId) sourceRemovalDialog.open()
                else if (sourceRemovalDialog.visible) sourceRemovalDialog.close()
            }
        }
    }
    Dialog {
        id: sourceAliasDialog
        property string contextId: ""
        property string canonicalName: ""
        property string failure: ""
        title: "Rename source context"
        modal: true
        parent: Overlay.overlay
        width: Math.min(520, parent.width - 24)
        x: (parent.width - width) / 2; y: Math.max(12, (parent.height - height) / 2)
        closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: "Canonical context: " + sourceAliasDialog.canonicalName; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            TextField { id: sourceAliasName; objectName: "sourceAliasName"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: "Source display name"; placeholderText: "Empty restores the canonical name"; onAccepted: sourceAliasDialog.save() }
            Label { Layout.fillWidth: true; text: "Only the display label changes. Credentials, context identity and sessions remain unchanged."; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; wrapMode: Text.Wrap }
            Label { objectName: "sourceAliasError"; Layout.fillWidth: true; visible: text !== ""; text: sourceAliasDialog.failure; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap; Accessible.name: text }
        }
        footer: DialogButtonBox {
            Button { objectName: "cancelSourceAlias"; text: "Cancel"; enabled: !workspace.busy; onClicked: sourceAliasDialog.close() }
            Button { objectName: "saveSourceAlias"; text: workspace.busy ? "Saving..." : "Save"; enabled: !workspace.busy; onClicked: sourceAliasDialog.save() }
        }
        function edit(context) {
            contextId = context.id; canonicalName = context.context; failure = ""
            sourceAliasName.text = context.name
            open(); sourceAliasName.forceActiveFocus(); sourceAliasName.selectAll()
        }
        function save() {
            failure = ""
            if (!workspace.renameSourceContext(contextId, sourceAliasName.text)) failure = "Another source operation is still running. Try again when it finishes."
        }
        onClosed: { contextId = ""; canonicalName = ""; sourceAliasName.text = ""; failure = "" }
        Connections {
            target: workspace
            function onSourceImportFinished(success) {
                if (!sourceAliasDialog.visible) return
                if (success) sourceAliasDialog.close()
                else sourceAliasDialog.failure = workspace.sourceImportError
            }
        }
    }
    signal sourcesRequested()
    signal fileRequested()
    signal folderRequested()
    signal sourceFailuresRequested()
    property string section: "alerts"
    readonly property var sections: ["alerts", "appearance", "diagnostics", "graphics", "privacy", "sources", "sync", "workspace", "about"].map(id => ({id: id, label: workspace.uiText["settings." + id]}))
    property string diagnosticTimestamp: ""
    function refreshDiagnostics() { diagnosticTimestamp = workspace.refreshSettingsDiagnostics() }
    function synchronizeAppearance() {
        theme.currentIndex = workspace.themeNames.indexOf(workspace.themeName)
        variant.currentIndex = variant.model.indexOf(workspace.themeVariant)
        language.currentIndex = language.model.findIndex(option => option.code === workspace.uiLanguage)
    }
    function synchronize() {
        synchronizeAppearance()
        if (visible && section === "sources") workspace.refreshSourceTable()
        requests.value = workspace.requestLimit
        inactive.value = workspace.inactiveSyncMinutes
        logs.text = workspace.logLimitMb.toString()
        yaml.text = workspace.yamlLimitMiB.toString()
    }
    onSectionChanged: { synchronize(); if (section === "diagnostics") refreshDiagnostics() }
    onVisibleChanged: if (visible) { synchronize(); if (section === "diagnostics") refreshDiagnostics() }
    Component.onCompleted: synchronize()
    Connections {
        target: workspace
        function onAppearanceChanged() { settings.synchronizeAppearance() }
        function onLanguageChanged() { settings.synchronizeAppearance() }
        function onCatalogsChanged() { if (settings.visible && settings.section === "sources") workspace.refreshSourceTable() }
    }
    padding: 14
    background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
    component SectionButton: Button {
        required property string destination
        readonly property bool selected: settings.section === destination
        font.bold: true; implicitHeight: 32; padding: 8
        onClicked: { settings.section = destination; if (destination === "alerts") workspace.setWorkspacePage("alerts") }
        contentItem: Label { text: parent.text; color: parent.selected ? parent.palette.buttonText : workspace.appearanceColors.muted; font: parent.font; verticalAlignment: Text.AlignVCenter }
        background: Rectangle {
            color: parent.down || parent.hovered ? workspace.appearanceColors.inset : "transparent"
            border.width: parent.activeFocus ? 1 : 0; border.color: workspace.appearanceColors.accent
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; visible: parent.parent.selected; color: workspace.appearanceColors.accent }
        }
    }
    component SettingRow: GridLayout {
        required property string label
        required property string help
        default property alias controls: editor.data
        readonly property bool narrow: width < 650
        Layout.fillWidth: true; columns: narrow ? 1 : 2
        columnSpacing: 14; rowSpacing: 6
        data: [
        Label { Layout.row: 0; Layout.column: 0; Layout.preferredWidth: parent.narrow ? -1 : 240; text: parent.label; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap }
        ,ColumnLayout { id: editor; Layout.row: parent.narrow ? 1 : 0; Layout.column: parent.narrow ? 0 : 1; Layout.fillWidth: true }
        ,Label { Layout.row: parent.narrow ? 2 : 1; Layout.column: parent.narrow ? 0 : 1; Layout.fillWidth: true; text: parent.help; visible: text !== ""; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
        ,Rectangle { Layout.row: parent.narrow ? 3 : 2; Layout.column: 0; Layout.columnSpan: parent.columns; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 10; Layout.bottomMargin: 10; color: workspace.appearanceColors.border }
        ]
    }
    TableColumnsDialog { id: resourceColumns; tableType: "resource"; columns: workspace.resourceColumns }
    TableColumnsDialog { id: eventColumns; tableType: "event"; columns: workspace.eventColumns }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: workspace.uiText["settings.title"]; font.pixelSize: 16; font.bold: true; color: workspace.appearanceColors.accent }
        Flow {
            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
            visible: settings.width >= 650
            SectionButton { objectName: "alertsWorkspaceButton"; text: workspace.uiText["settings.alerts"]; destination: "alerts" }
            SectionButton { objectName: "settingsAppearanceSection"; text: workspace.uiText["settings.appearance"]; destination: "appearance" }
            SectionButton { objectName: "settingsDiagnosticsSection"; text: workspace.uiText["settings.diagnostics"]; destination: "diagnostics" }
            SectionButton { objectName: "settingsGraphicsSection"; text: workspace.uiText["settings.graphics"]; destination: "graphics" }
            SectionButton { objectName: "settingsPrivacySection"; text: workspace.uiText["settings.privacy"]; destination: "privacy" }
            SectionButton { objectName: "settingsSourcesSection"; text: workspace.uiText["settings.sources"]; destination: "sources" }
            SectionButton { objectName: "settingsSyncSection"; text: workspace.uiText["settings.sync"]; destination: "sync" }
            SectionButton { objectName: "settingsWorkspaceSection"; text: workspace.uiText["settings.workspace"]; destination: "workspace" }
            SectionButton { objectName: "settingsAboutSection"; text: workspace.uiText["settings.about"]; destination: "about" }
        }
        ComboBox {
            objectName: "settingsSectionPicker"; Layout.fillWidth: true; visible: settings.width < 650
            model: settings.sections; textRole: "label"; valueRole: "id"
            currentIndex: model.findIndex(option => option.id === settings.section)
            Accessible.name: workspace.uiText["settings.title"]
            onActivated: { settings.section = currentValue; if (currentValue === "alerts") workspace.setWorkspacePage("alerts") }
        }
        Loader {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; visible: settings.section === "alerts"
            property bool opened: false
            active: opened; onVisibleChanged: if (visible) opened = true
            Component.onCompleted: if (visible) opened = true
            sourceComponent: AlertView {}
        }
        ScrollView {
            id: scroll
            objectName: "settingsContent"
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; visible: settings.section !== "alerts"
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: scroll.availableWidth; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: workspace.uiText["settings." + settings.section]; font.bold: true; font.pixelSize: 14; color: workspace.appearanceColors.accent }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "appearance"
                    SettingRow { label: workspace.uiText["settings.language"]; help: workspace.uiText["settings.languageHelp"]; ComboBox { id: language; objectName: "inlineUiLanguage"; Layout.fillWidth: true; model: workspace.uiLanguages; textRole: "name"; valueRole: "code"; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.language"]; onActivated: workspace.saveUiLanguage(currentValue) } }
                    SettingRow { label: workspace.uiText["settings.theme"]; help: workspace.uiText["settings.themeHelp"]; ComboBox { id: theme; objectName: "inlineAppearanceTheme"; Layout.fillWidth: true; model: workspace.themeNames; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.theme"]; onActivated: workspace.saveAppearance(currentText, workspace.themeVariant) } }
                    SettingRow { label: workspace.uiText["settings.variant"]; help: workspace.uiText["settings.variantHelp"]; ComboBox { id: variant; objectName: "inlineAppearanceVariant"; Layout.fillWidth: true; model: ["dark", "light"]; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.variant"]; onActivated: workspace.saveAppearance(workspace.themeName, currentText) } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "graphics"
                    SettingRow { label: "Radar background water"; help: "Only background water animates. Events do not create water effects."; CheckBox { objectName: "inlineRadarWaterEnabled"; text: "Enabled"; checked: workspace.radarWaterEnabled; enabled: !workspace.busy; onClicked: workspace.saveRadarWater(checked, workspace.radarWaterSpeedPercent) } }
                    SettingRow { label: "Water speed"; help: "0 hides waves. Hidden or minimized Radar does not animate."; RowLayout { Layout.fillWidth: true; Slider { objectName: "inlineRadarWaterSpeed"; Layout.fillWidth: true; from: 0; to: 100; stepSize: 5; value: workspace.radarWaterSpeedPercent; enabled: !workspace.busy; Accessible.name: "Water speed in percent"; onMoved: if (!pressed) workspace.saveRadarWater(workspace.radarWaterEnabled, Math.round(value)); onPressedChanged: if (!pressed && enabled) workspace.saveRadarWater(workspace.radarWaterEnabled, Math.round(value)) } Label { text: workspace.radarWaterSpeedPercent + "%"; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight } } }
                    SettingRow { label: "Reduced motion"; help: "Freezes water and removes moving alert effects. No screensaver is used."; CheckBox { objectName: "inlineReducedMotion"; text: "Enabled"; checked: workspace.alerts.reducedMotion; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(workspace.alerts.muted, checked) } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "sync"
                    SettingRow { label: "Inactive session sync"; help: "Minutes. 0 disables inactive sync. Closed sessions never synchronize."; SpinBox { id: inactive; objectName: "inlineInactiveMinutes"; from: 0; to: 2147483647; editable: true; enabled: !workspace.busy; Accessible.name: "Inactive session sync in minutes" } }
                    SettingRow { label: "Request limit per minute"; help: "0 adds no extra limit. Requests remain at least 400 ms apart, with up to four parallel reads. Inspector reads have priority."; SpinBox { id: requests; objectName: "inlineRequestLimit"; from: 0; to: 60000; editable: true; enabled: !workspace.busy; Accessible.name: "Request limit per minute" } }
                    SettingRow { label: "Retained logs per Pod"; help: "Positive whole MB. Default 5 MB."; TextField { id: logs; objectName: "inlineLogLimit"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: "Retained logs per Pod in MB" } }
                    Button { objectName: "inlineSaveSync"; text: workspace.busy ? "Saving..." : "Save sync settings"; enabled: !workspace.busy; onClicked: workspace.saveReadSettings(requests.value, inactive.value, logs.text) }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "privacy"
                    SettingRow { label: "Telemetry"; help: "No product telemetry is sent. Kubernetes and explicitly confirmed authentication contact the configured endpoints."; Label { objectName: "privacyTelemetry"; text: "Off"; font.bold: true } }
                    SettingRow { label: "Secret values"; help: "Loaded Secret values and diff previews are masked. Explicitly revealed values and self-entered local YAML drafts may be visible."; Label { text: "Masked by default" } }
                    SettingRow { label: "YAML document / patch limit"; help: "Positive whole MiB. Default 3 MiB. Checked before sending, not by interrupting running requests."; RowLayout { Layout.fillWidth: true; TextField { id: yaml; objectName: "inlineYamlLimit"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: "YAML document and patch limit in MiB" } Button { objectName: "inlineSaveYamlLimit"; text: "Save"; enabled: !workspace.busy; onClicked: workspace.saveYamlLimit(yaml.text) } } }
                    SettingRow { label: "Request audit retention"; help: "At most 200 completed requests in memory. No authorization headers, response bodies, log contents or query parameters are retained. Diagnostics refresh explicitly."; Label { text: "In memory only" } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "workspace"
                    SettingRow { label: "Workspace restoration"; help: "Reopen saved tabs on next launch. Off starts with closed tabs but retains sessions and their view preferences. Changing this does not close current tabs or port forwards; forwards never restart automatically."; CheckBox { objectName: "inlineWorkspaceRestore"; text: "Restore tabs on launch"; checked: workspace.workspaceRestoreEnabled; enabled: !workspace.busy; Accessible.name: "Restore workspace tabs on launch"; onClicked: workspace.saveWorkspaceRestore(checked) } }
                    SettingRow { label: "Session configuration"; help: "Save closed configuration snapshots or independent copies without changing the active session."; Button { objectName: "settingsManageSessions"; text: "Manage sessions"; enabled: !workspace.busy && workspace.sessions.length > 0; onClicked: sessionManager.open() } }
                    SettingRow { label: "Resource columns"; help: "Show, hide, order, resize and pin columns. Saved layouts are not overwritten by new defaults."; Button { objectName: "settingsResourceColumns"; text: "Configure resource columns"; onClicked: resourceColumns.open() } }
                    SettingRow { label: "Event columns"; help: "Event layouts are independent from resource layouts."; Button { objectName: "settingsEventColumns"; text: "Configure Event columns"; onClicked: eventColumns.open() } }
                    SettingRow { label: "Saved views"; help: "Reload persisted layouts and filters without querying Kubernetes."; Button { objectName: "settingsReloadViews"; text: "Reload saved views"; enabled: !workspace.busy; onClicked: { workspace.reloadSavedViews(); workspace.reloadTableLayouts() } } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "sources"
                    RowLayout { Layout.fillWidth: true; TextField { id: importPath; objectName: "settingsSourcePath"; Layout.fillWidth: true; placeholderText: "Kubeconfig file or folder (~ supported)"; enabled: !workspace.busy; Accessible.name: "Kubeconfig file or folder"; onAccepted: workspace.importFile(text) } Button { objectName: "settingsImportSource"; text: "Import"; enabled: !workspace.busy; onClicked: importPath.text.trim() ? workspace.importFile(importPath.text) : settings.fileRequested() } }
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        IconButton { glyph: "ConfigMap"; text: "Browse file"; enabled: !workspace.busy; onClicked: settings.fileRequested() }
                        IconButton { glyph: "Namespace"; text: "Browse folder"; enabled: !workspace.busy; onClicked: settings.folderRequested() }
                        IconButton { glyph: "Reset"; text: "Reload imported metadata"; enabled: !workspace.busy; onClicked: workspace.reload() }
                        IconButton { objectName: "pasteSourceButton"; glyph: "Pencil"; text: "Paste kubeconfig"; enabled: !workspace.busy; onClicked: pasteSourceDialog.open() }
                        IconButton { objectName: "importHomeButton"; glyph: "Cluster"; text: "Import ~/.kube/config"; enabled: !workspace.busy; onClicked: workspace.importHome() }
                        IconButton { objectName: "refreshSourceFilesButton"; glyph: "Reset"; text: "Reimport changed source files"; enabled: !workspace.busy && workspace.contexts.length > 0; onClicked: workspace.refreshSources() }
                    }
                    Label { Layout.fillWidth: true; text: workspace.sourceImportError; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger }
                    RowLayout {
                        Layout.fillWidth: true; visible: workspace.sourceImportNotice !== ""
                        Label { Layout.fillWidth: true; text: workspace.sourceImportNotice; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                        IconButton { objectName: "settingsSourceImportDetails"; glyph: "Event"; text: "View import failures"; visible: workspace.sourceImportIssues.length > 0; onClicked: settings.sourceFailuresRequested() }
                    }
                    ResourceGrid {
                        objectName: "settingsSourceList"
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 400
                        visible: settings.section === "sources"; prefix: "source"
                        tableModel: workspace.sourceTable; columns: workspace.sourceColumns
                        sortColumn: workspace.sourceSortColumn; sortDirection: workspace.sourceSortDirection
                        rowHeight: settings.width < 650 ? 44 : 32
                        sortableColumns: 7; actionColumns: [7,8]; accessoryColumns: [7,8]
                        emptyText: "No imported contexts. Open or paste a kubeconfig."
                        onSortRequested: column => workspace.sortSourceColumn(column)
                        onCopyRequested: (row,column) => workspace.copySourceCell(pathAt(row),column)
                        onCopyPathRequested: (identity,column) => workspace.copySourceCell(identity,column)
                        onInspectRequested: identity => { if (!workspace.busy) workspace.openContext(identity) }
                        onActionRequested: (identity,column) => {
                            if (workspace.busy) return
                            if (column === 8) workspace.requestSourceRemoval(identity)
                            else { const context = workspace.contexts.find(context => context.id === identity); if (context) sourceAliasDialog.edit(context) }
                        }
                        cellAccessory: Component {
                            IconButton {
                                id: sourceAction
                                readonly property string identity: parent.identity
                                readonly property bool isRemoval: parent.column === 8
                                objectName: (isRemoval ? "removeSource_" : "renameSource_") + identity
                                glyph: isRemoval ? "Close" : "Pencil"
                                text: (isRemoval ? "Remove source context " : "Rename source context ") + parent.resourceName
                                enabled: !workspace.busy
                                onClicked: {
                                    if (sourceAction.isRemoval) workspace.requestSourceRemoval(sourceAction.identity)
                                    else { const context = workspace.contexts.find(context => context.id === sourceAction.identity); if (context) sourceAliasDialog.edit(context) }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "diagnostics"
                    RowLayout { Layout.fillWidth: true; Label { Layout.fillWidth: true; text: "Runtime diagnostics"; font.bold: true } Button { objectName: "refreshSettingsDiagnostics"; text: "Refresh snapshot"; onClicked: settings.refreshDiagnostics() } }
                    Label { objectName: "diagnosticSampledAt"; Layout.fillWidth: true; text: settings.diagnosticTimestamp ? "Snapshot: " + new Date(settings.diagnosticTimestamp).toLocaleString(Qt.locale()) : ""; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; wrapMode: Text.Wrap }
                    ResourceGrid {
                        id: diagnosticGrid
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 480
                        visible: settings.section === "diagnostics"; prefix: "diagnostic"; inspectable: false
                        findShortcutEnabled: !auditGrid.containsActiveFocus
                        tableModel: workspace.diagnosticTable; columns: workspace.diagnosticColumns
                        sortColumn: workspace.diagnosticSortColumn; sortDirection: workspace.diagnosticSortDirection
                        emptyText: "No runtime snapshot."
                        cellName: function(row,column,identity) { return column===1 ? "diagnosticValue_"+identity : "diagnosticCell_"+row+"_"+column }
                        onSortRequested: column => workspace.sortDiagnosticColumn("diagnostic",column)
                        onCopyRequested: (row,column) => workspace.copyDiagnosticCell("diagnostic",pathAt(row),column)
                        onCopyPathRequested: (identity,column) => workspace.copyDiagnosticCell("diagnostic",identity,column)
                    }
                    Label { text: "Request audit"; font.bold: true; color: workspace.appearanceColors.accent }
                    ResourceGrid {
                        id: auditGrid
                        objectName: "settingsRequestAudit"
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 300
                        visible: settings.section === "diagnostics"; prefix: "audit"; inspectable: false
                        findShortcutEnabled: auditGrid.containsActiveFocus
                        tableModel: workspace.requestAuditTable; columns: workspace.auditColumns
                        sortColumn: workspace.auditSortColumn; sortDirection: workspace.auditSortDirection
                        emptyText: "No requests in this session."
                        onSortRequested: column => workspace.sortDiagnosticColumn("audit",column)
                        onCopyRequested: (row,column) => workspace.copyDiagnosticCell("audit",pathAt(row),column)
                        onCopyPathRequested: (identity,column) => workspace.copyDiagnosticCell("audit",identity,column)
                    }
                }
                AboutView { Layout.fillWidth: true; visible: settings.section === "about" }
                Label { objectName: "inlineSettingsError"; Layout.fillWidth: true; visible: workspace.error !== ""; text: workspace.error; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap }
            }
        }
    }
}
