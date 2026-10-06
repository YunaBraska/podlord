import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property bool following: true
    property bool restoring: false
    function openEntry(index) {
        if (index < 0 || index >= lines.count) return false
        lines.currentIndex = index
        entryText.text = workspace.logEntry(index)
        entry.open()
        return true
    }
    onVisibleChanged: { if (visible) Qt.callLater(restore); else restoring = false }
    function remember() {
        if (!visible || restoring) return
        let index = lines.indexAt(1, lines.contentY + 1)
        let entry = lines.itemAtIndex(index)
        workspace.rememberLogPosition(workspace.logRows.entryId(index), entry ? Math.max(0, lines.contentY - entry.y) : 0, following)
    }
    function restore() {
        if (!visible) { restoring = false; return }
        following = workspace.logFollow
        if (following) lines.positionViewAtEnd()
        else {
            let index = workspace.logRows.findEntry(workspace.logAnchor)
            if (index < 0 && workspace.logAnchor !== "") {
                lines.positionViewAtBeginning()
                workspace.reportLogEviction()
            } else if (index >= 0) {
                lines.positionViewAtIndex(index, ListView.Beginning)
                lines.contentY += workspace.logOffset
            }
        }
        restoring = false
    }
    Connections {
        target: workspace
        enabled: panel.visible
        function onLogViewChanging() { panel.remember(); panel.restoring = true }
        function onLogViewChanged() { Qt.callLater(panel.restore) }
    }
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            objectName: "logContainers"
            Layout.fillWidth: true
            model: workspace.logContainers
            textRole: "name"
            valueRole: "id"
            currentIndex: workspace.logContainers.findIndex(option => option.id === workspace.logSelection)
            Accessible.name: "Pod log container"
            onActivated: workspace.selectLogContainer(currentValue)
        }
        Button { objectName: "pauseLogs"; text: workspace.logsPaused ? "Resume" : "Pause"; onClicked: workspace.pauseLogs(!workspace.logsPaused) }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Button { objectName: "liveFollow"; text: "Live follow"; onClicked: workspace.followLogs() }
        Button { objectName: "openLogEntry"; text: "Open entry"; enabled: lines.currentIndex >= 0; onClicked: panel.openEntry(lines.currentIndex) }
        Button { objectName: "copyLogEntry"; text: "Copy entry"; enabled: lines.currentIndex >= 0; onClicked: workspace.copyLogEntry(lines.currentIndex) }
    }
    Label { objectName: "logStatus"; Layout.fillWidth: true; text: workspace.logStatus; textFormat: Text.PlainText; visible: text !== ""; wrapMode: Text.Wrap }
    Label { objectName: "logEvictionNotice"; Layout.fillWidth: true; text: workspace.logPositionNotice; textFormat: Text.PlainText; visible: text !== ""; wrapMode: Text.Wrap }
    ListView {
        id: lines
        objectName: "podLogEntries"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        reuseItems: true
        model: workspace.logRows
        focus: true
        onMovementStarted: { panel.following = false; panel.remember() }
        onMovementEnded: panel.remember()
        onContentYChanged: { if (!panel.following) panel.remember() }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Home) { panel.following = false; positionViewAtBeginning(); currentIndex = 0; Qt.callLater(panel.remember); event.accepted = true; return }
            if ([Qt.Key_Up, Qt.Key_PageUp].indexOf(event.key) >= 0) { panel.following = false; Qt.callLater(panel.remember) }
            if (event.matches(StandardKey.Copy)) { workspace.copyLogEntry(currentIndex); event.accepted = true }
        }
        WheelHandler { target: null; onWheel: function(event) { panel.following = false; Qt.callLater(panel.remember); event.accepted = false } }
        ScrollBar.vertical: ScrollBar { onPressedChanged: { if (pressed) { panel.following = false; panel.remember() } } }
        delegate: ItemDelegate {
            required property int index
            required property string line
            objectName: "logEntry_" + index
            width: lines.width
            height: 30
            text: line
            Accessible.name: line
            highlighted: lines.currentIndex === index
            contentItem: Label { text: line; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
            onClicked: lines.currentIndex = index
            onDoubleClicked: panel.openEntry(index)
            Keys.onReturnPressed: panel.openEntry(index)
            Keys.onEnterPressed: panel.openEntry(index)
        }
    }
    Dialog {
        id: entry
        title: "Log entry"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(600, Overlay.overlay.width - 24)
        height: Math.min(420, Overlay.overlay.height - 24)
        standardButtons: Dialog.Close
        onClosed: entryText.clear()
        ScrollView {
            anchors.fill: parent
            TextArea { id: entryText; objectName: "fullLogEntry"; readOnly: true; textFormat: TextEdit.PlainText; wrapMode: TextArea.Wrap; Accessible.name: "Complete log entry" }
        }
    }
}
