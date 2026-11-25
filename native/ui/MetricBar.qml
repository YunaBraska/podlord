import QtQuick
import QtQuick.Controls

ProgressBar {
    id: bar
    required property var metric
    objectName: "metric_" + metric.id + "_bar"
    visible: metric.barAvailable === true
    implicitHeight: 8
    from: 0
    to: 1
    value: metric.fraction
    Accessible.name: metric.label + " usage and configured reference"
    background: Rectangle { implicitHeight: 8; color: workspace.appearanceColors.border; radius: 2 }
    contentItem: Item {
        implicitHeight: 8
        Rectangle {
            width: parent.width * bar.visualPosition
            height: parent.height
            radius: 2
            color: workspace.appearanceColors.accent
        }
    }
    Repeater {
        model: bar.metric.markers
        Rectangle {
            required property var modelData
            objectName: "metric_" + bar.metric.id + "_marker_" + modelData.label
            width: 2
            height: bar.height + 4
            y: -2
            x: Math.min(bar.width - width, Math.max(0, modelData.position * bar.width - width / 2))
            color: workspace.appearanceColors.text
            Accessible.role: Accessible.Indicator
            Accessible.name: bar.metric.label + " " + modelData.label + " reference marker"
        }
    }
}
