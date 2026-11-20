import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Podlord.Graphics 1.0

Pane {
    padding: 12
    background: Rectangle { color: workspace.appearanceColors.panel }
    contentItem: ColumnLayout {
        spacing: 8
        SelectableText { text: workspace.uiText["dashboard.title"]; font.bold: true }
        SelectableText {
            objectName: "dashboardSummary"
            Layout.fillWidth: true
            text: workspace.dashboardSummary
            Accessible.name: text
        }
        ListView {
            id: metricRows
            objectName: "resourceDashboard"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: workspace.dashboardTable
            clip: true
            reuseItems: true
            cacheBuffer: 0
            spacing: 0
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                required property int index
                required property string resourceName
                required property string resourceKind
                required property string resourceNamespace
                required property string resourcePath
                required property var resourceMetrics
                required property color resourceKindColor
                required property int resourceHealth
                readonly property var shownMetrics: resourceMetrics.filter(metric => metric.available)
                width: metricRows.width
                height: implicitHeight
                spacing: 4
                FlatButton {
                    objectName: "dashboardResource_" + index
                    Layout.fillWidth: true
                    text: resourceKind + "  " + (resourceNamespace ? resourceNamespace + "/" : "") + resourceName
                    Accessible.name: "Inspect " + text
                    onClicked: workspace.inspectPath(resourcePath)
                    background: Rectangle {
                        color: parent.down || workspace.inspectorPath === resourcePath
                            ? workspace.appearanceColors.selection
                            : parent.hovered ? workspace.appearanceColors.raised : "transparent"
                        border.width: parent.activeFocus ? 1 : 0
                        border.color: workspace.appearanceColors.accent
                    }
                    contentItem: RowLayout {
                        spacing: 6
                        KindGlyph {
                            objectName: "dashboardKindGlyph"
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            kind: resourceKind
                            fill: workspace.appearanceColors.inset
                            stroke: resourceKindColor
                            Accessible.ignored: true
                        }
                        Text { text: resourceKind; color: resourceKindColor; font: parent.parent.font; Accessible.ignored: true }
                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: (resourceNamespace ? resourceNamespace + "/" : "") + resourceName
                            font: parent.parent.font
                            color: workspace.appearanceColors.text
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        Rectangle {
                            visible: resourceHealth > 0
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 4
                            color: resourceHealth > 1 ? workspace.appearanceColors.danger : workspace.appearanceColors.warning
                            Accessible.ignored: true
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    spacing: 5
                    Repeater {
                        model: shownMetrics
                        RowLayout {
                            required property var modelData
                            objectName: "dashboardMetric_" + modelData.id
                            Layout.fillWidth: true
                            SelectableText { text: modelData.label; color: workspace.appearanceColors.muted }
                            SelectableText {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: modelData.usage
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                Accessible.description: modelData.presentReferences
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Layout.bottomMargin: 8
                    implicitHeight: 1
                    color: workspace.appearanceColors.border
                    visible: index < metricRows.count - 1
                }
            }
            SelectableText {
                objectName: "dashboardEmpty"
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 24)
                visible: metricRows.count === 0
                text: workspace.uiText["dashboard.empty"]
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
