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
    function widthFor(metric) {
        const value = valueFont.advanceWidth(String(metric.display || metric.usage))
        const content = 18 + glyphSize + 4 + value
        return Math.ceil(Math.max((metric.bar ? 112 : 68) * workspace.textScale, content))
    }
    readonly property real cardHeight: Math.max(strip.Window.window && strip.Window.window.touchLayout ? 44 : 32, Math.ceil(workspace.textSizes.caption * 1.3) + 10)
    readonly property var availableMetrics: workspace.pulseMetrics.filter(metric =>
        metric.id === "pods" || metric.id === "nodes" ? Number(metric.usage) > 0 : metric.available === true)
    readonly property int visibleCount: {
        let used = 0
        let count = 0
        for (const metric of availableMetrics) {
            const next = widthFor(metric) + (count ? spacing : 0)
            if (used + next > availableWidth) break
            used += next
            count++
        }
        return count
    }
    readonly property var overflowMetrics: availableMetrics.slice(visibleCount)
    spacing: 6
    readonly property real fullWidth: availableMetrics.reduce((total, metric) => total + widthFor(metric), 0)
        + spacing * Math.max(0, availableMetrics.length - 1)
    Layout.preferredWidth: availableMetrics.slice(0, visibleCount).reduce((total, metric) => total + widthFor(metric), 0)
        + spacing * Math.max(0, visibleCount - 1)
    Layout.preferredHeight: cardHeight
        Repeater {
            model: strip.availableMetrics.length
            delegate: FlatButton {
                id: metricCard
                required property int index
                readonly property var modelData: strip.availableMetrics[index]
                objectName: "pulse_" + modelData.id
                visible: index < strip.visibleCount
                width: strip.widthFor(modelData)
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
                        SelectableText {
                            objectName: "pulse_" + metricCard.modelData.id + "_usage"
                            Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
                            text: metricCard.modelData.display || metricCard.modelData.usage
                            font: valueFont.font; wrapMode: TextEdit.NoWrap; clip: true
                            TapHandler { gesturePolicy: TapHandler.DragThreshold; onTapped: metricCard.clicked() }
                        }
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
