import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "sessionManagementDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: "Session configuration"
    header: DialogHeader { dialog: dialog; closeObjectName: "cancelSessionManagement"; closeEnabled: !workspace.busy }
    width: Math.min(580, parent ? parent.width - 24 : 580)
    height: Math.min(560, parent ? parent.height - 24 : 560)
    property string selectedSession: ""
    property string selectedContext: ""
    property bool submitted: false
    closePolicy: workspace.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    function alignSelection() {
        sessions.currentIndex = sessions.indexOfValue(selectedSession)
        contexts.currentIndex = contexts.indexOfValue(selectedContext)
    }
    function loadSession(id) {
        submitted = false
        selectedSession = id
        const config = workspace.sessionConfiguration(id)
        selectedContext = config.contextId || ""
        namespaces.text = config.namespaces || ""
        copyName.text = ""
        alignSelection()
    }
    onOpened: loadSession(workspace.currentSession || (sessions.count ? sessions.valueAt(0) : ""))
    Connections {
        target: workspace
        function onCatalogsChanged() { if (dialog.visible) Qt.callLater(dialog.alignSelection) }
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: "Creates a saved snapshot. Your open session stays unchanged."
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Label { text: "Session" }
            ComboBox {
                id: sessions
                objectName: "sessionManageSelector"
                Layout.fillWidth: true
                model: dialog.visible ? workspace.sessions : []
                textRole: "name"
                valueRole: "id"
                enabled: !workspace.busy
                Accessible.name: "Session to configure, not activate"
                onActivated: dialog.loadSession(currentValue)
            }
            Label { text: "Context" }
            ComboBox {
                id: contexts
                objectName: "sessionManageContext"
                Layout.fillWidth: true
                model: dialog.visible ? workspace.contexts : []
                textRole: "name"
                valueRole: "id"
                enabled: !workspace.busy
                Accessible.name: "Snapshot context"
                onActivated: dialog.selectedContext = currentValue
            }
            Label { Layout.fillWidth: true; text: "Namespaces"; wrapMode: Text.Wrap }
            TextField {
                id: namespaces
                objectName: "sessionManageNamespaces"
                Layout.fillWidth: true
                selectByMouse: true
                enabled: !workspace.busy
                placeholderText: "All namespaces"
                Accessible.name: "Namespace scope"
                Accessible.description: "Comma-separated namespace names; empty includes all namespaces."
            }
            IconButton {
                glyph: "Save"; showText: true
                objectName: "saveSessionConfiguration"
                Layout.fillWidth: true
                text: "Save snapshot"
                enabled: !workspace.busy && dialog.selectedSession.length > 0
                onClicked: { dialog.submitted = true; workspace.saveSessionConfiguration(dialog.selectedSession, dialog.selectedContext, namespaces.text) }
            }
            Label { Layout.fillWidth: true; text: "Copy name (optional)"; wrapMode: Text.Wrap }
            TextField {
                id: copyName
                objectName: "sessionCopyName"
                Layout.fillWidth: true
                selectByMouse: true
                enabled: !workspace.busy
                Accessible.name: "Independent session copy name"
            }
            IconButton {
                glyph: "Save"; showText: true
                objectName: "duplicateSessionConfiguration"
                Layout.fillWidth: true
                text: "Save a copy"
                Accessible.description: "Copy the original session, not the unsaved context or namespace changes above."
                enabled: !workspace.busy && dialog.selectedSession.length > 0
                onClicked: { dialog.submitted = true; workspace.duplicateSession(dialog.selectedSession, copyName.text) }
            }
            Label {
                objectName: "sessionManagementError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: dialog.submitted ? workspace.sessionManagementError : ""
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            Label {
                objectName: "sessionManagementNotice"
                Layout.fillWidth: true
                visible: text.length > 0
                text: dialog.submitted ? workspace.sessionManagementNotice : ""
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
