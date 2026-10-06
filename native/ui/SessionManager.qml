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
                text: "Saving a changed configuration creates a closed snapshot. The active session, editor and port-forwards remain unchanged. Open saved results explicitly from Saved sessions."
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
            Label { text: "Imported context" }
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
            Label { Layout.fillWidth: true; text: "Namespaces, comma-separated; empty means all"; wrapMode: Text.Wrap }
            TextField {
                id: namespaces
                objectName: "sessionManageNamespaces"
                Layout.fillWidth: true
                selectByMouse: true
                enabled: !workspace.busy
                Accessible.name: "Namespace scope"
            }
            Button {
                objectName: "saveSessionConfiguration"
                Layout.fillWidth: true
                text: "Save configuration snapshot"
                enabled: !workspace.busy && dialog.selectedSession.length > 0
                onClicked: { dialog.submitted = true; workspace.saveSessionConfiguration(dialog.selectedSession, dialog.selectedContext, namespaces.text) }
            }
            Label { Layout.fillWidth: true; text: "Independent copy name; empty means unnamed"; wrapMode: Text.Wrap }
            TextField {
                id: copyName
                objectName: "sessionCopyName"
                Layout.fillWidth: true
                selectByMouse: true
                enabled: !workspace.busy
                Accessible.name: "Independent session copy name"
            }
            Button {
                objectName: "duplicateSessionConfiguration"
                Layout.fillWidth: true
                text: "Save independent copy of original session"
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
    footer: DialogButtonBox {
        Button {
            objectName: "cancelSessionManagement"
            text: "Close"
            enabled: !workspace.busy
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onRejected: dialog.close()
    }
}
