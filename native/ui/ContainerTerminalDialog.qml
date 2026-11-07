import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    required property var hostWindow
    property bool followsInspector: false
    property string ownerSession: ""
    readonly property bool inputPriority: hostWindow.inputPriority
    // The window overlay and inputArea already use window-local coordinates.
    readonly property rect usableArea: hostWindow.inputArea
    readonly property real viewportTop: Math.max(8, usableArea.y)
    readonly property real viewportBottom: Math.min(parent.height, usableArea.y + usableArea.height)
    objectName: "expandedTerminal"
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: viewportTop
    width: Math.max(0, parent.width - 16)
    height: Math.max(0, viewportBottom - viewportTop - 8)
    padding: 2
    title: workspace.terminalConnection.name || workspace.inspectorName || "Terminal"
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    header: null
    footer: null
    onClosed: ownerSession = ""
    onOpened: Qt.callLater(function() {
        const view = terminalContent.item
        if (dialog.visible && view && view.terminal && view.terminal.connected) view.inputSurface.forceActiveFocus()
    })
    Connections {
        target: workspace
        function onChanged() {
            if (dialog.visible && (dialog.ownerSession !== workspace.currentSession
                || (dialog.followsInspector && workspace.inspectorPage !== "terminal"))) dialog.close()
        }
    }
    contentItem: Loader {
        id: terminalContent
        active: dialog.visible
        source: "ContainerTerminalView.qml"
        onLoaded: {
            item.expanded = true
            item.inputPriority = Qt.binding(function() { return dialog.inputPriority })
            item.closeRequested.connect(function() { dialog.reject() })
        }
    }
}
