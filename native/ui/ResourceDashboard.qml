import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    padding: 12
    background: Rectangle { color: workspace.appearanceColors.panel; border.color: workspace.appearanceColors.border }
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
            spacing: 6
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property int index
                required property string resourceName
                required property string resourceKind
                required property string resourceNamespace
                required property string resourcePath
                required property var resourceMetrics
                readonly property var shownMetrics: resourceMetrics.filter(metric => metric.available || !!metric.presentReferences)
                width: metricRows.width
                height: card.implicitHeight + 12
                color: workspace.appearanceColors.inset
                border.color: workspace.appearanceColors.border
                ColumnLayout {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 5
                    FlatButton {
                        objectName: "dashboardResource_" + index
                        Layout.fillWidth: true
                        text: resourceKind + "  " + (resourceNamespace ? resourceNamespace + "/" : "") + resourceName
                        Accessible.name: "Inspect " + text
                        onClicked: workspace.inspectPath(resourcePath)
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            color: workspace.appearanceColors.text
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 6
                        Repeater {
                            model: shownMetrics
                            Rectangle {
                                required property var modelData
                                width: Math.min(320, Math.max(120, metricRows.width - 12))
                                height: modelData.presentReferences ? 82 : 44
                                color: workspace.appearanceColors.panel
                                border.color: workspace.appearanceColors.border
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    spacing: 0
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SelectableText { text: modelData.label; color: workspace.appearanceColors.muted }
                                        Item { Layout.fillWidth: true }
                                        SelectableText { text: modelData.usage; font.bold: true }
                                    }
                                    SelectableText {
                                        Layout.fillWidth: true
                                        visible: modelData.presentReferences !== ""
                                        text: modelData.presentReferences
                                        color: workspace.appearanceColors.muted
                                        wrapMode: TextEdit.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
            SelectableText {
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 24)
                visible: metricRows.count === 0
                text: workspace.uiText["dashboard.empty"]
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
