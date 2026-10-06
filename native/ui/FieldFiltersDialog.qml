import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var controller
    objectName: "fieldFiltersPopup"
    property Item anchorItem: null
    onAnchorItemChanged: if (visible && anchorItem === null) close()
    readonly property bool quantityField: ["cpu", "memory", "storage"].indexOf(controller.filterPickerField) >= 0
    readonly property bool durationField: controller.filterPickerField === "createdAt"
    parent: Overlay.overlay
    title: anchorItem ? column.currentText : "Resource field filters"
    modal: anchorItem === null
    width: Math.min(anchorItem ? 360 : 640, parent ? parent.width - 24 : 640)
    height: Math.min(anchorItem ? 500 : 600, parent ? parent.height - 24 : 600)
    x: anchorItem ? Math.max(12, Math.min(parent.width - width - 12, anchorItem.mapToItem(parent, 0, 0).x)) : parent ? Math.round((parent.width - width) / 2) : 0
    y: anchorItem ? Math.max(12, Math.min(parent.height - height - 12, anchorItem.mapToItem(parent, 0, anchorItem.height).y)) : parent ? Math.round((parent.height - height) / 2) : 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: expressionInput.forceActiveFocus()
    standardButtons: Dialog.Close
    function openFilters() {
        openField("name")
    }
    function openField(field, anchor) {
        if (controller.prepareFilterPicker(field)) {
            anchorItem = anchor || null
            column.currentIndex = controller.filterFields.findIndex(function(value) { return value.id === field })
            optionSearch.clear()
            open()
        }
    }
    Connections {
        target: dialog.controller
        function onFilterPickerChanged() {
            if (dialog.controller.filterPickerField === "") dialog.close()
        }
    }
    Connections {
        target: dialog.anchorItem
        function onVisibleChanged() { if (dialog.anchorItem && !dialog.anchorItem.visible) dialog.close() }
        function onObjectNameChanged() { if (dialog.anchorItem && dialog.anchorItem.objectName !== "sidebarField_" + dialog.controller.filterPickerField) dialog.close() }
    }
    Connections {
        target: dialog.anchorItem ? dialog.anchorItem.parent : null
        function onYChanged() { dialog.close() }
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            visible: dialog.anchorItem === null
            text: dialog.quantityField
                ? "Numeric filters compare actual measurements, never missing values or limits. Ranges use AND; equal values use OR. Without numeric terms, text matches the displayed cache value. Different fields and the main search must all match."
                : dialog.durationField ? "Age uses the cached creation timestamp in whole seconds. Units: ms, s, m, h, d, w; compound durations: 1h30m. Ranges use AND; exact numeric values use OR. Missing, invalid or future timestamps never match numeric comparisons. Quoted text and patterns match the displayed age."
                : "Values within a field are alternatives. Different fields and the main search must all match."
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        ComboBox {
            id: column
            visible: dialog.anchorItem === null
            objectName: "fieldFilterColumn"
            Layout.fillWidth: true
            model: dialog.controller.filterFields
            textRole: "name"
            valueRole: "id"
            Accessible.name: "Resource filter field"
            onActivated: {
                optionSearch.clear()
                dialog.controller.prepareFilterPicker(currentValue)
            }
        }
        TextField {
            id: expressionInput
            objectName: "fieldFilterExpression"
            Layout.fillWidth: true
            text: dialog.controller.resourceFieldFilters[dialog.controller.filterPickerField] || ""
            placeholderText: dialog.quantityField ? (dialog.controller.filterPickerField === "cpu" ? ">=500m <1, text, /regex/" : ">=64Mi <1Gi, text, /regex/")
                : dialog.durationField ? '>=5m <1h, "5m", /regex/'
                : dialog.controller.filterPickerField === "ready" ? '"1/2", ~1/, /regex/'
                : dialog.controller.filterPickerField === "restarts" ? '>=3 =0 (alternatives), /regex/'
                : 'Alternatives, "exact", ~prefix, suffix~, /regex/'
            Accessible.name: "Field filter expression"
            Accessible.description: dialog.quantityField ? "Numeric filters use actual cached measurements. CPU uses cores, c, milliCPU or millicores. Memory/storage units are case-insensitive: m or MB means megabytes, mi or MiB means mebibytes. Ranges all match; exact numeric alternatives use OR. Without numeric terms, exact text, prefix, suffix and regex match the displayed value, including unavailable, stale or incomplete markers." : "Edit the complete expression, including custom values. Filtering uses cached data only."
            onTextEdited: dialog.controller.filterField(dialog.controller.filterPickerField, text)
        }
        Label {
            Layout.fillWidth: true
            text: dialog.controller.filterError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: dialog.controller.appearanceColors.danger
            visible: text !== ""
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: optionSearch
                objectName: "fieldFilterOptionSearch"
                Layout.fillWidth: true
                placeholderText: "Find cached values"
                Accessible.name: "Find cached filter values"
            }
            Button {
                objectName: "refreshFieldFilterValues"
                text: "Reload values"
                Accessible.description: "Refresh this option snapshot from the session cache, without a network request."
                onClicked: dialog.controller.prepareFilterPicker(dialog.controller.filterPickerField)
            }
        }
        ListView {
            id: values
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: dialog.controller.filterPickerValues
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckDelegate {
                id: valueControl
                required property string modelData
                required property int index
                objectName: "fieldFilterValue_" + index
                width: values.width
                height: visible ? implicitHeight : 0
                visible: modelData.toLowerCase().indexOf(optionSearch.text.toLowerCase()) >= 0
                checked: {
                    const filters = dialog.controller.resourceFieldFilters
                    return dialog.controller.filterValueSelected(modelData)
                }
                Accessible.name: modelData
                indicator: Rectangle {
                    x: valueControl.leftPadding
                    y: (valueControl.height - height) / 2
                    width: 18; height: 18
                    color: valueControl.checked ? dialog.controller.appearanceColors.accent : dialog.controller.appearanceColors.inset
                    border.color: valueControl.activeFocus ? dialog.controller.appearanceColors.accent : dialog.controller.appearanceColors.border
                    Label { anchors.centerIn: parent; text: valueControl.checked ? "✓" : ""; color: dialog.controller.appearanceColors.inset }
                }
                contentItem: Label {
                    text: valueControl.modelData
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: valueControl.indicator ? valueControl.indicator.width + valueControl.spacing : 0
                }
                onClicked: dialog.controller.selectFilterValue(modelData, checked)
                ToolTip {
                    visible: valueControl.hovered || valueControl.activeFocus
                    contentItem: Label {
                        text: valueControl.modelData
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: copyMenu.popup()
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                        copyMenu.popup()
                        event.accepted = true
                    }
                }
                Menu {
                    id: copyMenu
                    MenuItem { objectName: "copyFieldFilterValue_" + valueControl.index; text: "Copy value"; onTriggered: dialog.controller.copyFilterValue(valueControl.modelData) }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: values.count === 0 ? "No cached values. You can still enter an expression." : "The option list stays stable until you reload it."
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: dialog.controller.appearanceColors.muted
        }
        Button { objectName: "clearFieldFilters"; text: "Reset all resource filters"; onClicked: dialog.controller.resetResourceFilters() }
    }
}
