import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    objectName: "podlordWindow"
    width: 1440
    height: 920
    minimumWidth: 320
    minimumHeight: 360
    font.family: workspace.monospaceFamily
    font.pixelSize: workspace.textSizes.body
    font.weight: Font.Normal
    readonly property real controlHeight: Math.max(touchLayout ? 44 : 32, Math.ceil(workspace.textSizes.body * 1.3) + 12)
    property bool sidebarOpen: true
    property bool searchOpen: false
    readonly property bool terminalExpanded: terminalDialog.visible
    function openContainerTerminal(followsInspector) {
        terminalDialog.ownerSession = workspace.currentSession
        terminalDialog.followsInspector = followsInspector
        terminalDialog.open()
    }
    ContainerTerminalDialog { id: terminalDialog; hostWindow: window }
    ContextRemovalDialog {}
    function focusSearchIfOpen() {
        if (!window.searchOpen || !searchToggle.enabled) return
        if (workspace.workspacePage === "ports") portsView.item.focusSearch()
        else if (workspace.workspacePage === "events") eventFilterInput.forceActiveFocus()
        else resourceFilterInput.forceActiveFocus()
    }
    readonly property bool hasResources: workspace.currentSession !== "" && (workspace.totalResourceCount > 0 || ["resources", "dashboard"].includes(workspace.workspacePage))
    readonly property bool hasEvents: workspace.currentSession !== "" && (workspace.totalEventCount > 0 || workspace.workspacePage === "events")
    readonly property bool hasConnections: workspace.currentSession !== "" && (workspace.portForwards.length > 0 || !!workspace.terminalConnection.active || workspace.workspacePage === "ports")
    readonly property real contentRightMargin: sidebarHost.visible ? sidebarHost.width + 6 : 0
    readonly property var mapContexts: workspace.mapContexts
    readonly property bool wideLayout: width >= 900
    readonly property bool compactLandscape: width >= 600 && height < 600 && width > height
    readonly property bool touchLayout: !wideLayout || compactLandscape
    readonly property bool sidebarDocked: wideLayout || compactLandscape
    readonly property bool hasInspector: workspace.inspectorPath !== "" || !!workspace.sourceInspection.snapshotId
    readonly property bool compactInspector: (compactLandscape || width < 600) && hasInspector && !(workspace.workspacePage === "map" && workspace.mapOverview)
    readonly property real keyboardTop: Qt.inputMethod.visible
        && Qt.inputMethod.keyboardRectangle.width >= width * 0.75
        && Qt.inputMethod.keyboardRectangle.height > 0
        && Qt.inputMethod.keyboardRectangle.y + Qt.inputMethod.keyboardRectangle.height >= height - SafeArea.margins.bottom
        ? Math.max(0, Math.min(height, Qt.inputMethod.keyboardRectangle.y)) : height
    readonly property rect inputArea: Qt.rect(0, SafeArea.margins.top, width,
        Math.max(0, Math.min(keyboardTop, height - SafeArea.margins.bottom) - SafeArea.margins.top))
    readonly property bool inputPriority: Qt.inputMethod.visible
        && ((width > height && inputArea.height < 300)
            || (workspace.sourceEditing && inputArea.height < 480))
    bottomPadding: Math.max(SafeArea.margins.bottom, height - keyboardTop)
    visible: true
    DropArea {
        id: configDrop; objectName: "kubeconfigDropArea"
        anchors.fill: parent
        enabled: !workspace.busy
        onDropped: drop => {
            const paths = drop.urls.map(url => url.toString()).filter(url => url.startsWith("file:") || url.startsWith("content:"))
            if (paths.length > 0) { workspace.importFiles(paths); drop.acceptProposedAction() }
        }
        Rectangle { anchors.fill: parent; visible: configDrop.containsDrag; color: "transparent"; border.width: 2; border.color: workspace.appearanceColors.accent }
    }
    property string ociImportConfigPath: ""
    readonly property var quickOpenEntries: quickOpenMenu.visible ? [{name: workspace.uiText["nav.contexts"], header: true}]
        .concat(window.mapContexts.map(entry => ({id: entry.id, name: entry.name, context: true}))) : []
    function editTabContextAlias(tabId) {
        const contextId = workspace.sessionConfiguration(tabId).contextId
        const context = workspace.contexts.find(entry => entry.id === contextId)
        if (context) sourceAliasDialog.edit(context)
    }
    Action { id: mapCommand; text: "Open Map"; checkable: true; checked: workspace.workspacePage === "map"; onTriggered: workspace.workspacePage === "map" && !workspace.mapOverview ? workspace.showMapOverview() : workspace.setWorkspacePage("map") }
    Action { id: resourcesCommand; enabled: window.hasResources; text: "Open Resources"; checkable: true; checked: workspace.workspacePage === "resources" || workspace.workspacePage === "dashboard"; onTriggered: workspace.setWorkspacePage("resources") }
    Action { id: eventsCommand; enabled: window.hasEvents; text: "Open Events"; checkable: true; checked: workspace.workspacePage === "events"; onTriggered: workspace.setWorkspacePage("events") }
    Action { id: sourcesCommand; text: "Open Sources Settings"; onTriggered: { if (workspace.setWorkspacePage("settings")) settingsLoader.item.section = "sources" } }
    Action { id: portCommand; text: "Open Port Forwards"; enabled: workspace.canPortForward; onTriggered: workspace.preparePortForward() }
    Action { id: settingsCommand; text: "Open Settings"; checkable: true; checked: workspace.workspacePage === "settings" || workspace.workspacePage === "alerts"; onTriggered: workspace.setWorkspacePage("settings") }
    Action { id: connectionsCommand; enabled: window.hasConnections; text: workspace.uiText["nav.ports"]; checkable: true; checked: workspace.workspacePage === "ports"; onTriggered: workspace.setWorkspacePage("ports") }
    ActionGroup {
        id: workspaceNavigationActions
        actions: [mapCommand, resourcesCommand, eventsCommand, connectionsCommand, settingsCommand]
    }
    Action { id: sidebarCommand; text: "Show or hide radar and filters"; enabled: workspace.currentSession !== ""; onTriggered: window.toggleSidebar() }
    Action { id: musicCommand; text: workspace.uiText[workspace.music.playing ? "music.pause" : "music.play"]; onTriggered: workspace.music.playing ? workspace.music.pause() : workspace.music.play() }
    Action {
        id: searchCommand
        text: workspace.uiText["nav.search"]
        checkable: true; checked: window.searchOpen
        enabled: workspace.currentSession !== "" && (["resources", "events", "ports", "dashboard"].indexOf(workspace.workspacePage) >= 0 || (workspace.workspacePage === "map" && !workspace.mapOverview))
        onTriggered: {
            window.searchOpen = !window.searchOpen
            if (window.searchOpen) Qt.callLater(window.focusSearchIfOpen)
            else { searchToggle.forceActiveFocus(); Qt.inputMethod.hide() }
        }
    }
    Action {
        id: quickOpenCommand
        text: workspace.uiText["context.openFile"]
        enabled: !workspace.busy
        onTriggered: {
            const anchor = quickOpenControl.visible ? quickOpenControl : workspaceActionsButton
            quickOpenMenu.popup(anchor, 0, anchor.height)
        }
    }
    Action { id: problemsCommand; text: "Toggle Problems"; enabled: workspace.currentSession !== ""; onTriggered: workspace.setFilterMode(workspace.problemsOnly ? "" : "problems") }
    CommandPalette { id: commands; actions: [mapCommand, resourcesCommand, eventsCommand, sourcesCommand, portCommand, settingsCommand, problemsCommand] }
    Shortcut { sequences: ["Ctrl+K", "Meta+K"]; onActivated: commands.open() }
    Shortcut { sequences: [StandardKey.Find]; enabled: workspace.workspacePage === "map" && !workspace.mapOverview; onActivated: { window.searchOpen = true; Qt.callLater(window.focusSearchIfOpen) } }
    Shortcut { sequences: [StandardKey.Back]; enabled: workspace.workspacePage !== "map" && workspace.canInspectBack; onActivated: workspace.navigateInspector(-1) }
    Shortcut { sequences: [StandardKey.Forward]; enabled: workspace.workspacePage !== "map" && workspace.canInspectForward; onActivated: workspace.navigateInspector(1) }
    onVisibilityChanged: function(visibility) { workspace.setWindowVisible(visibility !== Window.Hidden && visibility !== Window.Minimized) }
    Component.onCompleted: workspace.attachWindow(window)
    onActiveChanged: workspace.setWindowFocused(active)
    onClosing: function(close) { close.accepted = workspace.requestWindowClose(); if (close.accepted) workspace.finishWindowClose() }
    title: workspace.currentSession === "" ? "Podlord" : "Podlord - " + workspace.title
    palette.window: workspace.appearanceColors.app
    palette.windowText: workspace.appearanceColors.text
    palette.base: workspace.appearanceColors.inset
    palette.placeholderText: workspace.appearanceColors.muted
    palette.alternateBase: workspace.appearanceColors.panel
    palette.text: workspace.appearanceColors.text
    palette.button: workspace.appearanceColors.raised
    palette.buttonText: workspace.appearanceColors.text
    palette.highlight: workspace.appearanceColors.selection
    palette.highlightedText: workspace.appearanceColors.text
    palette.toolTipBase: workspace.appearanceColors.panel
    palette.toolTipText: workspace.appearanceColors.text
    palette.brightText: workspace.appearanceColors.danger
    palette.link: workspace.appearanceColors.accent
    palette.linkVisited: workspace.appearanceColors.accentMuted
    palette.accent: workspace.appearanceColors.accent
    palette.mid: workspace.appearanceColors.border
    palette.dark: workspace.appearanceColors.inset
    palette.light: workspace.appearanceColors.raised
    palette.disabled.text: workspace.appearanceColors.muted
    palette.disabled.windowText: workspace.appearanceColors.muted
    palette.disabled.buttonText: workspace.appearanceColors.muted
    background: Rectangle {
        objectName: "themeSurface"
        color: workspace.appearanceColors.app
        border.width: 1
        border.color: workspace.appearanceColors.glow
        Image { objectName: "themeTexture"; anchors.fill: parent; source: workspace.appearanceColors.texture; fillMode: Image.Tile; smooth: false }
    }
    Connections {
        target: workspace.alerts
        function onZoomPreviewReady(session, path, percent) {
            if (session !== workspace.currentSession) return
            window.sidebarOpen = true
            if (!window.sidebarDocked) sidebarDrawer.open()
            Qt.callLater(function() { if (session === workspace.currentSession) sidebar.focusPath(path, percent) })
        }
    }
    Connections {
        target: workspace
        function onChanged() {
            if (workspace.portForwardTarget.session && !portForwardDialog.visible) portForwardDialog.open()
            else if (!workspace.portForwardTarget.session && portForwardDialog.visible) portForwardDialog.close()
            if (workspace.deletionPending && !deleteConfirmation.visible) deleteConfirmation.open()
            else if (!workspace.deletionPending && deleteConfirmation.visible) deleteConfirmation.close()
            if (workspace.discardPending && !discardChanges.visible) discardChanges.open()
            else if (!workspace.discardPending && discardChanges.visible) discardChanges.close()
        }
        function onWindowCloseApproved() { window.close() }
        function onYamlApplyChanged() {
            if (workspace.yamlApplyPreview && !applyPreview.visible) applyPreview.open()
            else if (!workspace.yamlApplyPreview && applyPreview.visible) applyPreview.close()
            if (workspace.yamlReconcilePending && !reconcile.visible) reconcile.open()
            else if (!workspace.yamlReconcilePending && reconcile.visible) reconcile.close()
        }
    }
    ModalDialog {
        id: discardChanges
        objectName: "discardYamlDialog"
        title: "Unapplied YAML changes"
        width: Math.min(420, window.width - 32)
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        contentItem: Label { text: workspace.discardPrompt; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        footer: DialogButtonBox {
            Button {
                id: discardStayButton
                objectName: "discardStay"
                text: "Stay"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Keys.onReturnPressed: discardChanges.reject()
                Keys.onEnterPressed: discardChanges.reject()
            }
            Button {
                objectName: "discardAccept"
                text: "Discard changes"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                Keys.onReturnPressed: discardChanges.accept()
                Keys.onEnterPressed: discardChanges.accept()
            }
        }
        onOpened: {
            // Finish platform button ordering before the view can replace the safe focus.
            footer.ensurePolished()
            const buttons = footer.contentItem as ListView
            if (buttons) buttons.forceLayout()
            discardStayButton.forceActiveFocus(Qt.TabFocusReason)
        }
        onAccepted: workspace.confirmDiscard(true)
        onRejected: workspace.confirmDiscard(false)
    }
    ModalDialog {
        id: portForwardDialog
        objectName: "portForwardDialog"
        title: "Port forward"
        width: Math.min(520, window.width - 32)
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        property bool stopMode: !!workspace.portForwardTarget.existingId
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: (workspace.portForwardTarget.title || "") + "\n" + (workspace.portForwardTarget.namespace || "") + " / " + (workspace.portForwardTarget.kind || "") + " / " + (workspace.portForwardTarget.name || ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: "Port-forward target: " + text }
            Label { Layout.fillWidth: true; text: "The endpoint is local to this computer (127.0.0.1). Closing this session tab stops its forwards. Switching tabs does not."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            RowLayout {
                visible: !portForwardDialog.stopMode
                Label { text: "Local TCP port" }
                TextField { id: forwardLocal; objectName: "portForwardLocal"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Local TCP port, 1 through 65535" }
            }
            RowLayout {
                visible: !portForwardDialog.stopMode
                Label { text: "Target TCP port" }
                TextField { id: forwardRemote; objectName: "portForwardRemote"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Pod or Service TCP port, 1 through 65535" }
            }
            Label { Layout.fillWidth: true; visible: portForwardDialog.stopMode; text: "This resource already has a session-owned forward. Stop it to release its local port."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; visible: portForwardDialog.stopMode; text: "127.0.0.1:" + workspace.portForwardTarget.localPort + " -> target:" + workspace.portForwardTarget.remotePort; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: workspace.error; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        }
        footer: DialogButtonBox {
            Button { id: forwardCancel; text: workspace.uiText["action.cancel"]; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "startPortForward"; text: "Start forwarding"; visible: !portForwardDialog.stopMode; enabled: !workspace.authenticationRequired && !workspace.busy; onClicked: workspace.startPreparedPortForward(forwardLocal.text, forwardRemote.text) }
            Button { objectName: "stopPortForward"; text: "Stop forwarding"; visible: portForwardDialog.stopMode; onClicked: workspace.stopPreparedPortForward() }
        }
        onAboutToShow: {
            forwardLocal.text = String(workspace.portForwardTarget.localPort || "")
            forwardRemote.text = String(workspace.portForwardTarget.remotePort || "")
        }
        onOpened: forwardCancel.forceActiveFocus(Qt.TabFocusReason)
        onRejected: workspace.cancelPreparedPortForward()
    }
    Loader {
        active: workspace.viewCloseNeedsDecision
        sourceComponent: Dialog {
        id: viewCloseFailure
        objectName: "viewCloseFailureDialog"
        title: workspace.uiText["view.closeFailureTitle"]
        width: Math.min(480, window.width - 32)
        parent: Overlay.overlay
        visible: true
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        contentItem: Label { width: viewCloseFailure.availableWidth; text: workspace.error + "\n" + workspace.uiText["view.closeFailureBody"]; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        footer: DialogButtonBox {
            Button { id: stayView; objectName: "stayViewClose"; text: "Stay"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "discardViewClose"; text: "Close without saving view"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onOpened: stayView.forceActiveFocus(Qt.TabFocusReason)
        onRejected: workspace.confirmViewClose(false)
        onAccepted: workspace.confirmViewClose(true)
        }
    }

    FileDialog {
        id: chooser
        objectName: "kubeconfigFileDialog"
        title: "Import kubeconfig"
        onAccepted: workspace.importFile(selectedFile.toString())
    }
    ModalDialog {
        id: ociImportDialog
        objectName: "ociImportDialog"
        title: "Import OCI API key"
        width: Math.min(480, window.width - 24)
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: "Choose an OKE context, then its OCI config and unencrypted RSA PEM key. The config's key_file path is not used."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            ComboBox { id: ociImportContext; objectName: "ociImportContext"; Layout.fillWidth: true; model: workspace.contexts; textRole: "name"; valueRole: "id"; Accessible.name: "OKE context for API key" }
            Button { objectName: "chooseOciConfig"; text: "Choose OCI config and key"; enabled: ociImportContext.currentIndex >= 0 && !workspace.busy; onClicked: ociConfigChooser.open() }
            Label { Layout.fillWidth: true; text: workspace.sourceImportError; visible: ociImportDialog.visible && text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger }
        }
        onClosed: window.ociImportConfigPath = ""
        Connections { target: workspace; function onSourceImportFinished(success) { if (success && ociImportDialog.visible) ociImportDialog.close() } }
    }
    FileDialog {
        id: ociConfigChooser
        title: "Select OCI config"
        onAccepted: { window.ociImportConfigPath = selectedFile.toString(); ociKeyChooser.open() }
        onRejected: window.ociImportConfigPath = ""
    }
    FileDialog {
        id: ociKeyChooser
        title: "Select OCI RSA private key (PEM)"
        onAccepted: {
            workspace.importOciCredentials(ociImportContext.currentValue, window.ociImportConfigPath, selectedFile.toString())
            window.ociImportConfigPath = ""
        }
        onRejected: window.ociImportConfigPath = ""
    }
    ModalDialog {
        id: gkeImportDialog
        objectName: "gkeImportDialog"
        property bool importing: false
        title: "Import GKE service-account key"
        width: Math.min(480, window.width - 24)
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: "Choose a GKE context and a Google service-account JSON key. A validated copy is kept in private app storage; the original file remains where you selected it."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            ComboBox { id: gkeImportContext; objectName: "gkeImportContext"; Layout.fillWidth: true; model: workspace.contexts; textRole: "name"; valueRole: "id"; Accessible.name: "GKE context for service-account key" }
            Button { objectName: "chooseGkeKey"; text: workspace.busy ? "Importing..." : "Choose service-account JSON key"; enabled: gkeImportContext.currentIndex >= 0 && !workspace.busy; onClicked: gkeKeyChooser.open() }
            Label { objectName: "gkeImportError"; Layout.fillWidth: true; text: workspace.sourceImportError; visible: gkeImportDialog.visible && text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
        }
        onClosed: importing = false
        Connections { target: workspace; function onSourceImportFinished(success) { if (gkeImportDialog.importing) { gkeImportDialog.importing = false; if (success) gkeImportDialog.close() } } }
    }
    FileDialog {
        id: gkeKeyChooser
        objectName: "gkeKeyChooser"
        title: "Select Google service-account JSON key"
        onAccepted: gkeImportDialog.importing = workspace.importGkeServiceAccount(gkeImportContext.currentValue, selectedFile.toString())
    }
    FileDialog {
        id: quickChooser
        objectName: "quickFileDialog"
        title: "Open kubeconfig file"
        onAccepted: workspace.quickImportFile(selectedFile.toString())
    }
    Shortcut { sequences: [StandardKey.Open]; enabled: !workspace.busy; onActivated: quickChooser.open() }
    FolderDialog {
        id: folderChooser
        objectName: "kubeconfigFolderDialog"
        title: "Import kubeconfig folder"
        onAccepted: workspace.importFile(selectedFolder.toString())
    }
    ModalDialog {
        id: sourceImportDetails
        objectName: "sourceImportDetails"
        title: "Source import failures"
        width: Math.min(650, window.width - 32)
        height: Math.min(420, window.height - 32)
        anchors.centerIn: parent
        modal: true
        header: DialogHeader { dialog: sourceImportDetails }
        contentItem: ListView {
            clip: true
            reuseItems: true
            model: workspace.sourceImportIssues
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width
                text: modelData.sourcePath + "\n" + modelData.message
                contentItem: Label { text: parent.text; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            }
        }
    }
    ModalDialog {
        id: deleteConfirmation
        objectName: "resourceDeleteDialog"
        title: "Delete this resource?"
        width: Math.min(560, window.width - 32)
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { objectName: "resourceDeleteTarget"; Layout.fillWidth: true; text: workspace.deletionTarget; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            Label { Layout.fillWidth: true; text: "Only this UID will be authorized. Kubernetes grace periods and finalizers remain in effect. This cannot be undone."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox {
            Button { id: deleteCancel; objectName: "cancelResourceDelete"; text: workspace.uiText["action.cancel"]; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "confirmResourceDelete"; text: "Delete this resource"; enabled: workspace.deletionPending && !workspace.authenticationRequired && !workspace.busy; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAboutToShow: footer.contentItem.currentIndex = -1
        onOpened: deleteCancel.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.confirmDeletion(true)
        onRejected: workspace.confirmDeletion(false)
    }
    ModalDialog {
        id: applyPreview
        objectName: "applyYamlDialog"
        title: "Review YAML changes"
        width: Math.min(850, window.width - 32)
        height: Math.min(640, window.height - 32)
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: workspace.yamlApplyTarget; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea { objectName: "yamlApplyDiff"; readOnly: true; selectByMouse: true; text: workspace.yamlApplyDiff; textFormat: TextEdit.PlainText; font.family: workspace.monospaceFamily; Accessible.name: "Field changes, Secret values hidden" }
            }
            Label { Layout.fillWidth: true; text: "Only Apply sends this patch. UID and original version must still match. Secret values remain hidden."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox {
            Button { id: applyCancel; objectName: "cancelYamlApply"; text: workspace.uiText["action.cancel"]; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "confirmYamlApply"; text: "Apply to this resource"; enabled: workspace.yamlApplyPreview && !workspace.discardPending; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAboutToShow: footer.contentItem.currentIndex = -1
        onOpened: applyCancel.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.confirmYamlApply(true)
        onRejected: workspace.confirmYamlApply(false)
    }
    ModalDialog {
        id: reconcile
        objectName: "yamlReconcileDialog"
        title: "Compare and reconcile changes"
        width: Math.min(850, window.width - 32)
        height: Math.min(680, window.height - 32)
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: workspace.yamlApplyTarget; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumHeight: 80; Layout.preferredHeight: 80
                TextArea { objectName: "yamlComparison"; readOnly: true; selectByMouse: true; text: workspace.yamlComparison; textFormat: TextEdit.PlainText; font.family: workspace.monospaceFamily; Accessible.name: "Original, current resource and your requested changes, Secret values hidden" }
            }
            ListView {
                Layout.fillWidth: true
                Layout.minimumHeight: count > 0 ? 48 : 0
                Layout.preferredHeight: Math.min(144, count * 48)
                clip: true
                reuseItems: true
                model: workspace.yamlConflicts
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                            id: conflictRow
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 48
                            property string choice: workspace.yamlConflictChoice(index)
                            Connections { target: workspace; function onYamlApplyChanged() { conflictRow.choice = workspace.yamlConflictChoice(conflictRow.index) } }
                            Label { Layout.fillWidth: true; text: modelData.path + ": " + conflictRow.choice; textFormat: Text.PlainText; elide: Text.ElideRight; Accessible.name: text }
                            Button { objectName: "chooseYamlMine_" + index; text: "Use mine"; Accessible.name: "Use my value for " + modelData.path; onClicked: workspace.chooseYamlConflict(index, true) }
                            Button { objectName: "chooseYamlServer_" + index; text: "Use server"; Accessible.name: "Use server value for " + modelData.path; onClicked: workspace.chooseYamlConflict(index, false) }
                }
            }
            Label { Layout.fillWidth: true; text: "Unrelated server changes are retained. Choose every overlap. Continue creates a new preview; it does not send a write."; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        }
        footer: DialogButtonBox {
            Button { id: reconcileCancel; objectName: "cancelYamlReconcile"; text: workspace.uiText["action.cancel"]; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "continueYamlReconcile"; text: "Continue to preview"; enabled: workspace.yamlReconcileReady; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onOpened: reconcileCancel.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.finishYamlReconcile(true)
        onRejected: workspace.finishYamlReconcile(false)
    }
    ModalDialog {
        id: authentication
        objectName: "authenticationConfirmation"
        title: "Retry authentication: " + workspace.title
        width: Math.min(440, window.width - 24)
        height: Math.min(implicitHeight, window.height - 24)
        anchors.centerIn: parent
        modal: true
        property string session: ""
        property string prompt: ""
        onOpened: { session = workspace.currentSession; prompt = workspace.authenticationPrompt }
        contentItem: ScrollView {
            id: authenticationBody
            implicitWidth: 380
            implicitHeight: authenticationText.implicitHeight
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Label {
                id: authenticationText
                width: authenticationBody.availableWidth
                text: authentication.prompt
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            Button { objectName: "authenticationAccept"; text: "Authenticate"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { objectName: "authenticationCancel"; text: workspace.uiText["action.cancel"]; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: { if (session === workspace.currentSession) workspace.refresh(true) }
    }
    SourceAliasDialog { id: sourceAliasDialog; objectName: "sourceAliasDialog" }
    component NavButton: IconButton {
        showText: false
        font.bold: true
    }
    function toggleSidebar() {
        if (sidebarDocked) sidebarOpen = !sidebarOpen
        else if (sidebarDrawer.visible) sidebarDrawer.close()
        else sidebarDrawer.open()
    }
    header: ToolBar {
        objectName: "workspaceToolbar"
        visible: !window.inputPriority
        padding: 0
        leftPadding: 0; rightPadding: 0; topPadding: SafeArea.margins.top; bottomPadding: 0
        implicitHeight: Math.max(navigationRow.implicitHeight, headerMetrics.implicitHeight) + 12 + topPadding
        background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
        RowLayout {
            id: headerLayout
            anchors.fill: parent; anchors.margins: 6
            spacing: 6
            Item {
                id: navigationSlot
                Layout.fillWidth: true; Layout.minimumWidth: 0
                Layout.preferredWidth: navigationRow.fullWidth
                Layout.preferredHeight: navigationRow.implicitHeight
                Row {
                    id: navigationRow
                    width: parent.width
                    height: implicitHeight
                    anchors.verticalCenter: parent.verticalCenter
                    LayoutMirroring.enabled: workspace.uiRightToLeft
                    LayoutMirroring.childrenInherit: true
                    spacing: window.touchLayout ? 0 : 4
                    readonly property var controls: [searchToggle, quickOpenControl, mapControl, resourcesControl, eventsControl, connectionsControl, settingsControl, musicControl]
                    readonly property real fullWidth: controls.filter(control => control.available !== false)
                        .reduce((total, control) => total + control.implicitWidth + spacing, workspaceActionsButton.implicitWidth)
                    function fits(control) {
                        let used = workspaceActionsButton.implicitWidth
                        for (const candidate of controls) {
                            if (candidate.available === false) continue
                            used += candidate.implicitWidth + spacing
                            if (candidate === control) return used <= width
                        }
                        return false
                    }
                    IconButton {
                        id: searchToggle
                        readonly property bool available: searchCommand.enabled
                        objectName: "workspaceSearchButton"; glyph: "Search"; text: workspace.uiText["nav.search"]
                        action: searchCommand; visible: available && navigationRow.fits(this)
                        activeIndicator: workspace.workspacePage === "events" ? workspace.eventFilterText !== ""
                            : workspace.workspacePage === "ports" ? workspace.portFilterText !== "" : workspace.filterText !== ""
                        Accessible.name: "Show or hide current search"
                        Accessible.description: "Search this view. Hiding search keeps the filter."
                    }
                    IconButton { id: quickOpenControl; readonly property bool available: window.mapContexts.length > 0; objectName: "quickOpenDropdown"; glyph: "ConfigMap"; action: quickOpenCommand; visible: available && navigationRow.fits(this) }
                    NavButton { id: mapControl; readonly property bool available: true; objectName: "mapWorkspaceButton"; glyph: "Cluster"; action: mapCommand; text: workspace.uiText["nav.map"]; visible: available && navigationRow.fits(this) }
                    NavButton { id: resourcesControl; readonly property bool available: window.hasResources; objectName: "resourcesWorkspaceButton"; glyph: "Pod"; action: resourcesCommand; text: workspace.uiText["nav.resources"]; visible: available && navigationRow.fits(this) }
                    NavButton { id: eventsControl; readonly property bool available: window.hasEvents; objectName: "eventsWorkspaceButton"; glyph: "Event"; action: eventsCommand; text: workspace.uiText["nav.events"]; visible: available && navigationRow.fits(this) }
                    NavButton { id: connectionsControl; readonly property bool available: window.hasConnections; objectName: "portForwardTasksButton"; glyph: "Service"; action: connectionsCommand; Accessible.name: "Connections in this tab"; visible: available && navigationRow.fits(this) }
                    NavButton { id: settingsControl; objectName: "settingsWorkspaceButton"; glyph: "Settings"; action: settingsCommand; text: workspace.uiText["nav.settings"]; visible: navigationRow.fits(this) }
                    IconButton {
                        id: musicControl
                        objectName: "workspaceMusicButton"
                        glyph: workspace.music.playing ? "Pause" : "Play"
                        action: musicCommand
                        activeIndicator: workspace.music.playing
                        visible: navigationRow.fits(this)
                    }

                IconButton { id: workspaceActionsButton; objectName: "workspaceActionsButton"; glyph: "Menu"; text: workspace.uiText["action.more"]; visible: navigationRow.controls.some(control => control.available !== false && !control.visible) || headerMetrics.overflowMetrics.length > 0 || ["resources", "events"].includes(workspace.workspacePage); activeIndicator: navigationRow.controls.some(control => !control.visible && control.checkable && control.checked); onClicked: workspaceActions.popup(workspaceActionsButton, 0, workspaceActionsButton.height) }
                }
            }
            PulseStrip {
                id: headerMetrics
                objectName: "headerMetrics"
                availableWidth: Math.max(0, headerLayout.width - navigationRow.fullWidth - headerLayout.spacing - (sidebarControl.visible ? sidebarControl.implicitWidth + headerLayout.spacing : 0) - (closeContextControl.visible ? closeContextControl.implicitWidth + headerLayout.spacing : 0))
                Layout.alignment: Qt.AlignRight
                Layout.preferredHeight: implicitHeight
                onDetailsRequested: workspace.setWorkspacePage("dashboard")
            }
            IconButton { id: sidebarControl; objectName: "toggleSidebar"; glyph: "Sidebar"; action: sidebarCommand; visible: workspace.currentSession !== "" && !(workspace.workspacePage === "map" && workspace.mapOverview) }
            IconButton { id: closeContextControl; objectName: "closeCurrentSession"; glyph: "Close"; text: "Close context"; visible: workspace.currentSession !== "" && workspace.tabs.length < 2; enabled: !workspace.busy; onClicked: workspace.close(workspace.currentSession) }
        }
    }

    Menu {
        id: workspaceActions
        objectName: "workspaceActionsMenu"
        readonly property bool compactMenu: window.touchLayout || Qt.platform.os === "android" || Qt.platform.os === "ios"
        width: compactMenu ? Math.min(240, window.width - 24) : implicitWidth
        height: compactMenu ? Math.min(implicitHeight, window.height - 48) : implicitHeight
        Binding { target: workspaceActions; property: "popupType"; value: Popup.Item; when: workspaceActions.compactMenu }
        readonly property var overflowControls: visible ? navigationRow.controls.filter(control => control.available !== false && !control.visible) : []
        readonly property var overflowEntries: overflowControls.concat(headerMetrics.overflowMetrics.map(metric => ({metric: metric})))
        Instantiator {
            active: workspaceActions.visible
            model: workspaceActions.overflowEntries
            delegate: MenuItem {
                required property var modelData
                readonly property bool metricEntry: !!modelData.metric
                objectName: metricEntry ? "overflowMetric_" + modelData.metric.id : modelData.objectName
                action: metricEntry ? null : modelData.action
                text: metricEntry ? modelData.metric.label + ": " + (modelData.metric.display || modelData.metric.usage) : modelData.text
                enabled: metricEntry || modelData.enabled
                checkable: !metricEntry && modelData.checkable
                checked: !metricEntry && modelData.checked
                Accessible.name: metricEntry ? text : modelData.Accessible.name
                implicitHeight: window.controlHeight
                ToolTip.visible: metricEntry && (hovered || activeFocus)
                ToolTip.text: metricEntry ? modelData.metric.description : ""
                onTriggered: if (metricEntry) headerMetrics.detailsRequested()
            }
            onObjectAdded: (index, object) => workspaceActions.insertItem(index, object)
            onObjectRemoved: (index, object) => workspaceActions.removeItem(object)
        }
        MenuSeparator { visible: workspaceActions.overflowControls.length > 0 || headerMetrics.overflowMetrics.length > 0; height: visible ? implicitHeight : 0 }
        MenuItem {
            objectName: workspace.workspacePage === "events" ? "eventFindButton" : "resourceFindButton"
            text: workspace.uiText["nav.search"]
            implicitHeight: visible ? (window.controlHeight) : 0
            visible: workspace.workspacePage === "resources" || workspace.workspacePage === "events"
            Accessible.name: workspace.uiText["nav.search"]
            onTriggered: if (queryRow.activeGrid) queryRow.activeGrid.openFind()
        }
        MenuItem {
            objectName: workspace.workspacePage === "events" ? "eventColumnsButton" : "resourceColumnsButton"
            text: workspace.uiText["action.columns"]
            implicitHeight: visible ? (window.controlHeight) : 0
            visible: workspace.workspacePage === "resources" || workspace.workspacePage === "events"
            Accessible.name: "Columns for the current table"
            onTriggered: if (queryRow.activeGrid) queryRow.activeGrid.openColumns()
        }
    }
    Menu {
        id: quickOpenMenu
        objectName: "quickOpenMenu"
        width: Math.min(360, window.width - 24)
        height: Math.min(implicitHeight, window.height - 48)
        MenuItem { objectName: "quickOpenFile"; text: workspace.uiText["context.openFile"]; implicitHeight: window.controlHeight; enabled: !workspace.busy; onTriggered: quickChooser.open() }
        MenuSeparator {}
        Instantiator {
            active: quickOpenMenu.visible
            model: window.quickOpenEntries
            delegate: MenuItem {
                required property var modelData
                implicitHeight: window.controlHeight
                objectName: modelData.header ? "" : "quickContext_" + modelData.id
                text: modelData.name
                enabled: !modelData.header && !workspace.busy
                checkable: false
                Accessible.name: modelData.header ? text : "Open context " + text
                onTriggered: if (!modelData.header) workspace.openContext(modelData.id)
            }
            onObjectAdded: (index, object) => quickOpenMenu.insertItem(index + (workspace.windowActionsAvailable ? 4 : 3), object)
            onObjectRemoved: (index, object) => quickOpenMenu.removeItem(object)
        }
        Instantiator {
            model: 1
            delegate: Menu {
                id: quickTabMenu
                objectName: "quickOpenTabMenu"
                title: workspace.uiText["action.openInTab"]
                width: Math.min(360, window.width - 24)
                height: Math.min(implicitHeight, window.height - 48)
                Instantiator {
                    active: quickTabMenu.visible
                    model: window.mapContexts
                    delegate: MenuItem {
                        required property var modelData
                        objectName: "quickContextNewTab_" + modelData.id
                        text: modelData.name
                        implicitHeight: window.controlHeight
                        enabled: !workspace.busy
                        Accessible.name: text
                        onTriggered: workspace.openContext(modelData.id, false, true)
                    }
                    onObjectAdded: (index, object) => quickTabMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => quickTabMenu.removeItem(object)
                }
            }
            onObjectAdded: (index, object) => {
                quickOpenMenu.insertMenu(1, object)
                quickOpenMenu.itemAt(1).objectName = "quickOpenTab"
            }
            onObjectRemoved: (index, object) => quickOpenMenu.removeMenu(object)
        }
    }
    Instantiator {
        model: workspace.windowActionsAvailable ? 1 : 0
        delegate: Menu {
            id: quickWindowMenu
            objectName: "quickOpenWindowMenu"
            title: workspace.uiText["action.openInWindow"]
            width: Math.min(360, window.width - 24)
            height: Math.min(implicitHeight, window.height - 48)
        Instantiator {
            active: quickWindowMenu.visible
            model: window.quickOpenEntries
            delegate: MenuItem {
                required property var modelData
                implicitHeight: window.controlHeight
                objectName: modelData.header ? "" : "quickWindowContext_" + modelData.id
                text: modelData.name
                enabled: !modelData.header && workspace.sessionWindowActionsReady
                Accessible.name: "Open " + text + " in a separate window"
                onTriggered: if (!modelData.header) workspace.openContext(modelData.id, true)
            }
            onObjectAdded: (index, object) => quickWindowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => quickWindowMenu.removeItem(object)
        }
        }
        onObjectAdded: (index, object) => {
            quickOpenMenu.insertMenu(1, object)
            quickOpenMenu.itemAt(1).objectName = "quickOpenWindow"
        }
        onObjectRemoved: (index, object) => quickOpenMenu.removeMenu(object)
    }
    Drawer {
        id: sidebarDrawer
        objectName: "sidebarDrawer"
        edge: Qt.RightEdge
        width: Math.min(392, window.width - 24); height: window.height
        onVisibleChanged: if (visible && (window.sidebarDocked || workspace.currentSession === "")) close()
        Connections {
            target: workspace
            function onChanged() { if (workspace.currentSession === "") sidebarDrawer.close() }
        }
        Connections {
            target: window
            function onSidebarDockedChanged() { if (window.sidebarDocked) sidebarDrawer.close() }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 6
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: sessionTabs.implicitHeight
            spacing: 2
            visible: !window.inputPriority && !window.compactInspector && workspace.tabs.length > 1
            TabBar {
                id: sessionTabs
                objectName: "sessionTabs"
                visible: workspace.tabs.length > 1
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                clip: true
                    spacing: 0
                    function indexForCurrentSession() {
                        for (var i = 0; i < workspace.tabs.length; ++i) {
                            if (workspace.tabs[i].id === workspace.currentSession) return i
                        }
                        return -1
                    }
                    function activateIndex(index) {
                        const tab = itemAt(index)
                        if (!tab || !tab.enabled) return
                        tab.forceActiveFocus(Qt.TabFocusReason)
                        workspace.activate(workspace.tabs[index].id)
                        setCurrentIndex(indexForCurrentSession())
                    }
                    Binding {
                        target: sessionTabs
                        property: "currentIndex"
                        value: sessionTabs.indexForCurrentSession()
                    }
                    Repeater {
                        model: workspace.tabs
                        WorkspaceTab {
                            id: sessionTab
                            required property var modelData
                            objectName: "activateSession_" + modelData.id
                            implicitHeight: window.controlHeight
                            implicitWidth: Math.min(Math.max(120, sessionName.implicitWidth + tabActions.implicitWidth + (connectionBadge.visible ? connectionBadge.implicitWidth : 0) + 24), window.wideLayout ? 240 : 160)
                            width: implicitWidth
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 0
                            bottomPadding: 0
                            text: modelData.name
                            checkable: true
                            checked: modelData.id === workspace.currentSession
                            enabled: !workspace.busy
                            Accessible.name: modelData.name + (modelData.connectionCount > 0 ? ", " + modelData.connectionCount + " open connections" : "")
                            onClicked: sessionTabs.activateIndex(sessionTab.TabBar.index)
                            contentItem: Row {
                                spacing: 4
                                Label {
                                    id: sessionName
                                    width: Math.max(0, sessionTab.availableWidth - tabActions.implicitWidth - (connectionBadge.visible ? connectionBadge.implicitWidth + 8 : 4))
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name
                                    font: sessionTab.font
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                }
                                Label {
                                    id: connectionBadge
                                    objectName: "sessionConnections_" + modelData.id
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.connectionCount > 0
                                    text: String(modelData.connectionCount)
                                    font.pixelSize: workspace.textSizes.caption
                                    font.bold: true
                                    padding: 3
                                    Accessible.name: modelData.connectionCount + " open connections"
                                    background: Rectangle { color: workspace.appearanceColors.raised; border.color: workspace.appearanceColors.accent }
                                }
                                Row {
                                    id: tabActions
                                    spacing: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    IconButton {
                                        objectName: "detachSession_" + modelData.id
                                        glyph: "OpenExternal"
                                        text: "Open " + modelData.name + " in a separate window"
                                        visible: workspace.windowActionsAvailable
                                        enabled: workspace.sessionWindowActionsReady
                                        width: window.touchLayout ? 44 : 24
                                        height: width
                                        onClicked: workspace.detachSession(modelData.id)
                                    }
                                    IconButton {
                                        objectName: "closeSession_" + modelData.id
                                        glyph: "Close"
                                        text: workspace.uiText["action.close"]
                                        Accessible.name: "Close " + modelData.name
                                        enabled: !workspace.busy
                                        width: window.touchLayout ? 44 : 24
                                        height: width
                                        onClicked: workspace.close(modelData.id)
                                    }
                                }
                            }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: tabMenu.popup(sessionTab, 0, sessionTab.height) }
                            TapHandler { acceptedDevices: PointerDevice.TouchScreen; onLongPressed: tabMenu.popup(sessionTab, 0, sessionTab.height) }
                            Keys.priority: Keys.BeforeItem
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                    tabMenu.popup(sessionTab, 0, sessionTab.height)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                    var target = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? workspace.tabs.length - 1 : sessionTab.TabBar.index + (event.key === Qt.Key_Left ? -1 : 1)
                                    if (target >= 0 && target < workspace.tabs.length) sessionTabs.activateIndex(target)
                                    event.accepted = true
                                }
                            }
                            Menu {
                                id: tabMenu
                                MenuItem { objectName: "renameTab_" + modelData.id; text: workspace.uiText["context.rename"] + "..."; enabled: !workspace.busy; onTriggered: window.editTabContextAlias(modelData.id) }
                                MenuItem { objectName: "detachTab_" + modelData.id; text: "Open in separate window"; visible: workspace.windowActionsAvailable; enabled: workspace.sessionWindowActionsReady; onTriggered: workspace.detachSession(modelData.id) }
                            }
                            ToolTip.visible: hovered || activeFocus
                            ToolTip.text: modelData.name + (modelData.connectionCount > 0 ? " / " + modelData.connectionCount + " open connections" : "")
                        }
                    }
            }
        }
        RowLayout {
            id: queryRow
            Layout.rightMargin: window.contentRightMargin
            readonly property var activeGrid: workspace.workspacePage === "events" ? eventGridLoader.item : workspace.workspacePage === "map" ? sidebar : resourceGrid
            readonly property bool navigating: window.searchOpen && (["resources", "events"].indexOf(workspace.workspacePage) >= 0 || (workspace.workspacePage === "map" && !workspace.mapOverview))
            readonly property bool hasQuery: (workspace.workspacePage === "events" ? workspace.eventFilterText : workspace.filterText).trim() !== ""
            Layout.fillWidth: true
            visible: !window.compactInspector && ((window.searchOpen && (["resources", "events", "dashboard"].indexOf(workspace.workspacePage) >= 0 || (workspace.workspacePage === "map" && !workspace.mapOverview))) || workspace.authenticationRequired || workspace.authenticationRunning) || (window.compactInspector && (workspace.authenticationRequired || workspace.authenticationRunning))
            TextField {
                id: resourceFilterInput
                objectName: "resourceFilter"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                enabled: workspace.currentSession !== "" && !workspace.busy
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: window.controlHeight
                text: workspace.filterText
                placeholderText: "Search resources"
                Accessible.name: "Filter resources"
                Accessible.description: "Space-separated alternatives; quoted exact values; ~prefix; suffix~; /regular expression/; integer comparisons such as >=2."
                ToolTip.visible: hovered; ToolTip.text: Accessible.description
                visible: window.searchOpen && (["resources", "dashboard"].indexOf(workspace.workspacePage) >= 0 || (workspace.workspacePage === "map" && !workspace.mapOverview))
                onTextEdited: if (workspace.filter(text) && workspace.workspacePage !== "map") Qt.callLater(resourceGrid.navigateRows, 0)
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && text.trim() !== "") {
                        queryRow.activeGrid.navigateRows(event.modifiers & Qt.ShiftModifier ? -1 : 1); event.accepted = true
                    }
                }
                Keys.onEscapePressed: { window.searchOpen = false; searchToggle.forceActiveFocus() }
            }
            TextField {
                id: eventFilterInput; objectName: "eventFilter"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: window.controlHeight
                visible: window.searchOpen && workspace.workspacePage === "events"
                text: workspace.eventFilterText; placeholderText: "Search events"; Accessible.name: "Search events"
                onTextEdited: if (workspace.filterEvents(text)) Qt.callLater(eventGridLoader.item.navigateRows, 0)
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && text.trim() !== "") {
                        eventGridLoader.item.navigateRows(event.modifiers & Qt.ShiftModifier ? -1 : 1); event.accepted = true
                    }
                }
                Keys.onEscapePressed: { window.searchOpen = false; searchToggle.forceActiveFocus() }
            }
            IconButton { objectName: "queryPrevious"; glyph: "Previous"; text: workspace.uiText["tooltip.previousMatch"]; visible: queryRow.navigating; enabled: queryRow.hasQuery && queryRow.activeGrid && queryRow.activeGrid.count > 0; onClicked: queryRow.activeGrid.navigateRows(-1) }
            IconButton { objectName: "queryNext"; glyph: "Next"; text: workspace.uiText["tooltip.nextMatch"]; visible: queryRow.navigating; enabled: queryRow.hasQuery && queryRow.activeGrid && queryRow.activeGrid.count > 0; onClicked: queryRow.activeGrid.navigateRows(1) }
            Label { objectName: "queryMatchCount"; visible: queryRow.navigating; text: queryRow.hasQuery && queryRow.activeGrid ? (queryRow.activeGrid.navigationIndex + 1) + "/" + queryRow.activeGrid.count : "0/0"; textFormat: Text.PlainText; color: workspace.appearanceColors.accent; Accessible.name: "Search match " + text }
            Button { objectName: "authenticationButton"; text: "Authenticate"; visible: workspace.authenticationRequired; enabled: !workspace.busy && !workspace.authenticationRunning; onClicked: authentication.open() }
            Button { objectName: "cancelAuthentication"; text: "Cancel login"; visible: workspace.authenticationRunning; onClicked: workspace.cancelAuthentication() }
        }
        Label { objectName: "authenticationInstruction"; Layout.fillWidth: true; text: workspace.authenticationInstruction; visible: text !== "" && !workspace.authenticationChallenge.id; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        Loader {
            id: mfaForm
            Layout.fillWidth: true
            readonly property var challenge: workspace.authenticationChallenge
            active: !!challenge.id
            visible: active
            sourceComponent: ColumnLayout {
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    visible: !window.inputPriority
                    text: "AWS MFA: " + mfaForm.challenge.role + " / " + mfaForm.challenge.device
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    Accessible.name: text
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: mfaCode
                        objectName: "authenticationMfaCode"
                        readonly property string challengeId: mfaForm.challenge.id || ""
                        property string errorText: ""
                        onChallengeIdChanged: { text = ""; errorText = "" }
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        maximumLength: 6
                        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhNoPredictiveText
                        validator: RegularExpressionValidator { regularExpression: /^[0-9]{0,6}$/ }
                        placeholderText: "Six-digit MFA code"
                        Accessible.name: "AWS MFA code for " + mfaForm.challenge.role
                        function submit() {
                            const id = mfaForm.challenge.id
                            const value = text
                            text = ""
                            const accepted = workspace.submitAuthenticationCode(id, value)
                            if (!accepted && mfaForm.challenge.id === id) errorText = "Enter the current six-digit code."
                        }
                        onAccepted: if (acceptableInput && text.length === 6) submit()
                        Component.onCompleted: if (window.active) forceActiveFocus()
                    }
                    Button { objectName: "submitAuthenticationMfa"; text: "Continue"; enabled: mfaCode.acceptableInput && mfaCode.text.length === 6; onClicked: mfaCode.submit() }
                    Button { objectName: "cancelAuthenticationMfa"; text: workspace.uiText["action.cancel"]; onClicked: workspace.cancelAuthentication() }
                }
                Label { Layout.fillWidth: true; text: mfaCode.errorText; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
            }
        }
        Label { objectName: "filterErrorMessage"; Layout.rightMargin: window.contentRightMargin; Layout.fillWidth: true; text: workspace.filterError; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; visible: text !== ""; wrapMode: Text.Wrap; Accessible.name: text }
        Label { objectName: "errorMessage"; Layout.rightMargin: window.contentRightMargin; Layout.fillWidth: true; text: workspace.error || workspace.uiText[workspace.music.error] || ""; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; visible: text !== ""; wrapMode: Text.Wrap; Accessible.name: text }
        Label {
            objectName: "syncProblemMessage"; Layout.rightMargin: window.contentRightMargin
            Layout.fillWidth: true; Layout.minimumWidth: 0
            text: workspace.syncProblem; textFormat: Text.PlainText
            visible: text !== ""; maximumLineCount: 1; wrapMode: Text.NoWrap; elide: Text.ElideRight
            color: workspace.appearanceColors.danger; Accessible.name: text
            ToolTip.visible: problemHover.hovered; ToolTip.text: text
            HoverHandler { id: problemHover }
        }
        Button { objectName: "reloadSavedViews"; text: "Reload saved filters and sorts"; visible: workspace.viewStateFailed; enabled: !workspace.busy; onClicked: workspace.reloadSavedViews() }
        SplitView {
            id: workspaceSplit
            objectName: "workspaceSplit"
            Layout.fillWidth: true; Layout.fillHeight: true
            orientation: Qt.Horizontal
            SplitView {
                id: inspectorSplit
                objectName: "workspaceInspectorSplit"
                SplitView.fillWidth: true; SplitView.minimumWidth: Math.min(window.compactLandscape ? 240 : 300, window.width - 12)
                orientation: Qt.Vertical
                handle: Rectangle {
                    objectName: "inspectorResizeHandle"
                    implicitHeight: 6; implicitWidth: 6
                    activeFocusOnTab: true
                    color: activeFocus || SplitHandle.pressed || SplitHandle.hovered ? workspace.appearanceColors.accent : workspace.appearanceColors.border
                    Accessible.role: Accessible.Grip
                    Accessible.name: "Resize inspector with Up or Down"
                    Keys.onPressed: event => {
                        if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down) return
                        dockedInspector.SplitView.preferredHeight = Math.max(dockedInspector.SplitView.minimumHeight,
                            Math.min(inspectorSplit.height - Math.min(120, workspaceSplit.height * 0.25) - height,
                                dockedInspector.height + (event.key === Qt.Key_Up ? 20 : -20)))
                        event.accepted = true
                    }
                }
                Item {
                    objectName: "workspaceContent"
                    visible: !window.compactInspector
                    SplitView.fillHeight: visible; SplitView.minimumHeight: visible ? Math.min(120, workspaceSplit.height * 0.25) : 0
                ResourceGrid {
                    id: resourceGrid
                    anchors.fill: parent
                    visible: workspace.workspacePage === "resources"
                    inlineTools: false
                    tableModel: workspace.visibleResourceTable
                    sortColumn: workspace.sortColumnIndex; sortDirection: workspace.sortDirection
                    emptyText: workspace.currentSession === "" ? workspace.uiText["resource.emptyMessage"] : workspace.loading ? workspace.uiText["resource.loadingMessage"] : workspace.uiText["resource.noMatchingMessage"]
                    onSortRequested: function(column) { workspace.sortColumn(column) }
                    onInspectRequested: function(path) { workspace.inspectPath(path) }
                    onCopyRequested: function(row, column) { workspace.copyCell(row, column) }
                    onCopyPathRequested: function(path, column) { workspace.copyPathCell(path, column, false) }
                }
                Item {
                    anchors.fill: parent
                    visible: workspace.workspacePage === "map"
                    RowLayout {
                        id: mapNavigationSlot
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        z: 4
                        visible: workspace.mapOverview
                    }
                    RowLayout {
                        id: mapNavigationRow
                        visible: workspace.workspacePage === "map"
                        parent: workspace.mapOverview ? mapNavigationSlot : sidebar.mapNavigationHost
                        Layout.fillWidth: true
                        Label {
                            id: mapCaption
                            objectName: "mapTitle"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.minimumHeight: workspace.mapOverview ? implicitHeight : window.touchLayout ? 44 : 24
                            verticalAlignment: Text.AlignVCenter
                            text: workspace.mapOverview ? (workspace.mapCatalogWarning || "Contexts") : workspace.mapContextCaption
                            color: workspace.mapCatalogWarning ? "#FFE866" : "#DDE7EC"
                            leftPadding: 4; rightPadding: 4
                            background: Rectangle {
                                width: Math.min(mapCaption.width, mapCaption.contentWidth + 8)
                                color: "#061621"
                                border.width: mapCaption.activeFocus ? 1 : 0
                                border.color: mapCaption.color
                            }
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            wrapMode: Text.NoWrap
                            Accessible.description: workspace.mapCatalogWarning
                            ToolTip.visible: mapTitleHover.hovered && workspace.mapCatalogWarning !== ""
                            ToolTip.text: workspace.mapCatalogWarning
                            HoverHandler { id: mapTitleHover }
                            activeFocusOnTab: !workspace.mapOverview
                            Accessible.role: workspace.mapOverview ? Accessible.StaticText : Accessible.Button
                            Accessible.onPressAction: if (!workspace.mapOverview) mapIdentityMenu.popup(mapCaption, 0, mapCaption.height)
                            TapHandler { acceptedButtons: Qt.RightButton; enabled: !workspace.mapOverview; onTapped: mapIdentityMenu.popup(mapCaption, 0, mapCaption.height) }
                            TapHandler { acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Stylus; enabled: !workspace.mapOverview; onLongPressed: mapIdentityMenu.popup(mapCaption, 0, mapCaption.height) }
                            Keys.onPressed: function(event) {
                                if (!workspace.mapOverview && (event.key === Qt.Key_Menu || event.key === Qt.Key_Return
                                    || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))) {
                                    mapIdentityMenu.popup(mapCaption, 0, mapCaption.height)
                                    event.accepted = true
                                }
                            }
                            Menu {
                                id: mapIdentityMenu
                                objectName: "mapIdentityMenu"
                                MenuItem { objectName: "copyMapContextName"; text: workspace.uiText["context.copyName"]; onTriggered: workspace.copyMapIdentity("name") }
                                MenuItem { objectName: "copyMapClusterName"; text: workspace.uiText["context.copyCluster"]; onTriggered: workspace.copyMapIdentity("cluster") }
                            }
                        }
                    }
                    Item {
                        anchors.fill: parent
                        Loader { anchors.fill: parent; active: parent.visible && workspace.workspacePage === "map" && workspace.mapOverview; visible: active; sourceComponent: MapOverview { contexts: window.mapContexts; onSourcesRequested: chooser.open() } }
                        Item { id: resourceMapHost; objectName: "resourceMapHost"; anchors.fill: parent; visible: !workspace.mapOverview }
                    }
                }
                Loader {
                    id: eventGridLoader
                    anchors.fill: parent
                    visible: workspace.workspacePage === "events"
                    property bool opened: false
                    active: opened
                    onVisibleChanged: if (visible) opened = true
                    sourceComponent: ResourceGrid {
                        inlineTools: false
                        prefix: "event"; tableModel: workspace.eventTable
                        sortColumn: workspace.eventSortColumnIndex; sortDirection: workspace.eventSortDirection
                        emptyText: workspace.loading ? "Loading events…" : "No matching events."
                        onSortRequested: function(column) { workspace.sortEventColumn(column) }
                        onInspectRequested: function(path) { workspace.inspectEventPath(path) }
                        onCopyRequested: function(row, column) { workspace.copyEventCell(row, column) }
                        onCopyPathRequested: function(path, column) { workspace.copyPathCell(path, column, true) }
                    }
                }
                Loader {
                    anchors.fill: parent
                    visible: workspace.workspacePage === "dashboard"
                    property bool opened: false
                    active: opened
                    onVisibleChanged: if (visible) opened = true
                    sourceComponent: ResourceDashboard {}
                }
                Loader {
                    id: portsView
                    enabled: !window.terminalExpanded
                    anchors.fill: parent
                    visible: workspace.workspacePage === "ports"
                    property bool opened: false
                    active: opened
                    onVisibleChanged: if (visible) opened = true
                    sourceComponent: ConnectionsView {
                        onTerminalRequested: window.openContainerTerminal(false)
                        searchOpen: window.searchOpen
                        onSearchClosed: { window.searchOpen = false; searchToggle.forceActiveFocus() }
                        onAuthenticationRequested: authentication.open()
                    }
                }
                Loader {
                    id: settingsLoader
                    anchors.fill: parent
                    visible: workspace.workspacePage === "settings" || workspace.workspacePage === "alerts"
                    property bool opened: false
                    active: opened
                    onVisibleChanged: if (visible) opened = true
                    sourceComponent: SettingsView {
                        onRenameContextRequested: context => sourceAliasDialog.edit(context)
                        onFileRequested: chooser.open()
                        onFolderRequested: folderChooser.open()
                        onOciImportRequested: ociImportDialog.open()
                        onGkeImportRequested: gkeImportDialog.open()
                        onSourceFailuresRequested: sourceImportDetails.open()
                    }
                }
                }
                Loader {
                    id: dockedInspector
                    objectName: "inspectorLoader"
                    SplitView.fillHeight: window.compactInspector
                    SplitView.preferredHeight: window.compactInspector ? inspectorSplit.height : Math.min(420, workspaceSplit.height * 0.60)
                    SplitView.minimumHeight: window.compactInspector ? 0 : Math.min(300, workspaceSplit.height * 0.70)
                    visible: window.hasInspector && !(workspace.workspacePage === "map" && workspace.mapOverview)
                    sourceComponent: workspace.sourceInspection.snapshotId ? sourceInspectorComponent : resourceInspectorComponent
                }
                Component { id: resourceInspectorComponent; ResourceInspector { objectName: "workspaceInspector" } }
                Component { id: sourceInspectorComponent; SourceInspector {} }
            }
            Item {
                id: sidebarHost
                objectName: "workspaceSidebarHost"
                visible: workspace.currentSession !== "" && window.sidebarDocked && window.sidebarOpen && (!window.inputPriority || sidebar.ownsInputFocus) && !(workspace.workspacePage === "map" && workspace.mapOverview)
                SplitView.preferredWidth: sidebar.compactFilterRail ? sidebar.collapsedFilterWidth : window.compactLandscape ? 240 : 392
                SplitView.minimumWidth: sidebar.compactFilterRail ? sidebar.collapsedFilterWidth : window.compactLandscape ? 228 : 300
                SplitView.maximumWidth: sidebar.compactFilterRail ? sidebar.collapsedFilterWidth : window.compactLandscape ? 320 : 720
            }
        }
        RowLayout {
            Layout.fillWidth: true
            objectName: "workspaceFooter"
            visible: !window.inputPriority && window.width >= 600 && window.height >= 600
                && !["settings", "alerts"].includes(workspace.workspacePage)
                && (workspace.totalResourceCount > 0 || (workspace.workspacePage === "map" && workspace.mapOverview && window.mapContexts.length > 0))
            Layout.minimumHeight: Math.max(24, Math.ceil(workspace.textSizes.caption * 1.3) + 4); Layout.maximumHeight: Layout.minimumHeight; Layout.preferredHeight: Layout.minimumHeight
            LayoutMirroring.enabled: workspace.uiRightToLeft; LayoutMirroring.childrenInherit: true
            Label { objectName: "resourceMatchCount"; Layout.minimumWidth: 0; Layout.maximumWidth: window.width * 0.45; font.pixelSize: workspace.textSizes.caption; maximumLineCount: 1; wrapMode: Text.NoWrap; elide: Text.ElideRight; text: workspace.workspacePage === "map" ? (workspace.mapOverview ? "contexts: " + window.mapContexts.length : "matches: " + workspace.resourceCount + "/" + workspace.totalResourceCount ) : "visible: " + workspace.displayedResourceCount + "/" + workspace.totalResourceCount; Accessible.name: workspace.workspacePage === "map" && workspace.mapOverview ? text : text + ", " + workspace.resourceCount + " matching resources" }
            Label { objectName: "syncStatus"; Layout.fillWidth: true; Layout.minimumWidth: 0; font.pixelSize: workspace.textSizes.caption; maximumLineCount: 1; wrapMode: Text.NoWrap; text: workspace.workspacePage === "map" && workspace.mapOverview ? "" : workspace.syncSummary; textFormat: Text.PlainText; elide: Text.ElideRight; Accessible.name: text; ToolTip.visible: syncHover.hovered; ToolTip.text: text; HoverHandler { id: syncHover } }
        }
    }
    ResourceSidebar {
        id: sidebar
        parent: window.sidebarDocked ? sidebarHost : sidebarDrawer.contentItem
        expandedRadarHost: workspace.workspacePage === "map" && !workspace.mapOverview && !window.compactInspector ? resourceMapHost : null
        radarVisible: workspace.workspacePage !== "map" || !workspace.mapOverview
        collapsibleFilters: window.compactLandscape
        suppressSmallRadar: window.compactLandscape
        anchors.fill: parent
        visible: window.sidebarDocked ? sidebarHost.visible : sidebarDrawer.visible
        onRenameRequested: window.editTabContextAlias(workspace.currentSession)
    }
}
