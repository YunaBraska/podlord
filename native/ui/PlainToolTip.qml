import QtQuick
import QtQuick.Controls

ToolTip {
    id: tip
    property Item target: null
    parent: target
    text: target ? target.tipText : ""
    visible: !!target && target.visible && target.wantsTip
    width: Math.min(560, implicitWidth)
    contentItem: Label {
        objectName: "plainTipText"
        text: tip.text
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font: tip.font
        color: tip.palette.toolTipText
        Accessible.name: text
    }
}
