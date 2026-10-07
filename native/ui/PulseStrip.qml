import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Flow {
    id: strip
    signal detailsRequested()
    spacing: 6
    Layout.preferredWidth: workspace.pulseMetrics.reduce((width, metric) => width + (metric.bar ? 170 : 62), 0)
        + spacing * Math.max(0, workspace.pulseMetrics.length - 1)
        Repeater {
            model: workspace.pulseMetrics
            delegate: Button {
                id: metricCard
                required property var modelData
                objectName: "pulse_" + modelData.id
                width: modelData.bar ? Math.floor(Math.min(170, Math.max(62, (strip.width - 2 * strip.spacing) / 3))) : 62
                height: strip.Window.window && strip.Window.window.width < 900 ? 44 : 36
                leftPadding: 7; rightPadding: 7; topPadding: 3; bottomPadding: 3
                Accessible.name: modelData.label + ": " + modelData.usage
                onClicked: detailsRequested()
                background: Rectangle { color: workspace.appearanceColors.inset; border.color: metricCard.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border }
                contentItem: Column {
                    spacing: 1
                    Label { text: metricCard.modelData.label; font.pixelSize: 9; color: workspace.appearanceColors.muted }
                    Label { objectName: "pulse_" + metricCard.modelData.id + "_usage"; width: parent.width; text: metricCard.modelData.display || metricCard.modelData.usage; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                    ProgressBar { width: parent.width; height: 3; visible: metricCard.modelData.bar; value: metricCard.modelData.fraction; from: 0; to: 1 }
                }
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.usage + "\n" + modelData.description
            }
        }
}
