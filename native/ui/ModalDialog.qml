import QtQuick
import QtQuick.Controls
import QtQuick.Window

Dialog {
    id: modalRoot
    readonly property rect dialogArea: {
        if (!parent) return Qt.rect(0, 0, 0, 0)
        const window = parent.Window.window
        if (window && window.inputArea !== undefined) return window.inputArea
        const margins = parent.SafeArea.margins
        return Qt.rect(margins.left, margins.top, Math.max(0, parent.width - margins.left - margins.right),
            Math.max(0, parent.height - margins.top - margins.bottom))
    }
    modal: true
    focus: true
    header: DialogHeader { dialog: modalRoot; closeEnabled: modalRoot.closePolicy !== Popup.NoAutoClose }
    Overlay.modal: Rectangle { color: "#99000000" }
}
