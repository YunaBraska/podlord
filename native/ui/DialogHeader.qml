import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    required property var dialog
    property string closeObjectName: (dialog.objectName || "dialog") + "Close"
    property bool closeEnabled: true
    padding: 8
    background: null
    contentItem: RowLayout {
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: parent.parent.dialog.title
            textFormat: Text.PlainText
            font.bold: true
            elide: Text.ElideRight
        }
        IconButton {
            objectName: parent.parent.closeObjectName
            glyph: "Close"
            text: workspace.uiText["action.close"]
            enabled: parent.parent.closeEnabled
            onClicked: parent.parent.dialog.reject()
        }
    }
}
