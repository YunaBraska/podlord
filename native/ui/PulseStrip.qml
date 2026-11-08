import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Row {
    id: strip
    signal detailsRequested()
    property real availableWidth: implicitWidth
    readonly property int cardWidth: 112
    readonly property var availableMetrics: workspace.pulseMetrics.filter(metric => metric.bar ? metric.available === true : Number(metric.usage) > 0)
    readonly property int visibleCount: Math.min(availableMetrics.length, Math.max(0, Math.floor((availableWidth + spacing) / (cardWidth + spacing))))
    readonly property var overflowMetrics: availableMetrics.slice(visibleCount)
    spacing: 6
    Layout.preferredWidth: visibleCount * cardWidth + spacing * Math.max(0, visibleCount - 1)
    Layout.preferredHeight: strip.Window.window && strip.Window.window.touchLayout ? 44 : 32
        Repeater {
            model: strip.availableMetrics.length
            delegate: Button {
                id: metricCard
                required property int index
                readonly property var modelData: strip.availableMetrics[index]
                objectName: "pulse_" + modelData.id
                visible: index < strip.visibleCount
                width: strip.cardWidth
                height: strip.Window.window && strip.Window.window.touchLayout ? 44 : 32
                leftPadding: 7; rightPadding: 7; topPadding: 3; bottomPadding: 3
                Accessible.name: modelData.label + ": " + modelData.usage
                onClicked: detailsRequested()
                background: Rectangle { color: workspace.appearanceColors.inset; border.color: metricCard.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border }
                contentItem: Item {
                    RowLayout {
                        anchors.fill: parent; anchors.bottomMargin: metricCard.modelData.bar ? 4 : 0
                        spacing: 4
                        KindGlyph {
                            visible: !metricCard.modelData.bar
                            Layout.preferredWidth: 16; Layout.preferredHeight: 16
                            kind: metricCard.modelData.id === "pods" ? "Pod" : "Node"
                            fill: workspace.appearanceColors.inset; stroke: workspace.appearanceColors.text
                        }
                        Label { visible: metricCard.modelData.bar; text: metricCard.modelData.id === "memory" ? "MEM" : metricCard.modelData.id === "storage" ? "DISK" : "CPU"; font.pixelSize: 10; color: workspace.appearanceColors.muted }
                        Label { objectName: "pulse_" + metricCard.modelData.id + "_usage"; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: metricCard.modelData.display || metricCard.modelData.usage; font.pixelSize: 11; maximumLineCount: 1; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    ProgressBar {
                        objectName: "pulse_" + metricCard.modelData.id + "_bar"
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 3
                        visible: metricCard.modelData.bar
                        value: metricCard.modelData.fraction; from: 0; to: 1
                        background: Rectangle { color: workspace.appearanceColors.border }
                    }
                }
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.usage + "\n" + modelData.description
            }
        }
}
