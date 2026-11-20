import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
                readonly property var shownMetrics: resourceMetrics.filter(metric => metric.available || !!metric.presentReferences)
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
                    Layout.leftMargin: 6
                    Layout.preferredHeight: childrenRect.height
                    spacing: 12
                    Repeater {
                        model: shownMetrics
                        ColumnLayout {
                            required property var modelData
                            width: Math.min(320, Math.max(120, metricRows.width - 18))
                            height: implicitHeight
                            spacing: 2
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
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 24)
                visible: metricRows.count === 0
                text: workspace.uiText["dashboard.empty"]
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
