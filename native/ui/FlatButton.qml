import QtQuick
import QtQuick.Controls
import QtQuick.Window

Button {
    id: control
    property bool destructive: false
    implicitHeight: Math.max(Window.window && (Window.window.width < 900 || Window.window.compactLandscape === true) ? 44 : 32,
        implicitContentHeight + topPadding + bottomPadding)
    padding: 6
    background: Rectangle {
        color: control.down || control.checked ? workspace.appearanceColors.selection
            : control.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.inset
        border.color: control.destructive ? workspace.appearanceColors.danger
            : control.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
    }
    contentItem: Label {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: !control.enabled ? workspace.appearanceColors.muted
            : control.destructive ? workspace.appearanceColors.danger : workspace.appearanceColors.text
    }
}
