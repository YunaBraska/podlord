import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs as Dialogs

Pane {
    id: view
    padding: 0
    Layout.minimumWidth: 0
    property var draft: null
    property bool savedSelection: false
    property bool selectNew: false
    property bool editorClosed: false
    property string savedDraft: ""
    property var pendingEditorAction: null
    readonly property bool touchLayout: Qt.platform.os === "ios" || Qt.platform.os === "android" || view.width < 900 || (Window.window && Window.window.compactLandscape === true)
    property string selectedSoundId: "none"
    readonly property var fieldOptions: workspace.alerts.fields.map(field => ({id: field, label: workspace.uiText["alert.field." + field]}))
    readonly property var holdOptions: [
        {id: "no-match", label: workspace.uiText["alert.hold.match"]},
        {id: "duration", label: workspace.uiText["alert.hold.duration"]},
        {id: "new-in-view", label: workspace.uiText["alert.hold.new"]},
        {id: "once", label: workspace.uiText["alert.hold.once"]}
    ]
    readonly property var animationOptions: ["none", "blink", "pulse", "sweep", "outline"].map(id => ({id: id, label: workspace.uiText["alert.animation." + id]}))
    readonly property var soundCatalog: workspace.alerts.sounds
    readonly property var soundOptions: soundCatalog.filter(sound => sound.id !== "none")
    property bool ruleEnabled: false
    ListModel { id: criteria }
    function selectRule(identity) {
        const rule = workspace.alerts.rules.find(candidate => candidate.id === identity)
        if (!rule) return false
        if (!draft || draft.id !== rule.id) return changeEditor(() => edit(rule))
        return true
    }
    function toggleRule(identity) {
        const rule = workspace.alerts.rules.find(candidate => candidate.id === identity)
        if (!rule || workspace.alerts.busy) return false
        const toggled = Object.assign({}, rule, {enabled: !rule.enabled})
        if (!workspace.alerts.saveRule(toggled)) return false
        if (draft && draft.id === identity) {
            view.ruleEnabled = toggled.enabled
            const baseline = JSON.parse(savedDraft)
            baseline.enabled = toggled.enabled
            baseline.draft = draft.draft && !toggled.enabled
            savedDraft = JSON.stringify(baseline)
        }
        return true
    }
    function edit(rule) {
        editorClosed = false
        draft = rule
        ruleGrid.selectIdentity(rule.id, 2)
        criteria.clear()
        for (let g = 0; g < rule.groups.length; ++g)
            for (const c of rule.groups[g]) criteria.append({group: g, field: c.field, expression: c.expression})
        name.text = rule.name; description.text = rule.description; description.cursorPosition = 0; view.ruleEnabled = rule.enabled
        alertColorInput.text = rule.color; colorMode.currentIndex = holdOptions.findIndex(option => option.id === rule.colorMode); colorSeconds.value = rule.colorSeconds
        animation.currentIndex = animationOptions.findIndex(option => option.id === rule.animation); animationMode.currentIndex = holdOptions.findIndex(option => option.id === rule.animationMode); animationSeconds.value = rule.animationSeconds
        zoomEnabled.checked = rule.zoom > 0; zoom.value = Math.max(1, rule.zoom); selectedSoundId = rule.sound; soundMinimum.value = rule.soundMinimumMatches
        pageScroll.contentItem.contentY = 0
        savedDraft = JSON.stringify(ruleDraft())
    }
    function add() {
        return changeEditor(() => {
        edit({id: "", name: "New alert", description: "", enabled: true, builtIn: false, draft: true,
            groups: [[{field: "name", expression: ""}]], color: "#e3aa46", colorMode: "no-match", colorSeconds: 5,
            animation: "none", animationMode: "no-match", animationSeconds: 5, zoom: 0, sound: "none", soundMinimumMatches: 1})
        name.forceActiveFocus(Qt.TabFocusReason)
        name.selectAll()
        })
    }
    function addCriterion(group) {
        let at = 0
        while (at < criteria.count && criteria.get(at).group <= group) ++at
        criteria.insert(at, {group: group, field: "name", expression: ""})
    }
    function removeGroup(group) {
        for (let i = criteria.count - 1; i >= 0; --i) {
            const current = criteria.get(i).group
            if (current === group) criteria.remove(i)
            else if (current > group) criteria.setProperty(i, "group", current - 1)
        }
    }
    function groupSize(group) {
        let count = 0
        for (let i = 0; i < criteria.count; ++i) if (criteria.get(i).group === group) ++count
        return count
    }
    function ruleDraft() {
        const groups = []
        for (let i = 0; i < criteria.count; ++i) {
            const c = criteria.get(i)
            if (!groups[c.group]) groups[c.group] = []
            groups[c.group].push({field: c.field, expression: c.expression})
        }
        const savingDraft = draft.draft && (draft.id === "" || !view.ruleEnabled)
        return {id: draft.id, name: name.text, description: description.text, enabled: view.ruleEnabled, builtIn: draft.builtIn, draft: savingDraft,
            groups: groups.filter(g => g !== undefined), color: alertColorInput.text, colorMode: colorMode.currentValue, colorSeconds: colorSeconds.value,
            animation: animation.currentValue, animationMode: animationMode.currentValue, animationSeconds: animationSeconds.value,
            zoom: zoomEnabled.checked ? zoom.value : 0, sound: selectedSoundId, soundMinimumMatches: soundMinimum.value}
    }
    function save() {
        savedSelection = true; selectNew = draft.id === ""
        if (!workspace.alerts.saveRule(ruleDraft())) savedSelection = false
    }
    function dismissEditor() {
        draft = null
        editorClosed = true
        savedSelection = false
        selectNew = false
    }
    function changeEditor(action) {
        if (draft && JSON.stringify(ruleDraft()) !== savedDraft) {
            pendingEditorAction = action
            discardAlertChanges.open()
            return false
        }
        action()
        return true
    }
    function closeEditor() { return changeEditor(() => dismissEditor()) }
    function changeSelection(action) {
        savedSelection = true; selectNew = false
        const accepted = action === "delete" ? workspace.alerts.deleteRule(draft.id) : workspace.alerts.reload()
        if (!accepted) savedSelection = false
    }
    Component.onCompleted: {
        if (!workspace.alerts.busy && workspace.alerts.rules.length) edit(workspace.alerts.rules[0])
    }
    Connections {
        target: workspace.alerts
        function onRulesChanged() {
            if (workspace.alerts.busy) return
            if (workspace.alerts.error !== "") { view.savedSelection = false; return }
            const rules = workspace.alerts.rules
            if (view.editorClosed || (!view.savedSelection && view.draft !== null)) return
            const selected = !view.selectNew && view.draft && rules.find(r => r.id === view.draft.id)
            view.savedSelection = false
            if (selected) view.edit(selected)
            else if (rules.length) view.edit(view.selectNew ? rules[rules.length - 1] : rules[0])
            else view.draft = null
            view.selectNew = false
        }
    }
    ModalDialog {
        id: discardAlertChanges
        objectName: "discardAlertChanges"
        parent: Overlay.overlay
        title: workspace.uiText["alert.unsavedTitle"]
        modal: true
        width: Math.min(440, parent.width - 24)
        onClosed: { view.pendingEditorAction = null; if (view.draft) ruleGrid.selectIdentity(view.draft.id, 2) }
        contentItem: Label { text: workspace.uiText["alert.unsavedMessage"]; wrapMode: Text.Wrap; padding: 16 }
        footer: DialogButtonBox {
            Button { objectName: "keepAlertChanges"; text: workspace.uiText["alert.keepEditing"]; onClicked: discardAlertChanges.close() }
            Button { objectName: "discardAlertChangesButton"; text: workspace.uiText["alert.discard"]; onClicked: { const action = view.pendingEditorAction; discardAlertChanges.close(); if (action) action() } }
        }
    }
    Component {
        id: toggleComponent
        IconButton {
            readonly property var rule: workspace.alerts.rules.find(candidate => candidate.id === parent.identity)
            implicitWidth: ruleGrid.rowHeight
            implicitHeight: ruleGrid.rowHeight
            padding: 4
            activeFocusOnTab: true
            onActiveFocusChanged: if (activeFocus) ruleGrid.focusCell(parent.row, parent.column, parent.tableView)
            objectName: "toggleAlert_" + parent.row
            glyph: rule && rule.enabled ? "Visible" : "Hidden"
            text: rule ? (rule.enabled ? "Disable " : "Enable ") + rule.name : "Toggle alarm"
            enabled: !!rule && !workspace.alerts.busy
            onClicked: view.toggleRule(parent.identity)
        }
    }
    component ActionButton: IconButton {
        glyph: ""
        showText: true
        font.bold: true
        implicitWidth: Math.max(44, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: view.touchLayout ? 44 : 28; leftPadding: 6; rightPadding: 6; topPadding: 4; bottomPadding: 4
    }
    component FormRow: GridLayout {
        required property string label
        default property alias controls: values.data
        Layout.fillWidth: true; columns: view.width < 600 ? 1 : 2; columnSpacing: 8; rowSpacing: 4
        data: [Label { text: parent.label; Layout.fillWidth: parent.columns === 1; Layout.preferredWidth: parent.columns === 1 ? -1 : 130; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted },
            GridLayout { id: values; Layout.fillWidth: true; Layout.minimumWidth: 0; columns: view.width < 600 ? 1 : 3; columnSpacing: 6; rowSpacing: 6 }]
    }
    component HoldMode: ComboBox {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        model: view.holdOptions; textRole: "label"; valueRole: "id"
    }
    Dialogs.ColorDialog {
        id: colorDialog
        objectName: "alertColorDialog"
        title: workspace.uiText["alert.chooseColor"]
        options: Dialogs.ColorDialog.DontUseNativeDialog
        onAccepted: alertColorInput.text = selectedColor.toString()
    }
    contentItem: ScrollView {
        id: pageScroll
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
        width: pageScroll.availableWidth
        height: Math.max(pageScroll.availableHeight, implicitHeight)
        spacing: 8
        Label { id: error; objectName: "alertError"; Layout.fillWidth: true; text: workspace.alerts.error; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
        Label { objectName: "alertEvaluationError"; Layout.fillWidth: true; text: workspace.alerts.evaluationError; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
        ResourceGrid {
            id: ruleGrid
            objectName: "alertRulesTable"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: pageScroll.availableWidth
            Layout.preferredHeight: view.width < 600 ? Math.min(132, Math.max(112, view.height * 0.35)) : Math.min(220, Math.max(160, view.height * 0.4))
            Layout.minimumHeight: view.width < 600 ? Layout.preferredHeight : implicitHeight
            Layout.maximumHeight: Layout.preferredHeight
            prefix: "alert"
            emptyText: "No alarm rules."
            tableModel: workspace.alerts.tableModel
            columns: workspace.alertColumns
            sortColumn: workspace.alerts.tableSortColumn
            sortDirection: workspace.alerts.tableSortDirection
            rowHeight: (view.touchLayout ? 44 : 28) * workspace.textScale
            inspectable: false
            actionColumns: [0, 1, 2, 3, 4, 5]
            accessoryColumns: [0]
            cellAccessory: toggleComponent
            cellName: (row, column, identity) => column === 2 ? "alertRule_" + row : "alertCell_" + column + "_" + row
            onActionRequested: (identity, column, label) => column === 0 ? view.toggleRule(identity) : view.selectRule(identity)
            onRowNavigationRequested: identity => view.selectRule(identity)
            onSortRequested: column => workspace.alerts.sortTable(column)
            onCopyRequested: (row, column) => workspace.alerts.copyCell(row, column)
            onCopyPathRequested: (identity, column) => workspace.alerts.copyIdentity(identity, column)
        }
        RowLayout {
            Layout.fillWidth: true; Layout.maximumWidth: pageScroll.availableWidth; spacing: 6
            Flow {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: childrenRect.height; spacing: 6
                ActionButton { objectName: "addAlert"; glyph: "ZoomIn"; text: workspace.uiText["alert.new"]; enabled: !workspace.alerts.busy; onClicked: view.add() }
                ActionButton { objectName: "reloadAlerts"; text: workspace.uiText["alert.retryLoad"]; visible: workspace.alerts.error !== ""; enabled: !workspace.alerts.busy; onClicked: view.changeSelection("reload") }
            }
        }
        Pane {
            id: editor
            objectName: "alertEditor"
            Layout.fillWidth: true; Layout.minimumWidth: 0; visible: view.draft !== null; clip: true; padding: 10
            Layout.preferredHeight: form.implicitHeight + 20
            Layout.minimumHeight: 80
            Layout.maximumWidth: view.availableWidth
            contentItem: ColumnLayout {
                id: form
                width: editor.availableWidth; spacing: 10
                property bool editable: !workspace.alerts.busy && view.draft !== null && !view.draft.builtIn
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                ActionButton { objectName: "saveAlert"; glyph: "Save"; showText: false; text: workspace.uiText["action.save"]; visible: view.draft !== null && !view.draft.builtIn; enabled: !workspace.alerts.busy && view.draft !== null; onClicked: view.save() }
                ActionButton { objectName: "deleteAlert"; glyph: "Delete"; showText: false; text: workspace.uiText["action.delete"]; enabled: !workspace.alerts.busy && view.draft !== null && !view.draft.builtIn && view.draft.id !== ""; visible: view.draft !== null && !view.draft.builtIn && view.draft.id !== ""; onClicked: view.changeSelection("delete") }
            ActionButton { Layout.alignment: Qt.AlignTop; objectName: "closeAlertEditor"; glyph: "Close"; showText: false; text: workspace.uiText["alert.closeEditor"]; visible: view.draft !== null; enabled: !workspace.alerts.busy; onClicked: view.closeEditor() }
                }
                FormRow { label: workspace.uiText["alert.name"]; TextField { id: name; objectName: "alertName"; Layout.fillWidth: true; enabled: form.editable; Accessible.name: "Alert name" } }
                FormRow { label: workspace.uiText["alert.description"]; TextField { id: description; objectName: "alertDescription"; Layout.fillWidth: true; enabled: form.editable; Accessible.name: "Alert description" } }
                Label { visible: view.draft !== null && view.draft.draft; text: workspace.uiText["alert.draft"]; color: workspace.appearanceColors.muted; Layout.fillWidth: true }
                Label { visible: view.draft !== null && view.draft.builtIn; text: workspace.uiText["alert.builtinHelp"]; color: workspace.appearanceColors.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: workspace.uiText["alert.matchers"]; font.bold: true; color: workspace.appearanceColors.accent }
                    ActionButton { id: matcherHelp; objectName: "alertMatcherHelp"; text: workspace.uiText["alert.syntax"]; checkable: true; Accessible.name: text; Accessible.description: workspace.uiText["alert.syntaxHelp"] }
                }
                Label { Layout.fillWidth: true; visible: matcherHelp.checked; text: workspace.uiText["alert.syntaxHelp"]; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: workspace.appearanceColors.muted }
                Repeater {
                    model: criteria
                    ColumnLayout {
                        id: criterion
                        required property int index; required property int group; required property string field; required property string expression
                        readonly property bool booleanField: ["problems", "activity", "recentlyChanged", "newInView"].includes(field)
                        Layout.fillWidth: true; spacing: 4
                        enabled: form.editable
                        RowLayout {
                            Layout.fillWidth: true; visible: criterion.index === 0 || (criterion.index > 0 && criterion.index < criteria.count && criteria.get(criterion.index - 1).group !== criterion.group)
                            Label { Layout.fillWidth: true; text: workspace.uiText["alert.all"] + " " + (criterion.group + 1); font.bold: true; color: workspace.appearanceColors.accent }
                            ActionButton { objectName: "addAlertCriterion_" + criterion.group; glyph: "ZoomIn"; showText: false; text: workspace.uiText["alert.addCondition"]; onClicked: view.addCriterion(criterion.group) }
                            ActionButton { objectName: "removeAlertGroup_" + criterion.group; glyph: "Close"; showText: false; text: "Remove matcher group " + (criterion.group + 1); enabled: criteria.count > 0 && criteria.get(criteria.count - 1).group > 0; onClicked: view.removeGroup(criterion.group) }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width < 520 ? 1 : 4
                            Label { text: workspace.uiText["alert.all"]; color: workspace.appearanceColors.muted }
                            ComboBox { objectName: "alertField_" + criterion.index; Layout.fillWidth: true; Layout.preferredWidth: 180; Layout.maximumWidth: parent.columns === 1 ? Number.POSITIVE_INFINITY : 220; model: view.fieldOptions; textRole: "label"; valueRole: "id"; currentIndex: model.findIndex(option => option.id === criterion.field); Accessible.name: "Matcher field " + (criterion.index + 1); onActivated: criteria.setProperty(criterion.index, "field", currentValue) }
                            ColumnLayout {
                                Layout.fillWidth: true
                                ComboBox { objectName: "alertBoolean_" + criterion.index; Layout.fillWidth: true; visible: criterion.booleanField; model: ["true", "false"]; currentIndex: model.indexOf(criterion.expression); Accessible.name: "Boolean matcher " + (criterion.index + 1); onActivated: criteria.setProperty(criterion.index, "expression", currentText) }
                                TextField { objectName: "alertExpression_" + criterion.index; Layout.fillWidth: true; visible: !criterion.booleanField || !["true", "false"].includes(criterion.expression); text: criterion.expression; placeholderText: workspace.uiText["alert.expressionHint"]; Accessible.name: "Matcher expression " + (criterion.index + 1); onTextEdited: criteria.setProperty(criterion.index, "expression", text) }
                            }
                            ActionButton { objectName: "removeAlertCriterion_" + criterion.index; glyph: "Close"; showText: false; text: "Remove criterion " + (criterion.index + 1); enabled: view.groupSize(criterion.group) > 1; onClicked: criteria.remove(criterion.index) }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 6; enabled: form.editable
                    ActionButton { objectName: "addAlertAnd"; glyph: "ZoomIn"; showText: false; text: workspace.uiText["alert.addCondition"]; Accessible.name: text; onClicked: view.addCriterion(criteria.count ? criteria.get(criteria.count - 1).group : 0) }
                    ActionButton { objectName: "addAlertOr"; glyph: "ZoomIn"; showText: false; text: workspace.uiText["alert.addAlternative"]; onClicked: criteria.append({group: criteria.count ? criteria.get(criteria.count - 1).group + 1 : 0, field: "name", expression: ""}) }
                }
                Label { text: "Per-rule actions"; font.bold: true; color: workspace.appearanceColors.accent }
                FormRow {
                    label: workspace.uiText["alert.color"]; enabled: form.editable
                    GridLayout {
                        Layout.fillWidth: true
                        columns: width < 520 ? 1 : 2
                        RowLayout {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            ComboBox {
                                objectName: "alertColorChoice"; Layout.fillWidth: true; Layout.minimumWidth: 0; textRole: "label"; valueRole: "id"
                                model: [{id: "none", label: workspace.uiText["alert.color.none"]}, {id: "status", label: workspace.uiText["alert.color.status"]}, {id: "fresh", label: workspace.uiText["alert.color.fresh"]}, {id: "custom", label: workspace.uiText["alert.color.custom"]}]
                                currentIndex: ["none", "status", "fresh"].includes(alertColorInput.text) ? ["none", "status", "fresh"].indexOf(alertColorInput.text) : 3
                                Accessible.name: workspace.uiText["alert.color"]
                                onActivated: alertColorInput.text = currentValue === "custom" ? (alertColorInput.text.startsWith("#") ? alertColorInput.text : "#e3aa46") : currentValue
                            }
                            ActionButton {
                                id: chooseColor
                                objectName: "alertChooseColor"; text: workspace.uiText["alert.chooseColor"]; Accessible.name: text
                                implicitWidth: view.touchLayout ? 44 : 32; implicitHeight: view.touchLayout ? 44 : 32
                                onClicked: { colorDialog.selectedColor = /^#[0-9a-f]{6}$/i.test(alertColorInput.text) ? alertColorInput.text : "#e3aa46"; colorDialog.open() }
                                contentItem: RowLayout {
                                    spacing: 6
                                    Rectangle { Layout.preferredWidth: 16; Layout.preferredHeight: 16; color: /^#[0-9a-f]{6}$/i.test(alertColorInput.text) ? alertColorInput.text : alertColorInput.text === "fresh" ? "#7DFFC3" : alertColorInput.text === "status" ? workspace.appearanceColors.danger : workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border; Accessible.ignored: true }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            HoldMode { id: colorMode; objectName: "alertColorMode"; Accessible.name: "Color hold mode" }
                            SpinBox { id: colorSeconds; objectName: "alertColorSeconds"; from: 1; to: 60; editable: true; visible: colorMode.currentValue !== "no-match" && colorMode.currentValue !== "once"; Accessible.name: "Color hold seconds" }
                        }
                        TextField { id: alertColorInput; objectName: "alertColor"; Layout.fillWidth: true; Layout.columnSpan: parent.columns; visible: !["none", "status", "fresh"].includes(text); placeholderText: "#RRGGBB"; Accessible.name: "Radar alert color" }
                    }
                }
                FormRow {
                    label: workspace.uiText["alert.animation"]; enabled: form.editable
                    GridLayout {
                        Layout.fillWidth: true
                        columns: width < 520 ? 1 : 2
                        ComboBox { id: animation; objectName: "alertAnimation"; Layout.fillWidth: true; Layout.minimumWidth: 0; model: view.animationOptions; textRole: "label"; valueRole: "id"; Accessible.name: "Radar animation" }
                        RowLayout {
                            Layout.fillWidth: true
                            HoldMode { id: animationMode; objectName: "alertAnimationMode"; Accessible.name: "Animation hold mode" }
                            SpinBox { id: animationSeconds; objectName: "alertAnimationSeconds"; from: 1; to: 60; editable: true; visible: animationMode.currentValue !== "no-match" && animationMode.currentValue !== "once"; Accessible.name: "Animation hold seconds" }
                        }
                    }
                }
                FormRow {
                    label: workspace.uiText["alert.autoFocus"]
                    WrappingCheckBox { id: zoomEnabled; objectName: "alertZoomEnabled"; text: workspace.uiText["alert.enabledState"]; Accessible.name: workspace.uiText["alert.autoFocus"]; enabled: form.editable }
                    RowLayout {
                        Layout.fillWidth: true; visible: zoomEnabled.checked; enabled: form.editable
                        SpinBox { id: zoom; objectName: "alertZoom"; from: 1; to: 200; editable: true; Accessible.name: "Radar zoom percent" }
                        Label { text: "%" }
                    }
                    ActionButton { objectName: "previewAlertZoom"; text: "Preview"; Accessible.name: workspace.uiText["alert.autoFocus"] + ": " + text; enabled: !workspace.alerts.busy && !workspace.alerts.zoomPreviewBusy; onClicked: workspace.previewAlertZoom(view.ruleDraft()) }
                }
                Label { objectName: "alertZoomPreviewError"; Layout.fillWidth: true; visible: text !== ""; text: workspace.alerts.zoomPreviewError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
                FormRow {
                    label: workspace.uiText["alert.sound"]
                    WrappingCheckBox {
                        objectName: "alertSoundEnabled"; text: workspace.uiText["alert.enabledState"]
                        Accessible.name: workspace.uiText["alert.sound"]
                        checked: view.selectedSoundId !== "none"
                        enabled: form.editable && (checked || view.soundOptions.length > 0)
                        onClicked: view.selectedSoundId = checked ? view.soundOptions[0].id : "none"
                    }
                    RowLayout {
                        Layout.fillWidth: true; visible: view.selectedSoundId !== "none"
                        ComboBox { id: sound; objectName: "alertSound"; Layout.fillWidth: true; Layout.minimumWidth: 0; enabled: form.editable; model: view.soundOptions; currentIndex: view.soundOptions.findIndex(option => option.id === view.selectedSoundId); textRole: "name"; valueRole: "id"; Accessible.name: workspace.uiText["alert.sound"]; onActivated: view.selectedSoundId = currentValue }
                        ActionButton { objectName: "previewAlertSound"; glyph: "Play"; showText: false; text: "Preview"; Accessible.name: workspace.uiText["alert.sound"] + ": " + text; enabled: !workspace.alerts.muted; onClicked: workspace.alerts.previewSound(view.selectedSoundId) }
                    }
                    RowLayout {
                        Layout.fillWidth: true; visible: view.selectedSoundId !== "none"
                        Label { text: workspace.uiText["alert.minimumMatches"]; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        SpinBox { id: soundMinimum; objectName: "alertSoundMinimum"; from: 1; to: 2147483647; editable: true; enabled: form.editable; Accessible.name: workspace.uiText["alert.minimumMatches"] }
                    }
                }

            }
            background: Rectangle { color: workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border }
        }
        Item { Layout.fillHeight: true; visible: !editor.visible }
        Label { objectName: "alertEmptyState"; text: workspace.uiText[workspace.currentSession === "" ? "alert.emptyContext" : "alert.emptyMatches"]; visible: workspace.alerts.matches.length === 0; color: workspace.appearanceColors.muted }
        Flow {
            Layout.fillWidth: true; Layout.maximumWidth: pageScroll.availableWidth; Layout.preferredHeight: childrenRect.height; spacing: 6
            Repeater { model: workspace.alerts.matches; Button { required property var modelData; required property int index; objectName: "alertMatch_" + index; text: modelData.name + ": " + modelData.count; Accessible.name: text; onClicked: workspace.inspectPath(modelData.path) } }
        }
        }
    }
}
