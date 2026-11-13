pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Pane {
    id: radarPane
    enabled: visible
    property bool compact: false
    property bool expanded: false
    readonly property Item navigationHost: navigationSlot
    readonly property bool shortLandscape: Window.window && Window.window.width > Window.window.height && Window.window.height < 500
    property Item detailsHost: null
    readonly property int count: workspace.resourceCount
    readonly property int navigationIndex: grid.currentIndex
    function navigateRows(offset) {
        if (count === 0) return
        const row = offset === 0 ? 0 : navigationIndex < 0 ? (offset < 0 ? count - 1 : 0) : (navigationIndex + offset + count) % count
        if (grid.selectResource(row) && grid.focusResource(row)) radarInfo.showInfo(focusTarget)
    }
    padding: compact ? 4 : 12
    function dismissInfo(restoreFocus = false) {
        if (restoreFocus && radarPane.visible && grid.renderActive) radarNavigation.forceActiveFocus()
        if (infoPopup) infoPopup.close()
        if (radarInfo) radarInfo.showInfo(null)
    }
    function capturePreview(session) {
        if (!workspace.mapPreviewNeeded) return
        Qt.callLater(function() { if (radarPane.visible && workspace.mapPreviewNeeded) workspace.captureMapPreview(session, grid.previewImage()) })
    }
    onVisibleChanged: if (visible) capturePreview(workspace.currentSession); else dismissInfo()
    Connections { target: workspace; function onMapPreviewRequested(session) { radarPane.capturePreview(session) } }
    function focusPath(path, percent) {
        const index = workspace.alertResourceIndex(path)
        return index >= 0 && grid.focusResource(index, percent / 100)
    }
    function inspectResource(path) {
        const index = workspace.alertResourceIndex(path)
        if (grid.selectResource(index)) workspace.inspectPath(path)
    }
    background: Rectangle { color: workspace.appearanceColors.radarGlass; border.color: workspace.appearanceColors.border }
    contentItem: ColumnLayout {
        ToolBar {
            id: radarNavigation
            objectName: "radarNavigationOverlay"
            parent: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            z: 4
            padding: 4
            background: null
            contentItem: RowLayout {
                spacing: 4
                RowLayout {
                    id: navigationSlot
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: radarPane.expanded && children.length > 0
                }
                RowLayout {
                    Layout.fillWidth: !radarPane.expanded
                    Label { text: "Radar"; font.bold: true; visible: !radarPane.compact }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        objectName: "mapOverview"
                        visible: radarPane.expanded && !workspace.mapOverview
                        glyph: "Cluster"
                        text: workspace.uiText["nav.allContexts"]
                        showText: false
                        enabled: !workspace.busy
                        onClicked: workspace.showMapOverview()
                    }
                    IconButton { objectName: "radarZoomOut"; glyph: "ZoomOut"; text: "Zoom radar out"; onClicked: grid.zoomAt(1/1.18, grid.width/2, grid.height/2) }
                    IconButton { objectName: "radarZoom"; glyph: "ZoomIn"; text: "Zoom radar in"; onClicked: grid.zoomAt(1.18, grid.width/2, grid.height/2) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !radarPane.compact
            Rectangle {
                id: healthBar
                objectName: "radarHealthBar"
                Layout.preferredWidth: 120
                Layout.preferredHeight: 6
                color: workspace.appearanceColors.inset
                Row {
                    anchors.fill: parent
                    Repeater {
                        model: [{count: workspace.healthSummary.healthy || 0, color: "#7DFFC3"},
                            {count: workspace.healthSummary.warning || 0, color: "#FFE866"},
                            {count: workspace.healthSummary.critical || 0, color: "#FF5C5C"}]
                        Rectangle {
                            required property var modelData
                            width: workspace.healthSummary.total > 0 ? healthBar.width * modelData.count / workspace.healthSummary.total : 0
                            height: healthBar.height
                            color: modelData.color
                        }
                    }
                }
                Accessible.name: "Session resource health"
            }
            Label {
                objectName: "radarHealthSummary"
                Layout.fillWidth: true
                text: (workspace.healthSummary.total || 0) + " resources / " + (workspace.healthSummary.healthy || 0) + " healthy / "
                    + (workspace.healthSummary.warning || 0) + " warnings / " + (workspace.healthSummary.critical || 0) + " errors"
                    + (workspace.loading ? " / Loading" : "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Accessible.name: text
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            RadarIsland {
                id: grid
                objectName: "resourceRadar"
                anchors.fill: parent
                source: workspace.table
                alerts: workspace.alerts
                glyphFill: workspace.appearanceColors.inset
                inspectedPath: workspace.inspectorPath
                hoveredPath: radarInfo.target && radarInfo.target.hovered && radarInfo.target.wantsInfo ? radarInfo.target.resourcePath : ""
                focusedPath: {
                    const item = Window.window ? Window.window.activeFocusItem : null
                    return item && item.parent === grid && item.resourcePath !== undefined ? item.resourcePath : ""
                }
                identityScope: workspace.sessionConfiguration(workspace.currentSession).contextId || ""
                textColor: "#DDE7EC"
                captionPixelSize: workspace.textSizes.caption
                headingPixelSize: workspace.textSizes.small
                readonly property int count: workspace.resourceCount
                readonly property real cellWidth: 7 * viewPose.zoom
                readonly property bool renderActive: visible && Window.window !== null && Window.window.visibility !== Window.Hidden && Window.window.visibility !== Window.Minimized
                effectsEnabled: renderActive
                clip: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Grouping
                Accessible.name: "Resource radar, " + count + " resources"
                Accessible.description: "Deterministic resource island. Drag or use arrow keys to pan, wheel or plus and minus to zoom, Home and End to focus the first and last resource, Enter to inspect. Zero resets the map. Use the resource table and filters for the complete accessible resource list."
                property string shownSession: ""
                property bool restoring: false
                property string hoveredResourcePath: ""
                function updateHover() {
                    const path = renderActive && radarHover.hovered
                        ? resourceAt(radarHover.point.position.x, radarHover.point.position.y) : ""
                    hoveredResourcePath = path
                    if (path !== "") hoverTarget.resourcePath = path
                }
                function rememberPosition() {
                    if (visible && !restoring && shownSession === workspace.currentSession) workspace.rememberRadarView(viewPose)
                }
                function fitInitialView() {
                    if (!visible || restoring || workspace.loading || workspace.currentSession === ""
                        || shownSession !== workspace.currentSession || workspace.radarView.zoom !== undefined) return
                    restoring = true
                    const fitted = fitView()
                    restoring = false
                    if (fitted) rememberPosition()
                }
                function restorePosition() {
                    if (!visible) return
                    restoring = true
                    if (shownSession !== workspace.currentSession) {
                        selectResource(-1)
                        hoveredResourcePath = ""
                        hoverTarget.resourcePath = ""
                        radarInfo.showInfo(null)
                    }
                    shownSession = workspace.currentSession
                    if (workspace.radarView.zoom !== undefined) viewPose = workspace.radarView
                    else resetView()
                    restoring = false
                    fitInitialView()
                }
                onVisibleChanged: if (visible) Qt.callLater(restorePosition)
                onViewPoseChanged: rememberPosition()
                onWidthChanged: Qt.callLater(fitInitialView)
                onHeightChanged: Qt.callLater(fitInitialView)
                onCurrentResourceChanged: Qt.callLater(fitInitialView)
                Component.onCompleted: restorePosition()
                Connections {
                    target: workspace
                    function onChanged() {
                        if (grid.visible && grid.shownSession !== workspace.currentSession) grid.restorePosition()
                        else if (grid.visible) Qt.callLater(grid.fitInitialView)
                    }
                }
                Connections {
                    target: workspace.alerts
                    function onFocusRequested(session, path, percent) {
                        if (!grid.visible || session !== workspace.currentSession) return
                        radarPane.focusPath(path, percent)
                    }
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape && infoPopup.visible) { radarPane.dismissInfo(true); event.accepted = true; return }
                    if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) return
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_A) pan(24, 0)
                    else if (event.key === Qt.Key_Right || event.key === Qt.Key_D) pan(-24, 0)
                    else if (event.key === Qt.Key_Up || event.key === Qt.Key_W) pan(0, 24)
                    else if (event.key === Qt.Key_Down || event.key === Qt.Key_S) pan(0, -24)
                    else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) zoomAt(1.18, width/2, height/2)
                    else if (event.key === Qt.Key_Minus) zoomAt(1/1.18, width/2, height/2)
                    else if (event.key === Qt.Key_0) resetView()
                    else if (event.key === Qt.Key_Home) { grid.forceActiveFocus(); if (focusResource(count > 0 ? 0 : -1)) radarInfo.showInfo(focusTarget) }
                    else if (event.key === Qt.Key_End) { grid.forceActiveFocus(); if (focusResource(count - 1)) radarInfo.showInfo(focusTarget) }
                    else return
                    rememberPosition()
                    event.accepted = true
                }
                Keys.onReleased: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (currentIndex >= 0) workspace.inspectRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onTabPressed: function(event) {
                    event.accepted = !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier | Qt.ShiftModifier)) && activeFocus && traverseVisible(0)
                    if (event.accepted) focusTarget.forceActiveFocus(Qt.TabFocusReason)
                }
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        grid.zoomAt(Math.pow(1.18, (event.pixelDelta.y || event.angleDelta.y)/120), event.x, event.y)
                        event.accepted = true
                    }
                }
                DragHandler {
                    id: mapDrag
                    target: null
                    property point previous: Qt.point(0, 0)
                    onActiveChanged: if (active) { previous = Qt.point(0, 0); grid.forceActiveFocus() }
                    onActiveTranslationChanged: if (active) {
                        grid.pan(activeTranslation.x-previous.x, activeTranslation.y-previous.y)
                        previous = activeTranslation
                    }
                }
                HoverHandler {
                    id: radarHover
                    onPointChanged: grid.updateHover()
                    onHoveredChanged: {
                        grid.updateHover()
                        if (!hovered) radarInfo.syncTarget()
                    }
                }
                TapHandler {
                    gesturePolicy: TapHandler.DragThreshold
                    onTapped: {
                        const path = grid.resourceAt(point.position.x, point.position.y)
                        grid.forceActiveFocus()
                        if (path !== "") radarPane.inspectResource(path)
                    }
                }
                Connections {
                    target: grid
                    function onCurrentResourceChanged() { grid.updateHover() }
                    function onViewPoseChanged() { grid.updateHover() }
                }
                RadarWater {
                    id: water
                    objectName: "radarWater"
                    anchors.fill: parent
                    z: -1
                    viewPose: grid.viewPose
                    color: "#061621"
                    visible: grid.count > 0
                    speedPercent: workspace.radarWaterEnabled ? workspace.radarWaterSpeedPercent : 0
                    playing: grid.renderActive && visible && speedPercent > 0 && !mapDrag.active && !workspace.alerts.reducedMotion
                    Connections {
                        target: workspace
                        function onRequestStarted(session, path, monotonicMs) { if (session === workspace.currentSession) water.noteRequest() }
                    }
                }
                component ResourceTarget: Item {
                    id: tile
                    property bool keyboardTarget: false
                    property string resourcePath: ""
                    property var model: ({})
                    readonly property int resourceIndex: model.resourceIndex === undefined ? -1 : model.resourceIndex
                    readonly property bool detailsNeeded: wantsInfo || radarInfo.target === tile
                    readonly property var resourceMetrics: detailsNeeded && model.resourceMetrics ? model.resourceMetrics : []
                    readonly property bool inViewport: visible && grid.renderActive && x + width > 0 && x < grid.width && y + height > 0 && y < grid.height
                    visible: resourceIndex >= 0 && model.resourcePath === resourcePath
                    width: 5.5 * grid.viewPose.zoom
                    height: width
                    x: grid.width/2 + ((model.worldX || 0) + grid.viewPose.x)*grid.viewPose.zoom - width/2
                    y: grid.height/2 + ((model.worldY || 0) + grid.viewPose.y)*grid.viewPose.zoom - height/2
                    activeFocusOnTab: keyboardTarget
                    onActiveFocusChanged: if (activeFocus) {
                        const index = workspace.alertResourceIndex(resourcePath)
                        if (grid.selectResource(index) && !keyboardTarget) focusTarget.forceActiveFocus(Qt.OtherFocusReason)
                    }
                    readonly property bool hovered: resourcePath !== "" && grid.hoveredResourcePath === resourcePath
                    function refreshSnapshot() {
                        const next = grid.renderActive ? grid.resourceInfo(resourcePath, detailsNeeded) : ({})
                        model = next.resourcePath === resourcePath ? next : ({})
                        scale = model.animationScale || 1
                    }
                    onResourcePathChanged: refreshSnapshot()
                    onDetailsNeededChanged: Qt.callLater(refreshSnapshot)
                    Component.onCompleted: refreshSnapshot()
                    Connections {
                        target: grid
                        function onCurrentResourceChanged() { tile.refreshSnapshot() }
                        function onViewPoseChanged() { tile.refreshSnapshot() }
                        function onEffectsEnabledChanged() { tile.refreshSnapshot() }
                    }
                    Connections {
                        target: grid.tiles
                        function onDataChanged(first, last, roles) {
                            if (!tile.detailsNeeded) return
                            // Pulse frames update only the transform, not metadata or gauges.
                            if (roles.length === 1 && roles[0] === grid.tiles.animationRole) {
                                const frame = grid.resourceInfo(tile.resourcePath, false)
                                tile.scale = frame.animationScale || 1
                            } else tile.refreshSnapshot()
                        }
                    }
                    readonly property string text: {
                        if (!detailsNeeded) return ""
                        workspace.alerts.revision
                        const effect = workspace.alerts.effect(resourcePath)
                        const health = model.resourceHealth
                        return model.resourceKind + " " + model.resourceName + ", " + (model.resourceNamespace || "Cluster-scoped") + ", " + model.resourceStatus
                            + (health === 2 ? ", Error" : health === 1 ? ", Warning" : effect.color === "fresh" ? ", Recently changed" : "")
                    }
                    Accessible.role: Accessible.Button
                    Accessible.ignored: !detailsNeeded || (!keyboardTarget && resourcePath === focusTarget.resourcePath && focusTarget.detailsNeeded)
                    Accessible.name: text
                    Accessible.focusable: true
                    Accessible.onPressAction: radarPane.inspectResource(resourcePath)
                    Accessible.description: detailsNeeded ? (resourcePath === grid.inspectedPath ? "Selected resource" : "Open resource in inspector") : ""
                    readonly property bool wantsInfo: inViewport && (hovered || activeFocus || (keyboardTarget && grid.activeFocus && grid.currentIndex === resourceIndex))
                    onWantsInfoChanged: {
                        // Finish visibility evaluation before the reader observes this target.
                        if (wantsInfo && radarInfo.target !== tile) {
                            radarInfo.pendingTarget = tile
                            Qt.callLater(radarInfo.showPendingInfo)
                        }
                        if (radarInfo.target===tile) Qt.callLater(radarInfo.syncTarget)
                    }
                    onInViewportChanged: if (radarInfo.target===tile) Qt.callLater(radarInfo.syncTarget)
                    Component.onDestruction: if (radarInfo) {
                        if (radarInfo.pendingTarget === tile) radarInfo.pendingTarget = null
                        if (radarInfo.target === tile) radarInfo.target = null
                    }
                    Keys.onTabPressed: function(event) {
                        const backwards = (event.modifiers & Qt.ShiftModifier) !== 0
                        event.accepted = !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) && grid.traverseVisible(backwards ? -1 : 1)
                        if (event.accepted) focusTarget.forceActiveFocus(backwards ? Qt.BacktabFocusReason : Qt.TabFocusReason)
                    }
                    Keys.onBacktabPressed: function(event) {
                        event.accepted = !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) && grid.traverseVisible(-1)
                        if (event.accepted) focusTarget.forceActiveFocus(Qt.BacktabFocusReason)
                    }
                    Keys.onReleased: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            radarPane.inspectResource(resourcePath)
                            event.accepted = true
                        }
                    }
                }
                ResourceTarget {
                    id: focusTarget
                    objectName: "radarFocusTarget"
                    keyboardTarget: true
                    resourcePath: grid.currentIndex >= 0 ? grid.source.data(grid.source.index(grid.currentIndex, 0), Qt.UserRole) : ""
                }
                ResourceTarget { id: hoverTarget; objectName: "radarHoverTarget" }
                Label {
                    objectName: "emptyRadar"
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: radarNavigation.height + (grid.height - radarNavigation.height - height) / 2
                    visible: grid.count === 0 && grid.height - radarNavigation.height >= implicitHeight + 8
                    width: Math.min(320, parent.width - 24)
                    text: workspace.currentSession === "" ? "Import a kubeconfig and open a context." : workspace.loading ? "Loading resources..." : workspace.totalResourceCount === 0 ? "No resources to display." : "No matching resources."
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Popup {
                id: infoPopup
                objectName: "radarInfoPopup"
                parent: nearResource ? radarPane.Window.window.contentItem : radarPane.detailsHost ?? grid
                popupType: Popup.Item
                padding: 0
                margins: 0
                modal: false
                dim: false
                focus: false
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: null
                readonly property bool nearResource: radarPane.Window.window !== null
                    && radarPane.Window.window.width >= 1000 && !radarPane.shortLandscape
                    && radarInfo.target === hoverTarget
                readonly property point resourcePosition: nearResource
                    ? grid.mapToItem(parent, radarInfo.target.x + radarInfo.target.width / 2,
                        radarInfo.target.y + radarInfo.target.height / 2) : Qt.point(0, 0)
                readonly property point radarPosition: {
                    radarPane.width; grid.width; grid.height
                    if (!radarPane.Window.window) return Qt.point(0, 0)
                    radarPane.Window.window.width; radarPane.Window.window.height
                    return grid.mapToItem(radarPane.Window.window.contentItem, 0, 0)
                }
                readonly property real leftSpace: Math.max(0, radarPosition.x - 16)
                readonly property real rightSpace: radarPane.Window.window ? Math.max(0, radarPane.Window.window.contentItem.width - radarPosition.x - grid.width - 16) : 0
                readonly property bool adjacent: !radarPane.detailsHost && !radarPane.expanded && Math.max(leftSpace, rightSpace) >= 180
                readonly property bool onLeft: leftSpace >= rightSpace
                width: nearResource ? Math.min(360, parent.width - 32) : radarPane.detailsHost ? parent.width : adjacent ? Math.min(360, Math.max(leftSpace, rightSpace)) : Math.max(0, Math.min(360, parent.width - 16))
                height: Math.max(0, Math.min(infoHeader.implicitHeight + infoLayout.spacing + infoContents.implicitHeight + radarInfo.topPadding + radarInfo.bottomPadding,
                    nearResource ? Math.min(360, parent.height - 32) : radarPane.detailsHost ? parent.height : adjacent ? grid.height : Math.min(320,
                        Math.max(grid.height * 0.45, infoHeader.implicitHeight + infoLayout.spacing + 24 + radarInfo.topPadding + radarInfo.bottomPadding),
                        grid.height - radarNavigation.height - 8)))
                x: nearResource ? Math.max(16, Math.min(parent.width - width - 16,
                    resourcePosition.x + width + 28 <= parent.width ? resourcePosition.x + 12 : resourcePosition.x - width - 12))
                    : radarPane.detailsHost ? 0 : adjacent ? (onLeft ? -width - 8 : grid.width + 8) : parent.width - width - 8
                y: nearResource ? Math.max(16, Math.min(parent.height - height - 16, resourcePosition.y - height / 2))
                    : radarPane.detailsHost || adjacent ? 0 : parent.height - height - 8
                visible: radarPane.visible && grid.renderActive && radarInfo.hasDetails && width > 0 && height > 0
                onClosed: Qt.callLater(radarInfo.clearClosedTarget)
                contentItem: Pane {
                    id: radarInfo
                    objectName: "radarInfoPanel"
                    property var target: null
                    property var pendingTarget: null
                    readonly property bool hasDetails: !!target && target.visible && target.inViewport
                    readonly property var resourceDetails: hasDetails && target.model.resourceDetails ? target.model.resourceDetails : ({})
                    function showInfo(next) {
                        pendingTarget = null
                        target = next
                    }
                    function showPendingInfo() {
                        const next = pendingTarget
                        pendingTarget = null
                        if (next && next.wantsInfo) target = next
                    }
                    function syncTarget() {
                        if (target && (!target.visible || !target.inViewport || (!target.wantsInfo && !radarHover.hovered && !infoPopup.activeFocus))) target = null
                    }
                    function clearClosedTarget() {
                        if (infoPopup && !infoPopup.visible) showInfo(null)
                    }
                    onTargetChanged: Qt.callLater(syncTarget)
                    Connections {
                        target: infoPopup
                        function onActiveFocusChanged() { Qt.callLater(radarInfo.syncTarget) }
                    }
                    padding: radarPane.shortLandscape ? 4 : 8
                    Accessible.role: Accessible.Grouping
                    Accessible.name: "Resource details"
                    background: Rectangle { color: workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border }
                    contentItem: ColumnLayout {
                        id: infoLayout
                        spacing: 6
                        RowLayout {
                            id: infoHeader
                            Layout.fillWidth: true
                            Label {
                                objectName: "radarInfoText"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                visible: radarInfo.hasDetails
                                text: radarInfo.hasDetails ? radarInfo.target.model.resourceKind + " " + radarInfo.target.model.resourceName : ""
                                font.bold: true
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                color: workspace.appearanceColors.text
                                Accessible.name: text
                            }
                            IconButton {
                                objectName: "closeRadarInfo"
                                glyph: "Close"
                                text: workspace.uiText["action.close"]
                                onClicked: radarPane.dismissInfo(true)
                            }
                        }
                        ScrollView {
                            id: infoScroll
                            objectName: "radarInfoScroll"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            Layout.minimumHeight: 0
                            contentWidth: availableWidth
                            contentHeight: infoContents.implicitHeight
                            activeFocusOnTab: radarInfo.hasDetails || activeFocus
                            clip: true
                            Keys.onEscapePressed: radarPane.dismissInfo(true)
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            WheelHandler {
                                target: null
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: function(event) {
                                    const view = infoScroll.contentItem
                                    const delta = event.pixelDelta.y || event.angleDelta.y / 3
                                    if (view && delta) {
                                        view.cancelFlick()
                                        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height, view.contentY - delta))
                                    }
                                    event.accepted = true
                                }
                            }
                            ColumnLayout {
                                id: infoContents
                                width: infoScroll.availableWidth
                                spacing: 6
                                Label {
                                    objectName: "radarInfoNamespace"
                                    Layout.fillWidth: true
                                    visible: radarInfo.hasDetails
                                    text: radarInfo.hasDetails ? radarInfo.target.model.resourceNamespace || "Cluster-scoped" : ""
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    color: workspace.appearanceColors.muted
                                }
                                Label {
                                    objectName: "radarInfoStatus"
                                    Layout.fillWidth: true
                                    visible: radarInfo.hasDetails
                                    text: radarInfo.hasDetails ? workspace.uiText["alert.field.status"] + ": " + radarInfo.target.model.resourceStatus : ""
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    font.bold: true
                                    color: radarInfo.hasDetails && radarInfo.target.model.statusColor ? radarInfo.target.model.statusColor : workspace.appearanceColors.text
                                }
                                Label {
                                    objectName: "radarInfoIssue"
                                    Layout.fillWidth: true
                                    visible: radarInfo.hasDetails && !!radarInfo.resourceDetails.issue && radarInfo.resourceDetails.issue !== "-"
                                    text: workspace.uiText["alert.field.issue"] + ": " + (radarInfo.resourceDetails.issue || "")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                                Label {
                                    objectName: "radarInfoReadiness"
                                    Layout.fillWidth: true
                                    visible: radarInfo.hasDetails && text !== ""
                                    text: [radarInfo.resourceDetails.ready ? workspace.uiText["alert.field.ready"] + ": " + radarInfo.resourceDetails.ready : "",
                                        radarInfo.resourceDetails.restarts !== undefined && radarInfo.resourceDetails.restarts !== null && radarInfo.resourceDetails.restarts !== ""
                                            ? workspace.uiText["alert.field.restarts"] + ": " + radarInfo.resourceDetails.restarts : ""].filter(value => value !== "").join(" / ")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                }
                                Repeater {
                                    model: radarInfo.hasDetails ? [{label: workspace.uiText["alert.field.node"], value: radarInfo.resourceDetails.node},
                                        {label: workspace.uiText["alert.field.image"], value: radarInfo.resourceDetails.image}, {label: workspace.uiText["alert.field.owner"], value: radarInfo.resourceDetails.owner}]
                                        .filter(entry => !!entry.value && entry.value !== "-") : []
                                    Label {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: modelData.label + ": " + modelData.value
                                        textFormat: Text.PlainText
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                }
                                Repeater {
                                    model: radarInfo.hasDetails ? radarInfo.target.resourceMetrics.filter(metric => metric.available || !!metric.presentReferences) : []
                                    MetricGauge {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        metric: modelData
                                        omitMissing: true
                                        showMeasurement: radarInfo.target && radarInfo.target.resourceMetrics.find(value => value.timestamp)?.id === modelData.id
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: !radarPane.compact; text: "Drag or arrow keys: pan. Scroll or trackpad: zoom. + / -: zoom. 0: reset. Home / End: focus resource. Enter: inspect."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    }
}
