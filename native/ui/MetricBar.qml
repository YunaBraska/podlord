import QtQuick
import QtQuick.Controls

ProgressBar {
    id: bar
    required property var metric
    property bool showValues: false
    objectName: "metric_" + metric.id + "_bar"
    visible: metric.barAvailable === true
    implicitHeight: showValues ? 10 + referenceValues.implicitHeight : 8
    from: 0
    to: 1
    value: metric.fraction
    Accessible.name: metric.label + " usage and configured reference: " + metric.markerLabels
    background: Rectangle { y: 0; height: 8; color: workspace.appearanceColors.border; radius: 2 }
    contentItem: Item {
        implicitHeight: 8
        Rectangle {
            width: parent.width * bar.visualPosition
            height: 8
            radius: 2
            color: workspace.appearanceColors.accent
        }
    }
    Repeater {
        model: bar.metric.markers
        Rectangle {
            required property var modelData
            objectName: "metric_" + bar.metric.id + "_marker_" + modelData.label
            width: 1
            height: 6
            y: 1
            x: Math.min(bar.width - width, Math.max(0, modelData.position * bar.width - width / 2))
            color: workspace.appearanceColors.text
            Accessible.role: Accessible.Indicator
            Accessible.name: bar.metric.label + " " + modelData.label + " " + modelData.value
        }
    }
    SelectableText {
        id: referenceValues
        objectName: "metric_" + bar.metric.id + "_marker_values"
        visible: bar.showValues && text !== ""
        y: 10
        width: bar.width
        text: bar.metric.markerLabels || ""
        color: workspace.appearanceColors.text
        font.pixelSize: workspace.textSizes.cell
    }
}
