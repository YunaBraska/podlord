import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Podlord.Graphics 1.0

Pane {
    id: radarPane
    property bool compact: false
    padding: compact ? 4 : 12
    function focusRadar() { grid.forceActiveFocus() }
    function focusPath(path, percent) {
        const index = workspace.alertResourceIndex(path)
        return index >= 0 && grid.focusResource(index, percent / 100)
    }
    PlainToolTip {
        id: radarTip
        objectName: "radarTooltip"
        width: Math.min(480, Math.max(1, grid.width - 24))
        contentItem: ColumnLayout {
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
    background: Rectangle { color: workspace.appearanceColors.radarGlass; border.color: workspace.appearanceColors.border }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Radar"; font.bold: true; visible: !radarPane.compact }
            IconButton { objectName: "radarWorkspaceButton"; glyph: "Cluster"; text: "Focus resource radar"; onClicked: { workspace.setWorkspacePage("resources"); radarPane.focusRadar() } }
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
            identityScope: workspace.currentSession
            readonly property int count: workspace.resourceCount
            readonly property real cellWidth: 7 * viewPose.zoom
            readonly property bool renderActive: visible && Window.window !== null && Window.window.visibility !== Window.Hidden && Window.window.visibility !== Window.Minimized
            clip: true
            activeFocusOnTab: true
            Accessible.name: "Resource radar"
            Accessible.description: "Deterministic resource island. Drag or use arrow keys to pan, wheel or plus and minus to zoom, Home and End to focus the first and last resource, Enter to inspect. Zero resets the map."
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
                color: workspace.appearanceColors.radarGlass
                visible: workspace.radarWaterEnabled && workspace.radarWaterSpeedPercent > 0
                speedPercent: workspace.radarWaterSpeedPercent
                playing: grid.renderActive && visible && !mapDrag.active && !workspace.alerts.reducedMotion
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
                required property string resourceName
                required property string resourceKind
                required property string resourceNamespace
                required property string resourceStatus
                required property var model
                readonly property var resourceMetrics: wantsTip ? model.resourceMetrics : []
                required property color statusColor
                required property int resourceHealth
                required property color terrainColor
                property var alertEffect: { workspace.alerts.revision; return workspace.alerts.effect(resourcePath) }
                readonly property bool inViewport: grid.renderActive && x + width > 0 && x < grid.width && y + height > 0 && y < grid.height
                property bool animate: inViewport && !workspace.alerts.reducedMotion && alertEffect.animation !== undefined
                property color alertColor: alertEffect.color === "status" ? resourceHealth === 2 ? "#FF5C5C" : resourceHealth === 1 ? "#FFE866" : terrainColor : alertEffect.color === "fresh" ? "#7DFFC3" : alertEffect.color ? alertEffect.color : terrainColor
                readonly property color announceColor: resourceHealth === 2 ? "#FF5C5C" : resourceHealth === 1 ? "#FFE866" : "#7DFFC3"
                objectName: "radarTile_" + resourceIndex
                width: 5.5 * grid.viewPose.zoom
                height: width
                x: grid.width/2 + (worldX + grid.viewPose.x)*grid.viewPose.zoom - width/2
                y: grid.height/2 + (worldY + grid.viewPose.y)*grid.viewPose.zoom - height/2
                activeFocusOnTab: true
                onActiveFocusChanged: if (activeFocus) grid.selectResource(resourceIndex)
                opacity: animate && motion.item && alertEffect.animation === "blink" ? motion.item.opacityFactor : 1
                scale: animate && motion.item && alertEffect.animation === "pulse" ? motion.item.scaleFactor : 1
                readonly property bool hovered: hover.hovered
                readonly property string text: resourceKind + " " + resourceName + ", " + (resourceNamespace || "Cluster-scoped") + ", " + resourceStatus
                    + (resourceHealth === 2 ? ", Error" : resourceHealth === 1 ? ", Warning" : alertEffect.color === "fresh" ? ", Recently changed" : "")
                Accessible.role: Accessible.Button
                Accessible.name: text
                Accessible.onPressAction: { grid.selectResource(resourceIndex); workspace.inspectRow(resourceIndex) }
                Accessible.description: resourcePath === workspace.inspectorPath ? "Selected resource" : "Open resource in inspector"
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    anchors.margins: -1.5
                    border.width: 2
                    border.color: tile.alertColor
                    opacity: 0.35
                    visible: tile.inViewport && tile.alertEffect.color !== undefined
                }
                Rectangle {
                    anchors.fill: parent
                    color: tile.alertColor
                    border.width: tile.hovered || tile.activeFocus || tile.resourcePath === workspace.inspectorPath || (grid.activeFocus && grid.currentIndex === tile.resourceIndex) ? 1.2 : 0
                    border.color: "#7DFFC3"
                }
                Loader {
                    anchors.fill: parent
                    active: tile.width >= 9
                    sourceComponent: KindGlyph {
                        objectName: "radarGlyph_" + tile.resourceIndex
                        kind: tile.resourceKind
                        fill: workspace.appearanceColors.inset
                        stroke: tile.alertColor
                    }
                }
                HoverHandler { id: hover }
                TapHandler { gesturePolicy: TapHandler.DragThreshold; onTapped: { grid.selectResource(tile.resourceIndex); grid.forceActiveFocus(); workspace.inspectRow(tile.resourceIndex) } }
                Loader {
                    id: motion
                    anchors.fill: parent
                    active: tile.animate
                    sourceComponent: Item {
                        property real opacityFactor: 1
                        property real scaleFactor: 1
                        SequentialAnimation on opacityFactor {
                            running: tile.alertEffect.animation === "blink"
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.35; duration: 350 }
                            NumberAnimation { to: 1; duration: 350 }
                        }
                        SequentialAnimation on scaleFactor {
                            running: tile.alertEffect.animation === "pulse"
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.8; duration: 400 }
                            NumberAnimation { to: 1; duration: 400 }
                        }
                        Rectangle {
                            anchors.fill: parent; anchors.margins: -1; color: "transparent"; border.color: tile.announceColor; border.width: 2
                            visible: tile.alertEffect.animation === "outline"
                        }
                        Rectangle {
                            id: sweep
                            width: Math.max(1, parent.width*.12); height: parent.height; color: tile.announceColor
                            visible: tile.alertEffect.animation === "sweep"
                            NumberAnimation on x { running: sweep.visible; from: 0; to: Math.max(0, tile.width-sweep.width); duration: 800; loops: Animation.Infinite }
                        }
                    }
                }
                readonly property bool wantsTip: inViewport && (hovered || activeFocus || (grid.activeFocus && grid.currentIndex === resourceIndex))
                readonly property string tipText: wantsTip ? text + (resourceMetrics.length ? "\n" + resourceMetrics.map(function(metric) { return metric.label + ": " + metric.usage + "\n" + metric.references + (metric.timestamp ? "\nMeasured " + metric.timestamp + " / " + metric.window : "") }).join("\n") : "") : ""
                onWantsTipChanged: if (wantsTip) radarTip.target=tile
                Component.onDestruction: if (radarTip.target===tile) radarTip.target=null
                }
            }
            Label {
                objectName: "emptyRadar"
                anchors.centerIn: parent
                visible: grid.count === 0
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
