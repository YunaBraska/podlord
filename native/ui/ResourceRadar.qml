import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Pane {
    id: radarPane
    property bool compact: false
    property bool expanded: false
    readonly property int count: workspace.resourceCount
    readonly property int navigationIndex: grid.currentIndex
    function navigateRows(offset) {
        if (count === 0) return
        const row = offset === 0 ? 0 : ((navigationIndex < 0 ? 0 : navigationIndex) + offset + count) % count
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
    PlainToolTip {
        id: radarTip
        objectName: "radarTooltip"
        parent: grid
        readonly property rect anchor: target ? Qt.rect(target.x, target.y, target.width, target.height) : Qt.rect(0, 0, 0, 0)
        readonly property real roomAbove: Math.max(0, anchor.y - 16)
        readonly property real roomBelow: Math.max(0, grid.height - anchor.y - anchor.height - 16)
        readonly property bool opensBelow: roomBelow >= roomAbove
        readonly property bool activeInteraction: tipHover.hovered || tipScroll.activeFocus
        visible: !!target && target.visible && target.inViewport && (target.wantsTip || activeInteraction)
        onClosed: if (target && !target.wantsTip) target = null
        width: Math.min(480, Math.max(1, grid.width - 24))
        height: Math.min(implicitHeight, Math.max(1, opensBelow ? roomBelow : roomAbove))
        x: Math.max(8, Math.min(Math.max(8, grid.width - width - 8), anchor.x + anchor.width / 2 - width / 2))
        y: Math.max(8, Math.min(Math.max(8, grid.height - height - 8), opensBelow ? anchor.y + anchor.height + 8 : anchor.y - height - 8))
        contentItem: ScrollView {
            id: tipScroll
            objectName: "radarTooltipScroll"
            implicitHeight: tipContents.implicitHeight
            contentWidth: availableWidth
            contentHeight: tipContents.implicitHeight
            activeFocusOnTab: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            HoverHandler { id: tipHover; margin: 16 }
            ColumnLayout {
                id: tipContents
                width: tipScroll.availableWidth
                spacing: 10
                Label {
                    objectName: "plainTipText"
                    Layout.fillWidth: true
                    text: radarTip.target ? radarTip.target.text : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: radarTip.palette.toolTipText
                    Accessible.name: text
                }
                Repeater {
                    model: radarTip.visible && radarTip.target ? radarTip.target.resourceMetrics : []
                    MetricGauge { required property var modelData; Layout.fillWidth: true; metric: modelData }
                }
            }
        }
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
            IconButton { objectName: "resetRadar"; glyph: "Reset"; text: "Reset radar view"; onClicked: grid.resetView() }
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
        RadarIsland {
            id: grid
            objectName: "resourceRadar"
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: workspace.table
            alerts: workspace.alerts
            glyphFill: workspace.appearanceColors.inset
            inspectedPath: workspace.inspectorPath
            hoveredPath: radarTip.target && radarTip.target.hovered && radarTip.target.wantsTip ? radarTip.target.resourcePath : ""
            focusedPath: {
                const item = Window.window ? Window.window.activeFocusItem : null
                return item && item.parent === grid && item.resourcePath !== undefined ? item.resourcePath : ""
            }
            identityScope: workspace.currentSession
            textColor: Qt.tint("#DDE7EC", Qt.rgba(radarPane.palette.text.r, radarPane.palette.text.g, radarPane.palette.text.b, 0.2))
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
            function restorePosition() {
                if (!visible) return
                restoring = true
                if (shownSession !== workspace.currentSession) selectResource(-1)
                shownSession = workspace.currentSession
                viewPose = workspace.radarView
                restoring = false
            }
            onVisibleChanged: if (visible) Qt.callLater(restorePosition)
            onViewPoseChanged: if (visible && !restoring && shownSession === workspace.currentSession) workspace.rememberRadarView(viewPose)
            Component.onCompleted: restorePosition()
            Connections {
                target: workspace
                function onChanged() {
                    if (grid.visible && grid.shownSession !== workspace.currentSession) grid.restorePosition()
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
                event.accepted = true
            }
            Keys.onReturnPressed: if (currentIndex >= 0) workspace.inspectRow(currentIndex)
            Keys.onEnterPressed: if (currentIndex >= 0) workspace.inspectRow(currentIndex)
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
            TapHandler { onTapped: grid.forceActiveFocus() }
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
            Repeater {
                model: grid.tiles
                delegate: Item {
                id: tile
                required property int resourceIndex
                required property real worldX
                required property real worldY
                required property string resourcePath
                required property var model
                readonly property bool detailsNeeded: wantsTip || radarTip.target === tile
                readonly property var resourceMetrics: detailsNeeded ? model.resourceMetrics : []
                readonly property bool inViewport: grid.renderActive && x + width > 0 && x < grid.width && y + height > 0 && y < grid.height
                objectName: "radarTile_" + resourceIndex
                width: 5.5 * grid.viewPose.zoom
                height: width
                x: grid.width/2 + (worldX + grid.viewPose.x)*grid.viewPose.zoom - width/2
                y: grid.height/2 + (worldY + grid.viewPose.y)*grid.viewPose.zoom - height/2
                activeFocusOnTab: true
                onActiveFocusChanged: if (activeFocus) grid.selectResource(resourceIndex)
                scale: model.animationScale
                readonly property bool hovered: hover.hovered
                readonly property string text: {
                    if (!detailsNeeded) return ""
                    workspace.alerts.revision
                    const effect = workspace.alerts.effect(resourcePath)
                    const health = model.resourceHealth
                    return model.resourceKind + " " + model.resourceName + ", " + (model.resourceNamespace || "Cluster-scoped") + ", " + model.resourceStatus
                        + (health === 2 ? ", Error" : health === 1 ? ", Warning" : effect.color === "fresh" ? ", Recently changed" : "")
                }
                Accessible.role: Accessible.Button
                Accessible.ignored: !detailsNeeded
                Accessible.name: text
                Accessible.onPressAction: { grid.selectResource(resourceIndex); workspace.inspectRow(resourceIndex) }
                Accessible.description: detailsNeeded ? (resourcePath === grid.inspectedPath ? "Selected resource" : "Open resource in inspector") : ""
                HoverHandler { id: hover }
                TapHandler { gesturePolicy: TapHandler.DragThreshold; onTapped: { grid.selectResource(tile.resourceIndex); grid.forceActiveFocus(); workspace.inspectRow(tile.resourceIndex) } }
                readonly property bool wantsTip: inViewport && (hovered || activeFocus || (grid.activeFocus && grid.currentIndex === resourceIndex))
                readonly property string tipText: detailsNeeded ? text + (resourceMetrics.length ? "\n" + resourceMetrics.map(function(metric) { return metric.label + ": " + metric.usage + "\n" + metric.references + (metric.timestamp ? "\nMeasured " + metric.timestamp + " / " + metric.window : "") }).join("\n") : "") : ""
                onWantsTipChanged: if (wantsTip) radarTip.target=tile
                Component.onDestruction: if (radarTip.target===tile) radarTip.target=null
                }
            }
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
        Label { Layout.fillWidth: true; visible: !radarPane.compact; text: "Drag or arrow keys: pan. Scroll or trackpad: zoom. + / -: zoom. 0: reset. Home / End: focus resource. Enter: inspect."; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    }
}
