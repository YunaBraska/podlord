import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: commandPopup
    objectName: "commandPalette"
    required property var actions
    readonly property var matches: {
        if (!visible) return []
        const terms = search.text.trim().toLowerCase().split(/\s+/)
        return actions.filter(action => {
            const words = action.text.toLowerCase().split(/\s+/)
            return terms.every(term => words.some(word => word.startsWith(term)))
        })
    }
    parent: Overlay.overlay
    modal: true
    focus: true
    title: "Commands"
    width: Math.min(560, parent.width - 24)
    x: (parent.width - width) / 2
    y: Math.max(12, (parent.height - height) / 2)
    function execute(index) {
        const action = matches[index]
        if (action && action.enabled) { close(); action.trigger() }
    }
    onOpened: { search.text = ""; commands.currentIndex = 0; search.forceActiveFocus() }
    onClosed: search.text = ""
    contentItem: ColumnLayout {
        spacing: 8
        TextField {
            id: search
            objectName: "commandPaletteSearch"
            Layout.fillWidth: true
            placeholderText: "Find a command"
            Accessible.name: "Find a command"
            onTextChanged: commands.currentIndex = 0
            onAccepted: commandPopup.execute(commands.currentIndex)
            Keys.onDownPressed: commands.currentIndex = Math.min(commands.count - 1, commands.currentIndex + 1)
            Keys.onUpPressed: commands.currentIndex = Math.max(0, commands.currentIndex - 1)
        }
        Label {
            objectName: "commandPaletteEmpty"
            Layout.fillWidth: true
            text: commands.count ? "Enter to run, Esc to close." : "No commands match."
            textFormat: Text.PlainText
            elide: Text.ElideRight
        }
        ListView {
            id: commands
            objectName: "commandPaletteList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(40, Math.min(228, commandPopup.parent.height - 200))
            clip: true
            model: commandPopup.matches
            currentIndex: 0
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                objectName: "commandEntry_" + modelData.text
                width: ListView.view.width
                height: 38
                text: modelData.text + (modelData.enabled ? "" : " (unavailable)")
                enabled: modelData.enabled
                highlighted: ListView.isCurrentItem
                onClicked: commandPopup.execute(index)
            }
        }
    }
    footer: DialogButtonBox {
        Button { objectName: "commandPaletteClose"; text: "Close"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onRejected: commandPopup.reject()
    }
}
