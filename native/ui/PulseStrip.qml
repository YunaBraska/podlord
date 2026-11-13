import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Row {
    id: strip
    signal detailsRequested()
    property real availableWidth: implicitWidth
    FontMetrics {
        id: valueFont
        font.family: workspace.monospaceFamily
        font.pixelSize: workspace.textSizes.caption
        font.weight: Font.Normal
    }
    readonly property int glyphSize: Math.max(16, Math.ceil(workspace.textSizes.caption))
    readonly property int cardWidth: {
        const iconWidth = Math.max(16, valueFont.font.pixelSize)
        let widest = 0
        for (const metric of availableMetrics)
            widest = Math.max(widest, valueFont.advanceWidth(String(metric.display || metric.usage)))
        return Math.ceil(Math.max(112 * workspace.textScale, 14 + iconWidth + 4 + widest + 2))
    }
    readonly property real cardHeight: Math.max(strip.Window.window && strip.Window.window.touchLayout ? 44 : 32, Math.ceil(workspace.textSizes.caption * 1.3) + 10)
    readonly property var availableMetrics: workspace.pulseMetrics.filter(metric => metric.bar ? metric.available === true : Number(metric.usage) > 0)
    readonly property int visibleCount: Math.min(availableMetrics.length, Math.max(0, Math.floor((availableWidth + spacing) / (cardWidth + spacing))))
    readonly property var overflowMetrics: availableMetrics.slice(visibleCount)
    spacing: 6
    Layout.preferredWidth: visibleCount * cardWidth + spacing * Math.max(0, visibleCount - 1)
    Layout.preferredHeight: cardHeight
        Repeater {
            model: strip.availableMetrics.length
            delegate: Button {
                id: metricCard
                required property int index
                readonly property var modelData: strip.availableMetrics[index]
                objectName: "pulse_" + modelData.id
                visible: index < strip.visibleCount
                width: strip.cardWidth
                height: strip.cardHeight
                leftPadding: 7; rightPadding: 7; topPadding: 3; bottomPadding: 3
                Accessible.name: modelData.label + ": " + modelData.usage
                onClicked: detailsRequested()
                background: Rectangle { color: workspace.appearanceColors.inset; border.color: metricCard.activeFocus ? workspace.appearanceColors.accent : workspace.appearanceColors.border }
                contentItem: Item {
                    RowLayout {
                        anchors.fill: parent; anchors.bottomMargin: metricCard.modelData.bar ? 4 : 0
                        spacing: 4
                        KindGlyph {
                            objectName: "pulse_" + metricCard.modelData.id + "_glyph"
                            Layout.preferredWidth: strip.glyphSize; Layout.preferredHeight: strip.glyphSize
                            kind: metricCard.modelData.id === "cpu" ? "Cpu" : metricCard.modelData.id === "memory" ? "Memory"
                                : metricCard.modelData.id === "storage" ? "Storage" : metricCard.modelData.id === "pods" ? "Pod" : "Node"
                            fill: workspace.appearanceColors.inset; stroke: workspace.appearanceColors.text
                            Accessible.ignored: true
                        }
                        Label { objectName: "pulse_" + metricCard.modelData.id + "_usage"; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: metricCard.modelData.display || metricCard.modelData.usage; font: valueFont.font; maximumLineCount: 1; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    ProgressBar {
                        objectName: "pulse_" + metricCard.modelData.id + "_bar"
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 3
                        id: usageBar
                        visible: metricCard.modelData.bar
                        value: metricCard.modelData.fraction; from: 0; to: 1
                        background: Rectangle { color: workspace.appearanceColors.border }
                        contentItem: Item { Rectangle { height: parent.height; width: parent.width * usageBar.visualPosition; color: workspace.appearanceColors.accent } }
                    }
                }
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.description
            }
        }
}
