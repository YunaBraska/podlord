import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    implicitWidth: Math.max(implicitIndicatorWidth + leftPadding + rightPadding,
        Math.min(implicitContentWidth + leftPadding + rightPadding, parent ? parent.width : Number.POSITIVE_INFINITY))
    contentItem: Label {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.enabled ? workspace.appearanceColors.text : workspace.appearanceColors.muted
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
    }
}
