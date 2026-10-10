import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Podlord.Graphics 1.0

ColumnLayout {
    id: root
    spacing: 6
    property var terminal: workspace.containerTerminal
    property alias inputSurface: surface
    ScrollView {
        id: toolbarScroll
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(controls.implicitHeight, root.height * 0.4)
        Layout.minimumHeight: 0
        contentWidth: availableWidth
        contentHeight: controls.implicitHeight
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Flow {
        id: controls
        width: toolbarScroll.availableWidth
        spacing: 6
        ComboBox {
            id: container
            objectName: "terminalContainer"
            width: 180
            model: workspace.terminalContainers
            enabled: !root.terminal || !root.terminal.active
            Accessible.name: "Container for interactive shell"
        }
        TextField {
            id: shell
            objectName: "terminalShell"
            width: 160
            text: "/bin/sh"
            enabled: !root.terminal || !root.terminal.active
            selectByMouse: true
            Accessible.name: "Absolute POSIX shell path"
        }
        Button {
            objectName: "terminalConnect"
            text: "Connect"
            enabled: workspace.canStartTerminal
            Accessible.name: "Start interactive shell in selected container"
            onClicked: {
                if (workspace.startContainerTerminal(container.currentText, shell.text)) Qt.callLater(function() { surface.forceActiveFocus() })
            }
        }
        Button {
            objectName: "terminalDisconnect"; text: "Disconnect"
            enabled: root.terminal !== null && root.terminal.active
            Accessible.description: "Disconnects this stream. Remote processes may continue; exit the shell first to stop it."
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: Accessible.description
            onClicked: workspace.stopContainerTerminal()
        }
        Button { id: copyButton; objectName: "terminalCopy"; text: "Copy selection"; enabled: root.terminal !== null; onClicked: surface.copySelection() }
        Button { objectName: "terminalPaste"; text: "Paste..."; enabled: root.terminal !== null && root.terminal.connected; onClicked: pasteReview.open() }
        Button { objectName: "terminalFollow"; text: "Follow output"; enabled: root.terminal !== null; onClicked: surface.followOutput() }
        Button { id: terminalKeys; objectName: "terminalKeys"; text: "Keys"; checkable: true; enabled: root.terminal !== null && root.terminal.connected; Accessible.name: "Show terminal special keys" }
        Repeater {
            model: [
                { id: "escape", label: "Esc", key: Qt.Key_Escape, input: "", help: "Send Escape to the running program" },
                { id: "tab", label: "Tab", key: Qt.Key_Tab, input: "", help: "Send Tab for shell completion" },
                { id: "interrupt", label: "Ctrl+C", key: 0, input: "\u0003", help: "Interrupt the running program" },
                { id: "eof", label: "Ctrl+D", key: 0, input: "\u0004", help: "Send end of input to the running program" },
                { id: "left", label: "Left", key: Qt.Key_Left, input: "", help: "Send the left arrow key" },
                { id: "right", label: "Right", key: Qt.Key_Right, input: "", help: "Send the right arrow key" },
                { id: "up", label: "Up", key: Qt.Key_Up, input: "", help: "Send the up arrow key" },
                { id: "down", label: "Down", key: Qt.Key_Down, input: "", help: "Send the down arrow key" }
            ]
            delegate: Button {
                required property var modelData
                objectName: "terminalKey_" + modelData.id
                text: modelData.label
                visible: terminalKeys.checked
                enabled: root.terminal !== null && root.terminal.connected
                Accessible.name: modelData.help
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.help
                onClicked: if (root.terminal.key(modelData.key, modelData.input, Qt.NoModifier)) surface.forceActiveFocus()
            }
        }
        }
    }
    Label {
        objectName: "terminalStatus"
        Layout.fillWidth: true
        text: root.terminal ? root.terminal.target + " / " + root.terminal.status : "Choose a container and connect. Session close disconnects the stream; remote processes may continue. No automatic reconnect."
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        Accessible.name: text
    }
    TerminalSurface {
        id: surface
        objectName: "terminalSurface"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        terminal: root.terminal
        foreground: workspace.appearanceColors.text
        background: workspace.appearanceColors.inset
        fontFamily: workspace.monospaceFamily
        Accessible.role: Accessible.Terminal
        Accessible.name: root.terminal ? "Interactive container terminal: " + root.terminal.target : "Container terminal"
        Accessible.description: visibleText
        onPasteRequested: pasteReview.open()
        onControlsFocusRequested: {
            const target = root.terminal ? copyButton : container
            const pane = toolbarScroll.contentItem
            pane.contentY = Math.min(target.y, Math.max(0, pane.contentHeight - pane.height))
            target.forceActiveFocus(Qt.ShortcutFocusReason)
        }
        Rectangle { anchors.fill: parent; color: "transparent"; border.width: surface.activeFocus ? 2 : 1; border.color: surface.activeFocus ? workspace.appearanceColors.glow : workspace.appearanceColors.border }
    }
    Label { Layout.fillWidth: true; text: "Control-C interrupts; Escape reaches vi. Shift+Esc focuses controls. Shift + Page Up/Down scrolls history. Shift + drag selects when an application tracks the mouse."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
    Dialog {
        id: pasteReview
        objectName: "terminalPasteReview"
        title: "Paste into running shell?"
        modal: true
        width: Math.min(520, root.width)
        property string payload: ""
        property string pasteError: ""
        property var reviewedTerminal: null
        onOpened: { const content = surface.clipboardForPaste(); payload = content.text; pasteError = content.error; reviewedTerminal = root.terminal }
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: "This text may execute commands. Confirm only trusted content."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Label { objectName: "terminalPasteError"; Layout.fillWidth: true; visible: text !== ""; text: pasteReview.pasteError; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 140
                TextArea { text: pasteReview.payload; readOnly: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; font.family: workspace.monospaceFamily }
            }
        }
        footer: DialogButtonBox {
            Button { objectName: "terminalPasteConfirm"; text: "Paste"; enabled: pasteReview.pasteError === "" && pasteReview.payload !== "" && pasteReview.reviewedTerminal !== null && pasteReview.reviewedTerminal === root.terminal && pasteReview.reviewedTerminal.connected; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { objectName: "terminalPasteCancel"; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onAccepted: pasteReview.accept()
            onRejected: pasteReview.reject()
        }
        onAccepted: {
            const selected = reviewedTerminal
            const text = payload
            payload = ""; pasteError = ""; reviewedTerminal = null
            if (selected && selected === root.terminal && selected.connected) selected.paste(text)
        }
        onRejected: { payload = ""; pasteError = ""; reviewedTerminal = null }
    }
}
