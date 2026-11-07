import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
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
    ToolTip.visible: !showText && (hovered || visualFocus)
    ToolTip.text: text
    background: Rectangle {
        color: control.down || control.checked ? workspace.appearanceColors.selection
            : control.hovered ? workspace.appearanceColors.raised : workspace.appearanceColors.inset
        border.color: control.checked || control.activeIndicator || control.activeFocus
            ? workspace.appearanceColors.accent : workspace.appearanceColors.border
    }
    contentItem: RowLayout {
        spacing: control.glyph !== "" && control.showText ? 6 : 0
        KindGlyph {
            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: 18; Layout.preferredHeight: 18
            visible: control.glyph !== ""
            kind: control.glyph
            fill: workspace.appearanceColors.inset
            stroke: control.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
        }
        Label {
            visible: control.showText
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
        }
    }
}
