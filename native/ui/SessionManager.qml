import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ModalDialog {
    id: surfaceDialog
    objectName: "sessionManagementDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: workspace.uiText["session.configure"]
    header: DialogHeader { dialog: surfaceDialog; closeObjectName: "cancelSessionManagement"; closeEnabled: !workspace.busy }
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
        function onCatalogsChanged() { if (surfaceDialog.visible) Qt.callLater(surfaceDialog.alignSelection) }
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: workspace.uiText["session.configure"] }
            ComboBox {
                id: sessions
                objectName: "sessionManageSelector"
                Layout.fillWidth: true
                model: surfaceDialog.visible ? workspace.sessions : []
                textRole: "name"
                valueRole: "id"
                enabled: !workspace.busy
                Accessible.name: "Session to configure, not activate"
                onActivated: surfaceDialog.loadSession(currentValue)
            }
            Label { text: workspace.uiText["context.name"] }
            ComboBox {
                id: contexts
                objectName: "sessionManageContext"
                Layout.fillWidth: true
                model: surfaceDialog.visible ? workspace.contexts : []
                textRole: "name"
                valueRole: "id"
                enabled: !workspace.busy
                Accessible.name: "Snapshot context"
                onActivated: surfaceDialog.selectedContext = currentValue
            }
            Label { Layout.fillWidth: true; text: workspace.uiText["session.namespaces"]; wrapMode: Text.Wrap }
            TextField {
                id: namespaces
                objectName: "sessionManageNamespaces"
                Layout.fillWidth: true
                selectByMouse: true
                enabled: !workspace.busy
                placeholderText: workspace.uiText["session.allNamespaces"]
                Accessible.name: "Namespace scope"
                Accessible.description: "Comma-separated namespace names; empty includes all namespaces."
            }
            IconButton {
                glyph: "Save"; showText: true
                objectName: "saveSessionConfiguration"
                Layout.fillWidth: true
                text: workspace.uiText["action.save"]
                enabled: !workspace.busy && surfaceDialog.selectedSession.length > 0
                onClicked: { surfaceDialog.submitted = true; workspace.saveSessionConfiguration(surfaceDialog.selectedSession, surfaceDialog.selectedContext, namespaces.text) }
            }
            Label { Layout.fillWidth: true; text: workspace.uiText["session.copyName"]; wrapMode: Text.Wrap }
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
                text: workspace.uiText["session.saveCopy"]
                Accessible.description: "Copy the original session, not the unsaved context or namespace changes above."
                enabled: !workspace.busy && surfaceDialog.selectedSession.length > 0
                onClicked: { surfaceDialog.submitted = true; workspace.duplicateSession(surfaceDialog.selectedSession, copyName.text) }
            }
            Label {
                objectName: "sessionManagementError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: surfaceDialog.submitted ? workspace.sessionManagementError : ""
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
