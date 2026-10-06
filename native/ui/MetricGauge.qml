import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: gauge
    required property var metric
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Label { text: gauge.metric.label; font.bold: true }
        Item { Layout.fillWidth: true }
        Label { objectName: "metric_" + gauge.metric.id + "_usage"; Layout.maximumWidth: gauge.width * 0.65; text: gauge.metric.usage; textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignRight; Accessible.name: gauge.metric.label + " observed usage: " + text }
    }
    ProgressBar {
        id: bar
        objectName: "metric_" + gauge.metric.id + "_bar"
        Layout.fillWidth: true
        Layout.preferredHeight: 8
        from: 0; to: 1; value: gauge.metric.fraction
        Accessible.name: gauge.metric.label + " usage relative to the largest displayed reference"
        background: Rectangle { implicitHeight: 8; color: workspace.appearanceColors.inset; radius: 2 }
        contentItem: Item {
            implicitHeight: 8
            Rectangle { width: parent.width * bar.visualPosition; height: parent.height; radius: 2; color: workspace.appearanceColors.accent }
        }
        Repeater {
            model: gauge.metric.markers
            Rectangle {
                required property var modelData
                objectName: "metric_" + gauge.metric.id + "_marker_" + modelData.label
                width: 2; height: bar.height+4; y: -2
                x: Math.min(bar.width-width, Math.max(0, modelData.position*bar.width-width/2))
                color: workspace.appearanceColors.text
                Accessible.role: Accessible.Indicator
                Accessible.name: gauge.metric.label + " " + modelData.label + " reference marker"
            }
        }
    }
    Label { objectName: "metric_" + gauge.metric.id + "_references"; Layout.fillWidth: true; text: gauge.metric.references; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted; Accessible.name: text }
    Label { objectName: "metric_" + gauge.metric.id + "_measurement"; Layout.fillWidth: true; text: gauge.metric.source + (gauge.metric.timestamp ? "\nMeasured " + gauge.metric.timestamp + " / window " + gauge.metric.window : "\nNo observed measurement is available."); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted; Accessible.name: text }
}
