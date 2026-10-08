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
    font.pixelSize: 13
    property bool sidebarOpen: true
    property bool searchOpen: false
    readonly property bool wideLayout: width >= 900
    readonly property bool compactLandscape: width >= 600 && height < 600 && width > height
    readonly property bool touchLayout: !wideLayout || compactLandscape
    readonly property bool sidebarDocked: wideLayout || compactLandscape
    visible: true
    property bool sourcesExpanded: false
    FieldFiltersDialog { id: fieldFiltersDialog; controller: workspace }
    Action { id: resourcesCommand; text: "Open Resources"; onTriggered: workspace.setWorkspacePage("resources") }
    Action { id: eventsCommand; text: "Open Events"; onTriggered: workspace.setWorkspacePage("events") }
    Action { id: sourcesCommand; text: "Open Sources Settings"; onTriggered: { if (workspace.setWorkspacePage("settings")) settingsLoader.item.section = "sources" } }
    Action { id: portCommand; text: "Open Port Forwards"; enabled: workspace.canPortForward; onTriggered: workspace.preparePortForward() }
    Action { id: settingsCommand; text: "Open Settings"; onTriggered: workspace.setWorkspacePage("settings") }
    Action { id: problemsCommand; text: "Toggle Problems"; enabled: workspace.currentSession !== ""; onTriggered: workspace.setFilterMode(workspace.problemsOnly ? "" : "problems") }
    Action { id: k3dCommand; text: "Import K3D"; enabled: !workspace.busy; onTriggered: workspace.importK3d() }
    CommandPalette { id: commands; actions: [resourcesCommand, eventsCommand, sourcesCommand, portCommand, settingsCommand, problemsCommand, k3dCommand] }
    Shortcut { sequences: ["Ctrl+K", "Meta+K"]; onActivated: commands.open() }
    Shortcut { sequences: [StandardKey.Back]; enabled: workspace.canInspectBack; onActivated: workspace.navigateInspector(-1) }
    Shortcut { sequences: [StandardKey.Forward]; enabled: workspace.canInspectForward; onActivated: workspace.navigateInspector(1) }
    onVisibilityChanged: function(visibility) { workspace.setWindowVisible(visibility !== Window.Hidden && visibility !== Window.Minimized) }
    onClosing: function(close) { close.accepted = workspace.requestWindowClose() }
    title: "Podlord Native - " + workspace.title
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
    Dialog {
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
        onOpened: discardStayButton.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.confirmDiscard(true)
        onRejected: workspace.confirmDiscard(false)
    }
    Dialog {
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
                TextField { id: forwardLocal; objectName: "portForwardLocal"; Layout.fillWidth: true; text: String(workspace.portForwardTarget.localPort || ""); inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Local TCP port, 1 through 65535" }
            }
            RowLayout {
                visible: !portForwardDialog.stopMode
                Label { text: "Target TCP port" }
                TextField { id: forwardRemote; objectName: "portForwardRemote"; Layout.fillWidth: true; text: String(workspace.portForwardTarget.remotePort || ""); inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Pod or Service TCP port, 1 through 65535" }
            }
            Label { Layout.fillWidth: true; visible: portForwardDialog.stopMode; text: "This resource already has a session-owned forward. Stop it to release its local port."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; visible: portForwardDialog.stopMode; text: "127.0.0.1:" + workspace.portForwardTarget.localPort + " -> target:" + workspace.portForwardTarget.remotePort; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: workspace.error; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        }
        footer: DialogButtonBox {
            Button { id: forwardCancel; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "startPortForward"; text: "Start forwarding"; visible: !portForwardDialog.stopMode; enabled: !workspace.authenticationRequired && !workspace.busy; onClicked: workspace.startPreparedPortForward(forwardLocal.text, forwardRemote.text) }
            Button { objectName: "stopPortForward"; text: "Stop forwarding"; visible: portForwardDialog.stopMode; onClicked: workspace.stopPreparedPortForward() }
        }
        onOpened: forwardCancel.forceActiveFocus(Qt.TabFocusReason)
        onRejected: workspace.cancelPreparedPortForward()
    }
    Loader {
        active: workspace.viewCloseNeedsDecision
        sourceComponent: Dialog {
        id: viewCloseFailure
        objectName: "viewCloseFailureDialog"
        title: "Filters and sort choices could not be saved"
        width: Math.min(480, window.width - 32)
        parent: Overlay.overlay
        visible: true
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        contentItem: Label { width: viewCloseFailure.availableWidth; text: workspace.error + "\nStay keeps the current view available. Closing retains the last successfully saved filters and sorts, not the unsaved choices."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
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
        title: "Import kubeconfig"
        onAccepted: { sourcePath.text = selectedFile.toString(); workspace.importFile(sourcePath.text) }
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
        title: "Import kubeconfig folder"
        onAccepted: { sourcePath.text = selectedFolder.toString(); workspace.importFile(sourcePath.text) }
    }
    Dialog {
        id: sourceImportDetails
        objectName: "sourceImportDetails"
        title: "Source import failures"
        width: Math.min(650, window.width - 32)
        height: Math.min(420, window.height - 32)
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Close
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
    Dialog {
        id: deleteConfirmation
        objectName: "resourceDeleteDialog"
        title: "Delete this resource?"
        width: Math.min(560, window.width - 32)
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        contentItem: ColumnLayout {
            Label { objectName: "resourceDeleteTarget"; Layout.fillWidth: true; text: workspace.deletionTarget; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            Label { Layout.fillWidth: true; text: "Only this UID will be authorized. Kubernetes grace periods and finalizers remain in effect. This cannot be undone."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox {
            Button { id: deleteCancel; objectName: "cancelResourceDelete"; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "confirmResourceDelete"; text: "Delete this resource"; enabled: workspace.deletionPending && !workspace.authenticationRequired && !workspace.busy; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onOpened: deleteCancel.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.confirmDeletion(true)
        onRejected: workspace.confirmDeletion(false)
    }
    Dialog {
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
            Button { id: applyCancel; objectName: "cancelYamlApply"; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "confirmYamlApply"; text: "Apply to this resource"; enabled: workspace.yamlApplyPreview && !workspace.discardPending; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAboutToShow: footer.contentItem.currentIndex = -1
        onOpened: applyCancel.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.confirmYamlApply(true)
        onRejected: workspace.confirmYamlApply(false)
    }
    Dialog {
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
            Button { id: reconcileCancel; objectName: "cancelYamlReconcile"; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "continueYamlReconcile"; text: "Continue to preview"; enabled: workspace.yamlReconcileReady; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onOpened: reconcileCancel.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: workspace.finishYamlReconcile(true)
        onRejected: workspace.finishYamlReconcile(false)
    }
    Dialog {
        id: authentication
        objectName: "authenticationConfirmation"
        title: "Retry authentication: " + workspace.title
        anchors.centerIn: parent
        modal: true
        property string session: ""
        property string prompt: ""
        onOpened: { session = workspace.currentSession; prompt = workspace.authenticationPrompt }
        Label { text: authentication.prompt; textFormat: Text.PlainText; wrapMode: Text.WordWrap; width: 380 }
        footer: DialogButtonBox {
            Button { objectName: "authenticationAccept"; text: "Authenticate"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { objectName: "authenticationCancel"; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: { if (session === workspace.currentSession) workspace.refresh(true) }
    }
    Dialog {
        id: renameSessionDialog
        objectName: "renameSessionDialog"
        title: "Rename session"
        width: Math.min(440, window.width - 32)
        anchors.centerIn: parent
        modal: true
        closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        property string targetSession: ""
        property bool attempted: false
        function openFor(id, name) { targetSession = id; sessionNameInput.text = name; attempted = false; open() }
        function save() { if (!workspace.busy) { attempted = true; workspace.renameSession(targetSession, sessionNameInput.text) } }
        onOpened: { sessionNameInput.forceActiveFocus(Qt.TabFocusReason); sessionNameInput.selectAll() }
        Connections { target: workspace; function onSessionRenamed(id) { if (id === renameSessionDialog.targetSession) renameSessionDialog.close() } }
        contentItem: ColumnLayout {
            Label { text: "Session name" }
            TextField { id: sessionNameInput; objectName: "sessionNameInput"; Layout.fillWidth: true; enabled: !workspace.busy; Accessible.name: "Session name"; onAccepted: renameSessionDialog.save() }
            Label { Layout.fillWidth: true; text: "Leave empty for an automatic name. The session, source snapshot and running operations stay unchanged."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Label { objectName: "sessionRenameError"; Layout.fillWidth: true; text: renameSessionDialog.attempted ? workspace.sessionRenameError : ""; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        }
        footer: DialogButtonBox {
            Button { objectName: "cancelSessionRename"; text: "Cancel"; enabled: !workspace.busy; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "saveSessionRename"; text: workspace.busy ? "Saving..." : "Rename"; enabled: !workspace.busy; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; onClicked: renameSessionDialog.save() }
        }
    }
    component NavButton: IconButton {
        showText: window.width >= 600 && !window.compactLandscape
        font.bold: true
        font.pixelSize: 13
        checkable: true
        leftPadding: showText ? 10 : 6; rightPadding: leftPadding
    }
    function toggleSidebar() {
        if (sidebarDocked) sidebarOpen = !sidebarOpen
        else if (sidebarDrawer.visible) sidebarDrawer.close()
        else sidebarDrawer.open()
    }
    header: ToolBar {
        objectName: "workspaceToolbar"
        padding: 0
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        implicitHeight: headerLayout.implicitHeight + 12
        background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
        GridLayout {
            id: headerLayout
            anchors.fill: parent; anchors.margins: 6
            columns: window.width >= 1000 && !window.compactLandscape ? 2 : 1
            columnSpacing: 12; rowSpacing: 4
            Item {
                id: navigationSlot
                visible: !window.compactLandscape
                Layout.fillWidth: true; Layout.minimumWidth: 0
                Layout.preferredWidth: navigationRow.children.reduce((total, control) => total + control.implicitWidth, 0) + navigationRow.spacing * Math.max(0, navigationRow.children.length - 1)
                Layout.preferredHeight: Math.max(34, navigationRow.implicitHeight)
                Flow {
                    id: navigationRow
                    parent: window.compactLandscape ? railContent : navigationSlot
                    width: parent.width
                    height: implicitHeight
                    LayoutMirroring.enabled: workspace.uiRightToLeft
                    LayoutMirroring.childrenInherit: true
                    spacing: window.touchLayout ? 0 : 4
                    IconButton {
                        id: searchToggle
                        objectName: "workspaceSearchButton"; glyph: "Search"; text: workspace.uiText["nav.search"]
                        checkable: true; checked: window.searchOpen
                        activeIndicator: workspace.workspacePage === "events" ? workspace.eventFilterText !== ""
                            : workspace.workspacePage === "ports" ? workspace.portFilterText !== "" : workspace.filterText !== ""
                        enabled: ["resources", "events", "ports", "dashboard"].indexOf(workspace.workspacePage) >= 0
                        Accessible.name: "Show or hide current search"
                        Accessible.description: "Search the current cached view. Hiding search retains its filter."
                        onClicked: {
                            window.searchOpen = !window.searchOpen
                            if (window.searchOpen) Qt.callLater(function() {
                                if (workspace.workspacePage === "ports") portsView.focusSearch()
                                else if (workspace.workspacePage === "events") eventFilterInput.forceActiveFocus()
                                else resourceFilterInput.forceActiveFocus()
                            })
                        }
                    }
                    NavButton { objectName: "resourcesWorkspaceButton"; glyph: "Pod"; action: resourcesCommand; text: workspace.uiText["nav.resources"]; checked: workspace.workspacePage === "resources" || workspace.workspacePage === "dashboard" }
                    NavButton { objectName: "eventsWorkspaceButton"; glyph: "Event"; action: eventsCommand; text: workspace.uiText["nav.events"]; checked: workspace.workspacePage === "events" }
                    NavButton { objectName: "portForwardTasksButton"; glyph: "Service"; text: workspace.uiText["nav.ports"]; checked: workspace.workspacePage === "ports"; Accessible.name: "Port forwards in this session"; onClicked: workspace.setWorkspacePage("ports") }
                    NavButton { objectName: "settingsWorkspaceButton"; glyph: "Filters"; action: settingsCommand; text: workspace.uiText["nav.settings"]; checked: workspace.workspacePage === "settings" || workspace.workspacePage === "alerts" }
                IconButton { objectName: "workspaceActionsButton"; glyph: "Menu"; text: "Workspace actions"; onClicked: workspaceActions.popup() }
                IconButton { objectName: "toggleSidebar"; glyph: "Sidebar"; text: "Show or hide radar and filters"; onClicked: window.toggleSidebar() }
                }
            }
            PulseStrip { condensed: window.compactLandscape; Layout.fillWidth: true; Layout.preferredHeight: implicitHeight; onDetailsRequested: workspace.setWorkspacePage("dashboard") }
        }
    }
    Menu {
        id: workspaceActions
        MenuItem { objectName: "commandPaletteButton"; text: "Commands"; Accessible.name: "Open command palette"; onTriggered: commands.open() }
        MenuItem { objectName: "refreshButton"; text: "Refresh"; enabled: !workspace.busy && !workspace.authenticationRequired && workspace.currentSession !== ""; onTriggered: workspace.refresh() }
    }
    Menu {
        id: quickOpenMenu
        objectName: "quickOpenMenu"
        width: Math.min(360, window.width - 24)
        height: Math.min(implicitHeight, window.height - 48)
        MenuItem { text: "Open kubeconfig file..."; implicitHeight: window.touchLayout ? 44 : 32; enabled: !workspace.busy; onTriggered: quickChooser.open() }
        MenuSeparator {}
        Instantiator {
            active: quickOpenMenu.visible
            model: [{name: "Saved sessions", header: true}]
                .concat(workspace.sessions.map(entry => ({id: entry.id, name: entry.name, context: false})),
                    [{name: "Imported contexts", header: true}],
                    workspace.contexts.map(entry => ({id: entry.id, name: entry.name, context: true})))
            delegate: MenuItem {
                required property var modelData
                implicitHeight: window.touchLayout ? 44 : 32
                objectName: modelData.header ? "" : (modelData.context ? "quickContext_" : "quickSession_") + modelData.id
                text: modelData.name
                enabled: !modelData.header && !workspace.busy
                checkable: !modelData.header && !modelData.context
                checked: checkable && modelData.id === workspace.currentSession
                Accessible.name: modelData.header ? text : (modelData.context ? "Open context " : "Open session ") + text
                onTriggered: modelData.context ? workspace.openContext(modelData.id) : workspace.activate(modelData.id)
            }
            onObjectAdded: (index, object) => quickOpenMenu.insertItem(index + 2, object)
            onObjectRemoved: (index, object) => quickOpenMenu.removeItem(object)
        }
    }
    Drawer {
        id: sidebarDrawer
        objectName: "sidebarDrawer"
        edge: Qt.RightEdge
        width: Math.min(392, window.width - 24); height: window.height
        onVisibleChanged: if (visible && window.sidebarDocked) close()
        Connections {
            target: window
            function onSidebarDockedChanged() { if (window.sidebarDocked) sidebarDrawer.close() }
        }
    }
    ScrollView {
        id: navigationRail
        objectName: "landscapeNavigation"
        visible: window.compactLandscape
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.margins: 6
        width: 44
        contentWidth: 44; contentHeight: navigationRow.implicitHeight
        clip: true
        Item { id: railContent; width: 44; height: navigationRow.implicitHeight }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 6
        anchors.leftMargin: window.compactLandscape ? 56 : 6
        spacing: 4
        Frame {
            id: sourcePanel
            objectName: "sourceManagementPanel"
            Layout.fillWidth: true
            visible: !settingsLoader.visible && (window.sourcesExpanded || workspace.currentSession === "")
            contentItem: ColumnLayout {
                GridLayout {
                    Layout.fillWidth: true
                    columns: width < 600 ? 4 : 5
                    columnSpacing: 6
                    rowSpacing: 6
                    TextField {
                        id: sourcePath
                        objectName: "sourcePath"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 160
                        Layout.columnSpan: parent.columns === 4 ? 4 : 1
                        enabled: !workspace.busy
                        placeholderText: "Kubeconfig file or folder (~ supported)"
                        Accessible.name: "Kubeconfig file or folder path"
                        onAccepted: workspace.importFile(text)
                    }
                    Button { text: "Browse"; Layout.minimumWidth: 44; Layout.minimumHeight: 32; Layout.fillWidth: parent.columns === 4; enabled: !workspace.busy; onClicked: chooser.open() }
                    Button { objectName: "browseSourceFolder"; text: "Folder"; Layout.minimumWidth: 44; Layout.minimumHeight: 32; Layout.fillWidth: parent.columns === 4; enabled: !workspace.busy; onClicked: folderChooser.open() }
                    Button { objectName: "importButton"; text: "Import"; Layout.minimumWidth: 44; Layout.minimumHeight: 32; Layout.fillWidth: parent.columns === 4; enabled: !workspace.busy; onClicked: sourcePath.text.trim() ? workspace.importFile(sourcePath.text) : chooser.open() }
                    Button { objectName: "importK3dButton"; action: k3dCommand; text: "K3D"; Accessible.name: "Import generated K3D kubeconfigs"; Layout.minimumWidth: 44; Layout.minimumHeight: 32; Layout.fillWidth: parent.columns === 4 }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width < 600 ? 2 : 3
                    columnSpacing: 6
                    rowSpacing: 6
                    ComboBox {
                        id: contexts
                        objectName: "contexts"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 160
                        Layout.columnSpan: parent.columns === 2 ? 2 : 1
                        enabled: !workspace.busy
                        model: workspace.contexts
                        textRole: "name"
                        valueRole: "id"
                        displayText: count > 0 ? currentText : "No imported contexts"
                        Accessible.name: "Imported context"
                    }
                    Button { objectName: "openContext"; text: "Open context"; Layout.minimumHeight: 32; Layout.fillWidth: parent.columns === 2; enabled: !workspace.busy && contexts.currentIndex >= 0; onClicked: { if (workspace.openContext(contexts.currentValue)) window.sourcesExpanded = false } }
                    RowLayout {
                        Layout.fillWidth: parent.columns === 2
                        Button { objectName: "reloadSources"; text: "Reload sources"; Layout.minimumHeight: 32; Layout.fillWidth: true; enabled: !workspace.busy; onClicked: workspace.reload() }
                        IconButton { objectName: "closeSourceControls"; glyph: "Close"; text: "Close source controls"; visible: workspace.currentSession !== ""; onClicked: window.sourcesExpanded = false }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: openTabs.implicitHeight
            spacing: 2
            IconButton { id: quickOpenFile; objectName: "quickOpenFile"; glyph: "ConfigMap"; text: "Open kubeconfig file"; enabled: !workspace.busy; onClicked: quickChooser.open() }
            IconButton { objectName: "quickOpenDropdown"; glyph: "Menu"; text: "Open saved session or imported context"; enabled: !workspace.busy; onClicked: quickOpenMenu.popup(quickOpenFile, 0, quickOpenFile.height) }
            ScrollView {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: openTabs.implicitHeight
            contentWidth: openTabs.implicitWidth
            Row {
                id: openTabs
                spacing: 6
                Repeater {
                    model: workspace.tabs
                    Row {
                        required property var modelData
                        Button {
                            objectName: "activateSession_" + modelData.id
                            implicitHeight: window.touchLayout ? 44 : 32
                            text: modelData.name
                            width: Math.min(implicitWidth, window.wideLayout ? 240 : 160)
                            contentItem: Label { text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight }
                            ToolTip.visible: hovered || activeFocus
                            ToolTip.text: modelData.name
                            checkable: true
                            checked: modelData.id === workspace.currentSession
                            enabled: !workspace.busy
                            onClicked: workspace.activate(modelData.id)
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: tabMenu.popup() }
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) { tabMenu.popup(); event.accepted = true }
                            }
                            Menu {
                                id: tabMenu
                                MenuItem { objectName: "renameTab_" + modelData.id; text: "Rename session..."; enabled: !workspace.busy; onTriggered: renameSessionDialog.openFor(modelData.id, modelData.name) }
                            }
                        }
                        IconButton { objectName: "closeSession_" + modelData.id; glyph: "Close"; text: workspace.uiText["action.close"]; Accessible.name: "Close " + modelData.name; enabled: !workspace.busy; onClicked: workspace.close(modelData.id) }
                    }
                }
            }
            }
        }
        RowLayout {
            id: queryRow
            readonly property var activeGrid: workspace.workspacePage === "events" ? eventGrid : resourceGrid
            readonly property bool navigating: window.searchOpen && ["resources", "events"].indexOf(workspace.workspacePage) >= 0
            readonly property bool hasQuery: (workspace.workspacePage === "events" ? workspace.eventFilterText : workspace.filterText).trim() !== ""
            Layout.fillWidth: true
            visible: (window.searchOpen && ["resources", "events", "dashboard"].indexOf(workspace.workspacePage) >= 0) || workspace.authenticationRequired || workspace.authenticationRunning
            TextField {
                id: resourceFilterInput
                objectName: "resourceFilter"
                enabled: workspace.currentSession !== "" && !workspace.busy
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: window.touchLayout ? 44 : 32
                text: workspace.filterText
                placeholderText: "Search cached resources"
                Accessible.name: "Filter cached resources"
                Accessible.description: "Space-separated alternatives; quoted exact values; ~prefix; suffix~; /regular expression/; integer comparisons such as >=2."
                ToolTip.visible: hovered; ToolTip.text: Accessible.description
                visible: window.searchOpen && ["resources", "dashboard"].indexOf(workspace.workspacePage) >= 0
                onTextEdited: { workspace.filter(text); resourceGrid.navigateRows(0) }
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && text.trim() !== "") {
                        resourceGrid.navigateRows(event.modifiers & Qt.ShiftModifier ? -1 : 1); event.accepted = true
                    }
                }
                Keys.onEscapePressed: { window.searchOpen = false; searchToggle.forceActiveFocus() }
            }
            TextField {
                id: eventFilterInput; objectName: "eventFilter"
                Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: window.touchLayout ? 44 : 32
                visible: window.searchOpen && workspace.workspacePage === "events"
                text: workspace.eventFilterText; placeholderText: "Search cached Events"; Accessible.name: "Search cached Events"
                onTextEdited: { workspace.filterEvents(text); eventGrid.navigateRows(0) }
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && text.trim() !== "") {
                        eventGrid.navigateRows(event.modifiers & Qt.ShiftModifier ? -1 : 1); event.accepted = true
                    }
                }
                Keys.onEscapePressed: { window.searchOpen = false; searchToggle.forceActiveFocus() }
            }
            IconButton { objectName: "queryPrevious"; glyph: "Previous"; text: workspace.uiText["tooltip.previousMatch"]; visible: queryRow.navigating; enabled: queryRow.hasQuery && queryRow.activeGrid.count > 0; onClicked: queryRow.activeGrid.navigateRows(-1) }
            IconButton { objectName: "queryNext"; glyph: "Next"; text: workspace.uiText["tooltip.nextMatch"]; visible: queryRow.navigating; enabled: queryRow.hasQuery && queryRow.activeGrid.count > 0; onClicked: queryRow.activeGrid.navigateRows(1) }
            Label { objectName: "queryMatchCount"; visible: queryRow.navigating; text: queryRow.hasQuery ? (queryRow.activeGrid.navigationIndex + 1) + "/" + queryRow.activeGrid.count : "0/0"; textFormat: Text.PlainText; color: workspace.appearanceColors.accent; Accessible.name: "Search match " + text }
            Button { objectName: "authenticationButton"; text: "Authenticate"; visible: workspace.authenticationRequired; enabled: !workspace.busy && !workspace.authenticationRunning; onClicked: authentication.open() }
            Button { objectName: "cancelAuthentication"; text: "Cancel login"; visible: workspace.authenticationRunning; onClicked: workspace.cancelAuthentication() }
        }
        Label { objectName: "filterErrorMessage"; Layout.fillWidth: true; text: workspace.filterError; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; visible: text !== ""; wrapMode: Text.Wrap; Accessible.name: text }
        RowLayout {
            Layout.fillWidth: true
            visible: workspace.sourceImportNotice !== "" && (window.sourcesExpanded || workspace.currentSession === "" || workspace.sourceImportIssues.length > 0)
            Label { objectName: "sourceImportNotice"; Layout.fillWidth: true; text: workspace.sourceImportNotice; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            Button { objectName: "sourceImportDetailsButton"; text: "View failures"; visible: workspace.sourceImportIssues.length > 0; onClicked: sourceImportDetails.open() }
        }
        Label { objectName: "errorMessage"; Layout.fillWidth: true; text: workspace.error; textFormat: Text.PlainText; visible: text !== ""; wrapMode: Text.Wrap; Accessible.name: text }
        Label {
            objectName: "syncProblemMessage"
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
                    SplitView.fillHeight: true; SplitView.minimumHeight: Math.min(120, workspaceSplit.height * 0.25)
                ResourceGrid {
                    id: resourceGrid
                    anchors.fill: parent
                    visible: workspace.workspacePage === "resources"
                    tableModel: workspace.table
                    sortColumn: workspace.sortColumnIndex; sortDirection: workspace.sortDirection
                    emptyText: workspace.currentSession === "" ? workspace.uiText["resource.emptyMessage"] : workspace.loading ? workspace.uiText["resource.loadingMessage"] : workspace.uiText["resource.noMatchingMessage"]
                    onSortRequested: function(column) { workspace.sortColumn(column) }
                    onInspectRequested: function(path) { workspace.inspectPath(path) }
                    onCopyRequested: function(row, column) { workspace.copyCell(row, column) }
                    onCopyPathRequested: function(path, column) { workspace.copyPathCell(path, column, false) }
                }
                ResourceGrid {
                    id: eventGrid
                    anchors.fill: parent
                    visible: workspace.workspacePage === "events"
                    prefix: "event"; tableModel: workspace.eventTable
                    sortColumn: workspace.eventSortColumnIndex; sortDirection: workspace.eventSortDirection
                    emptyText: workspace.loading ? "Loading Events. Cached Events appear immediately." : "No matching Events in this cache."
                    onSortRequested: function(column) { workspace.sortEventColumn(column) }
                    onInspectRequested: function(path) { workspace.inspectEventPath(path) }
                    onCopyRequested: function(row, column) { workspace.copyEventCell(row, column) }
                    onCopyPathRequested: function(path, column) { workspace.copyPathCell(path, column, true) }
                }
                Loader {
                    anchors.fill: parent
                    visible: workspace.workspacePage === "dashboard"
                    property bool opened: false
                    active: opened
                    onVisibleChanged: if (visible) opened = true
                    sourceComponent: ResourceDashboard {}
                }
                PortForwardView {
                    id: portsView
                    anchors.fill: parent
                    visible: workspace.workspacePage === "ports"
                    searchOpen: window.searchOpen
                    onSearchClosed: { window.searchOpen = false; searchToggle.forceActiveFocus() }
                    onAuthenticationRequested: authentication.open()
                }
                Loader {
                    id: settingsLoader
                    anchors.fill: parent
                    visible: workspace.workspacePage === "settings" || workspace.workspacePage === "alerts"
                    property bool opened: false
                    active: opened
                    onVisibleChanged: if (visible) opened = true
                    sourceComponent: SettingsView {
                        onSourcesRequested: window.sourcesExpanded = true
                        onFileRequested: chooser.open()
                        onFolderRequested: folderChooser.open()
                    }
                }
                }
                ResourceInspector {
                    id: dockedInspector
                    objectName: "workspaceInspector"
                    SplitView.preferredHeight: Math.min(420, workspaceSplit.height * 0.60)
                    SplitView.minimumHeight: Math.min(300, workspaceSplit.height * 0.70)
                    visible: workspace.inspectorPath !== ""
                }
            }
            Item {
                id: sidebarHost
                objectName: "workspaceSidebarHost"
                visible: window.sidebarDocked && window.sidebarOpen
                SplitView.preferredWidth: window.compactLandscape ? 240 : 392
                SplitView.minimumWidth: window.compactLandscape ? 228 : 300
                SplitView.maximumWidth: window.compactLandscape ? 320 : 720
            }
        }
        RowLayout {
            Layout.fillWidth: true
            objectName: "workspaceFooter"
            Layout.minimumHeight: 24; Layout.maximumHeight: 24; Layout.preferredHeight: 24
            LayoutMirroring.enabled: workspace.uiRightToLeft; LayoutMirroring.childrenInherit: true
            Label { objectName: "resourceMatchCount"; Layout.minimumWidth: 0; Layout.maximumWidth: window.width * 0.45; font.pixelSize: 11; maximumLineCount: 1; wrapMode: Text.NoWrap; elide: Text.ElideRight; text: "visible: " + workspace.resourceCount + "/" + workspace.totalResourceCount; Accessible.name: text }
            Label { objectName: "syncStatus"; Layout.fillWidth: true; Layout.minimumWidth: 0; font.pixelSize: 11; maximumLineCount: 1; wrapMode: Text.NoWrap; text: workspace.syncSummary; textFormat: Text.PlainText; elide: Text.ElideRight; Accessible.name: text; ToolTip.visible: syncHover.hovered; ToolTip.text: text; HoverHandler { id: syncHover } }
        }
    }
    ResourceSidebar {
        id: sidebar
        parent: window.sidebarDocked ? sidebarHost : sidebarDrawer.contentItem
        collapsibleFilters: window.compactLandscape
        anchors.fill: parent
        visible: window.sidebarDocked ? sidebarHost.visible : sidebarDrawer.visible
        onFieldRequested: function(field, anchor) { fieldFiltersDialog.openField(field, window.sidebarDocked ? anchor : null); if (!window.sidebarDocked) sidebarDrawer.close() }
        onSourcesRequested: { window.sourcesExpanded = !sourcePanel.visible; if (!window.sidebarDocked) sidebarDrawer.close() }
        onRenameRequested: renameSessionDialog.openFor(workspace.currentSession, workspace.title)
        onFiltersRequested: { fieldFiltersDialog.openFilters(); if (!window.sidebarDocked) sidebarDrawer.close() }
        onFiltersScrolled: if (fieldFiltersDialog.anchorItem) fieldFiltersDialog.close()
    }
}
