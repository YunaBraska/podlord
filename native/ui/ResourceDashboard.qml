import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    padding: 12
    background: Rectangle { color: workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border }
    contentItem: ColumnLayout {
        Label { text: "Resource health and usage"; font.bold: true }
        Label { objectName: "dashboardSummary"; Layout.fillWidth: true; text: workspace.dashboardSummary; textFormat: Text.PlainText; wrapMode: Text.Wrap; Accessible.name: text }
        Label { Layout.fillWidth: true; text: "The same filtered session cache as Resources and Radar. Usage is never inferred from configured requests, limits or provisioned storage."; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        ListView {
            id: metricRows
            objectName: "resourceDashboard"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: workspace.dashboardTable
            clip: true; reuseItems: true; cacheBuffer: 0
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                required property int index
                required property string resourceName
                required property string resourceKind
                required property string resourceNamespace
                required property string resourcePath
                required property var resourceMetrics
                width: metricRows.width
                spacing: 8
                Button { objectName: "dashboardResource_" + index; Layout.fillWidth: true; text: resourceKind + " / " + resourceName + " / " + (resourceNamespace || "Cluster-scoped"); Accessible.name: "Inspect " + text; onClicked: workspace.inspectPath(resourcePath) }
                Repeater { model: resourceMetrics; MetricGauge { required property var modelData; Layout.fillWidth: true; metric: modelData } }
                Rectangle { Layout.fillWidth: true; height: 1; color: workspace.appearanceColors.border }
            }
            Label { anchors.centerIn: parent; width: Math.min(360, parent.width-24); visible: metricRows.count===0; text: "No Pods, Nodes or volumes in this filtered cache. Clear filters or check synchronization."; wrapMode: Text.Wrap; textFormat: Text.PlainText; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
