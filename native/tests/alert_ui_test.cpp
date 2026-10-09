#include "workspace.h"
#include "ui_input.h"
#include "browser_boundary.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QImage>
#include <QClipboard>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QPointer>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QLockFile>
#include <algorithm>
#include <cstdio>

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout=9000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) QTest::qWait(10);
    return ready();
}
QQuickItem* visualItem(QQuickItem* root, const QString& name) {
    if (root->objectName()==name) return root;
    for (auto* child : root->childItems()) if (auto* found=visualItem(child, name)) return found;
    return nullptr;
}
QQuickItem* item(QObject* root, const QString& name) {
    auto* window=qobject_cast<QQuickWindow*>(root);
    if (!window) return nullptr;
    if (auto* found = podlord::test::visibleItem(window->contentItem(), name)) return found;
    if (auto* found = visualItem(window->contentItem(), name)) return found;
    return window->findChild<QQuickItem*>(name);
}
bool click(QQuickWindow* window, const QString& name) {
    if (!podlord::test::revealWorkspaceAction(window, name)) return false;
    if (name=="radarWorkspaceButton") {
        auto* target=item(window,name);
        if (target && !target->isVisible()) {
            if (!click(window,"toggleSidebar") || !waitFor([&] { return target->isVisible(); })) return false;
        }
    }
    if (name=="alertsWorkspaceButton") {
        auto* target=item(window,name);
        if ((!target || !target->isVisible()) && !click(window,"settingsWorkspaceButton")) return false;
    }
    if (!waitFor([&] { auto* target=item(window,name); return target && target->isVisible() && target->isEnabled() && target->width()>0 && target->height()>0; })) {
        std::fprintf(stderr,"Unavailable click target: %s\n",qPrintable(name)); return false;
    }
    auto* target = item(window, name);
    if (!target || !target->isVisible() || !target->isEnabled() || target->width()<=0 || target->height()<=0) { std::fprintf(stderr, "Unavailable click target: %s\n", qPrintable(name)); return false; }
    if (!podlord::test::scrollIntoView(window,target)) return false;
    if (!waitFor([&] {
        for (auto* parent=target->parentItem(); parent; parent=parent->parentItem())
            if (parent->property("moving").toBool()) return false;
        return true;
    })) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint());
    return true;
}
bool type(QQuickWindow* window, const QString& name, const QString& text) {
    if (name == "resourceFilter") {
        const auto* target = item(window, name);
        if ((!target || !target->isVisible()) && !click(window, "workspaceSearchButton")) return false;
    }
    if (!waitFor([&] { const auto* target=item(window, name); return target && target->isVisible() && target->isEnabled(); })) {
        std::fprintf(stderr, "Unavailable typing target: %s\n", qPrintable(name));
        const auto capture=qEnvironmentVariable("PODLORD_ALERT_FAILURE_FRAME");
        if (!capture.isEmpty()) window->grabWindow().save(capture);
        return false;
    }
    auto* target = item(window, name);
    if (!podlord::test::scrollIntoView(window,target)) return false;
    target->forceActiveFocus(); QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    for (const auto ch : text) QTest::keyClick(window, ch.toLatin1());
    return true;
}
bool radarGaugesFit(const QString& expectedUsage = {}, const QString& expectedReference = {}) {
    for (auto* surface : QGuiApplication::allWindows()) if (auto* quick=qobject_cast<QQuickWindow*>(surface)) {
        auto* heading=item(quick,"plainTipText");
        if (!heading || !heading->isVisible() || !heading->parentItem()) continue;
        auto* content=heading->parentItem();
        const auto* bar=visualItem(content,"metric_cpu_bar"); const auto* memory=visualItem(content,"metric_memory_bar");
        const auto* request=visualItem(content,"metric_cpu_marker_request"); const auto* limit=visualItem(content,"metric_cpu_marker_limit");
        const auto* usage=visualItem(content,"metric_cpu_usage"); const auto* references=visualItem(content,"metric_cpu_references");
        const auto* measured=visualItem(content,"metric_cpu_measurement"); const auto* storage=visualItem(content,"metric_storage_measurement");
        if (!bar || !bar->isVisible() || !memory || !memory->isVisible() || !usage || !usage->isVisible() || !request || !limit || !references || !measured || !storage) continue;
        const auto observed=usage->property("text").toString();
        if (!observed.contains("mCPU") || observed.contains("Unavailable") || !observed.contains(expectedUsage)
            || !references->property("text").toString().contains(expectedReference)) continue;
        const auto markerFits=[&](const QQuickItem* marker) { const auto x=marker->mapToItem(bar,QPointF{}).x(); return marker->isVisible() && x>=0 && x+marker->width()<=bar->width()+1; };
        const auto usageLeft=usage->mapToItem(bar,QPointF{}).x();
        return bar->width()>0 && memory->width()>0 && markerFits(request) && markerFits(limit)
            && usageLeft>=0 && usageLeft+usage->width()<=bar->width()+1
            && measured->property("text").toString().contains("Measured ")
            && storage->property("text").toString().contains("No observed measurement is available.")
            && !storage->property("text").toString().contains("Measured ");
    }
    return false;
}
// Only the external Kubernetes API is simulated. Application, stores and QML are real.
class Kubernetes final : public QTcpServer {
public:
    int requests = 0;
    int externalImageRequests = 0;
    QString phase = "Running";
    QString metricScenario;
    QString healthScenario;
    QString radarScenario;
    bool secondFailed = false;
    bool holdPodResponses = false;
    QList<QPair<QPointer<QTcpSocket>, QByteArray>> heldPodResponses;
    const QDateTime created=QDateTime::currentDateTimeUtc().addSecs(-3600);
    const QDateTime measured=QDateTime::currentDateTimeUtc();
    Kubernetes() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection(); auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll()); if (!bytes->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++requests;
                    const auto path = QUrl::fromEncoded(bytes->split(' ')[1]).path();
                    if (path=="/external-image") ++externalImageRequests;
                    QJsonObject body;
                    if (path == "/api") body = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") body = {{"groups", metricScenario.isEmpty() || metricScenario=="metric_empty" ? QJsonArray{} : QJsonArray{QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "metrics.k8s.io/v1beta1"}}}}}}};
                    else if (path == "/api/v1") {
                        const bool event=healthScenario.startsWith("health_event_");
                        const bool claim=healthScenario.startsWith("health_pvc_");
                        body = {{"resources", QJsonArray{QJsonObject{{"name", event ? "events" : claim ? "persistentvolumeclaims" : "pods"}, {"kind", event ? "Event" : claim ? "PersistentVolumeClaim" : "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                        if (radarScenario=="radar_loading_partial") {
                            auto resources=body["resources"].toArray();
                            resources.append(QJsonObject{{"name", "configmaps"}, {"kind", "ConfigMap"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}});
                            body["resources"]=resources;
                        }
                    }
                    else if (path == "/apis/metrics.k8s.io/v1beta1") body={{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "PodMetrics"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else if (path == "/apis/metrics.k8s.io/v1beta1/pods") {
                        const QJsonObject sample{{"apiVersion", "metrics.k8s.io/v1beta1"}, {"kind", "PodMetrics"}, {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", "default"}}},
                            {"timestamp", measured.addSecs(metricScenario=="metric_stale" ? -26 : 0).toString(Qt::ISODateWithMs)}, {"window", "15s"},
                            {"containers", QJsonArray{QJsonObject{{"name", "worker"}, {"usage", QJsonObject{{"cpu", metricScenario=="metric_zero" ? "0" : metricScenario=="metric_invalid" ? "-1m" : "25m"}, {"memory", "32Mi"}}}}}}};
                        body={{"metadata", QJsonObject{}}, {"items", QJsonArray{sample}}};
                    }
                    else {
                        const QJsonObject container{{"name", "worker"}, {"ready", true}, {"restartCount", 2}, {"state", QJsonObject{{"running", QJsonObject{}}}}};
                        const QJsonObject resources{{"requests", QJsonObject{{"cpu", "10m"}, {"memory", "16Mi"}}}, {"limits", QJsonObject{{"cpu", "100m"}, {"memory", "64Mi"}}}};
                        const auto image=metricScenario=="metric_tooltip" ? QString("<img src=\"http://127.0.0.1:%1/external-image\"> untrusted resource value").arg(serverPort()) : QString(80, 'a')+"!";
                        const QJsonObject configured{{"name", "worker"}, {"image", image}, {"resources", resources}};
                        QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"},
                            {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", "default"}, {"uid", "uid-alpha"}, {"resourceVersion", phase}, {"creationTimestamp", created.toString(Qt::ISODateWithMs)}}},
                            {"spec", QJsonObject{{"containers", metricScenario=="metric_partial" ? QJsonArray{configured, QJsonObject{{"name", "missing"}}} : QJsonArray{configured}}}},
                            {"status", QJsonObject{{"phase", phase}, {"containerStatuses", QJsonArray{container}}}}};
                        if (healthScenario.startsWith("health_event_")) {
                            pod["kind"]="Event"; pod.remove("spec"); pod.remove("status");
                            pod["type"]=healthScenario=="health_event_normal" ? "Normal" : "Warning";
                            pod["reason"]="Scheduled"; pod["message"]="External Kubernetes test event";
                            if (healthScenario=="health_event_missing_time" || healthScenario=="health_event_invalid_time") {
                                auto metadata=pod["metadata"].toObject(); metadata["creationTimestamp"]=measured.addSecs(-60).toString(Qt::ISODateWithMs); pod["metadata"]=metadata;
                            } else pod["lastTimestamp"]=measured.addSecs(healthScenario=="health_event_old_warning" ? -3601 : healthScenario=="health_event_future" ? 60 : -60).toString(Qt::ISODateWithMs);
                            if (healthScenario=="health_event_expiry") pod["lastTimestamp"]=QDateTime::currentDateTimeUtc().addSecs(-1793).toString(Qt::ISODateWithMs);
                            if (healthScenario=="health_event_future_activation") pod["lastTimestamp"]=QDateTime::currentDateTimeUtc().addSecs(7).toString(Qt::ISODateWithMs);
                            if (healthScenario=="health_event_invalid_time" || healthScenario=="health_event_invalid_dates") pod["lastTimestamp"]="invalid";
                            if (healthScenario=="health_event_invalid_dates") {
                                auto metadata=pod["metadata"].toObject(); metadata["creationTimestamp"]="invalid"; pod["metadata"]=metadata;
                            }
                        } else if (healthScenario.startsWith("health_pvc_")) {
                            pod["kind"]="PersistentVolumeClaim";
                            pod["spec"]=QJsonObject{{"resources", QJsonObject{{"requests", QJsonObject{{"storage", "1Gi"}}}}}};
                            pod["status"]=QJsonObject{{"phase", healthScenario=="health_pvc_pending" ? "Pending" : "Bound"}};
                        } else if (healthScenario.startsWith("health_pod_")) {
                            auto unready=container; unready["ready"]=false; unready["restartCount"]=0;
                            pod["status"]=QJsonObject{{"phase", healthScenario=="health_pod_completed" ? "Succeeded" : healthScenario=="health_pod_starting" || healthScenario=="health_pod_grace_expiry" ? "Pending" : "Running"}, {"containerStatuses", QJsonArray{unready}}};
                            if (healthScenario=="health_pod_starting") {
                                auto metadata=pod["metadata"].toObject(); metadata["creationTimestamp"]=measured.addSecs(-5).toString(Qt::ISODateWithMs); pod["metadata"]=metadata;
                            }
                            if (healthScenario=="health_pod_grace_expiry") {
                                auto metadata=pod["metadata"].toObject(); metadata["creationTimestamp"]=QDateTime::currentDateTimeUtc().addSecs(-293).toString(Qt::ISODateWithMs); pod["metadata"]=metadata;
                            }
                        }
                        body={{"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}};
                        if (radarScenario.startsWith("radar_focus_")) {
                            auto second=pod; auto metadata=second["metadata"].toObject();
                            metadata["name"]="zeta"; metadata["uid"]="uid-zeta"; metadata["resourceVersion"]=secondFailed ? "2" : "1";
                            second["metadata"]=metadata;
                            second["status"]=QJsonObject{{"phase", secondFailed ? "Failed" : "Running"}, {"containerStatuses", QJsonArray{container}}};
                            body["items"]=QJsonArray{pod, second};
                        }
                        if (radarScenario=="radar_loading_partial" && path=="/api/v1/configmaps") {
                            pod["kind"]="ConfigMap"; pod.remove("spec"); pod.remove("status");
                            auto metadata=pod["metadata"].toObject(); metadata["uid"]="uid-configmap"; pod["metadata"]=metadata;
                            body["items"]=QJsonArray{pod};
                        }
                        if (healthScenario.startsWith("health_restart_")) {
                            QJsonArray rows;
                            for (int i=0; i<6; ++i) {
                                auto extra=pod; auto metadata=extra["metadata"].toObject();
                                if (i>0) { metadata["name"]="population-"+QString::number(i); metadata["uid"]="uid-population-"+QString::number(i); }
                                extra["metadata"]=metadata; auto restarted=container;
                                restarted["restartCount"]=i==5 ? 100 : healthScenario=="health_restart_population" ? 4 : i==0 ? 8 : 0;
                                extra["status"]=QJsonObject{{"phase", i==5 ? "Failed" : "Running"}, {"containerStatuses", QJsonArray{restarted}}}; rows.append(extra);
                            }
                            body["items"]=rows;
                        }
                        if (metricScenario=="metric_many") {
                            auto rows=body["items"].toArray();
                            for (int i=1; i<5000; ++i) {
                                auto extra=pod; auto metadata=extra["metadata"].toObject();
                                metadata["name"]="load-"+QString::number(i); metadata["uid"]="uid-load-"+QString::number(i); extra["metadata"]=metadata; rows.append(extra);
                            }
                            body["items"]=rows;
                        }
                    }
                    const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
                    if (holdPodResponses && path == "/api/v1/pods") {
                        heldPodResponses.append({socket, payload});
                        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                        return;
                    }
                    if (radarScenario=="radar_loading_partial" && path=="/api/v1/configmaps") {
                        QTimer::singleShot(1500, socket, [socket, payload] {
                            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                            socket->disconnectFromHost();
                        });
                        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                        return;
                    }
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    socket->disconnectFromHost(); connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                });
            }
        });
    }
    bool releasePodResponses() {
        const bool released = !heldPodResponses.isEmpty();
        for (const auto& [socket, payload] : heldPodResponses) if (socket) {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
            socket->disconnectFromHost();
        }
        heldPodResponses.clear();
        holdPodResponses = false;
        return released;
    }
};
bool run(const QString& scenario, const QString& realConfig={}, const QString& capturePath={}) {
    QTemporaryDir temporary; if (!temporary.isValid()) return false;
    Kubernetes server; if (realConfig.isEmpty() && !server.listen(QHostAddress::LocalHost, 0)) return false;
    if (scenario.startsWith("metric_")) server.metricScenario=scenario;
    if (scenario.startsWith("health_")) server.healthScenario=scenario;
    if (scenario.startsWith("radar_")) {
        server.radarScenario=scenario;
        if (scenario=="radar_health_warning") server.healthScenario="health_pod_not_ready";
        if (scenario=="radar_health_restarts") server.healthScenario="health_restart_outlier";
        if (scenario=="radar_health_critical" || scenario=="radar_refresh_preserves_alarm" || scenario.startsWith("radar_focus_")) server.phase="Failed";
    }
    if (scenario=="metric_radar_tooltip" || scenario=="metric_radar_gauges" || scenario=="metric_radar_gauges_narrow") server.phase=QString("<img src=\"http://127.0.0.1:%1/external-image\"> untrusted status").arg(server.serverPort());
    if (scenario == "reference_zoom_preview_unsaved") server.radarScenario = "radar_focus_preview";
    if (scenario == "reference_zoom_preview_many") server.metricScenario = "metric_many";
    const auto profile = temporary.filePath("profile");
    podlord::Workspace workspace(profile);
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml")); if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return window->isVisible() && !workspace.busy(); })) return false;
    if (!click(window, "alertsWorkspaceButton")) return false;
    if (!waitFor([&] { return item(window, "addAlert") && item(window, "addAlert")->isVisible(); })) return false;
    const auto loadSession = [&] {
        QFile source(temporary.filePath("source.config"));
        if (realConfig.isEmpty()) {
            if (!source.open(QIODevice::WriteOnly)) return false;
            source.write("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:" + QByteArray::number(server.serverPort()) + "'}\nusers:\n- name: local\n  user: {token: local-test}\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n- name: second\n  context: {cluster: local, user: local}\n"); source.close();
        }
        return workspace.importFile(realConfig.isEmpty() ? source.fileName() : realConfig) && waitFor([&] { return !workspace.busy() && !workspace.contexts().isEmpty(); })
            && workspace.openContext(workspace.contexts().first().toMap()["id"].toString())
            && waitFor([&] { return realConfig.isEmpty() ? scenario=="radar_loading_partial" ? workspace.totalResourceCount()==1 && workspace.loading()
                : workspace.totalResourceCount() == (scenario=="metric_many" || scenario=="reference_zoom_preview_many" ? 5000 : scenario.startsWith("health_restart_") || scenario=="radar_health_restarts" ? 6
                    : scenario.startsWith("radar_focus_") || scenario=="reference_zoom_preview_unsaved" ? 2 : 1) && !workspace.loading()
                : workspace.totalResourceCount()>=1051; }, realConfig.isEmpty() ? 9000 : 180000);
    };
    if (scenario.startsWith("reference_")) {
        const auto rules = [&] { return workspace.alerts()->rules(); };
        const auto settle = [&] { return waitFor([&] { return !workspace.alerts()->busy(); }); };
        if (!waitFor([&] { return item(window,"alertName") && item(window,"alertName")->isVisible(); })) return false;
        const auto selected = [&] { return item(window,"alertName")->property("text").toString(); };
        const auto baseline = rules();
        if (selected() != baseline.first().toMap()["name"].toString()) return false;
        if (scenario.startsWith("reference_controls_")) {
            const auto mode = scenario.mid(QString("reference_controls_").size());
            const auto choose = [&](const QString& control, int index) {
                if (!click(window, control)) return false;
                QTest::keyClick(window, Qt::Key_Home);
                for (int i = 0; i < index; ++i) QTest::keyClick(window, Qt::Key_Down);
                QTest::keyClick(window, Qt::Key_Return);
                return waitFor([&] { return item(window, control)->property("currentIndex").toInt() == index; });
            };
            if (item(window, "sourceManagementPanel")->isVisible()) return false;
            if (mode == "locked")
                return !item(window, "alertColorChoice")->isEnabled() && !item(window, "alertChooseColor")->isEnabled()
                    && !item(window, "alertColorMode")->isEnabled() && rules() == baseline && server.requests == 0;
            if (!click(window, "addAlert") || !type(window, "alertName", "Control regression") || !type(window, "alertExpression_0", "alpha")) return false;
            QString expectedColor = "#e3aa46", expectedMode = "no-match", expectedField = "name", expectedExpression = "alpha";
            if (mode.startsWith("boolean")) {
                if (!choose("alertField_0", workspace.alerts()->fields().indexOf("problems"))) return false;
                expectedField = "problems"; expectedExpression = mode == "boolean_invalid" ? "false" : "true";
                if (mode == "boolean_invalid") {
                    if (!click(window, "saveAlert") || workspace.alerts()->error().isEmpty() || rules() != baseline
                        || item(window, "alertBoolean_0")->property("currentIndex").toInt() != -1
                        || item(window, "alertExpression_0")->property("text").toString() != "alpha") return false;
                }
                if (!choose("alertBoolean_0", expectedExpression == "true" ? 0 : 1)) return false;
                if (item(window, "alertExpression_0")->isVisible()) return false;
            } else if (mode == "color_status") {
                if (!choose("alertColorChoice", 1)) return false;
                expectedColor = "status";
            } else if (mode == "color_custom" || mode == "color_cancel" || mode == "narrow") {
                if (mode == "narrow") window->resize(360, 600);
                if (!choose("alertColorChoice", 3) || !type(window, "alertColor", "#124abc")) return false;
                expectedColor = "#124abc";
                if (mode == "color_cancel") {
                    if (!click(window, "alertChooseColor")) return false;
                    auto* dialog = window->findChild<QObject*>("alertColorDialog");
                    if (!dialog || !waitFor([&] { return dialog->property("visible").toBool(); })) return false;
                    QTest::keyClick(QGuiApplication::focusWindow(), Qt::Key_Escape);
                    if (!waitFor([&] { return !dialog->property("visible").toBool(); })) {
                        std::fprintf(stderr, "Color dialog stayed open after Escape.\n"); return false;
                    }
                    if (!waitFor([&] { return item(window, "alertColor")->property("text").toString() == expectedColor; })) return false;
                }
                if (mode == "narrow") {
                    for (const auto& controlName : {"alertColorChoice", "alertColor", "alertChooseColor", "alertColorMode"}) {
                        auto* control = item(window, controlName);
                        if (!control || !podlord::test::scrollIntoView(window, control)) return false;
                        const auto bounds = control->mapRectToScene({0, 0, control->width(), control->height()});
                        if (bounds.left() < 0 || bounds.right() > window->width() || bounds.top() < 0 || bounds.bottom() > window->height()) {
                            std::fprintf(stderr, "Narrow alert control outside window: %s (%g,%g %gx%g) window=%dx%d scene=%g\n", controlName, bounds.x(), bounds.y(), bounds.width(), bounds.height(), window->width(), window->height(), window->contentItem()->width());
                            return false;
                        }
                    }
                }
            } else if (mode == "hold") {
                if (!choose("alertColorMode", 1) || !choose("alertAnimationMode", 2)) return false;
                expectedMode = "duration";
            } else if (mode == "once") {
                if (!choose("alertColorMode", 3) || !choose("alertAnimationMode", 3)
                    || item(window, "alertColorSeconds")->isVisible() || item(window, "alertAnimationSeconds")->isVisible()) return false;
                expectedMode = "once";
            } else return false;
            if (!click(window, "saveAlert") || !settle() || rules().size() != baseline.size() + 1) return false;
            const auto rule = rules().last().toMap();
            const auto criterion = rule["groups"].toList().first().toList().first().toMap();
            if (rule["color"] != expectedColor || rule["colorMode"] != expectedMode || criterion["field"] != expectedField
                || criterion["expression"] != expectedExpression || (mode == "hold" && rule["animationMode"] != "new-in-view")
                || (mode == "once" && rule["animationMode"] != "once")) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.alerts()->busy(); }) && restored.alerts()->rules() == rules() && server.requests == 0;
        }
        if (scenario.startsWith("reference_zoom_preview_")) {
            const bool noSession = scenario.endsWith("_no_session");
            if (!noSession && !loadSession()) return false;
            if (!click(window, "alertsWorkspaceButton")) return false;
            const bool invalid = scenario.endsWith("_invalid"), empty = scenario.endsWith("_empty");
            const bool custom = scenario.endsWith("_unsaved") || scenario.endsWith("_disabled") || scenario.endsWith("_fallback") || scenario.endsWith("_percent") || invalid;
            if (custom && (!click(window, "addAlert") || !type(window, "alertName", "Unsaved preview")
                || !type(window, "alertExpression_0", invalid ? "/[broken/" : scenario.endsWith("_fallback") ? "no-match" : scenario.endsWith("_unsaved") ? "zeta" : "alpha"))) return false;
            if (scenario.endsWith("_disabled") && !click(window, "alertEnabled")) return false;
            if (scenario.endsWith("_percent")) {
                auto* zoom = item(window, "alertZoom");
                if (!zoom || !podlord::test::scrollIntoView(window, zoom)) return false;
                auto* input = zoom->property("contentItem").value<QQuickItem*>();
                if (!input) return false;
                input->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll);
            QTest::keyClick(window, Qt::Key_1);
            QTest::keyClick(window, Qt::Key_7);
            QTest::keyClick(window, Qt::Key_5);
            QTest::keyClick(window, Qt::Key_Tab);
            }
            if (empty && !workspace.filter("no-visible-target")) return false;
            if (scenario.endsWith("_narrow")) { window->setWidth(720); window->setHeight(720); }
            if (scenario.endsWith("_hidden") && !click(window, "toggleSidebar")) return false;
            QSignalSpy previews(workspace.alerts(), SIGNAL(zoomPreviewReady(QString,QString,int)));
            QSignalSpy sounds(workspace.alerts(), &podlord::Alerts::soundRequested);
            const auto requests = server.requests;
            QGuiApplication::clipboard()->setText("unchanged clipboard");
            if (scenario.endsWith("_busy") || scenario.endsWith("_scope_reset") || scenario.endsWith("_refresh_in_flight")
                || scenario.endsWith("_invalid_input") || scenario.endsWith("_regex_limit")) {
                auto draft = baseline.first().toMap();
                if (scenario.endsWith("_invalid_input")) draft.clear();
                if (scenario.endsWith("_regex_limit")) draft["groups"] = QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "image"}, {"expression", "/(*NO_START_OPT)(a+)+$/"}}})}.toVariantList();
                const bool accepted = workspace.previewAlertZoom(draft);
                if (scenario.endsWith("_invalid_input")) return !accepted && !workspace.alerts()->zoomPreviewError().isEmpty() && previews.isEmpty() && server.requests == requests;
                if (!accepted) return false;
                if (scenario.endsWith("_busy")) {
                    if (workspace.previewAlertZoom(draft) || !waitFor([&] { return !workspace.alerts()->zoomPreviewBusy(); })) return false;
                    return previews.count() == 1 && sounds.isEmpty() && server.requests == requests && rules() == baseline;
                }
                if (scenario.endsWith("_scope_reset")) workspace.alerts()->showSession("");
                if (scenario.endsWith("_refresh_in_flight") && !workspace.refresh()) return false;
                if (!waitFor([&] { return !workspace.alerts()->zoomPreviewBusy(); })) return false;
                return previews.isEmpty() && sounds.isEmpty() && rules() == baseline && !QFile::exists(profile + "/alert-rules.json")
                    && (scenario.endsWith("_scope_reset") ? workspace.alerts()->zoomPreviewError().isEmpty()
                        : !workspace.alerts()->zoomPreviewError().isEmpty());
            }
            if (scenario.endsWith("_keyboard")) {
                auto* button = item(window, "previewAlertZoom");
                if (!button || !podlord::test::scrollIntoView(window, button)) return false;
                button->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(button->window(), Qt::Key_Space);
            } else if (!click(window, "previewAlertZoom")) return false;
            if (invalid || empty || noSession) {
                return waitFor([&] { auto* error = item(window, "alertZoomPreviewError"); return error && error->isVisible() && !error->property("text").toString().isEmpty(); })
                    && previews.isEmpty() && rules() == baseline && server.requests == requests && !QFile::exists(profile + "/alert-rules.json");
            }
            if (!waitFor([&] { return previews.count() == 1; })) {
                std::fprintf(stderr, "Zoom preview did not complete: count=%lld busy=%d error=%s\n", static_cast<long long>(previews.count()), workspace.alerts()->zoomPreviewBusy(), qPrintable(workspace.alerts()->zoomPreviewError()));
                return false;
            }
            auto* radar = item(window, "resourceRadar");
            const auto path = previews.first()[1].toString();
            const auto expected = scenario.endsWith("_unsaved") ? "zeta" : "alpha";
            const int percent = scenario.endsWith("_percent") ? 175 : 100;
            if (!waitFor([&] { return radar && radar->isVisible() && radar->property("currentIndex").toInt() == workspace.alertResourceIndex(path)
                && radar->property("viewPose").toMap()["zoom"].toDouble() == percent / 100.0; })) {
                std::fprintf(stderr, "Zoom preview target not focused: radar=%d visible=%d index=%d expected=%d zoom=%g expected=%g\n", radar != nullptr, radar && radar->isVisible(), radar ? radar->property("currentIndex").toInt() : -1, workspace.alertResourceIndex(path), radar ? radar->property("viewPose").toMap()["zoom"].toDouble() : -1, percent / 100.0);
                return false;
            }
            if (scenario.endsWith("_repeat") && (!click(window, "previewAlertZoom") || !waitFor([&] { return previews.count() == 2; }))) return false;
            if (scenario.endsWith("_many")) {
                QList<double> times;
                auto* button = item(window, "previewAlertZoom");
                if (!button || !podlord::test::scrollIntoView(button->window(), button)) return false;
                for (int sample = 0; sample < 20; ++sample) {
                    const auto count = previews.count(); QElapsedTimer timer; timer.start();
                    QTest::mouseClick(button->window(), Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
                    if (!waitFor([&] { return previews.count() == count + 1; })) return false;
                    QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
                    if (!frames.wait(1000)) return false;
                    times.append(timer.nsecsElapsed() / 1000000.0);
                }
                std::sort(times.begin(), times.end());
                std::printf("Zoom preview UI: 5000 cached resources, 20 input-to-frame samples, p50=%.3f ms p95=%.3f ms max=%.3f ms; no preview transport.\n", (times[9]+times[10])/2, times[18], times.last());
            }
            const bool accepted = path.endsWith(QString("/") + expected) && previews.first()[2].toInt() == percent && sounds.isEmpty()
                && rules() == baseline && server.requests == requests && !QFile::exists(profile + "/alert-rules.json")
                && QGuiApplication::clipboard()->text() == "unchanged clipboard" && (!custom || selected() == "Unsaved preview");
            if (!accepted) std::fprintf(stderr, "Zoom preview side effects: path=%s expected=%s percent=%d sounds=%lld rules-changed=%d requests=%d baseline=%d persisted=%d clipboard-changed=%d selected=%s\n", qPrintable(path), qPrintable(expected), previews.first()[2].toInt(), static_cast<long long>(sounds.count()), rules() != baseline, server.requests, requests, QFile::exists(profile + "/alert-rules.json"), QGuiApplication::clipboard()->text() != "unchanged clipboard", qPrintable(selected()));
            return accepted;
        }
        if (scenario=="reference_layout" || scenario=="reference_layout_narrow" || scenario=="reference_layout_light") {
            if (scenario=="reference_layout_narrow") { window->setWidth(720); window->setHeight(720); }
            if (scenario=="reference_layout_light" && (!workspace.saveAppearance(workspace.themeName(),"light") || !waitFor([&] { return !workspace.busy(); }))) return false;
            QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update(); if (!frames.wait(2000)) return false;
            const auto* table=item(window,"alertRulesTable"); const auto* editor=item(window,"alertEditor"); const auto* add=item(window,"addAlert");
            if (!table || !editor || table->width()<=0 || editor->width()<=0 || table->mapToScene({0,table->height()}).y()>add->mapToScene({0,0}).y()+1 || add->mapToScene({0,add->height()}).y()>editor->mapToScene({0,0}).y()+1) return false;
            const auto capture=qEnvironmentVariable("PODLORD_ALERT_LAYOUT_FRAME");
            return server.requests==0 && (capture.isEmpty() || window->grabWindow().save(capture));
        }
        if (scenario=="reference_scroll" || scenario=="reference_scroll_narrow") {
            if (scenario=="reference_scroll_narrow") { window->setWidth(720); window->setHeight(720); }
            QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update(); if (!frames.wait(2000)) return false;
            auto* editor=item(window,"alertEditor");
            auto* first=item(window,"alertName");
            auto* last=item(window,"alertSoundMinimum");
            if (!editor || !first || !last) { std::fprintf(stderr,"Alarm scroll controls unavailable.\n"); return false; }
            const auto bounds=[&](const QQuickItem* control) { return QRectF(control->mapToScene({0,0}),control->size()); };
            const auto viewport=bounds(editor);
            const auto point=viewport.center();
            QElapsedTimer inputClock; inputClock.start();
            const auto scroll=[&](int delta) {
                QWheelEvent wheel(point,window->mapToGlobal(point.toPoint()),QPoint{},QPoint{0,delta},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                wheel.setTimestamp(static_cast<ulong>(inputClock.msecsSinceReference()+inputClock.elapsed()));
                QCoreApplication::sendEvent(window,&wheel);
                QTest::qWait(30);
            };
            for (int step=0; step<60 && !viewport.contains(bounds(last).center()); ++step) scroll(-120);
            if (!viewport.contains(bounds(last).center())) { std::fprintf(stderr,"Alarm editor bottom control is unreachable by wheel.\n"); return false; }
            for (int step=0; step<60 && !viewport.contains(bounds(first).center()); ++step) scroll(120);
            if (!viewport.contains(bounds(first).center())) { std::fprintf(stderr,"Alarm editor first control is unreachable after reverse scrolling.\n"); return false; }
            return server.requests==0;
        }
        if (scenario == "reference_table_tab_entry") {
            auto* columns = item(window, "alertColumnsButton");
            auto* table = item(window, "alertTable");
            if (!columns || !table) return false;
            columns->forceActiveFocus(Qt::TabFocusReason);
            for (int step = 0; step < 32 && !table->hasActiveFocus(); ++step) {
                QTest::keyClick(window, Qt::Key_Tab);
                QCoreApplication::processEvents();
            }
            if (!table->hasActiveFocus()) return false;
            QTest::keyClick(window, Qt::Key_Home);
            QTest::keyClick(window, Qt::Key_Down);
            return waitFor([&] { return selected() == baseline[1].toMap()["name"].toString(); })
                && rules() == baseline && server.requests == 0;
        }
        if (scenario == "reference_find" || scenario == "reference_find_invalid") {
            if (!click(window, "alertFindButton")) return false;
            const auto expected = baseline[1].toMap()["name"].toString();
            if (!type(window, "alertFindInput", scenario.endsWith("_invalid") ? QString("/[/") : "\"" + expected + "\"")) return false;
            if (scenario.endsWith("_invalid")) {
                return waitFor([&] { return item(window, "alertFindError")->isVisible(); })
                    && !item(window, "alertFindNext")->isEnabled() && rules() == baseline && server.requests == 0;
            }
            if (!waitFor([&] { return item(window, "alertFindCount")->property("text").toString() == "1/1"; })
                || !click(window, "alertFindNext")) return false;
            return waitFor([&] { return selected() == expected; }) && rules() == baseline && server.requests == 0;
        }
        if (scenario.startsWith("reference_columns_")) {
            const auto mode = scenario.mid(QString("reference_columns_").size());
            const auto before = workspace.alertColumns();
            const auto resources = workspace.resourceColumns();
            const auto column = [](const QVariantList& columns, const QString& id) {
                for (const auto& value : columns) if (value.toMap()["id"] == id) return value.toMap();
                return QVariantMap{};
            };
            if (!click(window, "alertColumnsButton")) return false;
            if (mode == "hide" || mode == "show") {
                if (!click(window, "alertColumnVisible_name")) return false;
            } else if (mode == "pin" || mode == "restart") {
                if (!click(window, "alertColumnPinned_name")) return false;
            } else if (mode == "order") {
                if (!click(window, "alertColumnEarlier_name")) return false;
            } else if (mode == "last_visible") {
                for (const auto& value : before) if (!click(window, "alertColumnVisible_" + value.toMap()["id"].toString())) return false;
            } else if (mode == "width" || mode == "cancel" || mode == "defaults" || mode == "invalid_width") {
                if (!type(window, "alertColumnWidth_name", mode == "invalid_width" ? "0" : "317")) return false;
            } else return false;
            if (!click(window, mode == "cancel" ? "alertCancelColumns" : "alertSaveColumns")) return false;
            if (mode == "cancel") return workspace.alertColumns() == before && rules() == baseline && server.requests == 0;
            if (mode == "last_visible" || mode == "invalid_width") {
                return waitFor([&] { return !workspace.tableLayoutError().isEmpty(); })
                    && workspace.alertColumns() == before && rules() == baseline && server.requests == 0;
            }
            if (!waitFor([&] { return !workspace.tableLayoutSaving() && workspace.alertColumns() != before; })) return false;
            auto actual = workspace.alertColumns();
            if (mode == "show" || mode == "defaults") {
                if (!click(window, "alertColumnsButton")
                    || !click(window, mode == "show" ? "alertColumnVisible_name" : "alertResetColumns")
                    || !click(window, "alertSaveColumns")
                    || !waitFor([&] { return !workspace.tableLayoutSaving() && workspace.alertColumns() == before; })) return false;
                actual = workspace.alertColumns();
            }
            if (mode == "hide" && column(actual, "name")["visible"].toBool()) return false;
            if (mode == "width" && column(actual, "name")["width"].toInt() != 317) return false;
            if (mode == "order" && actual[1].toMap()["id"] != "name") return false;
            if (mode == "pin" && (!column(actual, "name")["pinned"].toBool() || !waitFor([&] { return item(window, "pinnedHeader_2") != nullptr; }))) return false;
            if (mode == "restart") {
                podlord::Workspace reopened(profile);
                if (!waitFor([&] { return !reopened.busy(); }) || reopened.alertColumns() != actual) return false;
            }
            return workspace.resourceColumns() == resources && rules() == baseline && server.requests == 0;
        }
        if (scenario.startsWith("reference_cell_")) {
            const auto mode = scenario.mid(QString("reference_cell_").size());
            if (!QStringList{"copy", "menu", "shift_menu", "escape", "navigation", "copy_active", "copy_when", "copy_actions", "copy_sound", "narrow", "narrow_reverse", "narrow_resize", "home", "end"}.contains(mode)) return false;
            if (mode.startsWith("narrow") && mode != "narrow_resize") { window->setWidth(720); window->setHeight(720); }
            const QMap<QString, QString> columns{{"copy_active", "Active"}, {"copy_when", "When"}, {"copy_actions", "Actions"}, {"copy_sound", "Sound"}, {"narrow", "Sound"}};
            const auto column = mode.startsWith("narrow") ? QString("Sound") : columns.value(mode, "Name");
            const QStringList order{"Active", "Name", "When", "Actions", "Sound"};
            const int logicalColumn = order.indexOf(column) + 1;
            const auto cellName = logicalColumn == 2 ? QString("alertRule_0") : "alertCell_" + QString::number(logicalColumn) + "_0";
            auto* toggle = item(window, "toggleAlert_0");
            if (!toggle || !podlord::test::scrollIntoView(window, toggle)) return false;
            const auto before = rules();
            QGuiApplication::clipboard()->setText("unchanged clipboard");
            toggle->forceActiveFocus(Qt::TabFocusReason);
            auto* nativeTable = item(window, "alertTable");
            if (!nativeTable) return false;
            nativeTable->forceActiveFocus(Qt::TabFocusReason);
            for (int step = 0; step < logicalColumn; ++step) QTest::keyClick(window, Qt::Key_Right);
            if (!waitFor([&] { auto* target = item(window, cellName); return target && target->property("current").toBool(); })) return false;
            auto* cell = item(window, cellName);
            auto expected = cell->property("text").toString();
            if (mode.startsWith("narrow")) {
                if (mode == "narrow_resize") { window->setWidth(720); window->setHeight(720); }
                auto* table = item(window, "alertRulesTable");
                if (!table || !waitFor([&] {
                    return QRectF(table->mapToScene({0,0}), table->size()).contains(cell->mapToScene({cell->width()/2, cell->height()/2}));
                })) {
                    std::fprintf(stderr,"Keyboard focus outside alert table: target=%s focus=%s targetX=%.1f width=%.1f tableX=%.1f width=%.1f targetY=%.1f height=%.1f tableY=%.1f height=%.1f\n",
                        qPrintable(cell->objectName()),qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),
                        cell->mapToScene({0,0}).x(),cell->width(),table ? table->mapToScene({0,0}).x() : -1,table ? table->width() : -1,
                        cell->mapToScene({0,0}).y(),cell->height(),table ? table->mapToScene({0,0}).y() : -1,table ? table->height() : -1);
                    return false;
                }
                if (mode == "narrow_reverse") {
                    for (int step = 0; step < logicalColumn; ++step) QTest::keyClick(window, Qt::Key_Left);
                    if (!waitFor([&] {
                        auto* enabled = item(window, "alertCell_0_0");
                        return enabled && enabled->property("current").toBool()
                            && QRectF(table->mapToScene({0,0}), table->size()).contains(enabled->mapToScene({enabled->width()/2, enabled->height()/2}));
                    })) {
                        auto* enabled = item(window, "alertCell_0_0");
                        std::fprintf(stderr,"Reverse column did not show On: focus=%s column=%d row=%d left=%d present=%d current=%d x=%.1f y=%.1f width=%.1f height=%.1f tableY=%.1f height=%.1f\n",
                            qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),nativeTable->property("currentColumn").toInt(),nativeTable->property("currentRow").toInt(),nativeTable->property("leftColumn").toInt(),enabled != nullptr,enabled && enabled->property("current").toBool(),
                            enabled ? enabled->mapToScene({0,0}).x() : -1,enabled ? enabled->mapToScene({0,0}).y() : -1,enabled ? enabled->width() : -1,enabled ? enabled->height() : -1,table->mapToScene({0,0}).y(),table->height());
                        return false;
                    }
                    QTest::keyClick(window, Qt::Key_Return);
                    if (!waitFor([&] { return !workspace.alerts()->busy() && rules().first().toMap()["enabled"] != before.first().toMap()["enabled"]; })) {
                        std::fprintf(stderr,"Keyboard toggle failed: focus=%s enabled=%d error=%s\n",qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),rules().first().toMap()["enabled"].toBool(),qPrintable(workspace.alerts()->error()));
                        return false;
                    }
                    QTest::keyClick(window, Qt::Key_Return);
                    if (!waitFor([&] { return !workspace.alerts()->busy() && rules() == before; })) {
                        std::fprintf(stderr,"Keyboard toggle restoration failed: focus=%s enabled=%d error=%s\n",qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),rules().first().toMap()["enabled"].toBool(),qPrintable(workspace.alerts()->error()));
                        return false;
                    }
                    expected = "unchanged clipboard";
                }
            }
            if (mode == "navigation" || mode == "home" || mode == "end") {
                if (mode == "home") QTest::keyClick(window, Qt::Key_Down);
                QTest::keyClick(window, mode == "home" ? Qt::Key_Home : mode == "end" ? Qt::Key_End : Qt::Key_Down);
                const int nextIndex = mode == "home" ? 0 : mode == "end" ? baseline.size() - 1 : 1;
                expected = baseline[nextIndex].toMap()["name"].toString();
                if (!waitFor([&] {
                    auto* next = item(window, "alertRule_" + QString::number(nextIndex));
                    return next && selected() == expected;
                })) return false;
            }
            if (mode.startsWith("copy") || mode == "navigation" || mode == "home" || mode == "end" || mode == "narrow" || mode == "narrow_resize") QTest::keySequence(window, QKeySequence::Copy);
            else if (mode != "narrow_reverse") {
                QTest::keyClick(window, mode == "shift_menu" ? Qt::Key_F10 : Qt::Key_Menu,
                    mode == "shift_menu" ? Qt::ShiftModifier : Qt::NoModifier);
                if (!waitFor([&] {
                    auto* copy = item(window, "alertMenuCopy");
                    return copy && copy->isVisible() && copy->isEnabled();
                })) return false;
                if (mode == "escape") {
                    QTest::keyClick(window, Qt::Key_Escape);
                    if (!waitFor([&] { return !item(window, "alertMenuCopy")->isVisible(); })) return false;
                    expected = "unchanged clipboard";
                } else {
                    auto* copy = item(window, "alertMenuCopy");
                    copy->forceActiveFocus(Qt::TabFocusReason);
                    QTest::keyClick(copy->window(), Qt::Key_Space);
                }
            }
            if (!waitFor([&] { return QGuiApplication::clipboard()->text() == expected; })
                || rules() != before || server.requests != 0) return false;
            const auto capture = qEnvironmentVariable("PODLORD_ALERT_LAYOUT_FRAME");
            if (capture.isEmpty()) return true;
            QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
            return (frames.count() > 0 || frames.wait(2000)) && window->grabWindow().save(capture);
        }
        if (scenario=="reference_locked") return !item(window,"alertName")->isEnabled() && !item(window,"deleteAlert")->isEnabled() && !item(window,"addAlertOr")->isEnabled() && item(window,"alertEnabled")->isEnabled();
        if (scenario=="reference_keyboard") {
            if (!click(window,"alertRule_0")) return false;
            QTest::keyClick(window,Qt::Key_Down);
            if (!waitFor([&] { return selected()==baseline[1].toMap()["name"].toString(); })) {
                std::fprintf(stderr,"Arrow navigation selected=%s focus=%s\n",qPrintable(selected()),qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}));
                return false;
            }
            QTest::keyClick(window,Qt::Key_End);
            if (!waitFor([&] { return selected()==baseline.last().toMap()["name"].toString(); })) return false;
            QTest::keyClick(window,Qt::Key_Home);
            return waitFor([&] { return selected()==baseline.first().toMap()["name"].toString(); }) && server.requests==0;
        }
        if (scenario=="reference_sound_preview") {
            if (!item(window,"previewAlertSound")->isEnabled() || item(window,"alertSound")->isEnabled()) return false;
            if (!workspace.alerts()->setPreferences(true,workspace.alerts()->reducedMotion()) || !settle()) return false;
            return !item(window,"previewAlertSound")->isEnabled() && server.requests==0;
        }
        if (scenario.startsWith("reference_sound_source_")) {
            const auto mode=scenario.mid(QString("reference_sound_source_").size());
            const QMap<QString,QPair<QString,QString>> cases{
                {"ui", {"panel-segment-load","ui-audio"}}, {"interface", {"warning-ping","interface-sounds"}},
                {"digital", {"electro-warning","digital-audio"}}, {"impact", {"bell-alert","impact-sounds"}},
                {"critical", {"critical-klaxon","sci-fi-sounds"}}, {"rpg", {"book-open","rpg-audio"}},
                {"music", {"command-ambient-loop","music-jingles"}}, {"project", {"none","project"}},
                {"filtered", {"warning-ping","interface-sounds"}}, {"failure", {"warning-ping","interface-sounds"}}};
            if (!cases.contains(mode) || !click(window,"duplicateAlert") || !settle()) return false;
            const auto input=cases.value(mode);
            if (mode=="filtered") {
                if (!type(window,"alertSoundSearch","no-sound-matches-this")) return false;
            } else if (input.first!="warning-ping") {
                if (!type(window,"alertSoundSearch",input.first=="none" ? "Silent alert action." : input.first)
                    || !waitFor([&] { return item(window,"alertSound")->property("count").toInt()==1; })
                    || !click(window,"alertSound")) return false;
                QTest::keyClick(window,Qt::Key_Down); QTest::keyClick(window,Qt::Key_Return);
                if (!waitFor([&] { return item(window,"alertSound")->property("currentValue").toString()==input.first; })) return false;
            }
            const auto unchanged=rules();
            BrowserBoundary browser;
            if (mode=="failure") {
                QDesktopServices::setUrlHandler("https",&browser,"unavailable");
                if (!click(window,"openAlertSoundSource") || !waitFor([&] { return item(window,"alertSoundSourceError")->isVisible(); })) return false;
                QTest::qWait(100);
                if (!browser.urls.isEmpty() || rules()!=unchanged) return false;
                QDesktopServices::setUrlHandler("https",&browser,"open");
            }
            if (!click(window,"openAlertSoundSource") || !waitFor([&] { return browser.urls.size()==1; })) return false;
            const QUrl expected(input.second=="project" ? "https://github.com/YunaBraska/podlord" : "https://kenney.nl/assets/"+input.second);
            return browser.urls.first()==expected && rules()==unchanged && server.requests==0
                && !item(window,"alertSoundSourceError")->isVisible();
        }
        if (scenario.startsWith("reference_sound_search_")) {
            const auto mode=scenario.mid(QString("reference_sound_search_").size());
            const QMap<QString,QPair<QString,int>> cases{
                {"name", {"warning-ping",1}}, {"purpose", {"industrial metal",1}},
                {"source", {"rpg-audio",2}}, {"license", {" CC0-1.0 ",117}},
                {"author", {"KeNNeY",116}}, {"music", {"music",3}},
                {"empty", {"no-sound-matches-this",0}}, {"clear", {"no-sound-matches-this",0}},
                {"select", {"industrial metal",1}}};
            if (!cases.contains(mode) || !click(window,"duplicateAlert") || !settle()) return false;
            if (!waitFor([&] { return item(window,"alertSoundSearch") && item(window,"alertSoundSearch")->isEnabled(); })) return false;
            const auto input=cases.value(mode);
            if (!type(window,"alertSoundSearch",input.first)) return false;
            if (!waitFor([&] { return item(window,"alertSound")->property("count").toInt()==input.second; })) return false;
            if (mode=="empty" && (!item(window,"alertSoundNoResults")->isVisible()
                || !item(window,"alertSoundAttribution")->property("text").toString().contains("Warning ping"))) return false;
            if (mode=="clear") {
                if (!type(window,"alertSoundSearch","") || !waitFor([&] { return item(window,"alertSound")->property("count").toInt()==117; })) return false;
            }
            const auto expected=mode=="select" ? "metal-impact" : "warning-ping";
            if (mode=="select") {
                const auto capture=qEnvironmentVariable("PODLORD_ALERT_SOUND_FRAME");
                if (!capture.isEmpty()) { QTest::qWait(50); if (!window->grabWindow().save(capture)) return false; }
                if (!click(window,"alertSound")) return false;
                const auto* popup = item(window,"alertSound")->property("popup").value<QObject*>();
                if (!popup || !waitFor([&] { return popup->property("visible").toBool(); })) {
                    auto* control = item(window,"alertSound");
                    auto* editor = item(window,"alertEditor");
                    auto* content = editor->property("contentItem").value<QObject*>();
                    std::fprintf(stderr,"Sound popup did not open: x=%.1f y=%.1f width=%.1f height=%.1f focus=%s contentY=%.1f moving=%d\n",
                        control->mapToScene({0,0}).x(),control->mapToScene({0,0}).y(),control->width(),control->height(),
                        qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),
                        content ? content->property("contentY").toDouble() : -1,content && content->property("moving").toBool());
                    const auto frame=qEnvironmentVariable("PODLORD_ALERT_FAILURE_FRAME");
                    if (!frame.isEmpty()) window->grabWindow().save(frame);
                    return false;
                }
                QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return);
                if (!waitFor([&] { return item(window,"alertSound")->property("currentValue").toString()==expected; })) {
                    std::fprintf(stderr,"Sound selection failed: selected=%s focus=%s count=%d\n",
                        qPrintable(item(window,"alertSound")->property("currentValue").toString()),
                        qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),
                        item(window,"alertSound")->property("count").toInt());
                    return false;
                }
            }
            if (!click(window,"saveAlert") || !settle()) return false;
            return rules().last().toMap()["sound"].toString()==expected && server.requests==0;
        }
        if (scenario=="reference_toggle" || scenario=="reference_toggle_failure") {
            QLockFile failureLock(profile+"/alert-rules.json.lock");
            if (scenario=="reference_toggle_failure" && (!QDir().mkpath(profile) || !failureLock.tryLock(0))) return false;
            if (!click(window,"toggleAlert_0") || !settle()) return false;
            if (scenario=="reference_toggle_failure") return rules()==baseline && !workspace.alerts()->error().isEmpty() && !selected().isEmpty();
            if (rules().first().toMap()["enabled"]==baseline.first().toMap()["enabled"] || server.requests!=0) return false;
            if (!click(window,"toggleAlert_0") || !settle() || rules()!=baseline) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.alerts()->busy(); }) && restored.alerts()->rules()==baseline;
        }
        if (scenario=="reference_sort") {
            QStringList expected; for (const auto& entry:baseline) expected.append(entry.toMap()["name"].toString()); expected.sort(Qt::CaseSensitive);
            if (!click(window,"alertHeader_2") || !waitFor([&] { return item(window,"alertRule_0")->property("text").toString()==expected.first(); })) return false;
            if (!click(window,"alertRule_0") || selected()!=expected.first() || !click(window,"alertHeader_2") || !waitFor([&] { return item(window,"alertRule_0")->property("text").toString()==expected.last(); })) return false;
            if (selected()!=expected.first() || !click(window,"alertHeader_2")) return false;
            return waitFor([&] { return item(window,"alertRule_0")->property("text").toString()==baseline.first().toMap()["name"].toString(); }) && rules()==baseline && server.requests==0;
        }
        if (scenario=="reference_copy") {
            auto* row=item(window,"alertRule_0"); if (!row) return false;
            QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,row->mapToScene({200,row->height()/2}).toPoint());
            if (!waitFor([&] { return item(window,"alertMenuCopy") && item(window,"alertMenuCopy")->isVisible(); }) || !click(window,"alertMenuCopy")) return false;
            const auto copied=QGuiApplication::clipboard()->text();
            bool accepted = true;
            return copied==baseline.first().toMap()["name"].toString()
                && QMetaObject::invokeMethod(workspace.alerts(),"copyText",Q_RETURN_ARG(bool,accepted),Q_ARG(QString,QString(65537,'a')))
                && !accepted && QGuiApplication::clipboard()->text()==copied && server.requests==0;
        }
        if (!click(window,"addAlert") || !type(window,"alertName","Group rule") || !type(window,"alertExpression_0","first")) return false;
        if (scenario=="reference_group_guard") return !item(window,"removeAlertGroup_0")->isEnabled() && !item(window,"removeAlertCriterion_0")->isEnabled();
        if (scenario=="reference_failed_draft") {
            QLockFile failureLock(profile+"/alert-rules.json.lock");
            if (!QDir().mkpath(profile) || !failureLock.tryLock(0) || !click(window,"saveAlert") || !settle()) return false;
            if (selected()!="Group rule" || workspace.alerts()->error().isEmpty() || rules()!=baseline) return false;
            failureLock.unlock();
            return click(window,"saveAlert") && settle() && rules().size()==baseline.size()+1 && selected()=="Group rule";
        }
        if (!click(window,"addAlertCriterion_0") || !type(window,"alertExpression_1","second")) return false;
        if (scenario=="reference_remove_criterion") {
            if (!click(window,"removeAlertCriterion_0") || !click(window,"saveAlert") || !settle()) return false;
            const auto groups=rules().last().toMap()["groups"].toList();
            return groups.size()==1 && groups.first().toList().size()==1 && groups.first().toList().first().toMap()["expression"]=="second";
        }
        if (!click(window,"addAlertOr") || !type(window,"alertExpression_2","third") || !click(window,"removeAlertGroup_0") || !click(window,"saveAlert") || !settle()) return false;
        const auto groups=rules().last().toMap()["groups"].toList();
        return groups.size()==1 && groups.first().toList().size()==1 && groups.first().toList().first().toMap()["expression"]=="third" && server.requests==0;
    }
    if (scenario == "empty") return item(window, "alertEmptyState") && item(window, "alertEmptyState")->isVisible();
    const auto expression=scenario.startsWith("radar_") ? "\"not-a-radar-resource\"" : scenario=="exact" ? "\"alpha\"" : scenario=="prefix" ? "~alp" : scenario=="suffix" ? "pha~" : scenario=="regex" ? "/^alp.*$/" : realConfig.isEmpty() ? "alpha" : "\"visual-crash-loop\"";
    if (!click(window, "addAlert") || !type(window, "alertName", "Alpha rule") || !type(window, "alertExpression_0", expression)) return false;
    if (scenario == "invalid") {
        if (!type(window, "alertName", "") || !click(window, "saveAlert")) return false;
        return waitFor([&] { return !item(window, "alertError")->property("text").toString().isEmpty(); })
            && !QFile::exists(profile + "/alert-rules.json");
    }
    QLockFile lock(profile + "/alert-rules.json.lock");
    if (scenario == "busy" && (!QDir().mkpath(profile) || !lock.tryLock(0))) return false;
    if (!click(window, "saveAlert")) return false;
    if (scenario == "busy") return waitFor([&] { return !item(window, "alertError")->property("text").toString().isEmpty(); });
    if (!waitFor([&] { return item(window, "alertRule_3") != nullptr; })) return false;
    if (scenario == "create") return QFile::exists(profile + "/alert-rules.json") && server.requests == 0;
    if (scenario == "duplicate") {
        const auto original = workspace.alerts()->rules().last().toMap();
        return click(window, "duplicateAlert") && waitFor([&] {
            const auto rules = workspace.alerts()->rules();
            return !workspace.alerts()->busy() && rules.size() == 5
                && rules.last().toMap()["id"] != original["id"]
                && rules.last().toMap()["name"].toString().startsWith(original["name"].toString())
                && item(window, "alertName")->property("text") == rules.last().toMap()["name"];
        }) && server.requests == 0;
    }
    if (scenario == "delete") {
        return click(window, "deleteAlert") && waitFor([&] {
            const auto rules = workspace.alerts()->rules();
            return !workspace.alerts()->busy() && rules.size() == 3
                && item(window, "alertName")->property("text") == rules.first().toMap()["name"];
        }) && server.requests == 0;
    }
    if (scenario == "restart") {
        podlord::Workspace restored(profile); QQmlApplicationEngine second;
        second.rootContext()->setContextProperty("workspace", &restored); second.load(QUrl("qrc:/podlord/Main.qml"));
        if (second.rootObjects().isEmpty()) return false;
        auto* other = qobject_cast<QQuickWindow*>(second.rootObjects().first());
        return waitFor([&] { return !restored.busy(); }) && click(other, "alertsWorkspaceButton")
            && waitFor([&] { return item(other, "alertRule_3") && item(other, "alertRule_3")->property("text").toString().contains("Alpha rule"); });
    }
    if (scenario=="and" || scenario=="or" || scenario=="numeric" || scenario=="missing_metric" || scenario=="hold" || scenario=="no_replay" || scenario=="regex_limit") {
        auto rule=workspace.alerts()->rules().last().toMap();
        if (scenario=="and") rule["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "name"}, {"expression", "alpha"}}, QJsonObject{{"field", "status"}, {"expression", "Failed"}}})}.toVariantList();
        if (scenario=="or") rule["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "name"}, {"expression", "absent"}}}), QJsonArray{QJsonObject{{"field", "name"}, {"expression", "alpha"}}}}.toVariantList();
        if (scenario=="numeric") rule["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "restarts"}, {"expression", ">=1 <3"}}})}.toVariantList();
        if (scenario=="missing_metric") rule["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "cpu"}, {"expression", ">1m"}}})}.toVariantList();
        if (scenario=="regex_limit") rule["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "image"}, {"expression", "/(*NO_START_OPT)(a+)+$/"}}})}.toVariantList();
        if (scenario=="hold") { rule["groups"]=QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "status"}, {"expression", "Running"}}})}.toVariantList(); rule["colorMode"]="duration"; rule["colorSeconds"]=1; }
        if (scenario=="no_replay") rule["zoom"]=100;
        if (!workspace.alerts()->saveRule(rule) || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
    }
    QSignalSpy focus(workspace.alerts(), &podlord::Alerts::focusRequested);
    if (!loadSession()) return false;
    if (scenario.startsWith("radar_")) {
        if (!click(window, "radarWorkspaceButton") || !waitFor([&] { return item(window, "radarTile_0")!=nullptr; })) return false;
        if (scenario == "radar_search_on_demand") {
            const auto* search = item(window, "resourceFilter");
            if (!search || search->isVisible()) return false;
            const auto requests = server.requests;
            return type(window, "resourceFilter", "\"alpha\"")
                && waitFor([&] { return workspace.filterText() == "\"alpha\"" && workspace.resourceCount() == 1
                    && item(window, "resourceFilter")->isVisible(); }) && server.requests == requests;
        }
        if (scenario=="radar_loading_partial") {
            const auto* summary=item(window, "radarHealthSummary");
            if (!summary || !summary->property("text").toString().contains("1 cached") || !summary->property("text").toString().contains("Loading")
                || !workspace.alerts()->matches().isEmpty() || !focus.isEmpty()) return false;
            if (!waitFor([&] { return !workspace.loading() && workspace.resourceCount()==2; })) return false;
            return summary->property("text").toString().contains("2 cached") && !summary->property("text").toString().contains("Loading");
        }
        if (scenario=="radar_refresh_preserves_alarm") {
            const auto problemCount=[&] {
                for (const auto& match : workspace.alerts()->matches()) {
                    const auto value=match.toMap();
                    if (value["name"].toString()=="Problem color") return value["count"].toInt();
                }
                return 0;
            };
            const auto red=QColor("#FF5C5C");
            if (!waitFor([&] { return problemCount()==1 && item(window,"radarTile_0")->property("alertColor").value<QColor>()==red; }) || !focus.isEmpty()) return false;
            server.holdPodResponses=true;
            server.phase="Running";
            if (!click(window,"refreshButton") || !waitFor([&] { return workspace.loading() && !server.heldPodResponses.isEmpty(); })) return false;
            const bool preserved=problemCount()==1 && item(window,"radarTile_0")->property("alertColor").value<QColor>()==red && focus.isEmpty();
            if (!server.releasePodResponses()) return false;
            if (!preserved) { std::fputs("Background sync removed an alarm from the still-visible cached resource.\n",stderr); return false; }
            return waitFor([&] { return !workspace.loading() && problemCount()==0 && item(window,"radarTile_0")->property("alertColor").value<QColor>()!=red; }) && focus.isEmpty();
        }
        if (scenario.startsWith("radar_focus_")) {
            if (!focus.isEmpty()) return false;
            if (scenario=="radar_focus_filtered" && !workspace.filter("alpha")) return false;
            const auto requests=server.requests; const auto previous=focus.count();
            server.secondFailed=true;
            if (!click(window, "refreshButton") || !waitFor([&] { return server.requests>requests && !workspace.loading() && focus.count()>previous; })) return false;
            auto* radar=item(window, "resourceRadar");
            const bool target=focus.last()[1].toString().endsWith("/zeta");
            const int index=radar->property("currentIndex").toInt();
            const bool visibleTarget=scenario=="radar_focus_filtered" ? index<=0 && workspace.resourceCount()==1 : index==workspace.alertResourceIndex("/api/v1/namespaces/default/pods/zeta");
            const auto settled=focus.count();
            if (!click(window, "refreshButton") || !waitFor([&] { return !workspace.loading(); })) return false;
            QTest::qWait(100);
            return target && visibleTarget && focus.count()==settled;
        }
        if (scenario == "radar_health_fresh") {
            server.phase = "Complete"; const auto requests = server.requests;
            if (!workspace.refresh() || !waitFor([&] { return server.requests > requests && !workspace.loading(); })) return false;
        }
        const QColor expected(scenario=="radar_health_warning" || scenario=="radar_health_restarts" ? "#FFE866" : scenario=="radar_health_critical" ? "#FF5C5C" : "#7DFFC3");
        if (!waitFor([&] { auto* tile=item(window,"radarTile_0"); return tile && tile->property("alertColor").value<QColor>()==expected; })) return false;
        const auto requests=server.requests;
        const auto capture=qEnvironmentVariable("PODLORD_RADAR_HIGHLIGHT_FRAME");
        if (!capture.isEmpty() && !window->grabWindow().save(capture)) return false;
        QTest::qWait(100);
        return server.requests==requests;
    }
    if (!click(window, "alertsWorkspaceButton")) return false;
    const auto alphaMatch=[&] { for (int i=0; i<4; ++i) if (auto* match=item(window, "alertMatch_"+QString::number(i)); match && match->property("text").toString()=="Alpha rule: 1") return true; return false; };
    if (scenario=="and" || scenario=="missing_metric") return !alphaMatch();
    if (scenario=="regex_limit") return waitFor([&] { return item(window, "alertEvaluationError")->property("text").toString().contains("exceeded"); }) && !alphaMatch();
    if (!waitFor(alphaMatch, realConfig.isEmpty() ? 9000 : 60000)) {
        std::fprintf(stderr, "Cache: %d resources; status: %s; error: %s; alert rules: %s; alert matches: %s\n", workspace.totalResourceCount(), qPrintable(workspace.status()), qPrintable(workspace.error()), QJsonDocument::fromVariant(workspace.alerts()->rules()).toJson(QJsonDocument::Compact).constData(), QJsonDocument::fromVariant(workspace.alerts()->matches()).toJson(QJsonDocument::Compact).constData());
        return false;
    }
    if (scenario.startsWith("health_")) {
        const int expected=scenario=="health_restart_outlier" ? 2 : scenario=="health_event_warning" || scenario=="health_event_missing_time" || scenario=="health_event_invalid_time" || scenario=="health_event_expiry" || scenario=="health_pvc_pending" || scenario=="health_pod_not_ready" || scenario=="health_restart_population" ? 1 : 0;
        const auto* match=item(window,"alertMatch_0");
        const bool correct=expected>0 ? match && match->property("text").toString()==QString("Problem color: %1").arg(expected) : !match || !match->property("text").toString().startsWith("Problem color:");
        if (!correct) std::fprintf(stderr,"Expected problems=%d; visible problem summary=%s\n",expected,qPrintable(match ? match->property("text").toString() : "none"));
        if (scenario=="health_event_warning" || scenario=="health_pvc_pending" || scenario=="health_pod_not_ready") {
            const auto collection=scenario=="health_event_warning" ? "events" : scenario=="health_pvc_pending" ? "persistentvolumeclaims" : "pods";
            if (!workspace.setWorkspacePage("resources") || !workspace.inspectPath(QString("/api/v1/namespaces/default/%1/alpha").arg(collection))) return false;
            if (!waitFor([&] { for (const auto& field : workspace.overviewFields()) if (field.toMap()["label"].toString()=="Issue" && !field.toMap()["value"].toString().isEmpty()) return true; return false; })) return false;
        }
        if (!correct || !workspace.setWorkspacePage("dashboard") || !workspace.dashboardSummary().contains(QString("%1 with problems.").arg(expected))) return false;
        if (scenario=="health_event_expiry" || scenario=="health_event_future_activation" || scenario=="health_pod_grace_expiry") {
            const auto requests=server.requests; const int after=scenario=="health_event_expiry" ? 0 : 1;
            if (!waitFor([&] { return workspace.dashboardSummary().contains(QString("%1 with problems.").arg(after)); }) || !workspace.setWorkspacePage("alerts")) return false;
            return waitFor([&] {
                const auto* updated=item(window,"alertMatch_0");
                return after>0 ? updated && updated->property("text").toString()=="Problem color: 1" : !updated || !updated->property("text").toString().startsWith("Problem color:");
            }) && server.requests==requests;
        }
        return true;
    }
    if (scenario=="real_health") {
        const auto failure=[&](const char* stage) {
            std::fprintf(stderr,"Real health failure at %s: cached=%d filtered=%d loading=%d status=%s error=%s matches=%s\n",
                stage,workspace.totalResourceCount(),workspace.resourceCount(),workspace.loading(),qPrintable(workspace.status()),
                qPrintable(workspace.error()),QJsonDocument::fromVariant(workspace.alerts()->matches()).toJson(QJsonDocument::Compact).constData());
            std::fprintf(stderr,"Render surface: platform=%s exposed=%d active=%d.\n",qPrintable(QGuiApplication::platformName()),window->isExposed(),window->isActive());
            return false;
        };
        if (realConfig.isEmpty() || !waitFor([&] { return !workspace.loading(); },180000)) return failure("completed initial sync");
        if (QGuiApplication::platformName()=="cocoa") {
            window->requestActivate();
            if (!waitFor([&] { return window->isExposed() && window->isActive(); },5000)) return failure("foreground desktop availability");
        }
        const auto* problems=item(window,"alertMatch_0");
        if (!problems || !problems->property("text").toString().startsWith("Problem color:") || problems->property("text").toString().section(':',1).trimmed().toInt()<34) return failure("default problem alarm");
        if (!workspace.filter("no-resource-matches-this") || workspace.resourceCount()!=0 || !alphaMatch()) return failure("session-wide alarm despite empty filter");
        QSignalSpy inspectorFrames(window,&QQuickWindow::frameSwapped);
        if (!workspace.filter("visual-pending-storage-1") || !workspace.setWorkspacePage("resources") || !workspace.inspectPath("/api/v1/namespaces/visual-a/persistentvolumeclaims/visual-pending-storage-1")) return failure("open pending PVC inspector");
        if (!waitFor([&] { for (const auto& field : workspace.overviewFields()) if (field.toMap()["label"].toString()=="Issue" && field.toMap()["value"].toString().contains("Pending")) return true; return false; })) return failure("pending PVC issue display");
        if (!capturePath.isEmpty()) {
            if (!waitFor([&] { return inspectorFrames.count()>0; },2000)) return failure("natural inspector frame");
            const auto captured=window->grabWindow();
            if (captured.isNull() || !captured.save(capturePath,"PNG")) return failure("inspector screenshot");
        }
        if (!workspace.closeInspector() || !click(window, "alertsWorkspaceButton")
            || !click(window, "alertEnabled") || !click(window, "saveAlert")
            || !waitFor([&] { return !workspace.alerts()->busy(); })
            || !click(window, "radarWorkspaceButton")) return failure("open real Radar");
        const auto radarFrames=qEnvironmentVariable("PODLORD_REAL_RADAR_HIGHLIGHTS");
        for (const auto& target : {QString("visual-pending-storage-1"), QString("visual-crash-loop")}) {
            QSignalSpy frames(window,&QQuickWindow::frameSwapped);
            if (!workspace.filterField("namespace", target=="visual-crash-loop" ? "\"visual-b\"" : "\"visual-a\"")
                || !type(window, "resourceFilter", '"'+target+'"')
                || !click(window,"radarWorkspaceButton")) return failure("filter real Radar resource");
            QTest::keyClick(window,Qt::Key_Home);
            if (!waitFor([&] { return workspace.resourceCount()==1 && item(window,"radarTile_0")!=nullptr; })) return failure("focus real Radar resource");
            const QColor expected(target=="visual-crash-loop" ? "#FF5C5C" : "#FFE866");
            if (!waitFor([&] { auto* tile=item(window,"radarTile_0"); return tile && tile->property("alertColor").value<QColor>()==expected; },60000)) return failure("real Radar severity highlight");
            if (!waitFor([&] { return frames.count()>0; },2000)) return failure("natural real Radar frame");
            if (QGuiApplication::platformName()=="cocoa" && (!window->isExposed() || !window->isActive())) return failure("foreground lost during real Radar frame");
            if (!radarFrames.isEmpty() && !window->grabWindow().save(radarFrames+'-'+target+".png")) return failure("real Radar screenshot");
        }
        std::fprintf(stdout,"Real Kubernetes health: Pending PVCs and failure Pods match the default alarm; filters do not hide the alarm; inspector shows Pending.\n");
        std::fprintf(stdout,"Real Kubernetes Radar: Pending PVC is yellow; failed/CrashLoop Pod is red; public search uses cache.\n");
        return true;
    }
    if (scenario=="real_metrics") {
        const auto failure=[&](const char* stage) {
            std::fprintf(stderr,"Real metrics failure at %s: cached=%d filtered=%d status=%s error=%s\n",stage,
                workspace.totalResourceCount(),workspace.resourceCount(),qPrintable(workspace.status()),qPrintable(workspace.error()));
            const auto capture=qEnvironmentVariable("PODLORD_RADAR_METRIC_FRAME");
            if (!capture.isEmpty()) window->grabWindow().save(capture+"-failure.png");
            return false;
        };
        if (!workspace.filterField("kind","\"Pod\"") || !workspace.filterField("namespace","\"visual-a\"")
            || !workspace.filter("\"visual-multi-container\"") || !workspace.setWorkspacePage("resources")) return failure("select exact Pod");
        const auto* model = workspace.table();
        const auto column = [&](const QString& id) {
            for (int c=0;c<model->columnCount();++c) if (model->headerData(c,Qt::Horizontal,Qt::UserRole)==id) return c;
            return -1;
        };
        const auto measuredTable = [&] {
            for (int row=0;row<model->rowCount();++row) if (model->data(model->index(row,column("kind"))).toString()=="Pod") {
                const auto cpu = model->data(model->index(row,column("cpu"))).toString(), memory = model->data(model->index(row,column("memory"))).toString();
                return cpu.contains("mCPU") && !cpu.contains("incomplete") && memory!="-"
                    && model->data(model->index(row,column("storage"))).toString()=="-"
                    && model->data(model->index(row,column("ready"))).toString()=="2/2"
                    && model->data(model->index(row,column("cluster"))).toString()=="default";
            }
            return false;
        };
        if (!waitFor(measuredTable,60000)) return failure("measured Pod table values");
        if (!workspace.inspectPath("/api/v1/namespaces/visual-a/pods/visual-multi-container")) return failure("open Pod inspector");
        const bool inspected = waitFor([&] {
            const auto* usage=item(window,"metric_cpu_usage");
            return usage && !usage->property("text").toString().contains("Unavailable") && !usage->property("text").toString().contains("incomplete");
        },60000);
        if (!inspected || !workspace.closeInspector() || !click(window,"radarWorkspaceButton")) return failure("fresh inspector and Pod radar");
        QTest::keyClick(window,Qt::Key_Home);
        if (!waitFor([&] { return item(window,"radarTile_0")!=nullptr; })) return failure("focus measured Pod in viewport");
        auto* radarTile=item(window,"radarTile_0"); radarTile->forceActiveFocus();
        QTest::mouseMove(window,radarTile->mapToScene({radarTile->width()/2,radarTile->height()/2}).toPoint());
        if (!waitFor([] { return radarGaugesFit(); },60000)) return failure("measured radar gauges");
        const auto radarCapture=qEnvironmentVariable("PODLORD_RADAR_METRIC_FRAME");
        if (!radarCapture.isEmpty() && !window->grabWindow().save(radarCapture)) return false;
        const auto themeFrames=qEnvironmentVariable("PODLORD_RADAR_THEME_FRAMES");
        if (!themeFrames.isEmpty()) {
            if (!QDir::isAbsolutePath(themeFrames) || !QDir().mkpath(themeFrames)) return false;
            QJsonArray captures;
            int themeIndex=0;
            for (const auto& theme : workspace.themeNames()) {
                for (const auto& variant : {QString("dark"), QString("light")}) {
                    if (!workspace.saveAppearance(theme,variant) || !waitFor([&] { return !workspace.busy(); })) return false;
                    if (!waitFor([&] { return item(window,"radarTile_0")!=nullptr; })) return failure("themed Pod viewport");
                    radarTile=item(window,"radarTile_0");
                    radarTile->forceActiveFocus();
                    QTest::mouseMove(window,radarTile->mapToScene({radarTile->width()/2,radarTile->height()/2}).toPoint());
                    if (!waitFor([] { return radarGaugesFit(); })) return false;
                    QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
                    if (frames.isEmpty() && !frames.wait(2000)) return false;
                    const auto filename=QString("%1-%2.png").arg(themeIndex,2,10,QChar('0')).arg(variant);
                    if (!window->grabWindow().save(QDir(themeFrames).filePath(filename))) return false;
                    captures.append(QJsonObject{{"theme",theme},{"variant",variant},{"file",filename}});
                }
                ++themeIndex;
            }
            QFile manifest(QDir(themeFrames).filePath("manifest.json"));
            const auto bytes=QJsonDocument(QJsonObject{{"boundary","real local Kubernetes Radar hover"},{"filteredResources",workspace.resourceCount()},{"cachedResources",workspace.totalResourceCount()},{"captures",captures}}).toJson();
            if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes)!=bytes.size()) return false;
            std::fprintf(stdout,"Real Kubernetes Radar themes: %lld inspected-boundary captures saved.\n",static_cast<long long>(captures.size()));
        }
        std::fprintf(stdout,"Real Kubernetes Radar hover: measured CPU/Memory, request/limit markers and bounded metric layout are visible.\n");
        if (capturePath.isEmpty()) return true;
        if (!workspace.setWorkspacePage("resources")) return false;
        auto columns=workspace.resourceColumns();
        const QStringList visible{"name","cpu","memory","storage","createdAt","ready","restarts"};
        for (auto& entry : columns) {
            auto value=entry.toMap(); const auto id=value["id"].toString();
            value["visible"]=visible.contains(id); value["pinned"]=id=="name";
            value["width"]=id=="name" ? 250 : id=="createdAt" || id=="ready" || id=="restarts" ? 110 : 160;
            entry=value;
        }
        if (!workspace.saveTableLayout("resource",columns) || !waitFor([&] { return !workspace.tableLayoutSaving(); }) || !workspace.tableLayoutError().isEmpty()) return false;
        if (!waitFor([&] { const auto* pinned=item(window,"pinnedHeader_0"); return pinned && pinned->isVisible() && pinned->width()>0; })) return false;
        auto* view=item(window,"resourceTable"); if (!view) return false;
        if (!waitFor([&] {
            for (const auto& id : visible.mid(1)) {
                QQmlExpression lookup(qmlContext(view),view,QString("itemAtIndex(index(0,%1))").arg(column(id)));
                auto* cell=lookup.evaluate().value<QQuickItem*>();
                if (!cell || view->property("moving").toBool()) return false;
                const auto left=cell->mapToItem(view,{0,0}).x();
                if (left < -1 || left+cell->width()>view->width()+1) return false;
            }
            return true;
        })) return false;
        QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
        if (!frames.wait(2000)) return false;
        const auto captured=window->grabWindow();
        return !captured.isNull() && captured.save(capturePath,"PNG");
    }
    if (scenario=="metric_tooltip" || scenario=="metric_radar_tooltip" || scenario=="metric_radar_gauges" || scenario=="metric_radar_gauges_narrow") {
        const bool radar=scenario!="metric_tooltip";
        if (scenario=="metric_radar_gauges_narrow") {
            window->setWidth(720); window->setHeight(620);
            if (!waitFor([&] { return window->contentItem()->width()==720; })) return false;
        }
        if (!radar) {
            if (!workspace.setWorkspacePage("resources")) return false;
            int visual=0;
            for (const auto& value:workspace.resourceColumns()) { if (value.toMap()["id"]=="image") break; ++visual; }
            auto* table=item(window,"resourceTable");
            if (!table || !waitFor([&] { return table->property("columns").toInt() == workspace.table()->columnCount(); })) return false;
            QSignalSpy ready(window, &QQuickWindow::frameSwapped); window->update();
            if (!ready.wait(2000)) return false;
            QQmlExpression position(qmlContext(table),table,QString("positionViewAtColumn(%1, TableView.Contain)").arg(visual)); position.evaluate();
            if (position.hasError()) return false;
        }
        if (!workspace.setWorkspacePage("resources") || (radar && !click(window,"radarWorkspaceButton")) || !waitFor([&] { return item(window,radar ? "radarTile_0" : "cell_0_5")!=nullptr; })) { std::fprintf(stderr,"Radar resource surface did not become operable.\n"); return false; }
        const auto requests=server.requests; auto* cell=item(window,radar ? "radarTile_0" : "cell_0_5");
        cell->forceActiveFocus();
        QTest::mouseMove(window,cell->mapToScene({cell->width()/2,cell->height()/2}).toPoint());
        const bool shown=waitFor([&] {
            for (auto* surface : QGuiApplication::allWindows()) if (auto* quick=qobject_cast<QQuickWindow*>(surface)) {
                const auto* tip=item(quick,"plainTipText");
                if (quick->isVisible() && tip && tip->isVisible() && tip->property("text").toString().contains("<img src=")) return true;
            }
            return false;
        });
        QTest::qWait(300);
        if (!shown && radar) {
            std::fprintf(stderr,"Radar tooltip did not open at (%g,%g), tile size %gx%g.\n",cell->mapToScene({0,0}).x(),cell->mapToScene({0,0}).y(),cell->width(),cell->height());
            const auto capture=qEnvironmentVariable("PODLORD_RADAR_METRIC_FRAME");
            if (!capture.isEmpty()) window->grabWindow().save(capture);
        }
        if (shown && (scenario=="metric_radar_gauges" || scenario=="metric_radar_gauges_narrow")) {
            if (!waitFor([] { return radarGaugesFit("25 mCPU","100 mCPU"); })) { std::fprintf(stderr,"Radar metric gauges or labeled reference markers are missing or overlap.\n"); return false; }
            const auto capture=qEnvironmentVariable("PODLORD_RADAR_METRIC_FRAME");
            if (!capture.isEmpty() && !window->grabWindow().save(capture)) return false;
        }
        if (!shown && !radar) {
            std::fprintf(stderr,"Tooltip cell: visible=%d hovered=%d focus=%d wantsTip=%d x=%g width=%g text=%s\n",cell->isVisible(),cell->property("hovered").toBool(),cell->hasActiveFocus(),cell->property("wantsTip").toBool(),cell->mapToScene({0,0}).x(),cell->width(),qPrintable(cell->property("text").toString()));
            if (const auto* tip=window->findChild<QObject*>("resourceValueTooltip")) std::fprintf(stderr,"Tooltip: visible=%d opened=%d target=%s text=%s width=%g height=%g\n",tip->property("visible").toBool(),tip->property("opened").toBool(),qPrintable(tip->property("target").value<QObject*>() ? tip->property("target").value<QObject*>()->objectName() : "missing"),qPrintable(tip->property("text").toString()),tip->property("width").toDouble(),tip->property("height").toDouble());
        }
        return shown && server.externalImageRequests==0 && server.requests==requests;
    }
    if (scenario=="metric_many") {
        const auto requests=server.requests; QList<qint64> timings; QMap<QString,QList<qint64>> pages;
        for (int i=0; i<30; ++i) {
            const auto page=QStringList{"resources","events","dashboard"}[i%3];
            QSignalSpy frames(window,&QQuickWindow::frameSwapped); QElapsedTimer elapsed; elapsed.start();
            if (!workspace.setWorkspacePage(page)) return false; window->update();
            if (!frames.wait(2000)) return false; const auto duration=elapsed.nsecsElapsed()/1000; timings.append(duration); pages[page].append(duration);
        }
        const auto first=timings.first(); std::sort(timings.begin(),timings.end());
        std::fprintf(stdout,"5000 cached Pods, public tab switch to rendered frame: first=%lld us, p50=%lld us, p95=%lld us, max=%lld us\n",first,timings[15],timings[28],timings.last());
        for (auto it=pages.begin();it!=pages.end();++it) { auto sorted=it.value(); const auto cold=sorted.first(); std::sort(sorted.begin(),sorted.end()); std::fprintf(stdout,"%s: first=%lld us, p50=%lld us, p95=%lld us\n",qPrintable(it.key()),cold,sorted[5],sorted[9]); }
        return server.requests==requests && workspace.resourceCount()==5000;
    }
    if (scenario=="metric_repeat") {
        QSignalSpy changed(&workspace, &podlord::Workspace::resourcePresentationChanged);
        const auto requests=server.requests;
        if (!workspace.refresh() || !waitFor([&] { return server.requests>requests && !workspace.loading(); })) return false;
        return changed.count()==0;
    }
    if (scenario=="dashboard") {
        if (!workspace.setWorkspacePage("dashboard") || !waitFor([&] { return item(window,"dashboardResource_0")!=nullptr; })) return false;
        const auto requests=server.requests;
        if (!workspace.filter("does-not-match")) return false;
        return workspace.dashboardTable()->rowCount()==0 && workspace.dashboardSummary().contains("0 / 1") && server.requests==requests;
    }
    if (scenario.startsWith("metric_")) {
        if (!workspace.inspectRow(0) || !waitFor([&] { return item(window, "metric_cpu_usage") != nullptr; })) {
            std::fprintf(stderr,"Inspector open failed: %s / %s\n",qPrintable(workspace.error()),qPrintable(workspace.inspectorStatus())); return false;
        }
        const bool measured=waitFor([&] {
            const auto* measured=item(window, "metric_cpu_usage");
            if (!measured) return false;
            const auto usage=measured->property("text").toString();
            if (scenario=="metric_empty") return usage.contains("Unavailable");
            if (scenario=="metric_invalid") return usage.contains("Unavailable") && workspace.status().contains("Invalid Metrics API quantity");
            if (scenario=="metric_zero") return usage.contains("0 mCPU") && !usage.contains("Unavailable");
            if (scenario=="metric_partial") return usage.contains("25 mCPU") && usage.contains("incomplete");
            if (scenario=="metric_stale") return usage.contains("25 mCPU") && usage.contains("stale");
            const auto* reference=item(window, "metric_cpu_references");
            return usage.contains("25 mCPU") && reference && reference->property("text").toString().contains("250% of request");
        });
        if (!measured) std::fprintf(stderr, "Visible metric fields: %s\n", QJsonDocument::fromVariant(workspace.overviewFields()).toJson(QJsonDocument::Compact).constData());
        return measured;
    }
    if (scenario=="real") return workspace.filter("no-resource-matches-this") && workspace.resourceCount()==0 && alphaMatch();
    if (QStringList{"exact", "prefix", "suffix", "regex", "or", "numeric"}.contains(scenario)) return true;
    if (scenario=="no_replay") {
        const auto first=workspace.currentSession();
        if (!focus.isEmpty()) return false;
        server.phase = "Complete";
        if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && focus.count() == 1; })) return false;
        if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return !workspace.busy() && !workspace.loading(); })) return false;
        if (!workspace.activate(first) || !waitFor([&] { return !workspace.busy(); })) return false;
        QTest::qWait(100); return focus.count()==1;
    }
    if (scenario=="hold") {
        const auto path=workspace.alerts()->matches().last().toMap()["path"].toString();
        if (workspace.alerts()->effect(path)["color"].toString()!="#e3aa46") return false;
        return waitFor([&] { return workspace.alerts()->effect(path)["color"].toString()!="#e3aa46"; }) && alphaMatch();
    }
    const auto requests = server.requests;
    if (scenario == "filter") {
        if (!workspace.filter("does-not-match")) return false;
        return workspace.resourceCount() == 0 && alphaMatch() && server.requests == requests;
    }
    if (scenario == "disable") return click(window, "alertEnabled") && click(window, "saveAlert")
        && waitFor([&] { return !alphaMatch(); }) && server.requests == requests;
    return false;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); if (argc != 2 && argc != 3 && argc != 4) return 2;
    const auto scenario=QString::fromLocal8Bit(argv[1]);
    if (argc==4 && scenario!="real_health" && scenario!="real_metrics") return 2;
    const bool passed = run(scenario, argc>=3 ? QString::fromLocal8Bit(argv[2]) : QString{}, argc==4 ? QString::fromLocal8Bit(argv[3]) : QString{});
    if (!passed) std::fprintf(stderr, "Alert UI scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
