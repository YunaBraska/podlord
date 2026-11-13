import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Podlord.Graphics 1.0

ColumnLayout {
    id: root
    property bool inputPriority: false
    property bool expanded: false
    property bool showKeys: false
    property bool showShell: false
    property var terminal: workspace.containerTerminal
    readonly property bool running: terminal !== null && terminal.active
    readonly property bool canConfigure: !running || workspace.terminalConnection.path === workspace.inspectorPath
    readonly property real controlHeight: root.Window.window ? root.Window.window.controlHeight : 44
    property alias inputSurface: surface
    signal closeRequested()
    spacing: inputPriority ? 2 : 4
    RowLayout {
        objectName: "terminalHeader"
        Layout.fillWidth: true
        spacing: 4
        Label {
            id: terminalTarget
            objectName: "terminalTarget"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: !root.inputPriority && (root.width >= 480 || root.running)
            text: workspace.terminalConnection.name || workspace.inspectorName
            font.bold: true
            elide: Text.ElideMiddle
            textFormat: Text.PlainText
            Accessible.name: root.terminal ? root.terminal.target : text
            ToolTip.visible: targetHover.hovered
            ToolTip.text: Accessible.name
            HoverHandler { id: targetHover }
        }
        ComboBox {
            id: container
            objectName: "terminalContainer"
            visible: !root.inputPriority && !root.running && root.canConfigure
            Layout.preferredWidth: Math.min(terminalTarget.visible ? 160 : 200, root.width * (terminalTarget.visible ? 0.35 : 0.48))
            Layout.minimumWidth: 64
            Layout.minimumHeight: root.controlHeight
            model: workspace.terminalContainers
            Accessible.name: "Container for interactive shell"
        }
        Label {
            objectName: "terminalConnectedContainer"
            visible: !root.inputPriority && root.terminal !== null && (root.running || !root.canConfigure)
            Layout.maximumWidth: root.width * 0.28
            Layout.minimumWidth: 0
            text: workspace.terminalConnection.container || ""
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
        }
        Label {
            objectName: "terminalStatus"
            visible: root.terminal !== null && (root.running || root.terminal.failed)
            text: root.terminal ? (root.terminal.connected ? "\u2713" : root.terminal.active ? "\u2026" : "!") : ""
            Accessible.name: root.terminal ? root.terminal.target + ": " + root.terminal.status : "Not connected"
            ToolTip.visible: statusHover.hovered
            ToolTip.text: root.terminal ? root.terminal.status : ""
            HoverHandler { id: statusHover }
        }
        Button {
            objectName: "terminalConnect"
            Layout.minimumHeight: root.controlHeight
            visible: !root.inputPriority && !root.running && root.canConfigure
            text: "Connect"
            enabled: workspace.canStartTerminal
            Accessible.name: text
            Accessible.description: "Start interactive shell in selected container"
            onClicked: {
                if (workspace.startContainerTerminal(container.currentText, shell.text)) Qt.callLater(function() { surface.forceActiveFocus() })
            }
        }
        IconButton {
            id: actionsButton
            objectName: "terminalActions"
            focusPolicy: Qt.StrongFocus
            visible: !root.inputPriority
            glyph: "Menu"
            text: "Terminal actions"
            onClicked: actions.popup(actionsButton, 0, actionsButton.height)
        }
        IconButton {
            id: closeButton
            objectName: "collapseTerminal"
            focusPolicy: Qt.StrongFocus
            visible: root.expanded && !root.inputPriority
            glyph: "Close"
            text: "Close terminal view"
            onClicked: root.closeRequested()
        }
    }
    TextField {
        id: shell
        objectName: "terminalShell"
        visible: root.showShell && !root.inputPriority && !root.running && root.canConfigure
        Layout.fillWidth: true
        Layout.minimumHeight: root.controlHeight
        text: "/bin/sh"
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        selectByMouse: true
        Accessible.name: "Absolute POSIX shell path"
    }
    Label {
        objectName: "terminalError"
        Layout.fillWidth: true
        visible: text !== ""
        text: workspace.terminalError
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: root.terminal && !root.terminal.failed ? workspace.appearanceColors.muted : workspace.appearanceColors.danger
    }
    Flickable {
        id: keyScroll
        visible: root.showKeys && !root.inputPriority
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        Layout.minimumHeight: 44
        contentWidth: keyRow.implicitWidth
        contentHeight: height
        clip: true
        Row {
            id: keyRow
            height: parent.height
            spacing: 4
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
                    width: Math.max(44, implicitWidth)
                    height: 44
                    enabled: root.terminal !== null && root.terminal.connected
                    Accessible.name: text
                    Accessible.description: modelData.help
                    onActiveFocusChanged: if (activeFocus) keyScroll.contentX = Math.max(0, Math.min(x, keyScroll.contentWidth - keyScroll.width))
                    ToolTip.visible: hovered || visualFocus
                    ToolTip.text: modelData.help
                    onClicked: if (root.terminal.key(modelData.key, modelData.input, Qt.NoModifier)) surface.forceActiveFocus()
                }
            }
        }
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
        fontPixelSize: workspace.textSizes.body
        Accessible.role: Accessible.Terminal
        Accessible.name: root.terminal ? "Interactive container terminal: " + root.terminal.target : "Container terminal"
        Accessible.description: visibleText
        onPasteRequested: pasteReview.open()
        onControlsFocusRequested: {
            Qt.inputMethod.hide()
            Qt.callLater(function() { (root.expanded ? closeButton : actionsButton).forceActiveFocus(Qt.ShortcutFocusReason) })
        }
        Rectangle { anchors.fill: parent; color: "transparent"; border.width: surface.activeFocus ? 2 : 1; border.color: surface.activeFocus ? workspace.appearanceColors.glow : workspace.appearanceColors.border }
    }
    Menu {
        id: actions
        width: Math.min(360, Math.max(0, root.width - 16))
        MenuItem {
            visible: !terminalTarget.visible && text !== ""
            enabled: false
            text: terminalTarget.text
            implicitHeight: Math.max(root.controlHeight, contentItem.implicitHeight + 12)
            contentItem: Label { text: terminalTarget.text; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: workspace.appearanceColors.text }
        }
        MenuItem { implicitHeight: root.controlHeight; objectName: "terminalCopy"; text: "Copy selection"; enabled: root.terminal !== null; onTriggered: surface.copySelection() }
        MenuItem { implicitHeight: root.controlHeight; objectName: "terminalPaste"; text: "Paste..."; enabled: root.terminal !== null && root.terminal.connected; onTriggered: pasteReview.open() }
        MenuItem { implicitHeight: root.controlHeight; objectName: "terminalFollow"; text: "Follow output"; enabled: root.terminal !== null; onTriggered: surface.followOutput() }
        MenuItem { implicitHeight: root.controlHeight; objectName: "terminalKeys"; text: "Special keys"; checkable: true; checked: root.showKeys; enabled: root.showKeys || (root.terminal !== null && root.terminal.connected); onTriggered: root.showKeys = checked }
        MenuItem { implicitHeight: root.controlHeight; text: "Shell path"; visible: !root.running && root.canConfigure; checkable: true; checked: root.showShell; onTriggered: root.showShell = checked }
        MenuItem { implicitHeight: root.controlHeight; objectName: "terminalHelp"; text: "Keyboard help"; onTriggered: helpDialog.open() }
        MenuSeparator {}
        MenuItem {
            implicitHeight: root.controlHeight
            objectName: "terminalDisconnect"
            text: "Disconnect"
            enabled: root.running
            Accessible.description: "Remote processes may continue after disconnecting. Exit the shell first to stop it."
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: Accessible.description
            onTriggered: workspace.stopContainerTerminal()
        }
    }
    ModalDialog {
        id: helpDialog
        objectName: "terminalHelpDialog"
        title: "Terminal keys"
        header: DialogHeader { dialog: helpDialog }
        modal: true
        width: Math.min(520, root.width)
        contentItem: Label {
            text: "Ctrl+C interrupts. Escape goes to the running program.\nShift+Esc moves focus to the controls.\nShift+Page Up/Down scrolls history. Shift+drag selects text."
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
    }
    ModalDialog {
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
            Button { objectName: "terminalPasteConfirm"; implicitHeight: root.controlHeight; text: "Paste"; enabled: pasteReview.pasteError === "" && pasteReview.payload !== "" && pasteReview.reviewedTerminal !== null && pasteReview.reviewedTerminal === root.terminal && pasteReview.reviewedTerminal.connected; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { objectName: "terminalPasteCancel"; implicitHeight: root.controlHeight; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
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
