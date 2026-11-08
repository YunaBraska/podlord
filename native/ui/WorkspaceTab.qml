import QtQuick
import QtQuick.Controls
import QtQuick.Window

TabButton {
    id: tab
    width: visible ? undefined : 0
    implicitWidth: Math.max(88, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Window.window && (Window.window.width < 900 || Window.window.compactLandscape === true) ? 44 : 32, implicitContentHeight + topPadding + bottomPadding)
    padding: 8
    font.bold: true
    Accessible.role: Accessible.PageTab
    contentItem: Label {
        text: tab.text
        textFormat: Text.PlainText
        font: tab.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: tab.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
    }
    background: Rectangle {
        color: tab.checked || tab.down ? workspace.appearanceColors.selection
            : tab.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.inset
        border.color: tab.checked || tab.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 2
            visible: tab.checked
            color: workspace.appearanceColors.accent
        }
    }
}
