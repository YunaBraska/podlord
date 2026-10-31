pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Pane {
    id: radarPane
    property bool compact: false
    property bool expanded: false
    readonly property bool detailsBeside: expanded && availableWidth >= 840
    readonly property real infoPanelHeight: detailsBeside ? 0 : expanded ? Math.min(160, Math.max(44, availableHeight / 3)) : Window.window && Window.window.height < 500 ? 44 : 112
    readonly property int count: workspace.resourceCount
    readonly property int navigationIndex: grid.currentIndex
    function navigateRows(offset) {
        if (count === 0) return
        const row = offset === 0 ? 0 : navigationIndex < 0 ? (offset < 0 ? count - 1 : 0) : (navigationIndex + offset + count) % count
        grid.selectResource(row); grid.focusResource(row)
    }
    padding: compact ? 4 : 12
    function focusRadar() { grid.forceActiveFocus() }
    function capturePreview(session) {
        if (!workspace.mapPreviewNeeded) return
        Qt.callLater(function() { if (radarPane.visible && workspace.mapPreviewNeeded) workspace.captureMapPreview(session, grid.previewImage()) })
    }
    onVisibleChanged: if (visible) capturePreview(workspace.currentSession)
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
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Radar"; font.bold: true; visible: !radarPane.compact }
            IconButton { objectName: "radarWorkspaceButton"; glyph: radarPane.expanded ? "Pod" : "Cluster"; text: radarPane.expanded ? "Show resource table" : "Focus resource radar"; onClicked: { workspace.setWorkspacePage("resources"); radarPane.focusRadar() } }
            Item { Layout.fillWidth: true }
            IconButton { objectName: "radarZoomOut"; glyph: "ZoomOut"; text: "Zoom radar out"; onClicked: grid.zoomAt(1/1.18, grid.width/2, grid.height/2) }
            Label { text: Math.round(grid.viewPose.zoom * 100) + "%"; visible: !radarPane.compact || radarPane.width >= 250 }
            IconButton { objectName: "radarZoom"; glyph: "ZoomIn"; text: "Zoom radar in"; onClicked: grid.zoomAt(1.18, grid.width/2, grid.height/2) }
            IconButton { objectName: "resetRadar"; glyph: "Reset"; text: "Reset radar view"; onClicked: grid.resetCamera() }
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
                text: (workspace.healthSummary.total || 0) + " cached / " + (workspace.healthSummary.healthy || 0) + " healthy / "
                    + (workspace.healthSummary.warning || 0) + " warnings / " + (workspace.healthSummary.critical || 0) + " errors"
                    + (workspace.loading ? " / Loading" : "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Accessible.name: text
            }
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: radarPane.detailsBeside ? 2 : 1
            columnSpacing: 8
            rowSpacing: 6
            RadarIsland {
                id: grid
                objectName: "resourceRadar"
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                function resetCamera() { resetView(); rememberPosition() }
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
                        radarInfo.target = null
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
                    if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) return
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_A) pan(24, 0)
                    else if (event.key === Qt.Key_Right || event.key === Qt.Key_D) pan(-24, 0)
                    else if (event.key === Qt.Key_Up || event.key === Qt.Key_W) pan(0, 24)
                    else if (event.key === Qt.Key_Down || event.key === Qt.Key_S) pan(0, -24)
                    else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) zoomAt(1.18, width/2, height/2)
                    else if (event.key === Qt.Key_Minus) zoomAt(1/1.18, width/2, height/2)
                    else if (event.key === Qt.Key_0) resetView()
                    else if (event.key === Qt.Key_Home) focusResource(count > 0 ? 0 : -1)
                    else if (event.key === Qt.Key_End) focusResource(count - 1)
                    else return
                    rememberPosition()
                    event.accepted = true
                }
                Keys.onReturnPressed: if (currentIndex >= 0) workspace.inspectRow(currentIndex)
                Keys.onEnterPressed: if (currentIndex >= 0) workspace.inspectRow(currentIndex)
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
                    onHoveredChanged: grid.updateHover()
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
                        if (wantsInfo) radarInfo.target=tile
                        if (radarInfo.target===tile) Qt.callLater(radarInfo.syncTarget)
                    }
                    onInViewportChanged: if (radarInfo.target===tile) Qt.callLater(radarInfo.syncTarget)
                    Component.onDestruction: if (radarInfo.target===tile) radarInfo.target=null
                    Keys.onTabPressed: function(event) {
                        const backwards = (event.modifiers & Qt.ShiftModifier) !== 0
                        event.accepted = !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) && grid.traverseVisible(backwards ? -1 : 1)
                        if (event.accepted) focusTarget.forceActiveFocus(backwards ? Qt.BacktabFocusReason : Qt.TabFocusReason)
                    }
                    Keys.onBacktabPressed: function(event) {
                        event.accepted = !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) && grid.traverseVisible(-1)
                        if (event.accepted) focusTarget.forceActiveFocus(Qt.BacktabFocusReason)
                    }
                    Keys.onReturnPressed: radarPane.inspectResource(resourcePath)
                    Keys.onEnterPressed: radarPane.inspectResource(resourcePath)
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
                    anchors.centerIn: parent
                    visible: grid.count === 0 && grid.height >= implicitHeight + 8
                    width: Math.min(320, parent.width - 24)
                    text: workspace.currentSession === "" ? "Import a kubeconfig and open a context." : workspace.loading ? "Loading resources. Cached resources appear as they arrive." : "No resources match. Reset the filters to see the available cache."
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Pane {
                id: radarInfo
                objectName: "radarInfoPanel"
                property var target: null
                readonly property bool hasDetails: !!target && target.visible && target.inViewport
                function syncTarget() {
                    if (target && (!target.visible || !target.inViewport)) target = null
                }
                onTargetChanged: Qt.callLater(syncTarget)
                Layout.fillWidth: !radarPane.detailsBeside
                Layout.minimumWidth: radarPane.detailsBeside ? 200 : 0
                Layout.preferredWidth: radarPane.detailsBeside ? Math.min(320, radarPane.availableWidth * 0.32) : -1
                Layout.fillHeight: radarPane.detailsBeside
                Layout.preferredHeight: radarPane.detailsBeside ? -1 : radarPane.infoPanelHeight
                padding: 8
                Accessible.role: Accessible.Grouping
                Accessible.name: "Cached resource details"
                background: Rectangle { color: workspace.appearanceColors.inset; border.color: workspace.appearanceColors.border }
                contentItem: ScrollView {
                    id: infoScroll
                    objectName: "radarInfoScroll"
                    contentWidth: availableWidth
                    contentHeight: infoContents.implicitHeight
                    activeFocusOnTab: radarInfo.hasDetails || activeFocus
                    clip: true
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
                        spacing: 10
                        Label {
                            objectName: "radarInfoText"
                            Layout.fillWidth: true
                            visible: radarInfo.hasDetails
                            text: radarInfo.target ? radarInfo.target.text : ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: workspace.appearanceColors.text
                            Accessible.name: text
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !radarInfo.hasDetails
                            text: "Hover or focus a resource."
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: workspace.appearanceColors.muted
                        }
                        Repeater {
                            model: radarInfo.hasDetails ? radarInfo.target.resourceMetrics : []
                            MetricGauge { required property var modelData; Layout.fillWidth: true; metric: modelData }
                        }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: !radarPane.compact; text: "Drag or arrow keys: pan. Scroll or trackpad: zoom. + / -: zoom. 0: reset. Home / End: focus resource. Enter: inspect."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    }
}
