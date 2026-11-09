import QtQuick
import QtQuick.Controls

Dialog {
    id: modalRoot
    modal: true
    focus: true
    header: DialogHeader { dialog: modalRoot; closeEnabled: modalRoot.closePolicy !== Popup.NoAutoClose }
    Overlay.modal: Rectangle { color: "#99000000" }
}
