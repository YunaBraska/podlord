import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: settings
    LayoutMirroring.enabled: workspace.uiRightToLeft
    LayoutMirroring.childrenInherit: true
    PasteSourceDialog { id: pasteSourceDialog }
    SessionManager { id: sessionManager }
    AwsProfileDialog { id: awsProfileDialog }
    ModalDialog {
        id: sourceRemovalDialog
        objectName: "sourceRemovalDialog"
        title: workspace.uiText["settings.removeContextTitle"]
        modal: true
        focus: true
        parent: Overlay.overlay
        width: Math.min(520, parent.width - 24)
        x: (parent.width - width) / 2; y: Math.max(12, (parent.height - height) / 2)
        closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: workspace.sourceRemoval.name || ""; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: workspace.uiText["settings.removeContextHelp"]; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(180 * workspace.textScale, count * Math.max(28, workspace.textSizes.body * 1.3))
                clip: true; reuseItems: true; model: workspace.sourceRemoval.sessions || []
                ScrollBar.vertical: ScrollBar {}
                delegate: Label { required property string modelData; width: ListView.view.width; height: Math.max(28, implicitHeight); text: modelData; textFormat: Text.PlainText; elide: Text.ElideRight; Accessible.name: modelData }
            }
            Label { visible: !(workspace.sourceRemoval.sessions || []).length; text: workspace.uiText["settings.noSessionsForContext"]; textFormat: Text.PlainText }
            Label { objectName: "sourceRemovalError"; Layout.fillWidth: true; visible: text !== ""; text: workspace.sourceImportError; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap; Accessible.name: text }
        }
        footer: DialogButtonBox {
            IconButton { glyph: ""; showText: true; font.bold: true; objectName: "cancelSourceRemoval"; text: workspace.uiText["action.cancel"]; enabled: !workspace.busy; onClicked: sourceRemovalDialog.close() }
            IconButton { glyph: ""; showText: true; font.bold: true; objectName: "confirmSourceRemoval"; text: workspace.busy ? workspace.uiText["settings.removing"] : workspace.uiText["settings.remove"]; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.removeContextAccessible"]; onClicked: workspace.confirmSourceRemoval() }
        }
        onAboutToHide: workspace.cancelSourceRemoval()
        Connections {
            target: workspace
            function onSourceRemovalChanged() {
                if (workspace.sourceRemoval.contextId) sourceRemovalDialog.open()
                else if (sourceRemovalDialog.visible) sourceRemovalDialog.close()
            }
        }
    }
    SourceAliasDialog { id: sourceAliasDialog }
    signal fileRequested()
    signal folderRequested()
    signal ociImportRequested()
    signal gkeImportRequested()
    signal sourceFailuresRequested()
    property string section: "alerts"
    readonly property var sections: ["alerts", "appearance", "diagnostics", "graphics", "privacy", "sources", "sync", "workspace", "about"].map(id => ({id: id, label: workspace.uiText["settings." + id]}))
    property string diagnosticTimestamp: ""
    function refreshDiagnostics() { diagnosticTimestamp = workspace.refreshSettingsDiagnostics() }
    Timer {
        objectName: "diagnosticsRefreshTimer"
        interval: 5000; repeat: true
        running: settings.visible && settings.section === "diagnostics"
            && settings.Window.window && settings.Window.window.visibility !== Window.Minimized
            && settings.Window.window.visibility !== Window.Hidden
        onTriggered: settings.refreshDiagnostics()
    }
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
        cacheHours.value = workspace.collectionCacheHours
        if (visible && section === "sync") workspace.refreshCacheUsage()
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
    component SectionButton: WorkspaceTab {
        required property string destination
        checked: settings.section === destination
        onClicked: { settings.section = destination; if (destination === "alerts") workspace.setWorkspacePage("alerts") }
    }
    component SettingRow: GridLayout {
        required property string label
        required property string help
        default property alias controls: editor.data
        readonly property bool narrow: width < 650 * workspace.textScale
        Layout.fillWidth: true; columns: narrow ? 1 : 2
        columnSpacing: 14; rowSpacing: 6
        data: [
        Label { Layout.row: 0; Layout.column: 0; Layout.preferredWidth: parent.narrow ? -1 : 240; text: parent.label; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap }
        ,ColumnLayout { id: editor; Layout.row: parent.narrow ? 1 : 0; Layout.column: parent.narrow ? 0 : 1; Layout.fillWidth: true; Layout.maximumWidth: parent.narrow ? Number.POSITIVE_INFINITY : 420 }
        ,Label { Layout.row: parent.narrow ? 2 : 1; Layout.column: parent.narrow ? 0 : 1; Layout.fillWidth: true; text: parent.help; visible: text !== ""; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; font.pixelSize: workspace.textSizes.small; wrapMode: Text.Wrap }
        ,Rectangle { Layout.row: parent.narrow ? 3 : 2; Layout.column: 0; Layout.columnSpan: parent.columns; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 10; Layout.bottomMargin: 10; color: workspace.appearanceColors.border }
        ]
    }
    TableColumnsDialog { id: resourceColumns; tableType: "resource"; columns: workspace.resourceColumns }
    TableColumnsDialog { id: eventColumns; tableType: "event"; columns: workspace.eventColumns }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: workspace.uiText["settings.title"]; font.pixelSize: workspace.textSizes.heading; font.bold: true; color: workspace.appearanceColors.accent }
        TabBar {
            id: settingsTabs
            Layout.fillWidth: true; Layout.minimumWidth: 0
            spacing: 0
            clip: true
            currentIndex: settings.sections.findIndex(entry => entry.id === settings.section)
            onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex < settings.sections.length) {
                const destination = settings.sections[currentIndex].id
                if (settings.section !== destination) settings.section = destination
            }
            visible: settings.width >= 650 * workspace.textScale
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
            objectName: "settingsSectionPicker"; Layout.fillWidth: true; visible: settings.width < 650 * workspace.textScale
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
            contentWidth: availableWidth; contentHeight: settingsFields.implicitHeight; clip: true
            ColumnLayout {
                id: settingsFields
                width: scroll.availableWidth; spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "appearance"
                    SettingRow { label: workspace.uiText["settings.theme"]; help: workspace.uiText["settings.themeHelp"]; ComboBox { id: theme; objectName: "inlineAppearanceTheme"; Layout.fillWidth: true; model: workspace.themeNames; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.theme"]; onActivated: workspace.saveAppearance(currentText, workspace.themeVariant) } }
                    SettingRow { label: workspace.uiText["settings.variant"]; help: workspace.uiText["settings.variantHelp"]; ComboBox { id: variant; objectName: "inlineAppearanceVariant"; Layout.fillWidth: true; model: ["dark", "light"]; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.variant"]; onActivated: workspace.saveAppearance(workspace.themeName, currentText) } }
                    SettingRow { label: workspace.uiText["settings.language"]; help: workspace.uiText["settings.languageHelp"]; ComboBox { id: language; objectName: "inlineUiLanguage"; Layout.fillWidth: true; model: workspace.uiLanguages; textRole: "name"; valueRole: "code"; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.language"]; onActivated: workspace.saveUiLanguage(currentValue) } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "graphics"
                    SettingRow { label: workspace.uiText["settings.radarWater"]; help: workspace.uiText["settings.radarWaterHelp"]; WrappingCheckBox { objectName: "inlineRadarWaterEnabled"; text: workspace.uiText["alert.enabledState"]; checked: workspace.radarWaterEnabled; enabled: !workspace.busy; onClicked: workspace.saveRadarWater(checked, workspace.radarWaterSpeedPercent) } }
                    SettingRow { label: workspace.uiText["settings.radarWaterSpeed"]; help: workspace.uiText["settings.radarWaterSpeedHelp"]; RowLayout { Layout.fillWidth: true; Slider { objectName: "inlineRadarWaterSpeed"; Layout.fillWidth: true; from: 0; to: 100; stepSize: 5; value: workspace.radarWaterSpeedPercent; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.waterSpeedPercent"]; onMoved: if (!pressed) workspace.saveRadarWater(workspace.radarWaterEnabled, Math.round(value)); onPressedChanged: if (!pressed && enabled) workspace.saveRadarWater(workspace.radarWaterEnabled, Math.round(value)) } Label { text: workspace.radarWaterSpeedPercent + "%"; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight } } }
                    SettingRow { label: workspace.uiText["settings.reducedMotion"]; help: workspace.uiText["settings.reducedMotionHelp"]; WrappingCheckBox { objectName: "inlineReducedMotion"; text: workspace.uiText["alert.enabledState"]; checked: workspace.alerts.reducedMotion; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(workspace.alerts.muted, checked) } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "sync"
                    SettingRow { label: workspace.uiText["settings.inactiveSessionSync"]; help: workspace.uiText["settings.inactiveSessionSyncHelp"]; SpinBox { id: inactive; objectName: "inlineInactiveMinutes"; from: 0; to: 2147483647; editable: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.inactiveSessionSyncMinutes"] } }
                    SettingRow { label: workspace.uiText["settings.requestLimitPerMinute"]; help: workspace.uiText["settings.requestLimitPerMinuteHelp"]; SpinBox { id: requests; objectName: "inlineRequestLimit"; from: 0; to: 60000; editable: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.requestLimitPerMinute"] } }
                    SettingRow { label: workspace.uiText["settings.resourceCacheTtl"]; help: workspace.uiText["settings.resourceCacheTtlHelp"]; SpinBox { id: cacheHours; objectName: "inlineCollectionCacheHours"; from: 1; to: 2147483647; editable: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.resourceCacheTtlHours"] } }
                    Label { objectName: "collectionCacheUsage"; Layout.fillWidth: true; text: workspace.uiText["settings.cachedPayloadEstimate"] + (workspace.cachePayloadBytes / 1048576).toFixed(2) + " MiB"; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.description: workspace.uiText["settings.cachedPayloadDescription"] }
                    SettingRow { label: workspace.uiText["settings.retainedLogsPerPod"]; help: workspace.uiText["settings.retainedLogsPerPodHelp"]; TextField { id: logs; objectName: "inlineLogLimit"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.retainedLogsPerPodMb"] } }
                    IconButton { glyph: "Save"; showText: true; font.bold: true; objectName: "inlineSaveSync"; text: workspace.busy ? workspace.uiText["settings.saving"] : workspace.uiText["settings.saveSyncSettings"]; enabled: !workspace.busy; onClicked: workspace.saveReadSettings(requests.value, inactive.value, logs.text, cacheHours.value) }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "privacy"
                    SettingRow { label: workspace.uiText["settings.telemetry"]; help: workspace.uiText["settings.telemetryHelp"]; Label { objectName: "privacyTelemetry"; text: workspace.uiText["settings.off"]; font.bold: true } }
                    SettingRow { label: workspace.uiText["settings.secretValues"]; help: workspace.uiText["settings.secretValuesHelp"]; Label { text: workspace.uiText["settings.maskedByDefault"] } }
                    SettingRow { label: workspace.uiText["settings.yamlLimit"]; help: workspace.uiText["settings.yamlLimitHelp"]; RowLayout { Layout.fillWidth: true; TextField { id: yaml; objectName: "inlineYamlLimit"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.yamlLimitMiB"] } IconButton { glyph: "Save"; showText: true; font.bold: true; objectName: "inlineSaveYamlLimit"; text: workspace.uiText["action.save"]; enabled: !workspace.busy; onClicked: workspace.saveYamlLimit(yaml.text) } } }
                    SettingRow { label: workspace.uiText["settings.requestAuditRetention"]; help: workspace.uiText["settings.requestAuditRetentionHelp"]; Label { text: workspace.uiText["settings.inMemoryOnly"] } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "workspace"
                    SettingRow { label: workspace.uiText["settings.workspaceRestore"]; help: workspace.uiText["settings.workspaceRestoreHelp"]; WrappingCheckBox { objectName: "inlineWorkspaceRestore"; text: workspace.uiText["settings.restoreTabsOnLaunch"]; checked: workspace.workspaceRestoreEnabled; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.restoreTabsOnLaunch"]; onClicked: workspace.saveWorkspaceRestore(checked) } }
                    SettingRow { label: workspace.uiText["settings.sessionConfiguration"]; help: workspace.uiText["settings.sessionConfigurationHelp"]; IconButton { glyph: ""; showText: true; font.bold: true; objectName: "settingsManageSessions"; text: workspace.uiText["settings.manageSessions"]; enabled: !workspace.busy && workspace.sessions.length > 0; onClicked: sessionManager.open() } }
                    SettingRow { label: workspace.uiText["settings.resourceColumns"]; help: workspace.uiText["settings.resourceColumnsHelp"]; IconButton { glyph: ""; showText: true; font.bold: true; objectName: "settingsResourceColumns"; text: workspace.uiText["settings.configureResourceColumns"]; onClicked: resourceColumns.open() } }
                    SettingRow { label: workspace.uiText["settings.eventColumns"]; help: workspace.uiText["settings.eventColumnsHelp"]; IconButton { glyph: ""; showText: true; font.bold: true; objectName: "settingsEventColumns"; text: workspace.uiText["settings.configureEventColumns"]; onClicked: eventColumns.open() } }
                    SettingRow { label: workspace.uiText["nav.savedViews"]; help: workspace.uiText["settings.reloadViewsHelp"]; IconButton { glyph: ""; showText: true; font.bold: true; objectName: "settingsReloadViews"; text: workspace.uiText["settings.reloadViews"]; enabled: !workspace.busy; onClicked: { workspace.reloadSavedViews(); workspace.reloadTableLayouts() } } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "sources"
                    RowLayout { Layout.fillWidth: true; TextField { id: importPath; objectName: "settingsSourcePath"; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText; visible: !workspace.appStore; Layout.fillWidth: true; placeholderText: workspace.uiText["settings.kubeconfigPathPlaceholder"]; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.kubeconfigPathPlaceholder"]; onAccepted: workspace.importFile(text) } IconButton { glyph: ""; showText: true; font.bold: true; objectName: "settingsImportSource"; text: workspace.uiText["action.import"]; enabled: !workspace.busy; onClicked: !workspace.appStore && importPath.text.trim() ? workspace.importFile(importPath.text) : settings.fileRequested() } }
                    IconButton {
                        id: sourceActionsButton
                        objectName: "sourceActionsMenuButton"
                        glyph: "Menu"
                        text: workspace.uiText["settings.moreSourceActions"]
                        Layout.alignment: Qt.AlignLeft
                        enabled: !workspace.busy
                        onClicked: sourceActions.open()
                    }
                    Popup {
                        id: sourceActions
                        objectName: "sourceActionsMenu"
                        parent: Overlay.overlay
                        x: Math.max(12, Math.min(sourceActionsButton.mapToItem(parent, 0, 0).x, parent.width - width - 12))
                        y: Math.max(12, Math.min(sourceActionsButton.mapToItem(parent, 0, sourceActionsButton.height).y, parent.height - height - 12))
                        width: Math.min(320, settings.width - 28)
                        height: Math.min(sourceActionItems.implicitHeight + 2 * padding, parent.height - 24)
                        padding: 8; focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        onOpened: browseSourceFile.forceActiveFocus()
                        background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
                        contentItem: ScrollView {
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                            id: sourceActionItems
                            width: parent.width
                            spacing: 4
                            IconButton { id: browseSourceFile; objectName: "browseSourceFile"; glyph: ""; showText: true; text: workspace.uiText["settings.browseKubeconfigFile"]; Layout.fillWidth: true; enabled: !workspace.busy; onClicked: { sourceActions.close(); settings.fileRequested() } }
                            IconButton { objectName: "browseSourceFolder"; visible: !workspace.appStore; glyph: ""; showText: true; text: workspace.uiText["settings.browseKubeconfigFolder"]; Layout.fillWidth: true; enabled: !workspace.busy; onClicked: { sourceActions.close(); settings.folderRequested() } }
                            IconButton { objectName: "pasteSourceButton"; glyph: ""; showText: true; text: workspace.uiText["settings.pasteKubeconfig"]; Layout.fillWidth: true; enabled: !workspace.busy; onClicked: { sourceActions.close(); pasteSourceDialog.open() } }
                            IconButton { objectName: "importHomeButton"; visible: !workspace.appStore; glyph: ""; showText: true; text: workspace.uiText["settings.importHomeKubeconfig"]; Layout.fillWidth: true; enabled: !workspace.busy; onClicked: { sourceActions.close(); workspace.importHome() } }
                            IconButton { objectName: "refreshSourceFilesButton"; visible: !workspace.appStore; glyph: ""; showText: true; text: workspace.uiText["settings.reimportChangedSources"]; Layout.fillWidth: true; enabled: !workspace.busy && workspace.contexts.length > 0; onClicked: { sourceActions.close(); workspace.refreshSources() } }
                            IconButton { objectName: "importOciKeyFromSettings"; glyph: ""; showText: true; text: workspace.uiText["settings.importOciKey"]; Layout.fillWidth: true; enabled: !workspace.busy && workspace.contexts.length > 0; onClicked: { sourceActions.close(); settings.ociImportRequested() } }
                            IconButton { objectName: "importAwsProfileFromSettings"; glyph: ""; showText: true; text: workspace.uiText["settings.importAwsProfile"]; Layout.fillWidth: true; enabled: !workspace.busy && workspace.contexts.length > 0; onClicked: { sourceActions.close(); awsProfileDialog.open() } }
                            IconButton { objectName: "importGkeKeyFromSettings"; glyph: ""; showText: true; text: workspace.uiText["settings.importGkeKey"]; Layout.fillWidth: true; enabled: !workspace.busy && workspace.contexts.length > 0; onClicked: { sourceActions.close(); settings.gkeImportRequested() } }
                            IconButton { glyph: ""; showText: true; text: workspace.uiText["settings.reloadImportedMetadata"]; Layout.fillWidth: true; enabled: !workspace.busy; onClicked: { sourceActions.close(); workspace.reload() } }
                            }
                        }
                    }
                    Label { Layout.fillWidth: true; text: workspace.sourceImportError; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger }
                    RowLayout {
                        Layout.fillWidth: true; visible: workspace.sourceImportNotice !== ""
                        Label { Layout.fillWidth: true; text: workspace.sourceImportNotice; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                        IconButton { objectName: "settingsSourceImportDetails"; glyph: "Event"; text: workspace.uiText["settings.viewImportFailures"]; visible: workspace.sourceImportIssues.length > 0; onClicked: settings.sourceFailuresRequested() }
                    }
                    ResourceGrid {
                        objectName: "settingsSourceList"
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 400
                        visible: settings.section === "sources"; prefix: "source"
                        tableModel: workspace.sourceTable; columns: workspace.sourceColumns
                        sortColumn: workspace.sourceSortColumn; sortDirection: workspace.sourceSortDirection
                        rowHeight: (settings.width < 650 * workspace.textScale ? 44 : 32) * workspace.textScale
                        sortableColumns: 8; actionColumns: [8,9]; accessoryColumns: [8,9]
                        emptyText: workspace.uiText["settings.noImportedContexts"]
                        onSortRequested: column => workspace.sortSourceColumn(column)
                        onCopyRequested: (row,column) => workspace.copySourceCell(pathAt(row),column)
                        onCopyPathRequested: (identity,column) => workspace.copySourceCell(identity,column)
                        onInspectRequested: identity => { if (!workspace.busy) workspace.inspectSourceContext(identity) }
                        onActionRequested: (identity,column) => {
                            if (workspace.busy) return
                            if (column === 9) workspace.requestSourceRemoval(identity)
                            else { const context = workspace.contexts.find(context => context.id === identity); if (context) sourceAliasDialog.edit(context) }
                        }
                        cellAccessory: Component {
                            IconButton {
                                id: sourceAction
                                readonly property string identity: parent.identity
                                readonly property bool isRemoval: parent.column === 9
                                objectName: (isRemoval ? "removeSource_" : "renameSource_") + identity
                                glyph: isRemoval ? "Close" : "Pencil"
                                text: (isRemoval ? workspace.uiText["settings.removeSourceContext"] : workspace.uiText["settings.renameSourceContext"]) + parent.resourceName
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
                    Label { Layout.fillWidth: true; text: workspace.uiText["settings.diagnostics"]; font.bold: true }
                    Label { objectName: "diagnosticSampledAt"; Layout.fillWidth: true; text: workspace.displayDateTime(settings.diagnosticTimestamp); textFormat: Text.PlainText; color: workspace.appearanceColors.muted; wrapMode: Text.Wrap }
                    ResourceGrid {
                        id: diagnosticGrid
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.preferredHeight: Math.min(300, Math.max(160, scroll.height * 0.38))
                        visible: settings.section === "diagnostics"; prefix: "diagnostic"; inspectable: false
                        findShortcutEnabled: !auditGrid.containsActiveFocus
                        tableModel: workspace.diagnosticTable; columns: workspace.diagnosticColumns
                        sortColumn: workspace.diagnosticSortColumn; sortDirection: workspace.diagnosticSortDirection
                        emptyText: workspace.uiText["settings.noRuntimeSnapshot"]
                        cellName: function(row,column,identity) { return column===1 ? "diagnosticValue_"+identity : "diagnosticCell_"+row+"_"+column }
                        onSortRequested: column => workspace.sortDiagnosticColumn("diagnostic",column)
                        onCopyRequested: (row,column) => workspace.copyDiagnosticCell("diagnostic",pathAt(row),column)
                        onCopyPathRequested: (identity,column) => workspace.copyDiagnosticCell("diagnostic",identity,column)
                    }
                    Label { text: workspace.uiText["settings.requestAuditTitle"]; font.bold: true; color: workspace.appearanceColors.accent }
                    ResourceGrid {
                        id: auditGrid
                        objectName: "settingsRequestAudit"
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 300
                        visible: settings.section === "diagnostics"; prefix: "audit"; inspectable: false
                        findShortcutEnabled: auditGrid.containsActiveFocus
                        tableModel: workspace.requestAuditTable; columns: workspace.auditColumns
                        sortColumn: workspace.auditSortColumn; sortDirection: workspace.auditSortDirection
                        emptyText: workspace.uiText["settings.noRequestsInSession"]
                        onSortRequested: column => workspace.sortDiagnosticColumn("audit",column)
                        onCopyRequested: (row,column) => workspace.copyDiagnosticCell("audit",pathAt(row),column)
                        onCopyPathRequested: (identity,column) => workspace.copyDiagnosticCell("audit",identity,column)
                    }
                }
                AboutView {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 640
                    Layout.alignment: Qt.AlignLeft
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.topMargin: 14
                    visible: settings.section === "about"
                }
                Label { objectName: "inlineSettingsError"; Layout.fillWidth: true; visible: workspace.error !== ""; text: workspace.error; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; wrapMode: Text.Wrap }
            }
        }
    }
}
