import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: gauge
    required property var metric
    property bool omitMissing: false
    property bool showMeasurement: true
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        SelectableText { text: gauge.metric.label; font.bold: true }
        SelectableText { objectName: "metric_" + gauge.metric.id + "_usage"; visible: !gauge.omitMissing || gauge.metric.available; Layout.minimumWidth: 0; Layout.fillWidth: true; text: gauge.metric.usage; wrapMode: TextEdit.NoWrap; clip: true; horizontalAlignment: Text.AlignRight; Accessible.name: gauge.metric.label + " observed usage: " + text }
    }
    MetricBar {
        metric: gauge.metric
        Layout.fillWidth: true
        Layout.preferredHeight: 8
    }
    SelectableText { objectName: "metric_" + gauge.metric.id + "_references"; Layout.fillWidth: true; visible: text !== ""; text: gauge.omitMissing ? gauge.metric.presentReferences : gauge.metric.references; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
    SelectableText {
        objectName: "metric_" + gauge.metric.id + "_measurement"
        Layout.fillWidth: true
        visible: gauge.showMeasurement && (!gauge.omitMissing || gauge.metric.available) && text !== ""
        text: gauge.metric.timestamp ? workspace.displayDateTime(gauge.metric.timestamp) : ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: workspace.appearanceColors.muted
    }
}
