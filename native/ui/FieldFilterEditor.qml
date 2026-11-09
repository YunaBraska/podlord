import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: editor
    required property var controller
    objectName: "fieldFiltersInline"
    signal dismissed()
    readonly property bool quantityField: ["cpu", "memory", "storage"].indexOf(controller.filterPickerField) >= 0
    readonly property bool durationField: controller.filterPickerField === "createdAt"
    padding: 4
    Accessible.role: Accessible.Grouping
    Accessible.name: "Field filter"
    Keys.onEscapePressed: dismissed()
    Component.onCompleted: expressionInput.forceActiveFocus()
    contentItem: ColumnLayout {
        spacing: 6
        TextField {
            id: expressionInput
            objectName: "fieldFilterExpression"
            Layout.fillWidth: true
            text: editor.controller.resourceFieldFilters[editor.controller.filterPickerField] || ""
            placeholderText: editor.quantityField ? (editor.controller.filterPickerField === "cpu" ? ">=500m <1, text, /regex/" : ">=64Mi <1Gi, text, /regex/")
                : editor.durationField ? '>=5m <1h, "5m", /regex/'
                : editor.controller.filterPickerField === "ready" ? '"1/2", ~1/, /regex/'
                : editor.controller.filterPickerField === "restarts" ? '>=3 =0 (alternatives), /regex/'
                : 'Alternatives, "exact", ~prefix, suffix~, /regex/'
            Accessible.name: "Field filter expression"
            Accessible.description: editor.quantityField ? "Numeric filters use actual measurements. CPU uses cores, c, milliCPU or millicores. Memory/storage units are case-insensitive: m or MB means megabytes, mi or MiB means mebibytes. Ranges all match; exact numeric alternatives use OR. Without numeric terms, exact text, prefix, suffix and regex match the displayed value, including unavailable, stale or incomplete markers." : "Edit the complete expression, including custom values. Filtering does not start requests."
            onTextEdited: editor.controller.filterField(editor.controller.filterPickerField, text)
        }
        Label {
            Layout.fillWidth: true
            text: editor.controller.filterError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: editor.controller.appearanceColors.danger
            visible: text !== ""
            Accessible.name: text
        }
        TextField {
            id: optionSearch
            objectName: "fieldFilterOptionSearch"
            Layout.fillWidth: true
            placeholderText: editor.controller.uiText["filters.findValues"]
            Accessible.name: editor.controller.uiText["filters.findValues"]
        }
        ListView {
            id: values
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180, Math.max(44, contentHeight))
            clip: true
            model: editor.controller.filterPickerValues
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckDelegate {
                id: valueControl
                required property string modelData
                required property int index
                readonly property string displayValue: modelData === "" && editor.controller.filterPickerField === "namespace"
                    ? editor.controller.uiText["filters.clusterWide"] : modelData
                objectName: "fieldFilterValue_" + index
                width: values.width
                height: visible ? implicitHeight : 0
                visible: displayValue.toLowerCase().indexOf(optionSearch.text.toLowerCase()) >= 0
                checked: {
                    const filters = editor.controller.resourceFieldFilters
                    return editor.controller.filterValueSelected(modelData)
                }
                Accessible.name: displayValue
                indicator: Rectangle {
                    x: valueControl.mirrored ? valueControl.width - width - valueControl.rightPadding : valueControl.leftPadding
                    y: (valueControl.height - height) / 2
                    width: 18; height: 18
                    color: valueControl.checked ? editor.controller.appearanceColors.accent : editor.controller.appearanceColors.inset
                    border.color: valueControl.activeFocus ? editor.controller.appearanceColors.accent : editor.controller.appearanceColors.border
                    Label { anchors.centerIn: parent; text: valueControl.checked ? "✓" : ""; color: editor.controller.appearanceColors.inset }
                }
                contentItem: Label {
                    text: valueControl.displayValue
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: !valueControl.mirrored && valueControl.indicator ? valueControl.indicator.width + valueControl.spacing : 0
                    rightPadding: valueControl.mirrored && valueControl.indicator ? valueControl.indicator.width + valueControl.spacing : 0
                }
                onClicked: editor.controller.selectFilterValue(modelData, checked)
                ToolTip {
                    visible: valueControl.hovered || valueControl.activeFocus
                    contentItem: Label {
                        text: valueControl.displayValue
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: copyMenu.popup(valueControl, 0, valueControl.height)
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                        copyMenu.popup(valueControl, 0, valueControl.height)
                        event.accepted = true
                    }
                }
                Menu {
                    id: copyMenu
                    MenuItem { objectName: "copyFieldFilterValue_" + valueControl.index; text: editor.controller.uiText["copy.value"]; onTriggered: editor.controller.copyFilterValue(valueControl.modelData) }
                }
            }
        }
    }
}
