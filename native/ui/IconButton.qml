import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Podlord.Graphics 1.0

ToolButton {
    id: control
    required property string glyph
    property bool showText: false
    property bool activeIndicator: false
    readonly property int targetSize: Window.window && (Window.window.width < 900 || Window.window.compactLandscape === true) ? 44 : 32
    implicitWidth: Math.max(targetSize, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: targetSize
    padding: 6
    Accessible.name: text
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: text
    background: Rectangle {
        color: control.down || control.checked ? workspace.appearanceColors.selection
            : control.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.inset
        border.color: control.checked || control.activeIndicator || control.activeFocus
            ? workspace.appearanceColors.accent : workspace.appearanceColors.border
    }
    contentItem: Item {
        implicitWidth: control.showText ? caption.implicitWidth : 18
        implicitHeight: 18
        KindGlyph {
            anchors.centerIn: parent
            width: 18; height: 18
            visible: !control.showText
            kind: control.glyph
            fill: workspace.appearanceColors.inset
            stroke: control.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
        }
        Label {
            id: caption
            anchors.centerIn: parent
            visible: control.showText
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
        }
    }
}
