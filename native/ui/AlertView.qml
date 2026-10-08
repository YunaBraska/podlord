import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

Pane {
    id: view
    padding: 0
    Layout.minimumWidth: 0
    property var draft: null
    property bool savedSelection: false
    property bool selectNew: false
    property string sourceError: ""
    property string selectedSoundId: "none"
    readonly property var fieldOptions: workspace.alerts.fields.map(field => ({id: field, label: workspace.uiText["alert.field." + field]}))
    readonly property var holdOptions: [
        {id: "no-match", label: workspace.uiText["alert.hold.match"]},
        {id: "duration", label: workspace.uiText["alert.hold.duration"]},
        {id: "new-in-view", label: workspace.uiText["alert.hold.new"]}
    ]
    readonly property var animationOptions: ["none", "blink", "pulse", "sweep", "outline"].map(id => ({id: id, label: workspace.uiText["alert.animation." + id]}))
    readonly property var soundCatalog: workspace.alerts.sounds
    readonly property var soundsById: {
        const entries = {}
        for (const sound of soundCatalog) entries[sound.id] = sound
        return Object.freeze(entries)
    }
    readonly property var selectedSound: soundsById[selectedSoundId]
    onSelectedSoundIdChanged: sourceError = ""
    readonly property var soundOptions: {
        const query = soundSearch.text.trim().toLowerCase()
        return soundCatalog.filter(s => !query || [s.id, s.name, s.purpose, s.author, s.license, s.source, s.asset, s.isMusic ? "music" : "sound"].join(" ").toLowerCase().includes(query))
    }
    ListModel { id: criteria }
    function selectRule(identity) {
        const rule = workspace.alerts.rules.find(candidate => candidate.id === identity)
        if (!rule) return false
        if (!draft || draft.id !== rule.id) edit(rule)
        return true
    }
    function toggleRule(identity) {
        const rule = workspace.alerts.rules.find(candidate => candidate.id === identity)
        if (!rule || workspace.alerts.busy) return false
        edit(rule)
        ruleEnabled.checked = !rule.enabled
        save()
        return true
    }
    function edit(rule) {
        draft = rule
        ruleGrid.selectIdentity(rule.id, 2)
        criteria.clear()
        for (let g = 0; g < rule.groups.length; ++g)
            for (const c of rule.groups[g]) criteria.append({group: g, field: c.field, expression: c.expression})
        name.text = rule.name; description.text = rule.description; ruleEnabled.checked = rule.enabled
        alertColorInput.text = rule.color; colorMode.currentIndex = holdOptions.findIndex(option => option.id === rule.colorMode); colorSeconds.value = rule.colorSeconds
        animation.currentIndex = animationOptions.findIndex(option => option.id === rule.animation); animationMode.currentIndex = holdOptions.findIndex(option => option.id === rule.animationMode); animationSeconds.value = rule.animationSeconds
        zoom.value = rule.zoom; selectedSoundId = rule.sound; soundSearch.text = ""; soundMinimum.value = rule.soundMinimumMatches
        editor.contentItem.contentY = 0
    }
    function add() {
        edit({id: "", name: "", description: "", enabled: true, builtIn: false,
            groups: [[{field: "name", expression: ""}]], color: "#e3aa46", colorMode: "no-match", colorSeconds: 5,
            animation: "none", animationMode: "no-match", animationSeconds: 5, zoom: 0, sound: "none", soundMinimumMatches: 1})
        name.forceActiveFocus(Qt.TabFocusReason)
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
        return {id: draft.id, name: name.text, description: description.text, enabled: ruleEnabled.checked, builtIn: draft.builtIn,
            groups: groups.filter(g => g !== undefined), color: alertColorInput.text, colorMode: colorMode.currentValue, colorSeconds: colorSeconds.value,
            animation: animation.currentValue, animationMode: animationMode.currentValue, animationSeconds: animationSeconds.value,
            zoom: zoom.value, sound: selectedSoundId, soundMinimumMatches: soundMinimum.value}
    }
    function save() {
        savedSelection = true; selectNew = draft.id === ""
        if (!workspace.alerts.saveRule(ruleDraft())) savedSelection = false
    }
    function changeSelection(action) {
        savedSelection = true; selectNew = action === "duplicate"
        const accepted = action === "duplicate" ? workspace.alerts.duplicateRule(draft.id)
            : action === "delete" ? workspace.alerts.deleteRule(draft.id) : workspace.alerts.reload()
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
            if (!view.savedSelection && view.draft !== null) return
            const selected = !view.selectNew && view.draft && rules.find(r => r.id === view.draft.id)
            view.savedSelection = false
            if (selected) view.edit(selected)
            else if (rules.length) view.edit(view.selectNew ? rules[rules.length - 1] : rules[0])
            else view.draft = null
            view.selectNew = false
        }
    }
    Component {
        id: toggleComponent
        Button {
            readonly property var rule: workspace.alerts.rules.find(candidate => candidate.id === parent.identity)
            padding: 4
            activeFocusOnTab: true
            onActiveFocusChanged: if (activeFocus) ruleGrid.focusCell(parent.row, parent.column, parent.tableView)
            objectName: "toggleAlert_" + parent.row
            text: rule && rule.enabled ? "On" : "Off"
            enabled: !!rule && !workspace.alerts.busy
            Accessible.name: rule ? (rule.enabled ? "Disable " : "Enable ") + rule.name : "Toggle alarm"
            onClicked: view.toggleRule(parent.identity)
        }
    }
    component ActionButton: Button {
        implicitWidth: Math.max(44, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: 28; leftPadding: 6; rightPadding: 6; topPadding: 4; bottomPadding: 4
    }
    component FormRow: GridLayout {
        required property string label
        default property alias controls: values.data
        Layout.fillWidth: true; columns: width < 600 ? 1 : 2; columnSpacing: 8; rowSpacing: 4
        data: [Label { text: parent.label; Layout.preferredWidth: parent.columns === 1 ? -1 : 130; color: workspace.appearanceColors.muted },
            RowLayout { id: values; Layout.fillWidth: true; spacing: 6 }]
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
    contentItem: ColumnLayout {
        spacing: 8
        Label { id: error; objectName: "alertError"; Layout.fillWidth: true; text: workspace.alerts.error; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
        Label { objectName: "alertEvaluationError"; Layout.fillWidth: true; text: workspace.alerts.evaluationError; visible: text !== ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
        ResourceGrid {
            id: ruleGrid
            objectName: "alertRulesTable"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: view.width < 600 ? Math.min(112, Math.max(56, view.height * 0.2)) : Math.min(220, Math.max(112, view.height * 0.32))
            Layout.minimumHeight: view.width < 600 ? 0 : implicitHeight
            Layout.maximumHeight: view.width < 600 ? Layout.preferredHeight : Number.POSITIVE_INFINITY
            prefix: "alert"
            emptyText: "No alarm rules."
            tableModel: workspace.alerts.tableModel
            columns: workspace.alertColumns
            sortColumn: workspace.alerts.tableSortColumn
            sortDirection: workspace.alerts.tableSortDirection
            rowHeight: 28
            inspectable: false
            actionColumns: [0, 1, 2, 3, 4, 5]
            accessoryColumns: [0]
            cellAccessory: toggleComponent
            cellName: (row, column, identity) => column === 2 ? "alertRule_" + row : "alertCell_" + column + "_" + row
            onCurrentRequested: identity => view.selectRule(identity)
            onActionRequested: (identity, column, label) => column === 0 ? view.toggleRule(identity) : view.selectRule(identity)
            onSortRequested: column => workspace.alerts.sortTable(column)
            onCopyRequested: (row, column) => workspace.alerts.copyCell(row, column)
            onCopyPathRequested: (identity, column) => workspace.alerts.copyIdentity(identity, column)
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            ActionButton { objectName: "addAlert"; text: workspace.uiText["action.add"]; enabled: !workspace.alerts.busy; onClicked: view.add() }
            ActionButton { objectName: "duplicateAlert"; text: workspace.uiText["action.duplicate"]; enabled: !workspace.alerts.busy && view.draft !== null && view.draft.id !== ""; onClicked: view.changeSelection("duplicate") }
            ActionButton { objectName: "deleteAlert"; text: workspace.uiText["action.delete"]; enabled: !workspace.alerts.busy && view.draft !== null && !view.draft.builtIn && view.draft.id !== ""; onClicked: view.changeSelection("delete") }
            ActionButton { objectName: "saveAlert"; text: workspace.uiText["action.save"]; enabled: !workspace.alerts.busy && view.draft !== null; onClicked: view.save() }
            ActionButton { objectName: "reloadAlerts"; text: workspace.uiText["alert.retryLoad"]; visible: workspace.alerts.error !== ""; enabled: !workspace.alerts.busy; onClicked: view.changeSelection("reload") }
            CheckBox { id: ruleEnabled; objectName: "alertEnabled"; text: workspace.uiText["alert.active"]; enabled: !workspace.alerts.busy && view.draft !== null }
            CheckBox { objectName: "muteAlerts"; text: workspace.uiText["audio.mute"]; checked: workspace.alerts.muted; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(checked, workspace.alerts.reducedMotion) }
            CheckBox { objectName: "reduceAlertMotion"; text: "Reduce motion"; checked: workspace.alerts.reducedMotion; enabled: !workspace.alerts.busy; onClicked: workspace.alerts.setPreferences(workspace.alerts.muted, checked) }
        }
        ScrollView {
            id: editor
            objectName: "alertEditor"
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; visible: view.draft !== null; clip: true; contentWidth: availableWidth
            Layout.minimumHeight: 80
            Layout.maximumWidth: view.availableWidth
            ColumnLayout {
                id: form
                width: editor.availableWidth; spacing: 8
                property bool editable: !workspace.alerts.busy && view.draft !== null && !view.draft.builtIn
                FormRow { label: workspace.uiText["alert.name"]; TextField { id: name; objectName: "alertName"; Layout.fillWidth: true; enabled: form.editable; Accessible.name: "Alert name" } }
                FormRow { label: workspace.uiText["alert.description"]; TextField { id: description; objectName: "alertDescription"; Layout.fillWidth: true; enabled: form.editable; Accessible.name: "Alert description" } }
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
                            Label { Layout.fillWidth: true; text: "OR matcher " + (criterion.group + 1); font.bold: true; color: workspace.appearanceColors.accent }
                            ActionButton { objectName: "addAlertCriterion_" + criterion.group; text: "+"; Accessible.name: "Add AND criterion to group " + (criterion.group + 1); onClicked: view.addCriterion(criterion.group) }
                            ActionButton { objectName: "removeAlertGroup_" + criterion.group; text: "Remove group"; enabled: criteria.count > 0 && criteria.get(criteria.count - 1).group > 0; onClicked: view.removeGroup(criterion.group) }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width < 520 ? 1 : 4
                            Label { text: "AND"; color: workspace.appearanceColors.muted }
                            ComboBox { objectName: "alertField_" + criterion.index; Layout.fillWidth: true; Layout.preferredWidth: 160; model: view.fieldOptions; textRole: "label"; valueRole: "id"; currentIndex: model.findIndex(option => option.id === criterion.field); Accessible.name: "Matcher field " + (criterion.index + 1); onActivated: criteria.setProperty(criterion.index, "field", currentValue) }
                            ColumnLayout {
                                Layout.fillWidth: true
                                ComboBox { objectName: "alertBoolean_" + criterion.index; Layout.fillWidth: true; visible: criterion.booleanField; model: ["true", "false"]; currentIndex: model.indexOf(criterion.expression); Accessible.name: "Boolean matcher " + (criterion.index + 1); onActivated: criteria.setProperty(criterion.index, "expression", currentText) }
                                TextField { objectName: "alertExpression_" + criterion.index; Layout.fillWidth: true; visible: !criterion.booleanField || !["true", "false"].includes(criterion.expression); text: criterion.expression; placeholderText: workspace.uiText["alert.expressionHint"]; Accessible.name: "Matcher expression " + (criterion.index + 1); onTextEdited: criteria.setProperty(criterion.index, "expression", text) }
                            }
                            ActionButton { objectName: "removeAlertCriterion_" + criterion.index; text: "X"; Accessible.name: "Remove criterion " + (criterion.index + 1); enabled: view.groupSize(criterion.group) > 1; onClicked: criteria.remove(criterion.index) }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6; enabled: form.editable
                    ActionButton { objectName: "addAlertAnd"; text: "Add AND criterion"; onClicked: view.addCriterion(criteria.get(criteria.count - 1).group) }
                    ActionButton { objectName: "addAlertOr"; text: "Add OR group"; onClicked: criteria.append({group: criteria.get(criteria.count - 1).group + 1, field: "name", expression: ""}) }
                }
                Label { text: workspace.uiText["alert.actions"]; font.bold: true; color: workspace.appearanceColors.accent }
                FormRow {
                    label: workspace.uiText["alert.color"]; enabled: form.editable
                    ColumnLayout {
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
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
                                onClicked: { colorDialog.selectedColor = /^#[0-9a-f]{6}$/i.test(alertColorInput.text) ? alertColorInput.text : "#e3aa46"; colorDialog.open() }
                                contentItem: RowLayout {
                                    spacing: 6
                                    Rectangle { Layout.preferredWidth: 16; Layout.preferredHeight: 16; color: /^#[0-9a-f]{6}$/i.test(alertColorInput.text) ? alertColorInput.text : workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border; Accessible.ignored: true }
                                    Label { text: chooseColor.text; font: chooseColor.font; color: chooseColor.palette.buttonText }
                                }
                            }
                        }
                        TextField { id: alertColorInput; objectName: "alertColor"; Layout.fillWidth: true; visible: !["none", "status", "fresh"].includes(text); placeholderText: "#RRGGBB"; Accessible.name: "Radar alert color" }
                        RowLayout {
                            Layout.fillWidth: true
                            HoldMode { id: colorMode; objectName: "alertColorMode"; Accessible.name: "Color hold mode" }
                            SpinBox { id: colorSeconds; from: 1; to: 60; editable: true; visible: colorMode.currentValue !== "no-match"; Accessible.name: "Color hold seconds" }
                        }
                    }
                }
                FormRow {
                    label: workspace.uiText["alert.animation"]; enabled: form.editable
                    ColumnLayout {
                        Layout.fillWidth: true
                        ComboBox { id: animation; Layout.fillWidth: true; model: view.animationOptions; textRole: "label"; valueRole: "id"; Accessible.name: "Radar animation" }
                        RowLayout {
                            Layout.fillWidth: true
                            HoldMode { id: animationMode; objectName: "alertAnimationMode"; Accessible.name: "Animation hold mode" }
                            SpinBox { id: animationSeconds; from: 1; to: 60; editable: true; visible: animationMode.currentValue !== "no-match"; Accessible.name: "Animation hold seconds" }
                        }
                    }
                }
                FormRow { label: "Zoom (%)"; SpinBox { id: zoom; objectName: "alertZoom"; enabled: form.editable; from: 0; to: 200; editable: true; Accessible.name: "Radar zoom percent" } ActionButton { objectName: "previewAlertZoom"; text: "Preview"; enabled: view.draft !== null && !workspace.alerts.busy && !workspace.alerts.zoomPreviewBusy; Accessible.name: "Preview draft radar zoom without saving"; onClicked: workspace.previewAlertZoom(view.ruleDraft()) } Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "0 disables automatic focus"; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted } }
                Label { objectName: "alertZoomPreviewError"; Layout.fillWidth: true; visible: text !== ""; text: workspace.alerts.zoomPreviewError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: workspace.appearanceColors.danger; Accessible.name: text }
                FormRow { label: "Find sound"; TextField { id: soundSearch; objectName: "alertSoundSearch"; Layout.fillWidth: true; enabled: form.editable; placeholderText: "Name, purpose, author, license or music"; Accessible.name: "Find alert sound" } }
                FormRow { label: "Sound"; ComboBox { id: sound; objectName: "alertSound"; Layout.fillWidth: true; enabled: form.editable; model: view.soundOptions; currentIndex: view.soundOptions.findIndex(s => s.id === view.selectedSoundId); textRole: "name"; valueRole: "id"; Accessible.name: "Alert sound"; onActivated: view.selectedSoundId = currentValue } ActionButton { objectName: "previewAlertSound"; text: "Preview"; enabled: !workspace.alerts.busy && !workspace.alerts.muted && view.selectedSoundId !== "none"; onClicked: workspace.alerts.previewSound(view.selectedSoundId) } ActionButton { objectName: "openAlertSoundSource"; text: "Source"; enabled: view.selectedSound !== undefined; Accessible.name: "Open selected sound source in the external browser"; onClicked: view.sourceError = Qt.openUrlExternally(view.selectedSound.source) ? "" : "The browser could not open this link. Please try again explicitly." } }
                Label { objectName: "alertSoundNoResults"; Layout.fillWidth: true; visible: view.soundOptions.length === 0; text: "No sounds match. The selected sound is unchanged."; wrapMode: Text.Wrap; color: workspace.appearanceColors.muted }
                Label { objectName: "alertSoundAttribution"; Layout.fillWidth: true; text: view.selectedSound ? view.selectedSound.name + "\n" + view.selectedSound.purpose + "\n" + view.selectedSound.author + " / " + view.selectedSound.license + "\n" + view.selectedSound.source : ""; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: workspace.appearanceColors.muted; Accessible.name: text }
                Label { objectName: "alertSoundSourceError"; Layout.fillWidth: true; visible: text !== ""; text: view.sourceError; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: workspace.appearanceColors.danger; Accessible.name: text }
                FormRow { label: "Minimum matches"; enabled: form.editable; SpinBox { id: soundMinimum; objectName: "alertSoundMinimum"; from: 1; to: 2147483647; editable: true; Accessible.name: "Minimum matches before sound" } }
            }
            background: Rectangle { color: workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border }
        }
        Label { objectName: "alertEmptyState"; text: workspace.currentSession === "" ? "Open a session to evaluate cached resources." : "No matching alerts in this session."; visible: workspace.alerts.matches.length === 0; color: workspace.appearanceColors.muted }
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater { model: workspace.alerts.matches; Button { required property var modelData; required property int index; objectName: "alertMatch_" + index; text: modelData.name + ": " + modelData.count; Accessible.name: text; onClicked: workspace.inspectPath(modelData.path) } }
        }
    }
}
