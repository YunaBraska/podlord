import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: settings
    LayoutMirroring.enabled: workspace.uiRightToLeft
    LayoutMirroring.childrenInherit: true
    PasteSourceDialog { id: pasteSourceDialog }
    AwsProfileDialog { id: awsProfileDialog }
    signal fileRequested()
    signal folderRequested()
    signal ociImportRequested()
    signal gkeImportRequested()
    signal sourceFailuresRequested()
    signal renameContextRequested(var context)
    property string section: "alerts"
    readonly property var sections: ["alerts", "appearance", "diagnostics", "sources", "sync", "about"].map(id => ({id: id, label: workspace.uiText["settings." + id]}))
    property string diagnosticTimestamp: ""
    property int pendingMusicVolume: -1
    property int pendingAlertVolume: -1
    property bool audioSaving: false
    Timer {
        id: volumeFeedback
        property bool music: false
        property int percent: 0
        interval: 250
        onTriggered: {
            if (!settings.visible || settings.section !== "appearance") return
            if (workspace.busy || settings.audioSaving || settings.pendingMusicVolume >= 0 || settings.pendingAlertVolume >= 0) { restart(); return }
            if (percent === (music ? workspace.musicVolumePercent : workspace.alertVolumePercent)) workspace.previewAudioVolume(music)
        }
    }
    function saveAudioVolume(music, value, commit) {
        const percent = Math.round(value)
        if (music) pendingMusicVolume = percent
        else pendingAlertVolume = percent
        if (commit === false) { volumeFeedback.stop(); return }
        volumeFeedback.music = music
        volumeFeedback.percent = percent
        volumeFeedback.restart()
        flushAudioVolumes()
    }
    function flushAudioVolumes() {
        if (audioSaving || (pendingMusicVolume < 0 && pendingAlertVolume < 0) || workspace.busy
            || musicVolumeSetting.adjusting || alertVolumeSetting.adjusting) return
        if (pendingMusicVolume === workspace.musicVolumePercent) pendingMusicVolume = -1
        if (pendingAlertVolume === workspace.alertVolumePercent) pendingAlertVolume = -1
        if (pendingMusicVolume < 0 && pendingAlertVolume < 0) return
        const music = pendingMusicVolume < 0 ? workspace.musicVolumePercent : pendingMusicVolume
        const alerts = pendingAlertVolume < 0 ? workspace.alertVolumePercent : pendingAlertVolume
        workspace.music.setVolumePercent(music)
        workspace.alerts.setVolumePercent(alerts)
        audioSaving = true
        if (!workspace.saveAudioVolumes(music, alerts)) {
            audioSaving = false
            pendingMusicVolume = -1
            pendingAlertVolume = -1
            volumeFeedback.stop()
            workspace.music.setVolumePercent(workspace.musicVolumePercent)
            workspace.alerts.setVolumePercent(workspace.alertVolumePercent)
        }
    }
    function refreshDiagnostics() { diagnosticTimestamp = workspace.refreshSettingsDiagnostics() }
    Timer {
        objectName: "diagnosticsRefreshTimer"
        interval: 5000; repeat: true
        running: settings.visible && settings.section === "diagnostics"
            && settings.Window.window && settings.Window.window.visibility !== Window.Minimized
            && settings.Window.window.visibility !== Window.Hidden
        onTriggered: settings.refreshDiagnostics()
    }
    Timer {
        id: syncAutosave
        interval: 450
        onTriggered: {
            if (workspace.busy) { restart(); return }
            if (requests.value !== workspace.requestLimit || inactive.value !== workspace.inactiveSyncMinutes
                || logs.text !== workspace.logLimitMb.toString() || cacheHours.value !== workspace.collectionCacheHours)
                workspace.saveReadSettings(requests.value, inactive.value, logs.text, cacheHours.value)
        }
    }
    function synchronizeAppearance() {
        theme.currentIndex = workspace.themeNames.indexOf(workspace.themeName)
        variant.currentIndex = variant.model.indexOf(workspace.themeVariant)
        language.currentIndex = language.model.findIndex(option => option.code === workspace.uiLanguage)
    }
    function synchronize() {
        synchronizeAppearance()
        if (visible && section === "sources") workspace.refreshSourceTable()
        if (!syncAutosave.running && !workspace.busy) {
            requests.value = workspace.requestLimit
            inactive.value = workspace.inactiveSyncMinutes
            cacheHours.value = workspace.collectionCacheHours
            logs.text = workspace.logLimitMb.toString()
        }
        if (visible && section === "sync") workspace.refreshCacheUsage()
        yaml.text = workspace.yamlLimitMiB.toString()
    }
    onSectionChanged: { synchronize(); if (section !== "appearance") volumeFeedback.stop(); if (section === "diagnostics") refreshDiagnostics() }
    onVisibleChanged: { if (visible) { synchronize(); if (section === "diagnostics") refreshDiagnostics() } else volumeFeedback.stop() }
    Component.onCompleted: synchronize()
    Connections {
        target: workspace
        function onAppearanceChanged() { settings.synchronizeAppearance() }
        function onLanguageChanged() { settings.synchronizeAppearance() }
        function onCatalogsChanged() { if (settings.visible && settings.section === "sources") workspace.refreshSourceTable() }
        function onChanged() { settings.flushAudioVolumes() }
        function onAudioVolumesSaveFinished(success) {
            if (!settings.audioSaving) return
            settings.audioSaving = false
            if (!success) {
                settings.pendingMusicVolume = -1
                settings.pendingAlertVolume = -1
                volumeFeedback.stop()
                return
            }
            if (settings.pendingMusicVolume >= 0) workspace.music.setVolumePercent(settings.pendingMusicVolume)
            if (settings.pendingAlertVolume >= 0) workspace.alerts.setVolumePercent(settings.pendingAlertVolume)
            settings.flushAudioVolumes()
        }
    }
    padding: 14
    background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
    component SectionButton: WorkspaceTab {
        required property string destination
        checked: settings.section === destination
        onClicked: { settings.section = destination; if (destination === "alerts") workspace.setWorkspacePage("alerts") }
    }
    component PreferenceSlider: Slider {
        id: preferenceSlider
        Layout.minimumHeight: 44
        background: Rectangle {
            x: preferenceSlider.leftPadding
            y: preferenceSlider.topPadding + (preferenceSlider.availableHeight - height) / 2
            width: preferenceSlider.availableWidth
            implicitHeight: 4
            height: implicitHeight
            radius: 2
            color: workspace.appearanceColors.border
            Rectangle {
                width: preferenceSlider.position * parent.width
                height: parent.height
                x: preferenceSlider.mirrored ? parent.width - width : 0
                radius: 2
                color: workspace.appearanceColors.text
            }
        }
        handle: Rectangle {
            x: preferenceSlider.leftPadding + preferenceSlider.visualPosition * (preferenceSlider.availableWidth - width)
            y: preferenceSlider.topPadding + (preferenceSlider.availableHeight - height) / 2
            implicitWidth: 24; implicitHeight: 24; radius: 12
            color: workspace.appearanceColors.text
            border.color: workspace.appearanceColors.panel
            border.width: 2
            antialiasing: true
            Rectangle {
                anchors.fill: parent; anchors.margins: -4
                visible: preferenceSlider.visualFocus
                color: "transparent"; radius: 16
                border.width: 2; border.color: workspace.appearanceColors.text
            }
        }
    }
    component VolumeSetting: SettingRow {
        id: audioRow
        required property bool music
        property alias adjusting: volume.pressed
        label: workspace.uiText[music ? "settings.musicVolume" : "settings.alertVolume"]
        help: ""
        RowLayout {
            Layout.fillWidth: true
            PreferenceSlider {
                id: volume
                objectName: audioRow.music ? "settingsMusicVolume" : "settingsAlertVolume"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 44
                from: 0; to: 100; stepSize: 1
                value: audioRow.music
                    ? (settings.pendingMusicVolume < 0 ? workspace.musicVolumePercent : settings.pendingMusicVolume)
                    : (settings.pendingAlertVolume < 0 ? workspace.alertVolumePercent : settings.pendingAlertVolume)
                Accessible.name: audioRow.label
                onMoved: {
                    if (audioRow.music) workspace.music.setVolumePercent(Math.round(value))
                    else workspace.alerts.setVolumePercent(Math.round(value))
                    settings.saveAudioVolume(audioRow.music, value, !pressed)
                }
                onPressedChanged: if (!pressed && enabled) settings.saveAudioVolume(audioRow.music, value)
            }
            Label {
                id: volumeText
                text: Math.round(volume.value) + "%"
                Layout.preferredWidth: Math.max(54, percentWidth.advanceWidth)
                horizontalAlignment: Text.AlignRight
                Accessible.ignored: true
                TextMetrics { id: percentWidth; text: "100%"; font: volumeText.font }
            }
        }
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
            SectionButton { objectName: "settingsDiagnosticsSection"; text: settings.sections[2].label; destination: "diagnostics" }
            SectionButton { objectName: "settingsSourcesSection"; text: workspace.uiText["settings.sources"]; destination: "sources" }
            SectionButton { objectName: "settingsSyncSection"; text: workspace.uiText["settings.sync"]; destination: "sync" }
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
                    VolumeSetting { id: musicVolumeSetting; music: true }
                    VolumeSetting { id: alertVolumeSetting; music: false }
                    SettingRow { label: workspace.uiText["settings.theme"]; help: ""; ComboBox { id: theme; objectName: "inlineAppearanceTheme"; Layout.fillWidth: true; model: workspace.themeNames; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.theme"]; onActivated: workspace.saveAppearance(currentText, workspace.themeVariant) } }
                    SettingRow { label: workspace.uiText["settings.variant"]; help: ""; ComboBox { id: variant; objectName: "inlineAppearanceVariant"; Layout.fillWidth: true; model: ["dark", "light"]; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.variant"]; onActivated: workspace.saveAppearance(workspace.themeName, currentText) } }
                    SettingRow { label: workspace.uiText["settings.language"]; help: ""; ComboBox { id: language; objectName: "inlineUiLanguage"; Layout.fillWidth: true; model: workspace.uiLanguages; textRole: "name"; valueRole: "code"; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.language"]; onActivated: workspace.saveUiLanguage(currentValue) } }
                    SettingRow { label: workspace.uiText["settings.workspaceRestore"]; help: ""; WrappingCheckBox { objectName: "inlineWorkspaceRestore"; text: workspace.uiText["settings.restoreTabsOnLaunch"]; checked: workspace.workspaceRestoreEnabled; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.workspaceRestore"] + ": " + text; onClicked: workspace.saveWorkspaceRestore(checked) } }
                    SettingRow { label: workspace.uiText["settings.radarWaterSpeed"]; help: ""; RowLayout { Layout.fillWidth: true; PreferenceSlider { objectName: "inlineRadarWaterSpeed"; Layout.fillWidth: true; from: 0; to: 100; stepSize: 5; value: workspace.radarWaterEnabled ? workspace.radarWaterSpeedPercent : 0; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.waterSpeedPercent"]; onMoved: if (!pressed) workspace.saveRadarWater(Math.round(value) > 0, Math.round(value)); onPressedChanged: if (!pressed && enabled) workspace.saveRadarWater(Math.round(value) > 0, Math.round(value)) } Label { text: (workspace.radarWaterEnabled ? workspace.radarWaterSpeedPercent : 0) + "%"; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight } } }
                    SettingRow { label: workspace.uiText["settings.reducedMotion"]; help: ""; WrappingCheckBox { objectName: "inlineReducedMotion"; text: workspace.uiText["alert.enabledState"]; Accessible.name: workspace.uiText["settings.reducedMotion"]; checked: workspace.alerts.reducedMotion; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(workspace.alerts.muted, checked) } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "sync"
                    SettingRow { label: workspace.uiText["settings.inactiveSessionSync"]; help: workspace.uiText["settings.inactiveSessionSyncHelp"]; SpinBox { id: inactive; objectName: "inlineInactiveMinutes"; from: 0; to: 2147483647; editable: true; onValueModified: syncAutosave.restart(); enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.inactiveSessionSyncMinutes"] } }
                    SettingRow { label: workspace.uiText["settings.requestLimitPerMinute"]; help: workspace.uiText["settings.requestLimitPerMinuteHelp"]; SpinBox { id: requests; objectName: "inlineRequestLimit"; from: 0; to: 60000; editable: true; onValueModified: syncAutosave.restart(); enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.requestLimitPerMinute"] } }
                    SettingRow { label: workspace.uiText["settings.resourceCacheTtl"]; help: workspace.uiText["settings.resourceCacheTtlHelp"]; SpinBox { id: cacheHours; objectName: "inlineCollectionCacheHours"; from: 1; to: 2147483647; editable: true; onValueModified: syncAutosave.restart(); enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.resourceCacheTtlHours"] } }
                    Label { objectName: "collectionCacheUsage"; Layout.fillWidth: true; text: workspace.uiText["settings.cachedPayloadEstimate"] + (workspace.cachePayloadBytes / 1048576).toFixed(2) + " MiB"; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.description: workspace.uiText["settings.cachedPayloadDescription"] }
                    SettingRow { label: workspace.uiText["settings.retainedLogsPerPod"]; help: workspace.uiText["settings.retainedLogsPerPodHelp"]; TextField { id: logs; objectName: "inlineLogLimit"; Layout.fillWidth: true; enabled: !workspace.busy; inputMethodHints: Qt.ImhDigitsOnly; onTextEdited: syncAutosave.restart(); Accessible.name: workspace.uiText["settings.retainedLogsPerPodMb"] } }
                }
                ColumnLayout {
                    Layout.fillWidth: true; visible: settings.section === "diagnostics"
                    SettingRow { label: workspace.uiText["settings.yamlLimit"]; help: workspace.uiText["settings.yamlLimitHelp"]; RowLayout { Layout.fillWidth: true; TextField { id: yaml; objectName: "inlineYamlLimit"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: workspace.uiText["settings.yamlLimitMiB"] } IconButton { glyph: "Save"; showText: true; font.bold: true; objectName: "inlineSaveYamlLimit"; text: workspace.uiText["action.save"]; enabled: !workspace.busy; onClicked: workspace.saveYamlLimit(yaml.text) } } }
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
                            else { const context = workspace.contexts.find(context => context.id === identity); if (context) settings.renameContextRequested(context) }
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
                                    else { const context = workspace.contexts.find(context => context.id === sourceAction.identity); if (context) settings.renameContextRequested(context) }
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
            }
        }
    }
}
