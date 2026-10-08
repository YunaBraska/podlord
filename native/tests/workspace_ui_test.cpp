#include "workspace.h"
#include <QInputMethodEvent>
#include "ui_input.h"
#include <QClipboard>
#include <QAccessible>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QLockFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QItemSelectionModel>
#include <cstdio>
#include <functional>
#include <algorithm>

namespace {
bool accessibleName(QQuickWindow* window, const QString& name) {
    QList<QAccessibleInterface*> pending{QAccessible::queryAccessibleInterface(window)};
    for (qsizetype index = 0; index < pending.size() && index < 10000; ++index) {
        auto* entry = pending[index];
        if (!entry || !entry->isValid() || entry->state().invisible) continue;
        if (entry->text(QAccessible::Name) == name) return true;
        for (int child = 0; child < entry->childCount(); ++child) pending.append(entry->child(child));
    }
    std::fprintf(stderr, "Visible accessibility name unavailable: %s\n", qPrintable(name));
    return false;
}
bool waitFor(const std::function<bool()>& ready, int timeout = 7000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) QTest::qWait(10);
    return ready();
}
bool displayedWithin(QQuickItem* view, QQuickItem* target) {
    if (!target || !target->isVisible() || target->width() <= 0 || target->height() <= 0) return false;
    const QRectF viewport(view->mapToScene(QPointF{}), QSizeF(view->width(), view->height()));
    return viewport.contains(target->mapToScene(QPointF(target->width() / 2, target->height() / 2)));
}
QJsonObject pod(const QString& name, const QString& kind = "Pod", const QString& version = "v1") {
    return {{"apiVersion", version}, {"kind", kind}, {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", "uid-" + name}, {"resourceVersion", "1"}}},
        {"status", QJsonObject{{"phase", "Running"}}}, {"data", QJsonObject{{"token", "secret-value-must-not-appear"}}}};
}
// Only the external Kubernetes HTTP boundary is simulated; stores, TLS resolution,
// request scheduling, QML, filtering, selection and clipboard use their real implementations.
class KubernetesBoundary final : public QTcpServer {
public:
    const QDateTime measured = QDateTime::currentDateTimeUtc();
    QString scenario;
    QStringList requests;
    QStringList methods;
    QList<qint64> starts;
    QElapsedTimer clock;
    QByteArray authorization;
    QList<QByteArray> authorizations;
    int refresh = 0;
    int detailReads = 0;
    int valueLists = 0;
    bool delayedReadDelivered = false;
    bool prematureDisconnect = false;
    explicit KubernetesBoundary(QString value) : scenario(std::move(value)) {
        clock.start();
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n")) return;
                    socket->disconnect(this);
                    const auto url = QUrl::fromEncoded(bytes->split(' ')[1]);
                    const auto path = url.path();
                    requests.append(url.toString());
                    methods.append(QString::fromLatin1(bytes->left(bytes->indexOf(' '))));
                    starts.append(clock.elapsed());
                    for (const auto& line : bytes->split('\n'))
                        if (line.startsWith("Authorization:")) authorization = line.mid(14).trimmed();
                    authorizations.append(authorization);
                    int status = 200;
                    QJsonObject document;
                    QByteArray raw;
                    if (path == "/api") {
                        document = {{"versions", QJsonArray{"v1"}}};
                        if (scenario == "auth") status = 401;
                        if (scenario == "redirect") status = 302;
                        if (scenario == "malformed") raw = "{bad JSON: secret-value-must-not-appear";
                        if (scenario == "invalid_discovery") document = {{"versions", QJsonArray{"../escape"}}};
                    } else if (path == "/apis") {
                        document = {{"groups", QJsonArray{QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "example.test/v1"}}}}}}};
                    } else if (path == "/api/v1") {
                        document = {{"resources", QJsonArray{
                            QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
                            QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}},
                            QJsonObject{{"name", "pods/log"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get"}}}}}};
                    } else if (path == "/apis/example.test/v1") {
                        document = {{"resources", QJsonArray{QJsonObject{{"name", "widgets"}, {"kind", "Widget"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}}}}};
                    } else if (path == "/api/v1/events") {
                        status = scenario == "forbidden" ? 403 : 200;
                        document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{}}};
                        if (scenario == "find_event") {
                            QJsonArray events;
                            for (const auto& name : QStringList{"alpha", "bravo"}) events.append(QJsonObject{{"apiVersion", "v1"}, {"kind", "Event"},
                                {"metadata", QJsonObject{{"name", "event-" + name}, {"namespace", "default"}, {"uid", "event-uid-" + name}}},
                                {"reason", "Probe"}, {"message", "Cached probe " + name}, {"type", "Normal"}, {"count", 1},
                                {"lastTimestamp", measured.toString(Qt::ISODateWithMs)}, {"involvedObject", QJsonObject{{"name", name}, {"namespace", "default"}, {"kind", "Pod"}, {"uid", "uid-" + name}}}});
                            document["items"] = events;
                        }
                    } else if (path == "/apis/example.test/v1/widgets") {
                        document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{pod("custom-widget", "Widget", "example.test/v1")}}};
                    } else if (path == "/api/v1/pods") {
                        ++refresh;
                        const bool second = !url.query().isEmpty() && url.query().contains("continue=");
                        const QString token = second && scenario != "repeated_page" ? "" : "next token";
                        document = {{"metadata", QJsonObject{{"continue", token}}}, {"items", QJsonArray{pod(second ? "bravo" : "alpha")}}};
                        if (scenario == "invalid_list") document["items"] = QJsonArray{QJsonObject{{"kind", "Pod"}, {"metadata", QJsonObject{{"name", "../escape"}}}}};
                        if (scenario == "rate_limit" && refresh == 1) status = 429;
                        if (scenario == "retain" && refresh >= 3) status = 500;
                        if (scenario == "cache_expiry" && refresh >= 3) status = 500;
                        if (scenario == "sync" && refresh >= 3) document["items"] = QJsonArray{pod(second ? "bravo" : "alpha-updated")};
                        if (scenario == "periodic_sync") {
                            auto values = document["items"].toArray(); auto value = values.first().toObject();
                            auto metadata = value["metadata"].toObject(); metadata["resourceVersion"] = QString::number(refresh); value["metadata"] = metadata; document["items"] = QJsonArray{value};
                        }
                    } else if (path.startsWith("/api/v1/namespaces/default/pods/")) {
                        document = pod(path.section('/', -1));
                    } else if (path.startsWith("/apis/example.test/v1/namespaces/default/widgets/")) {
                        document = pod(path.section('/', -1), "Widget", "example.test/v1");
                    } else { status = 404; }
                    if (scenario.startsWith("inspector_") && !scenario.startsWith("inspector_related_")) {
                        if (path == "/apis") document = {{"groups", scenario == "inspector_custom_secret" ? QJsonArray{QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "example.test/v1"}}}}} : QJsonArray{}}};
                        else if (path == "/api/v1") document = {{"resources", QJsonArray{
                            QJsonObject{{"name", "configmaps"}, {"kind", "ConfigMap"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
                            QJsonObject{{"name", "secrets"}, {"kind", "Secret"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                        else if (path == "/api/v1/configmaps" || path == "/api/v1/secrets") {
                            status = 200;
                            document = {{"metadata", QJsonObject{}}, {"items", path.endsWith("secrets")
                                ? QJsonArray{pod("alpha", "Secret")} : QJsonArray{pod("bravo", "ConfigMap"), pod("charlie", "ConfigMap")}}};
                            if (path == "/api/v1/configmaps" && ++valueLists > 1 && scenario == "inspector_list_wrong_version") {
                                auto items = document["items"].toArray(); auto first = items[0].toObject(); first["apiVersion"] = "example.test/v1"; items[0] = first; document["items"] = items;
                            }
                        } else if (path.startsWith("/api/v1/namespaces/default/configmaps/") || path.startsWith("/api/v1/namespaces/default/secrets/")) {
                            status = 200; ++detailReads;
                            const bool secret = path.contains("/secrets/");
                            document = pod(path.section('/', -1), secret ? "Secret" : "ConfigMap");
                            auto metadata = document["metadata"].toObject(); metadata["resourceVersion"] = scenario == "inspector_secret_session" || scenario == "inspector_edit_same_version" ? "1" : QString::number(detailReads);
                            if (scenario == "inspector_edit_identity") metadata.remove("resourceVersion");
                            document["metadata"] = metadata;
                            if (secret) {
                                document["data"] = QJsonObject{{"alpha", "YWxwaGEtcHJpdmF0ZS12YWx1ZQ=="}, {"beta", "YmV0YS1wcml2YXRlLXZhbHVl"}, {"binary", "AAH/"}, {"empty", ""}};
                                if (scenario == "inspector_secret_table_hover") {
                                    auto data = document["data"].toObject();
                                    data["alpha"] = QString::fromLatin1((QByteArray("<b>literal secret</b>\n") + QByteArray(600, 'x')).toBase64());
                                    document["data"] = data;
                                }
                                if (scenario == "inspector_secret_stringdata") document["stringData"] = QJsonObject{{"gamma", "string-private-value"}};
                                metadata["annotations"] = QJsonObject{{"kubectl.kubernetes.io/last-applied-configuration", QJsonDocument(document).toJson(QJsonDocument::Compact).constData()}, {"note", "ordinary annotation"}};
                                document["metadata"] = metadata;
                            } else {
                                QJsonObject data{{"message", "ordinary-value-v" + QString::number(scenario == "inspector_edit_same_version" ? 1 : detailReads)}, {"numericText", "123"}, {"booleanText", "true"}, {"nullText", "null"}};
                                for (int index = 0; index < 120; ++index) data.insert(QString("line%1").arg(index, 3, 10, QChar('0')), "ordinary readable line");
                                if (scenario == "inspector_markup") data["message"] = "<img src=\"http://127.0.0.1:" + QString::number(serverPort()) + "/presentation-leak\"/>";
                                document["data"] = data; document["immutable"] = false;
                            }
                            if (detailReads > 1) {
                                if (scenario == "inspector_failure" || scenario == "inspector_edit_failure" || scenario == "inspector_yaml_expiry_failure") status = 500;
                                if (scenario == "inspector_forbidden") status = 403;
                                if (scenario == "inspector_auth" || scenario == "inspector_edit_auth" || scenario == "inspector_yaml_expiry_auth") status = 401;
                                if (scenario == "inspector_redirect") status = 302;
                                if (scenario == "inspector_backoff") status = 429;
                                if (scenario == "inspector_malformed" || scenario == "inspector_edit_malformed") raw = "{bad response: alpha-private-value";
                                if (scenario == "inspector_secret_invalid_encoding") document["data"] = QJsonObject{{"alpha", "not@@base64"}};
                                if (scenario == "inspector_secret_invalid_field") document["data"] = "alpha-private-value";
                                if (scenario == "inspector_secret_invalid_key") document["data"] = QJsonObject{{"../alpha", "YWxwaGEtcHJpdmF0ZS12YWx1ZQ=="}};
                                if (scenario == "inspector_configmap_invalid_value") document["data"] = QJsonObject{{"message", false}};
                                if (scenario == "inspector_wrong_version") document["apiVersion"] = "example.test/v1";
                                if (status != 200) raw = "alpha-private-value YWxwaGEtcHJpdmF0ZS12YWx1ZQ==";
                            }
                        }
                        if (scenario == "inspector_custom_secret") {
                            const QJsonObject custom{{"apiVersion", "example.test/v1"}, {"kind", "Secret"},
                                {"metadata", QJsonObject{{"name", "custom-secret"}, {"namespace", "default"}, {"uid", "custom-uid"}, {"resourceVersion", "1"}}},
                                {"data", QJsonObject{{"nested", QJsonObject{{"enabled", true}, {"count", 42}, {"ratio", 0.125}, {"items", QJsonArray{QJsonValue::Null, "null", false}}}}}}};
                            if (path == "/apis/example.test/v1") document = {{"resources", QJsonArray{QJsonObject{{"name", "secrets"}, {"kind", "Secret"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                            else if (path == "/apis/example.test/v1/secrets") { status = 200; document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{custom}}}; }
                            else if (path == "/apis/example.test/v1/namespaces/default/secrets/custom-secret") { status = 200; document = custom; ++detailReads; }
                        }
                    }
                    if (scenario == "missing_kind") {
                        // Optional list kind is resolved by the discovery contract.
                        auto values = document["items"].toArray();
                        for (int index = 0; index < values.size(); ++index) { auto value = values[index].toObject(); value.remove("kind"); values[index] = value; }
                        if (document.contains("items")) document["items"] = values;
                    }
                    if (scenario.startsWith("problems_reference_")) {
                        const bool workload=scenario.contains("statefulset"), job=scenario.contains("job");
                        const QString version=workload ? "apps/v1" : job ? "batch/v1" : "v1";
                        const QString kind=workload ? "StatefulSet" : job ? "Job" : "Pod";
                        const QString plural=workload ? "statefulsets" : "jobs";
                        auto target=pod("target",kind,version);
                        auto meta=target["metadata"].toObject(); meta["creationTimestamp"]=measured.addSecs(scenario.endsWith("starting") ? -60 : -600).toString(Qt::ISODateWithMs);
                        if (scenario.endsWith("terminating")) meta["deletionTimestamp"]=measured.toString(Qt::ISODateWithMs);
                        target["metadata"]=meta;
                        if (workload) { target["spec"]=QJsonObject{{"replicas",3}}; target["status"]=QJsonObject{{"readyReplicas",scenario.endsWith("unready") ? 1 : 3}}; }
                        else if (job) target["status"]=QJsonObject{{"failed",scenario.endsWith("failed") ? 1 : 0},{"succeeded",scenario.endsWith("complete") ? 1 : 0}};
                        else if (!scenario.endsWith("terminating")) target["status"]=QJsonObject{{"phase","Pending"}};
                        if (path=="/apis") document={{"groups",workload || job ? QJsonArray{QJsonObject{{"preferredVersion",QJsonObject{{"groupVersion",version}}}}} : QJsonArray{}}};
                        else if (path=="/apis/"+version) { status=200; document={{"resources",QJsonArray{QJsonObject{{"name",plural},{"kind",kind},{"namespaced",true},{"verbs",QJsonArray{"list"}}}}}}; }
                        else if (path=="/apis/"+version+"/"+plural) { status=200; document={{"metadata",QJsonObject{}},{"items",QJsonArray{target}}}; }
                        else if (path=="/api/v1/pods") {
                            QJsonArray values{pod("alpha"),pod("bravo")}; if (!workload && !job) values.append(target);
                            document={{"metadata",QJsonObject{}},{"items",values}};
                        }
                    }
                    if (scenario.startsWith("radar_water_")) {
                        if (path == "/api/v1/events") {
                            auto event = pod("radar-event", "Event");
                            event["type"] = "Normal"; event["reason"] = "Scheduled";
                            event["message"] = "Observed local scheduling";
                            event["involvedObject"] = QJsonObject{{"kind", "Pod"}, {"name", "alpha"}, {"namespace", "default"}};
                            document["items"] = QJsonArray{event};
                        }
                        if (scenario.startsWith("radar_water_color_")) {
                            const auto kind = scenario.mid(QString("radar_water_color_").size());
                            if (path == "/apis/example.test/v1") document["resources"] = QJsonArray{QJsonObject{{"name", "widgets"}, {"kind", kind}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}}};
                            if (path == "/apis/example.test/v1/widgets") document["items"] = QJsonArray{pod("custom-widget", kind, "example.test/v1")};
                        }
                    }
                    if ((scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_initial_population" || scenario == "radar_reopen_position" || scenario.startsWith("radar_pooled_") || scenario == "columns_pin_all") && path == "/api/v1/pods") {
                        QJsonArray values;
                        const int start = url.query().contains("continue=") ? 500 : 0;
                        for (int index = start; index < start + 500; ++index) values.append(pod(QString("radar-%1").arg(index, 4, 10, QChar('0'))));
                        document["items"] = values;
                    }
                    if (scenario.startsWith("inspector_related_") || scenario.startsWith("filter_event_")) {
                        if (scenario.startsWith("inspector_related_alias_") && path == "/apis") {
                            auto groups = document["groups"].toArray();
                            groups.append(QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "events.k8s.io/v1"}}}});
                            document["groups"] = groups;
                        }
                        if (scenario.startsWith("inspector_related_alias_") && path == "/apis/events.k8s.io/v1") {
                            status = 200;
                            document = {{"resources", QJsonArray{QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                        }
                        if (scenario.startsWith("inspector_related_alias_") && path == "/apis/events.k8s.io/v1/events") {
                            status = 200;
                            auto event = pod("related-event", "Event", "events.k8s.io/v1");
                            if (scenario == "inspector_related_alias_distinct" || scenario == "inspector_related_alias_reuse") {
                                auto metadata = event["metadata"].toObject(); metadata["uid"] = "uid-modern-distinct"; event["metadata"] = metadata;
                            }
                            event["regarding"] = QJsonObject{{"uid", "uid-alpha"}, {"name", "alpha"}, {"namespace", "default"}};
                            event["reason"] = "Scheduled"; event["note"] = "Modern event message"; event["type"] = "Normal";
                            event["series"] = QJsonObject{{"count", 7}, {"lastObservedTime", "2026-10-03T10:01:00Z"}};
                            if (scenario == "inspector_related_alias_legacy_time") {
                                event.remove("series"); event["eventTime"] = QJsonValue::Null;
                                event["deprecatedCount"] = 7; event["deprecatedLastTimestamp"] = "2026-10-03T10:03:00Z";
                            }
                            document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{event}}};
                        }
                        if (path == "/api/v1/events") {
                            auto event = pod("related-event", "Event");
                            event["involvedObject"] = QJsonObject{{"uid", scenario == "inspector_related_wrong_uid" || scenario == "inspector_related_event_missing" ? "replaced-alpha" : "uid-alpha"}, {"name", "alpha"}, {"namespace", "default"}};
                            event["reason"] = "Scheduled"; event["message"] = "Local event message"; event["type"] = "Normal"; event["count"] = 3;
                            event["lastTimestamp"] = "2026-10-03T10:00:00Z";
                            document["items"] = QJsonArray{event};
                            if (scenario == "inspector_related_workspace" || scenario.startsWith("inspector_related_event_") || scenario.startsWith("inspector_related_table_events_") || scenario.startsWith("filter_event_")) {
                                auto second = event; auto metadata = second["metadata"].toObject(); metadata["name"] = "related-event-second"; metadata["uid"] = "uid-related-event-second";
                                second["metadata"] = metadata; second["count"] = 12; second["reason"] = "Started"; second["message"] = "Second event message";
                                second["lastTimestamp"] = "2026-10-03T11:00:00+01:00";
                                document["items"] = QJsonArray{event, second};
                            }
                        }
                        const auto relate = [&](QJsonObject object) {
                            auto metadata = object["metadata"].toObject();
                            if (metadata["name"] == "alpha") metadata["ownerReferences"] = QJsonArray{QJsonObject{{"uid", "uid-custom-widget"}, {"name", "custom-widget"}, {"kind", "Widget"}, {"apiVersion", "example.test/v1"}}};
                            if (metadata["name"] == "bravo") metadata["ownerReferences"] = QJsonArray{QJsonObject{{"uid", "uid-alpha"}, {"name", "alpha"}, {"kind", "Pod"}, {"apiVersion", "v1"}}};
                            object["metadata"] = metadata; return object;
                        };
                        if (document.contains("items") && path != "/api/v1/events") {
                            auto values = document["items"].toArray();
                            for (int index = 0; index < values.size(); ++index) values[index] = relate(values[index].toObject());
                            document["items"] = values;
                        } else if (path.contains("/pods/")) document = relate(document);
                    }
                    if (scenario == "markup") {
                        auto values = document["items"].toArray();
                        for (int index = 0; index < values.size(); ++index) {
                            auto value = values[index].toObject();
                            value["status"] = QJsonObject{{"phase", "<img src=\"http://127.0.0.1:" + QString::number(serverPort()) + "/presentation-leak\"/>"}};
                            values[index] = value;
                        }
                        if (document.contains("items")) document["items"] = values;
                    }
                    if (scenario.startsWith("table_")) {
                        if (path == "/apis") document = {{"groups", QJsonArray{
                            QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "example.test/v1"}}}},
                            QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "metrics.k8s.io/v1beta1"}}}}}}};
                        if (path == "/apis/metrics.k8s.io/v1beta1") {
                            status = 200;
                            document = {{"resources", QJsonArray{QJsonObject{
                                {"name", "pods"}, {"kind", "PodMetrics"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                        }
                        const bool overviewDetail = scenario.startsWith("table_overview_") && path.startsWith("/api/v1/namespaces/default/pods/");
                        if (path == "/api/v1/pods" || path == "/apis/metrics.k8s.io/v1beta1/pods" || overviewDetail) {
                            status = 200;
                            QJsonArray values;
                            const bool metrics = path.contains("metrics.k8s.io");
                            for (int index = 0; index < (metrics ? 2 : 3); ++index) {
                                const auto name = QStringList{"alpha", "bravo", "charlie"}[index];
                                if (overviewDetail && name != path.section('/', -1)) continue;
                                auto value = pod(name, metrics ? "PodMetrics" : "Pod", metrics ? "metrics.k8s.io/v1beta1" : "v1");
                                auto metadata = value["metadata"].toObject();
                                if (index < 2) metadata["creationTimestamp"] = measured.addDays(index == 0 ? -2 : -10).toString(Qt::ISODateWithMs);
                                metadata["ownerReferences"] = QJsonArray{QJsonObject{{"uid", "owner-uid"}, {"kind", "ReplicaSet"}, {"apiVersion", "apps/v1"}, {"name", "workload"}}};
                                value["metadata"] = metadata;
                                QJsonArray containers, states;
                                const int desired = index == 0 ? 10 : index == 1 ? 2 : 1;
                                const int ready = index == 0 && (scenario == "table_overview_full" || (scenario == "table_overview_refresh" && refresh > 1)) ? 10
                                    : index == 0 && scenario == "table_overview_none" ? 0 : index == 0 ? 2 : 1;
                                for (int container = 0; container < desired; ++container) {
                                    const auto containerName = "worker" + QString::number(container);
                                    if (metrics) containers.append(QJsonObject{{"name", containerName}, {"usage", QJsonObject{
                                        {"cpu", container > 0 || scenario == "table_zero" ? "0" : index == 0 ? "2" : "500m"},
                                        {"memory", container > 0 || scenario == "table_zero" ? "0" : index == 0 ? "2Gi" : "1024Mi"}}}});
                                    else containers.append(QJsonObject{{"name", containerName}, {"image", "registry.example/" + QString(90, 'a') + ":" + QString::number(refresh)},
                                        {"resources", QJsonObject{{"requests", QJsonObject{{"cpu", "10m"}, {"memory", "16Mi"}, {"ephemeral-storage", "1Gi"}}}}}});
                                    states.append(QJsonObject{{"name", containerName}, {"ready", container < ready},
                                        {"restartCount", container == 0 ? index == 0 ? 10 : 2 : 0}});
                                }
                                if (metrics) { value["timestamp"] = measured.toString(Qt::ISODateWithMs); value["window"] = "15s"; value["containers"] = containers; }
                                else { value["spec"] = QJsonObject{{"containers", containers}, {"nodeName", "local-node"}};
                                    value["status"] = QJsonObject{{"phase", scenario=="table_status_colors" ? index==0 ? "Running" : index==1 ? "Pending" : "Failed" : scenario == "table_overview_pending" && index == 0 ? "Pending" : "Running"}, {"containerStatuses", index == 2 || (scenario == "table_overview_pending" && index == 0) ? QJsonArray{} : states}}; }
                                values.append(value);
                            }
                            document = overviewDetail ? values.first().toObject() : QJsonObject{{"metadata", QJsonObject{}}, {"items", values}};
                        }
                    }
                    if (scenario.startsWith("workload_")) {
                        const QString kind = scenario == "workload_replicaset" ? "ReplicaSet" : scenario == "workload_statefulset" ? "StatefulSet"
                            : scenario == "workload_daemonset" ? "DaemonSet" : scenario == "workload_job" || scenario == "workload_images_empty" ? "Job"
                            : scenario == "workload_cronjob" ? "CronJob" : scenario == "workload_custom_pod" ? "Pod"
                            : scenario.startsWith("workload_status_node_") ? "Node" : scenario.startsWith("workload_status_replicaset_") ? "ReplicaSet"
                            : scenario == "workload_event_owner" || scenario == "workload_modern_event_owner" ? "Event" : "Deployment";
                        const QString version = scenario.startsWith("workload_custom_") ? "example.test/v1" : kind == "Job" || kind == "CronJob" ? "batch/v1"
                            : kind == "Node" ? "v1" : kind == "Event" ? (scenario == "workload_modern_event_owner" ? "events.k8s.io/v1" : "v1") : "apps/v1";
                        const QString collection = kind.toLower()+"s", base = version == "v1" ? "/api/v1" : "/apis/"+version;
                        status = 200;
                        if (path == "/api/v1") document = {{"resources", version == "v1" ? QJsonArray{QJsonObject{{"name", collection}, {"kind", kind}, {"namespaced", kind != "Node"}, {"verbs", QJsonArray{"get", "list"}}}} : QJsonArray{}}};
                        else if (path == "/apis") document = {{"groups", version == "v1" ? QJsonArray{} : QJsonArray{QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", version}}}}}}};
                        else if (path == base && version != "v1") document = {{"resources", QJsonArray{QJsonObject{{"name", collection}, {"kind", kind}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                        else if (path == base+"/"+collection || (scenario == "workload_overview_zero_replicas" && path == base+"/namespaces/default/"+collection+"/alpha")) {
                            auto resource=pod("alpha",kind,version);
                            if (kind == "Node") { auto metadata=resource["metadata"].toObject(); metadata.remove("namespace"); resource["metadata"]=metadata; }
                            QJsonArray containers{QJsonObject{{"name","main"},{"image","registry.example/main:2"}},QJsonObject{{"name","sidecar"},{"image","registry.example/sidecar:3"}}};
                            if (scenario == "workload_images_empty") containers=QJsonArray{QJsonObject{{"name","main"}}};
                            const QJsonObject templateSpec{{"containers",containers}};
                            QJsonObject spec{{"replicas",4},{"template",QJsonObject{{"spec",templateSpec}}}};
                            if (kind == "CronJob") spec=QJsonObject{{"jobTemplate",QJsonObject{{"spec",QJsonObject{{"template",QJsonObject{{"spec",templateSpec}}}}}}}};
                            if (kind == "Pod") spec=templateSpec;
                            QJsonObject state{{"readyReplicas",3}};
                            if (scenario == "workload_default_replicas") { spec.remove("replicas"); state["readyReplicas"]=0; }
                            if (scenario == "workload_zero_replicas" || scenario == "workload_overview_zero_replicas") { spec["replicas"]=0; state["readyReplicas"]=0; }
                            if (scenario == "workload_missing_ready") state.remove("readyReplicas");
                            if (scenario == "workload_status_deployment_available") state["availableReplicas"]=4;
                            if (scenario == "workload_status_replicaset_available") state["readyReplicas"]=4;
                            if (scenario == "workload_status_replicaset_unavailable") state["readyReplicas"]=0;
                            if (scenario == "workload_status_replicaset_zero") { spec["replicas"]=0; state["readyReplicas"]=0; }
                            if (kind == "Node") {
                                const auto status=scenario == "workload_status_node_ready" ? "True" : scenario == "workload_status_node_false" ? "False" : "Unknown";
                                state=QJsonObject{{"conditions", scenario == "workload_status_node_missing" ? QJsonArray{} : QJsonArray{QJsonObject{{"type","Ready"},{"status",status}}}}};
                            }
                            resource["spec"]=spec; resource["status"]=state;
                            if (kind == "Event") resource[version == "v1" ? "involvedObject" : "regarding"]=QJsonObject{{"kind","Pod"},{"name","alpha"},{"uid","event-target"}};
                            document=path.contains("/namespaces/") ? resource : QJsonObject{{"metadata",QJsonObject{}},{"items",QJsonArray{resource}}};
                        }
                    }
                    if (raw.isEmpty()) raw = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    const auto response = "HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nRetry-After: 1\r\nLocation: http://127.0.0.1:9/credential-leak\r\nContent-Length: " + QByteArray::number(raw.size()) + "\r\n\r\n" + raw;
                    const QPointer<QTcpSocket> live(socket);
                    const int delay = scenario.startsWith("inspector_") && path.contains("/namespaces/default/") ? 600 : (scenario == "slow_close" && path.startsWith("/api/v1/namespaces/default/pods/"))
                        || ((scenario == "coalesce" || scenario == "detail_expiry") && path.startsWith("/api/v1/namespaces/default/pods/"))
                        || ((scenario == "hidden_followup" || scenario == "shutdown_pending") && path == "/api") ? 400 : 0;
                    QTimer::singleShot(delay, this, [this, live, response, delay] {
                        if (live && live->state() == QAbstractSocket::ConnectedState) {
                            const bool accepted = live->write(response) == response.size();
                            if (delay) delayedReadDelivered = accepted;
                            live->disconnectFromHost();
                        } else if (delay) prematureDisconnect = true;
                    });
                    if (delay) connect(socket, &QTcpSocket::disconnected, this, [this] { if (!delayedReadDelivered) prematureDisconnect = true; });
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                });
            }
        });
    }
};
QQuickItem* visualItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name || (name == "closeSession" && root->objectName().startsWith("closeSession_"))) return root;
    for (auto* child : root->childItems()) if (auto* found = visualItem(child, name)) return found;
    return nullptr;
}
QQuickItem* item(QObject* root, const QString& name) {
    const auto parts = name.split('_');
    if (parts.size() == 2 && (parts[0] == "header" || parts[0] == "pinnedHeader" || parts[0].endsWith("Header"))) {
        const QStringList views = parts[0] == "pinnedHeader" ? QStringList{"resourcePinnedHeaderView", "eventPinnedHeaderView", "portPinnedHeaderView", "inspectorEventPinnedHeaderView", "inspectorLinkPinnedHeaderView", "valuePinnedHeaderView"}
            : QStringList{(parts[0] == "header" ? QString("resource") : parts[0].left(parts[0].size() - 6)) + "HeaderView"};
        for (const auto& viewName : views) {
            auto* view = item(root, viewName);
            if (!view || !view->isVisible() || view->width() <= 0) continue;
            QQmlExpression expression(qmlContext(view), view,
                QString("(function() { for (let column = 0; column < columns; ++column) { const header = itemAtCell(Qt.point(column, 0)); if (header && header.objectName === '%1') return header; } return null; })()").arg(name));
            auto* header = expression.evaluate().value<QQuickItem*>();
            if (expression.hasError()) std::fprintf(stderr, "Header lookup failed: %s\n", qPrintable(expression.error().toString()));
            if (header) return header;
        }
        return nullptr;
    }
    if (parts.size() == 3 && (parts[0] == "cell" || parts[0] == "eventCell" || parts[0] == "pinnedCell"
        || parts[0] == "inspectorEventCell" || parts[0] == "inspectorLinkCell")) {
        const QStringList views = parts[0] == "pinnedCell" ? QStringList{"resourcePinnedTable", "eventPinnedTable"}
            : QStringList{parts[0] == "cell" ? "resourceTable" : parts[0].left(parts[0].size() - 4) + "Table"};
        for (const auto& viewName : views) {
            auto* view = item(root, viewName);
            if (!view || !view->isVisible() || view->width() <= 0) continue;
            // Use the public view API: cached delegates can keep the same object name.
            QQmlExpression expression(qmlContext(view), view, QString("itemAtIndex(model.index(%1, %2))").arg(parts[1], parts[2]));
            auto* cell = expression.evaluate().value<QQuickItem*>();
            if (expression.hasError()) std::fprintf(stderr, "Table cell lookup failed: %s\n", qPrintable(expression.error().toString()));
            if (cell) return cell;
        }
        return nullptr;
    }
    auto* window = qobject_cast<QQuickWindow*>(root);
    if (window) if (auto* found = podlord::test::visibleItem(window->contentItem(), name)) return found;
    if (auto* found = root->findChild<QQuickItem*>(name)) return found;
    return window ? visualItem(window->contentItem(), name) : nullptr;
}
QString text(QObject* root, const QString& name) {
    auto* value = item(root, name); return value ? value->property("text").toString() : QString{};
}
bool click(QQuickWindow* window, QQuickItem* target, Qt::MouseButton button = Qt::LeftButton) {
    const auto name = target ? target->objectName() : QString{};
    if (!podlord::test::revealWorkspaceAction(window, name)) return false;
    if (!name.isEmpty()) target = item(window, name);
    if (target && !target->isVisible()) {
        const auto name=target->objectName();
        if (name=="sourcesButton" || name=="radarWorkspaceButton" || name=="resourceFieldFilters" || name=="renameCurrentSession")
            if (!click(window,item(window,"toggleSidebar"))) return false;
    }
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    QList<QQuickItem*> ancestors;
    for (auto* parent = target; parent; parent = parent->parentItem()) ancestors.prepend(parent);
    for (auto* parent : ancestors) parent->ensurePolished();
    QCoreApplication::processEvents();
    QSignalSpy frame(window, &QQuickWindow::frameSwapped);
    window->update();
    if (!frame.wait(1000)) return false;
    if (!name.isEmpty()) target = item(window, name);
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    if (!podlord::test::scrollIntoView(window,target)) return false;
    // Scrolling can recycle a delegate into another row or column before input.
    if (!name.isEmpty() && target->objectName() != name) {
        std::fprintf(stderr, "Pointer target recycled: %s -> %s\n", qPrintable(name), qPrintable(target->objectName()));
        return false;
    }
    const auto position = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    QTest::mouseClick(window, button, Qt::NoModifier, position); return true;
}
bool type(QQuickWindow* window, QQuickItem* target, const QString& value) {
    if (target && !target->isVisible() && (target->objectName() == "resourceFilter" || target->objectName() == "eventFilter"))
        if (!click(window, item(window, "workspaceSearchButton")) || !waitFor([&] { return target->isVisible(); })) return false;
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    target->forceActiveFocus();
    QTest::keySequence(window, QKeySequence::SelectAll);
    if (value.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
    for (const auto c : value) QTest::keyClick(window, c.toLatin1());
    return true;
}
bool execute(const QString& scenario) {
    const bool twoContexts = scenario == "view_restore_scope" || scenario == "sources_open_context" || scenario == "table_cluster_switch" || scenario == "radar_session" || scenario == "radar_island_session" || scenario == "inactive_sync" || scenario == "inspector_secret_session" || scenario.startsWith("inspector_edit_session_") || scenario.startsWith("inspector_edit_open_context_");
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    KubernetesBoundary server(scenario == "shell_search_events" ? "filter_event_message" : scenario == "query_event" ? "find_event" : scenario);
    if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    const auto source = temporary.filePath("source.config");
    QFile file(source);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QByteArray credentials = "token: local-test-token";
    if (scenario == "relative_token") {
        QFile token(temporary.filePath("token"));
        if (!token.open(QIODevice::WriteOnly) || token.write("local-test-token\n") < 0) return false;
        credentials = "tokenFile: token";
    }
    if (scenario == "empty_token") {
        QFile token(temporary.filePath("token")); if (!token.open(QIODevice::WriteOnly) || token.write("\n") < 0) return false;
        credentials = "tokenFile: token";
    }
    if (scenario == "plugin") credentials = "exec: {command: /must-not-execute, apiVersion: client.authentication.k8s.io/v1}";
    if (scenario == "basic") credentials = "username: local-user\n    password: local-password";
    if (scenario == "invalid_ca") credentials = "token: local-test-token";
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:" + QByteArray::number(server.serverPort())
        + (scenario == "invalid_ca" ? "\n    certificate-authority-data: bm90LWNlcnRpZmljYXRl" : "")
        + (scenario == "table_cluster_switch" ? "\n- name: other\n  cluster:\n    server: http://127.0.0.1:"+QByteArray::number(server.serverPort()) : "")
        + "\nusers:\n- name: local\n  user:\n    " + credentials
        + (twoContexts ? "\n- name: inactive\n  user: {token: inactive-token}" : "")
        + "\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n"
        + (scenario == "table_cluster_switch" ? "- name: inactive\n  context: {cluster: other, user: inactive}\n" : twoContexts ? "- name: inactive\n  context: {cluster: local, user: inactive}\n" : "");
    if (file.write(yaml) != yaml.size()) return false;
    file.close();
    const auto profile = temporary.filePath("profile");
    QDateTime reference = QDateTime::currentDateTimeUtc();
    if (scenario == "settings_invalid") {
        if (!QDir().mkpath(profile)) return false;
        QFile invalid(QDir(profile).filePath("read-settings.json"));
        if (!invalid.open(QIODevice::WriteOnly) || invalid.write("{\"version\":1,\"requestHardLimitPerMinute\":-1,\"inactiveSyncMinutes\":0}") < 0) return false;
    }
    podlord::Workspace workspace(profile, nullptr, [&] { return reference; });
    if (scenario.startsWith("accessibility_")) QAccessible::setActive(true);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return window->isVisible(); })) return false;
    QTest::qWait(10);
    if (!waitFor([&] { return item(window, "importButton")->isEnabled(); })) return false;
    if (scenario == "accessibility_empty") return accessibleName(window, "Workspace actions")
        && accessibleName(window, "Kubeconfig file or folder path") && server.requests.isEmpty();
    if (scenario == "empty_brand") {
        auto* logo = item(window, "resourceEmptyLogo");
        return logo && waitFor([&] { return logo->isVisible() && logo->property("status").toInt() == 1; })
            && !text(window, "emptyResources").isEmpty() && server.requests.isEmpty();
    }
    if (scenario == "shell_footer_empty") return text(window, "resourceMatchCount") == "visible: 0/0"
        && text(window, "syncStatus") == "API: 0/min  Synced: never" && server.requests.isEmpty();
    if (scenario.startsWith("radar_water_")) {
        if (!waitFor([&] { return !workspace.alerts()->busy(); })) return false;
        for (const auto& entry:workspace.alerts()->rules()) {
            auto rule=entry.toMap(); rule["enabled"]=false;
            if (!workspace.alerts()->saveRule(rule) || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
        }
        if (scenario=="radar_water_interaction" && (!workspace.saveRadarWater(true,100) || !waitFor([&] { return !workspace.busy(); }))) return false;
    }
    if (scenario == "settings_save" || scenario == "settings_restore" || scenario == "settings_busy" || scenario == "settings_conflict" || scenario == "limit") {
        if (!click(window, item(window, "settingsWorkspaceButton")) || !click(window, item(window, "settingsSyncSection"))) return false;
        auto* input = item(window, "inlineRequestLimit")->property("contentItem").value<QQuickItem*>();
        if (!type(window, input, "120")) return false;
        input->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Tab);
        auto* inactiveInput = item(window, "inlineInactiveMinutes")->property("contentItem").value<QQuickItem*>();
        if (!type(window, inactiveInput, "1")) return false;
        inactiveInput->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Tab);
        QLockFile lock(QDir(profile).filePath("read-settings.json.lock"));
        if (scenario == "settings_busy") { if (!QDir().mkpath(profile) || !lock.tryLock(0)) return false; }
        if (scenario == "settings_conflict") {
            const auto changed = podlord::ReadSettingsStore(profile).save({121, 0}, {});
            if (!std::holds_alternative<podlord::ReadSettings>(changed)) return false;
        }
        if (!click(window, item(window, "inlineSaveSync")) || !waitFor([&] { return !workspace.busy(); })) return false;
        if (scenario == "settings_busy" || scenario == "settings_conflict") return !workspace.error().isEmpty() && workspace.requestLimit() == 0;
        if (workspace.requestLimit() != 120 || workspace.inactiveSyncMinutes() != 1 || !workspace.error().isEmpty()) return false;
        if (scenario == "settings_save") return server.requests.isEmpty() && QFileInfo(QDir(profile).filePath("read-settings.json")).permissions() == (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
        if (scenario == "settings_restore") { podlord::Workspace restored(profile); return waitFor([&] { return restored.requestLimit() == 120 && restored.inactiveSyncMinutes() == 1 && !restored.busy(); }) && server.requests.isEmpty(); }
        if (!click(window, item(window, "resourcesWorkspaceButton"))) return false;
    }
    if (scenario == "empty") {
        auto* label = item(window, "emptyResources");
        auto* table = item(window, "resourceTable");
        const QRectF pane(table->mapToScene(QPointF{}), QSizeF(table->width(), table->height()));
        const QRectF message(label->mapToScene(QPointF{}), QSizeF(label->width(), label->height()));
        return label->isVisible() && pane.contains(message) && server.requests.isEmpty();
    }
    if (scenario.startsWith("quick_file_")) {
        const auto path = temporary.filePath("extensionless");
        if (scenario == "quick_file_invalid") {
            QFile invalid(path);
            if (!invalid.open(QIODevice::WriteOnly) || invalid.write("not a kubeconfig") < 0) return false;
        } else if (!QFile::copy(source, path)) return false;
        const auto input = scenario == "quick_file_missing" ? temporary.filePath("missing")
            : scenario == "quick_file_url" ? QUrl::fromLocalFile(path).toString() : path;
        if (scenario == "quick_file_profile") {
            if (!QDir().mkpath(profile)) return false;
            QFile corrupt(QDir(profile).filePath("sessions.json"));
            if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("invalid saved catalog") < 0) return false;
        }
        bool admitted = false;
        if (!QMetaObject::invokeMethod(&workspace, "quickImportFile", Q_RETURN_ARG(bool, admitted), Q_ARG(QString, input))
            || !admitted || !waitFor([&] { return !workspace.busy(); })) return false;
        if (scenario == "quick_file_profile") return !workspace.error().isEmpty() && workspace.contexts().isEmpty() && server.requests.isEmpty();
        if (scenario == "quick_file_invalid" || scenario == "quick_file_missing")
            return workspace.contexts().isEmpty() && workspace.sessions().isEmpty() && workspace.error().isEmpty()
                && workspace.sourceImportNotice().isEmpty() && server.requests.isEmpty();
        return workspace.contexts().size() == 1 && workspace.sessions().isEmpty() && workspace.currentSession().isEmpty()
            && server.requests.isEmpty();
    }
    if (!type(window, item(window, "sourcePath"), source) || !click(window, item(window, "importButton"))) return false;
    if (scenario.startsWith("settings_inline_")) {
        if (!waitFor([&] { return !workspace.busy(); }) || !click(window,item(window,"settingsWorkspaceButton"))) return false;
        const auto section=scenario.mid(QString("settings_inline_").size());
        const auto destination=section=="invalid" || section=="save" ? QString("Sync") : section=="narrow" ? QString("Appearance") : section.startsWith("runtime") || section.startsWith("diagnostic_") ? QString("Diagnostics") : section.left(1).toUpper()+section.mid(1);
        if (!click(window,item(window,"settings"+destination+"Section"))) return false;
        if (item(window,"sourceManagementPanel")->isVisible()) return false;
        if (section.startsWith("diagnostic_")) {
            const bool audit=section.contains("audit_");
            if (audit) {
                if (!workspace.openContext(workspace.contexts().first().toMap()["id"].toString())
                    || !waitFor([&] { return !workspace.loading() && !server.requests.isEmpty(); })
                    || !click(window,item(window,"settingsWorkspaceButton"))
                    || !podlord::test::selectSettingsSection(window,"diagnostics")) return false;
            }
            const auto prefix=audit ? QString("audit") : QString("diagnostic");
            if (!waitFor([&] { auto* table=item(window,prefix+"Table"); return table && table->property("rows").toInt()>(audit ? 0 : 10); })) {
                std::fprintf(stderr,"Shared diagnostic tables are absent.\n"); return false;
            }
            const auto calls=server.requests.size();
            auto* model=workspace.property(audit ? "requestAuditTable" : "diagnosticTable").value<QAbstractItemModel*>();
            if (!model) return false;
            if (section=="diagnostic_tables") {
                if (model->columnCount()!=3 || !item(window,"diagnosticSampledAt")
                    || item(window,"diagnosticSampledAt")->property("text").toString().isEmpty()) return false;
            } else if (section.endsWith("sort")) {
                auto* proxy=qobject_cast<QSortFilterProxyModel*>(model); if (!proxy) return false;
                for (int step=0;step<3;++step) {
                    if (!click(window,item(window,prefix+"Header_0")) || !waitFor([&] { return proxy->sortColumn()==(step==2 ? -1 : 0)
                        && (step==2 || proxy->sortOrder()==(step==0 ? Qt::AscendingOrder : Qt::DescendingOrder)); })) return false;
                }
            } else if (section.endsWith("copy")) {
                const int column=audit ? 2 : 1;
                const auto expected=model->index(0,column).data().toString();
                const auto name=audit ? prefix+"Cell_0_2" : "diagnosticValue_"+model->index(0,0).data(Qt::UserRole).toString();
                QGuiApplication::clipboard()->setText("untouched");
                if (!click(window,item(window,name))) return false;
                QTest::keySequence(window,QKeySequence::Copy);
                if (QGuiApplication::clipboard()->text()!=expected) return false;
            } else if (section=="diagnostic_audit_duration") {
                auto* table=item(window,"auditTable"); if (!table) return false;
                if (!podlord::test::scrollIntoView(window,table)) return false;
                for (int attempt=0;attempt<8 && !item(window,"auditHeader_5");++attempt) {
                    const auto position=table->mapToScene(QPointF(table->width()/2,table->height()/2));
                    QWheelEvent wheel(position,window->mapToGlobal(position.toPoint()),QPoint(-200,0),QPoint(-120,0),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
                    QCoreApplication::sendEvent(window,&wheel); QTest::qWait(10);
                }
                if (!click(window,item(window,"auditHeader_5"))) { std::fprintf(stderr,"Request duration header is not reachable.\n"); return false; }
                qint64 previous=-1; int measured=0;
                for (int row=0;row<model->rowCount();++row) {
                    const auto cell=model->index(row,5); const auto display=cell.data().toString();
                    if (display=="-") continue;
                    bool numeric=false; const auto duration=cell.data(Qt::UserRole+6).toLongLong(&numeric);
                    if (!numeric || duration<previous || duration!=display.section(' ',0,0).toLongLong()) {
                        std::fprintf(stderr,"Request duration has no numeric sort value.\n"); return false;
                    }
                    previous=duration; ++measured;
                }
                if (measured==0) { std::fprintf(stderr,"No completed request duration is visible.\n"); return false; }
            } else if (section.startsWith("diagnostic_sort_")) {
                const auto* proxy=qobject_cast<QSortFilterProxyModel*>(model); if (!proxy) return false;
                const auto column=section.endsWith("negative") ? -1 : section.endsWith("high") ? model->columnCount() : 0;
                const auto table=section.endsWith("table") ? QString("unsupported") : QString("diagnostic");
                const auto before=proxy->sortColumn();
                if (workspace.sortDiagnosticColumn(table,column) || proxy->sortColumn()!=before) return false;
            } else if (section.startsWith("diagnostic_copy_")) {
                const auto column=section.endsWith("negative") ? -1 : section.endsWith("high") ? model->columnCount() : 1;
                const auto table=section.endsWith("table") ? QString("unsupported") : QString("diagnostic");
                const auto identity=section.endsWith("empty") ? QString{} : section.endsWith("missing") ? QString("missing-counter")
                    : model->index(0,0).data(Qt::UserRole).toString();
                QGuiApplication::clipboard()->setText("untouched");
                if (workspace.copyDiagnosticCell(table,identity,column) || QGuiApplication::clipboard()->text()!="untouched") return false;
            } else if (section.endsWith("columns") || section.endsWith("columns_restart")) {
                if (!click(window,item(window,"diagnosticColumnsButton"))
                    || !click(window,item(window,"diagnosticColumnVisible_description"))
                    || !click(window,item(window,"diagnosticColumnPinned_label"))
                    || !click(window,item(window,"diagnosticSaveColumns"))
                    || !waitFor([&] { return !workspace.tableLayoutSaving(); }) || !workspace.tableLayoutError().isEmpty()) return false;
                const auto verify=[](const QVariantList& columns) {
                    bool hidden=false,pinned=false;
                    for (const auto& value:columns) { const auto column=value.toMap();
                        if (column["id"]=="description") hidden=!column["visible"].toBool();
                        if (column["id"]=="label") pinned=column["pinned"].toBool(); }
                    return hidden && pinned;
                };
                if (section.endsWith("_restart")) { podlord::Workspace reopened(profile); if (!verify(reopened.property("diagnosticColumns").toList())) return false; }
                else if (!verify(workspace.property("diagnosticColumns").toList())) return false;
            } else if (section.contains("find") || section.endsWith("narrow")) {
                if (section.endsWith("narrow")) {
                    window->resize(360,600);
                    if (!waitFor([&] { return window->contentItem()->width()==360 && window->contentItem()->height()==600; })
                        || !podlord::test::selectSettingsSection(window,"diagnostics")) return false;
                }
                if (section.endsWith("keyboard")) {
                    if (!click(window,item(window,audit ? "auditCell_0_2" : "diagnosticCell_0_0"))) return false;
                    QTest::keySequence(window,QKeySequence::Find);
                } else if (!click(window,item(window,prefix+"FindButton"))) return false;
                const bool empty=section.endsWith("find_empty");
                const auto expected=empty ? QString("0/0") : audit ? "1/"+QString::number(model->rowCount()) : QString("1/1");
                if (!type(window,item(window,prefix+"FindInput"),empty ? "no-such-counter" : audit ? "GET" : "RSS")
                    || !waitFor([&] { return text(window,prefix+"FindCount")==expected; })) return false;
                if (!empty && (!click(window,item(window,prefix+"FindNext")) || !click(window,item(window,prefix+"FindPrevious")))) return false;
            } else return false;
            const auto capture=qEnvironmentVariable("PODLORD_DIAGNOSTIC_SCREENSHOT");
            if (!capture.isEmpty() && !window->grabWindow().save(capture)) return false;
            return server.requests.size()==calls;
        }
        if (section.startsWith("runtime")) {
            if (!waitFor([&] { return item(window,"diagnosticValue_rss") && item(window,"diagnosticValue_memory")
                && item(window,"diagnosticValue_cpuTime") && item(window,"diagnosticValue_threads"); })) return false;
            auto* rss = item(window, "diagnosticValue_rss");
            auto* memory = item(window, "diagnosticValue_memory");
            auto* cpu = item(window, "diagnosticValue_cpuTime");
            auto* threads = item(window, "diagnosticValue_threads");
            if (!rss || !memory || !cpu || !threads || !rss->isVisible()) return false;
            const auto initial = rss->property("text").toString();
            const auto value = initial.section(' ', 0, 0).toDouble();
            if (value <= 0 || threads->property("text").toString().toInt() <= 0
                || memory->property("text").toString() == "Unavailable" || !cpu->property("text").toString().endsWith(" s")) return false;
            if (section == "runtime") return server.requests.isEmpty();
            const QByteArray allocation(64 * 1024 * 1024, 'a');
            QTest::qWait(100);
            if (rss->property("text").toString() != initial) return false;
            auto* refresh = item(window, "refreshSettingsDiagnostics");
            if (section == "runtime_keyboard") {
                refresh->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window, Qt::Key_Space);
            } else if (!click(window, refresh)) return false;
            return waitFor([&] { auto* current = item(window, "diagnosticValue_rss"); return current && current->property("text").toString().section(' ', 0, 0).toDouble() > value + 40; })
                && allocation.size() == 64 * 1024 * 1024 && server.requests.isEmpty();
        }
        if (section=="save") {
            auto* input=item(window,"inlineRequestLimit")->property("contentItem").value<QQuickItem*>();
            if (!type(window,input,"120")) return false;
            input->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Tab);
            if (!type(window,item(window,"inlineLogLimit"),"7") || !click(window,item(window,"inlineSaveSync")) || !waitFor([&] { return !workspace.busy(); })) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.busy() && restored.requestLimit()==120 && restored.logLimitMb()==7; }) && server.requests.isEmpty();
        }
        if (section=="invalid") return type(window,item(window,"inlineLogLimit"),"0") && click(window,item(window,"inlineSaveSync")) && !workspace.error().isEmpty() && workspace.logLimitMb()==5 && server.requests.isEmpty();
        if (section=="graphics") return item(window,"inlineRadarWaterEnabled")->isVisible() && click(window,item(window,"inlineRadarWaterEnabled")) && waitFor([&] { return !workspace.busy() && !workspace.radarWaterEnabled(); }) && server.requests.isEmpty();
        if (section=="diagnostics") return click(window,item(window,"refreshSettingsDiagnostics")) && item(window,"settingsRequestAudit")->property("count").toInt()==0 && workspace.settingsDiagnostics()["metrics"].toList().size()>=11 && server.requests.isEmpty();
        if (section=="privacy") return item(window,"privacyTelemetry")->isVisible() && text(window,"privacyTelemetry")=="Off" && type(window,item(window,"inlineYamlLimit"),"4") && click(window,item(window,"inlineSaveYamlLimit")) && waitFor([&] { return !workspace.busy() && workspace.yamlLimitMiB()==4; }) && server.requests.isEmpty();
        if (section=="workspace") return click(window,item(window,"settingsResourceColumns")) && waitFor([&] { return item(window,"resourceColumnVisible_kind") && item(window,"resourceColumnVisible_kind")->isVisible(); }) && server.requests.isEmpty();
        if (section=="sources") return item(window,"settingsSourcePath")->isVisible()
            && waitFor([&] { return item(window,"settingsSourceList")->property("count").toInt()==1; }) && server.requests.isEmpty();
        if (section=="about") return item(window,"settingsVersion")->isVisible() && server.requests.isEmpty();
        if (section=="sync") return item(window,"inlineRequestLimit")->isVisible() && server.requests.isEmpty();
        if (section=="narrow") window->setWidth(360);
        return waitFor([&] { auto* control=item(window,"inlineAppearanceTheme"); return control && displayedWithin(window->contentItem(),control); }) && server.requests.isEmpty();
    }
    if (!waitFor([&] { return item(window, "contexts")->property("count").toInt() == (twoContexts ? 2 : 1) && item(window, "openContext")->isEnabled(); })) return false;
    if (!click(window, item(window, "openContext"))) return false;
    if (scenario.startsWith("query_")) {
        const bool events = scenario == "query_event";
        if (!waitFor([&] { return !workspace.loading() && workspace.table()->rowCount() >= 3; }, 15000)) return false;
        if (events && (!waitFor([&] { return workspace.eventTable()->rowCount() == 2; }) || !workspace.setWorkspacePage("events"))) return false;
        const auto calls = server.requests.size();
        const auto inputName = events ? "eventFilter" : "resourceFilter";
        const auto query = scenario == "query_none" ? QString("absent") : scenario == "query_invalid" ? QString("/[invalid/") : events ? QString("Probe") : QString("Pod");
        if (scenario == "query_narrow") window->resize(600, 390);
        if (!type(window, item(window, inputName), query)) return false;
        auto* count = item(window, "queryMatchCount");
        auto* next = item(window, "queryNext");
        auto* previous = item(window, "queryPrevious");
        if (!count || !next || !previous) return false;
        auto* model = events ? workspace.eventTable() : workspace.table();
        const auto selection = item(window, events ? "eventTable" : "resourceTable")->property("selectionModel").value<QItemSelectionModel*>();
        if (!selection) return false;
        if (scenario == "query_none" || scenario == "query_invalid")
            return waitFor([&] { return count->property("text") == "0/0"; }) && !next->isEnabled() && !previous->isEnabled()
                && server.requests.size() == calls && (scenario != "query_invalid" || !workspace.filterError().isEmpty());
        if (!waitFor([&] { return count->property("text") == "1/2" && selection->currentIndex().row() == 0; })) return false;
        if (scenario == "query_keyboard") {
            item(window, inputName)->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return);
        } else if (!click(window, next)) return false;
        if (count->property("text") != "2/2" || selection->currentIndex().row() != 1) return false;
        if (scenario == "query_previous") {
            if (!click(window, previous) || count->property("text") != "1/2") return false;
        } else if (scenario == "query_keyboard") {
            item(window, inputName)->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return, Qt::ShiftModifier);
            if (count->property("text") != "1/2") return false;
        } else if (scenario == "query_wrap") {
            if (!click(window, next) || count->property("text") != "1/2"
                || !click(window, previous) || count->property("text") != "2/2") return false;
        } else if (scenario == "query_clear") {
            if (!type(window, item(window, inputName), "") || count->property("text") != "0/0" || next->isEnabled()) return false;
        } else if (scenario == "query_narrow") {
            if (!displayedWithin(window->contentItem(), next) || !displayedWithin(window->contentItem(), previous)
                || !displayedWithin(window->contentItem(), item(window, inputName))) return false;
        } else if (scenario == "query_logo") {
            if (!type(window, item(window, inputName), "absent")) return false;
            auto* logo = item(window, "resourceEmptyLogo");
            if (!logo || !waitFor([&] { return logo->isVisible() && logo->property("status").toInt() == 1; })) return false;
        } else if (scenario == "query_next" && (model->data(selection->currentIndex(), Qt::UserRole).toString().isEmpty())) return false;
        return workspace.inspectorPath().isEmpty() && server.requests.size() == calls;
    }
    if (scenario.startsWith("find_")) {
        if (scenario == "find_event") {
            if (!waitFor([&] { return !workspace.loading() && workspace.eventTable()->rowCount() == 2; }, 15000) || !workspace.setWorkspacePage("events")) return false;
            const auto calls = server.requests.size();
            if (!click(window,item(window,"eventFindButton")) || !type(window,item(window,"eventFindInput"),"Probe")) return false;
            if (!waitFor([&] { return text(window,"eventFindCount") == "1/2"; }) || !click(window,item(window,"eventFindNext"))) return false;
            if (text(window,"eventFindCount") != "2/2" || !click(window,item(window,"eventFindPrevious")) || text(window,"eventFindCount") != "1/2") return false;
            return workspace.eventTable()->rowCount() == 2 && workspace.inspectorPath().isEmpty() && server.requests.size() == calls;
        }
        if (!waitFor([&] { return !workspace.loading() && workspace.table()->rowCount() == 3; }, 15000)) return false;
        const auto calls = server.requests.size();
        if (scenario == "find_narrow") window->resize(640, 700);
        if (scenario == "find_keyboard") QTest::keySequence(window, QKeySequence::Find);
        else if (!click(window, item(window, "resourceFindButton"))) return false;
        auto* input = item(window, "resourceFindInput"); if (!input || !waitFor([&] { return input->isVisible(); })) return false;
        const auto query = scenario == "find_empty" ? QString{} : scenario == "find_no_match" ? QString("absent") : scenario == "find_invalid" ? QString("/[invalid/") : scenario == "find_regex" ? QString("/^(alpha|bravo)$/") : QString("Pod");
        if (!type(window, input, query)) return false;
        const auto selection = item(window, "resourceTable")->property("selectionModel").value<QItemSelectionModel*>(); if (!selection) return false;
        const auto selected = [&] { return workspace.table()->data(selection->currentIndex(), Qt::UserRole).toString(); };
        if (scenario == "find_empty" || scenario == "find_no_match" || scenario == "find_invalid") {
            if (!waitFor([&] { return text(window, "resourceFindCount") == "0/0"; }) || item(window,"resourceFindNext")->isEnabled()) return false;
            if (scenario == "find_invalid" && (!item(window,"resourceFindError")->isVisible() || text(window,"resourceFindError").isEmpty())) return false;
        } else {
            if (!waitFor([&] { return text(window,"resourceFindCount") == "1/2"; }) || !click(window,item(window,"resourceFindNext"))) return false;
            if (!selected().endsWith("/bravo") || text(window,"resourceFindCount") != "2/2") return false;
            if (scenario == "find_previous" || scenario == "find_wrap") {
                if (!click(window,item(window,"resourceFindPrevious")) || !selected().endsWith("/alpha")) return false;
                if (scenario == "find_wrap" && (!click(window,item(window,"resourceFindPrevious")) || !selected().endsWith("/bravo"))) return false;
            }
            if (scenario == "find_enter") { input->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Return); if (!selected().endsWith("/alpha")) return false; }
            if (scenario == "find_narrow" && (!displayedWithin(window->contentItem(),item(window,"resourceFindNext")) || !displayedWithin(window->contentItem(),input))) return false;
        }
        if (scenario == "find_filter_shrink" || scenario == "find_filter_ime") {
            if (!click(window,item(window,"workspaceSearchButton"))) return false;
            auto* filter = item(window,"resourceFilter");
            if (!filter || !filter->isVisible()) return false;
            filter->forceActiveFocus();
            if (scenario == "find_filter_ime") {
                QInputMethodEvent commit; commit.setCommitString("~alpha");
                QCoreApplication::sendEvent(window,&commit);
            } else if (!type(window,filter,"~alpha")) return false;
            if (!waitFor([&] { return workspace.resourceCount()==1 && text(window,"resourceFindCount")=="1/1"; })
                || !selected().endsWith("/alpha") || !click(window,item(window,"resourceFindNext"))
                || text(window,"resourceFindCount")!="1/1" || !workspace.inspectorPath().isEmpty()
                || server.requests.size()!=calls) return false;
            return true;
        }
        if (scenario == "find_escape") { input->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Escape); if (input->isVisible()) return false; }
        else if (!click(window,item(window,"resourceFindClose")) || input->isVisible()) return false;
        return workspace.table()->rowCount() == 3 && workspace.filterText().isEmpty() && workspace.inspectorPath().isEmpty() && server.requests.size() == calls;
    }
    if (scenario == "settings_invalid") return waitFor([&] { return !workspace.error().isEmpty() && !workspace.busy(); }) && server.requests.isEmpty();
    if (scenario.startsWith("accessibility_")) {
        if (!waitFor([&] { return workspace.totalResourceCount() == 3 && !workspace.loading(); })) return false;
        const auto calls = server.requests.size();
        if (!accessibleName(window,"Workspace actions")) return false;
        if (scenario == "accessibility_populated") {
            window->hide(); QTest::qWait(20); window->show(); window->requestActivate();
            return waitFor([&] { return window->isVisible(); }) && accessibleName(window,"Workspace actions") && server.requests.size() == calls;
        }
        if (scenario == "accessibility_settings") return click(window, item(window, "settingsWorkspaceButton"))
            && click(window, item(window, "settingsSourcesSection"))
            && accessibleName(window,"Workspace actions") && server.requests.size() == calls;
        if (scenario == "accessibility_dialog") {
            if (!click(window, item(window, "commandPaletteButton"))
                || !waitFor([&] { return item(window, "commandPaletteSearch")->isVisible(); })
                || !accessibleName(window, "Find a command")) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            return waitFor([&] { return !item(window, "commandPaletteSearch")->isVisible(); })
                && accessibleName(window,"Workspace actions") && server.requests.size() == calls;
        }
        return false;
    }
    if (scenario == "hidden_followup") {
        if (!waitFor([&] { return server.requests.contains("/api"); })) return false;
        if (!click(window, item(window, "closeSession")) || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
        QTest::qWait(600);
        return server.requests == QStringList{"/api"} && server.delayedReadDelivered && !server.prematureDisconnect;
    }
    if (scenario == "plugin" || scenario == "invalid_ca" || scenario == "empty_token")
        return waitFor([&] { return !text(window, "errorMessage").isEmpty(); }) && server.requests.isEmpty();
    if (scenario == "auth") {
        if (!waitFor([&] { return text(window, "syncProblemMessage").contains("Authentication failed"); })) return false;
        const int count = server.requests.size();
        QTest::qWait(100);
        return server.requests.size() == count && item(window, "authenticationButton")->isVisible() && !item(window, "refreshButton")->isEnabled();
    }
    if (scenario == "redirect") return waitFor([&] { return text(window, "syncProblemMessage").contains("Redirect refused"); }) && !server.requests.contains("/credential-leak");
    if (scenario == "malformed") return waitFor([&] { return text(window, "syncProblemMessage").contains("Malformed API response"); }) && !text(window, "syncProblemMessage").contains("secret-value");
    if (scenario == "invalid_discovery") return waitFor([&] { return text(window, "syncProblemMessage").contains("Invalid core API version"); }) && !server.requests.contains("/escape");
    if (scenario == "invalid_list") {
        const bool reported=waitFor([&] { return text(window, "syncProblemMessage").contains("Invalid resource list entry"); });
        if (!reported) std::fprintf(stderr, "Invalid list response was not reported; visible status: %s; requests: %s\n", qPrintable(text(window, "syncProblemMessage")), qPrintable(server.requests.join('\n')));
        return reported;
    }
    if (scenario.startsWith("shell_")) {
        if (!waitFor([&] { return workspace.totalResourceCount()==(scenario == "shell_search_events" ? 5 : 3) && !workspace.loading(); },10000)) return false;
        const auto calls=server.requests.size();
        auto* radar=item(window,"resourceRadar");
        if (scenario == "shell_footer_reference") {
            if (text(window, "resourceMatchCount") != "visible: 3/3"
                || text(window, "syncStatus") != QString("API: %1/min  Synced: 0s ago").arg(calls)) return false;
            reference = reference.addSecs(59);
            if (!workspace.filter("alpha") || text(window, "resourceMatchCount") != "visible: 1/3"
                || text(window, "syncStatus") != QString("API: %1/min  Synced: 59s ago").arg(calls)) return false;
            reference = reference.addSecs(1);
            if (!workspace.filter("") || text(window, "syncStatus") != "API: 0/min  Synced: 1m ago") return false;
            reference = reference.addSecs(3540);
            return workspace.filter("alpha") && text(window, "syncStatus") == "API: 0/min  Synced: 1h ago" && server.requests.size() == calls;
        }
        if (scenario == "shell_footer_reopen") {
            const auto session = workspace.currentSession();
            if (!workspace.close(session) || !waitFor([&] { return !workspace.busy(); })
                || text(window, "syncStatus") != "API: 0/min  Synced: never"
                || !workspace.activate(session) || !waitFor([&] { return !workspace.busy(); })) return false;
            return text(window, "syncStatus") == QString("API: %1/min  Synced: 0s ago").arg(calls) && server.requests.size() == calls;
        }
        if (scenario == "shell_landscape" || scenario == "shell_landscape_filters" || scenario == "shell_landscape_rotate") {
            window->resize(800,390);
            if (!waitFor([&] { return item(window, "landscapeNavigation")->isVisible() && radar->isVisible()
                && !item(window, "sidebarFilterScroll")->isVisible(); })) return false;
            if (scenario == "shell_landscape_filters") {
                if (!click(window, item(window, "toggleLandscapeFilters")) || !waitFor([&] { return item(window, "sidebarFilterScroll")->isVisible(); })
                    || !podlord::test::scrollIntoView(window, item(window, "problemsOnly")) || !click(window, item(window, "problemsOnly"))
                    || !workspace.problemsOnly() || !click(window, item(window, "toggleLandscapeFilters"))) return false;
                return !item(window, "sidebarFilterScroll")->isVisible() && workspace.problemsOnly() && server.requests.size() == calls;
            }
            if (!workspace.filter("alpha") || !click(window, item(window, "radarZoom"))) return false;
            const auto pose = radar->property("viewPose").toMap();
            const auto evidence = qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
            const QList<QSize> sizes = scenario == "shell_landscape_rotate" ? QList<QSize>{QSize(800,390),QSize(390,720),QSize(800,390),QSize(1440,920)}
                : QList<QSize>{QSize(600,360),QSize(640,360),QSize(800,390),QSize(896,414),QSize(1080,480)};
            for (const auto size : sizes) {
                window->resize(size);
                const bool landscape = size.width() >= 600 && size.height() < 600;
                if (!waitFor([&] {
                    auto* rail = item(window, "landscapeNavigation");
                    auto* host = item(window, "workspaceSidebarHost");
                    return rail->isVisible() == landscape && (landscape ? host->isVisible() && host->width() <= 320 : true)
                        && item(window, "resourceTable")->height() >= 32;
                })) return false;
                if (scenario == "shell_landscape_rotate") {
                    auto* drawer = window->findChild<QObject*>("sidebarDrawer");
                    if (!drawer) return false;
                    if (size.width() == 390) {
                        if (!click(window, item(window,"toggleSidebar")) || !waitFor([&] { return drawer->property("visible").toBool() && radar->isVisible(); })) return false;
                    } else if (!waitFor([&] { return !drawer->property("visible").toBool(); })) return false;
                }
                if (landscape) for (const auto* name : {"workspaceSearchButton","resourcesWorkspaceButton","eventsWorkspaceButton","portForwardTasksButton","settingsWorkspaceButton","workspaceActionsButton","toggleSidebar"}) {
                    auto* control = item(window,name);
                    if (!control || control->width() < 44 || control->height() < 44 || !podlord::test::scrollIntoView(window, control)
                        || control->mapToScene(QPointF{}).x() >= 56) return false;
                }
                if (radar != item(window,"resourceRadar") || radar->property("viewPose").toMap() != pose || workspace.filterText() != "alpha"
                    || workspace.resourceCount() != 1 || server.requests.size() != calls) return false;
                if (!evidence.isEmpty() && !window->grabWindow().save(evidence + "/landscape-" + QString::number(size.width()) + "x" + QString::number(size.height()) + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
            }
            return true;
        }
        if (scenario == "shell_search_demand" || scenario == "shell_search_events") {
            auto* input = item(window, "resourceFilter");
            if (input->isVisible() || !click(window, item(window, "workspaceSearchButton"))
                || !waitFor([&] { return input->isVisible() && input->hasActiveFocus(); })
                || !type(window, input, "\"alpha\"") || workspace.resourceCount() != 1) return false;
            if (scenario == "shell_search_events") {
                if (!click(window, item(window, "eventsWorkspaceButton"))
                    || !waitFor([&] { return item(window, "eventFilter")->isVisible() && workspace.totalEventCount() == 2; })
                    || !type(window, item(window, "eventFilter"), "Started") || workspace.eventCount() != 1
                    || !click(window, item(window, "workspaceSearchButton"))
                    || item(window, "eventFilter")->isVisible() || workspace.eventFilterText() != "Started"
                    || !click(window, item(window, "resourcesWorkspaceButton"))) return false;
            } else if (!click(window, item(window, "workspaceSearchButton"))) return false;
            if (input->isVisible() || workspace.resourceCount() != 1 || !click(window, item(window, "workspaceSearchButton"))
                || !waitFor([&] { return input->hasActiveFocus(); }) || text(window, "resourceFilter") != "\"alpha\"") return false;
            QTest::keyClick(window, Qt::Key_Escape);
            return !input->isVisible() && workspace.resourceCount() == 1
                && item(window, "workspaceSearchButton")->hasActiveFocus() && server.requests.size() == calls;
        }
        if (scenario == "shell_health_refresh") {
            auto* health = item(window, "sessionHealthSegments");
            if (!health || !waitFor([&] { return qAbs(health->height() - health->parentItem()->height()) < 1; })) return false;
            if (!podlord::test::revealWorkspaceAction(window, "refreshButton") || !click(window, item(window, "refreshButton"))) return false;
            if (!workspace.syncLoading() || workspace.loadingProgress() != 1) return false;
            bool stable = true;
            if (!waitFor([&] {
                stable = stable && workspace.loadingProgress() == 1 && qAbs(health->height() - health->parentItem()->height()) < 1;
                return !workspace.syncLoading();
            }, 10000)) return false;
            return stable && workspace.totalResourceCount() == 3 && server.requests.size() > calls;
        }
        if (scenario == "shell_quick_invalid_existing") {
            const auto session = workspace.currentSession(), notice = workspace.sourceImportNotice(), error = workspace.error();
            const auto contexts = workspace.contexts();
            if (!workspace.filter("alpha")) return false;
            const auto path = temporary.filePath("invalid");
            QFile invalid(path);
            if (!invalid.open(QIODevice::WriteOnly) || invalid.write("invalid source") < 0) return false;
            invalid.close();
            if (!workspace.quickImportFile(path) || !waitFor([&] { return !workspace.busy(); })) return false;
            return workspace.currentSession() == session && workspace.contexts() == contexts && workspace.filterText() == "alpha"
                && workspace.sourceImportNotice() == notice && workspace.error() == error && server.requests.size() == calls;
        }
        if (scenario == "shell_quick_sessions" || scenario == "shell_quick_reopen" || scenario == "shell_quick_context") {
            const auto session = workspace.currentSession();
            if (scenario == "shell_quick_reopen" && (!workspace.close(session) || !waitFor([&] { return !workspace.busy(); }))) return false;
            if (!click(window, item(window, "quickOpenDropdown"))) return false;
            const auto target = scenario == "shell_quick_context" ? "quickContext_" + workspace.contexts().first().toMap()["id"].toString() : "quickSession_" + session;
            if (!waitFor([&] { return item(window, target) && item(window, target)->isVisible(); })
                || !click(window, item(window, target)) || !waitFor([&] { return !workspace.busy(); })) return false;
            return workspace.currentSession() == session && server.requests.size() == calls;
        }
        if (scenario == "shell_quick_narrow") {
            window->resize(320, 720);
            if (!waitFor([&] {
                for (const auto& name : {"quickOpenFile", "quickOpenDropdown"}) {
                    auto* control = item(window, name);
                    if (!control || control->width() < 44 || control->height() < 44
                        || !displayedWithin(window->contentItem(), control)) return false;
                }
                return true;
            })) return false;
            if (!click(window, item(window, "quickOpenDropdown"))) return false;
            if (!waitFor([&] {
                auto* context = item(window, "quickContext_" + workspace.contexts().first().toMap()["id"].toString());
                return context && context->height() >= 44 && displayedWithin(window->contentItem(), context);
            }) || server.requests.size() != calls) return false;
            const auto evidence = qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
            return evidence.isEmpty() || window->grabWindow().save(evidence + "/quick-open-phone-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png");
        }
        if (scenario == "shell_tools_menu") {
            if (item(window, "refreshButton")->isVisible() || !click(window, item(window, "workspaceActionsButton"))) return false;
            if (!waitFor([&] { return item(window, "refreshButton")->isVisible() && item(window, "commandPaletteButton")->isVisible(); })) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            return waitFor([&] { return !item(window, "refreshButton")->isVisible(); }) && server.requests.size() == calls;
        }
        if (scenario == "shell_footer_narrow") {
            if (!workspace.renameSession(workspace.currentSession(), QString(96, 'x')) || !waitFor([&] { return !workspace.busy(); })) return false;
            for (const auto size : {QSize(320,360), QSize(390,720), QSize(1440,920)}) {
                window->resize(size);
                if (!waitFor([&] {
                    auto* footer = item(window, "workspaceFooter");
                    if (!footer || qAbs(footer->height()-24) > 0.5) return false;
                    const QRectF scene(0,0,window->width(),window->height());
                    for (const auto* name : {"resourceMatchCount", "syncStatus"}) {
                        auto* label = item(window, name);
                        if (!label || label->height() > 24 || !scene.adjusted(-0.5,-0.5,0.5,0.5).contains(label->mapRectToScene(label->boundingRect()))) return false;
                    }
                    return item(window, "resourceTable")->height() >= 32;
                })) {
                    for (const auto* name : {"workspaceToolbar", "workspaceFooter", "resourceMatchCount", "syncStatus", "resourceTable"}) {
                        auto* control = item(window, name);
                        const auto bounds = control->mapRectToScene(control->boundingRect());
                        std::fprintf(stderr,"%s at %dx%d: x=%g y=%g width=%g height=%g\n",name,window->width(),window->height(),bounds.x(),bounds.y(),bounds.width(),bounds.height());
                    }
                    const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
                    if (!evidence.isEmpty()) window->grabWindow().save(evidence + "/footer-failure-" + QString::number(size.width()) + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png");
                    return false;
                }
            }
            return server.requests.size() == calls;
        }
        if (scenario == "shell_touch") {
            window->resize(320,720);
            if (!waitFor([&] { return window->contentItem()->width() == 320; })) return false;
            QList<QRectF> controls;
            for (const auto* name : {"workspaceSearchButton", "resourcesWorkspaceButton", "eventsWorkspaceButton", "portForwardTasksButton", "settingsWorkspaceButton", "workspaceActionsButton", "toggleSidebar"}) {
                auto* control = item(window, name);
                if (!control || !waitFor([&] { return control->width() >= 44 && control->height() >= 44; })) return false;
                const auto bounds = control->mapRectToScene(control->boundingRect());
                if (bounds.left() < 0 || bounds.right() > 320.5) return false;
                for (const auto previous : controls) if (bounds.intersects(previous)) return false;
                controls.append(bounds);
            }
            const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
            if (!evidence.isEmpty()) {
                QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
                if (!frames.wait(1000) || !window->grabWindow().save(evidence + "/shell-phone-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
            }
            if (!click(window, item(window, "toggleSidebar")) || !waitFor([&] { return radar->isVisible(); })) return false;
            for (const auto* name : {"radarWorkspaceButton", "radarZoomOut", "radarZoom", "resetRadar", "resourceFieldFilters"}) {
                auto* control = item(window,name);
                if (!control || control->width() < 44 || control->height() < 44 || !podlord::test::scrollIntoView(window,control)) return false;
            }
            if (!evidence.isEmpty() && !window->grabWindow().save(evidence + "/shell-touch-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
            return server.requests.size() == calls;
        }
        if (scenario == "shell_reset_icon") {
            if (!type(window,item(window,"resourceFilter"),"\"alpha\"")) return false;
            auto* reset = item(window,"resetResourceFilters");
            if (!reset || reset->width() > 44 || !click(window,reset) || !click(window,item(window,"workspaceSearchButton"))) return false;
            const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
            if (!evidence.isEmpty() && !window->grabWindow().save(evidence + "/shell-desktop-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
            return workspace.resourceCount() == 3 && workspace.filterText().isEmpty() && server.requests.size() == calls;
        }
        if (scenario=="shell_sidebar") {
            if (!radar || !radar->isVisible() || !click(window,item(window,"eventsWorkspaceButton")) || !radar->isVisible()) return false;
            if (!click(window,item(window,"portForwardTasksButton"))) return false;
            const auto* ports = item(window,"portTable");
            return radar->isVisible() && ports && ports->isVisible() && server.requests.size()==calls;
        }
        if (scenario=="shell_navigation") {
            if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"alertsWorkspaceButton")) || !item(window,"addAlert")->isVisible()) return false;
            return click(window,item(window,"resourcesWorkspaceButton")) && item(window,"resourceTable")->isVisible() && radar->isVisible() && server.requests.size()==calls && !workspace.setWorkspacePage("radar");
        }
        if (scenario=="shell_narrow") {
            window->setWidth(360); QTest::qWait(120);
            if (radar->isVisible() || !click(window,item(window,"toggleSidebar")) || !waitFor([&] { return radar->isVisible(); })) return false;
            QTest::keyClick(window,Qt::Key_Escape);
            return waitFor([&] { return !radar->isVisible(); }) && server.requests.size()==calls;
        }
        if (scenario=="shell_metric_missing") return waitFor([&] { return text(window,"pulse_cpu_usage").contains("Unavailable") && text(window,"pulse_pods_usage")=="2"; }) && server.requests.size()==calls;
        return false;
    }
    if (scenario == "compact_toolbar") {
        if (!click(window,item(window,"workspaceSearchButton"))) return false;
        if (!waitFor([&] { const auto* tab=item(window,"activateSession_"+workspace.currentSession()); return tab && tab->isVisible() && tab->height()>0; })) return false;
        return waitFor([&] {
            const auto* tab=item(window,"activateSession_"+workspace.currentSession());
            const auto* filter=item(window,"resourceFilter");
            const auto gap=filter->mapToScene(QPointF{}).y()-tab->mapToScene(QPointF(0,tab->height())).y();
            return filter->isVisible() && gap>=0 && gap<=8;
        });
    }
    if (scenario.startsWith("problems_reference_")) {
        if (!waitFor([&] { return workspace.totalResourceCount()==3 && !workspace.loading(); })) return false;
        const auto calls=server.requests.size();
        if (!workspace.setFilterMode("problems")) return false;
        const bool problem=scenario.endsWith("unready") || scenario.endsWith("failed") || scenario.endsWith("pending") || scenario.endsWith("terminating");
        return workspace.resourceCount()==(problem ? 1 : 0) && (!problem || workspace.table()->data(workspace.table()->index(0,0),Qt::UserRole+1)=="target") && server.requests.size()==calls;
    }
    if (scenario == "repeated_page") return waitFor([&] { return text(window, "syncProblemMessage").contains("Repeated list continuation"); });
    if (scenario == "rate_limit") return waitFor([&] { return text(window, "syncProblemMessage").contains("Rate limited"); });
    if (scenario == "priority") {
        if (!waitFor([&] { const auto* cell = item(window, "cell_0_0"); return workspace.table()->rowCount() > 0 && cell && cell->width() > 0 && cell->height() > 0; })) return false;
        if (!click(window, item(window, "cell_0_0")) || !waitFor([&] { return workspace.inspected().contains("fetchedAt"); })) {
            std::fprintf(stderr, "Priority inspector did not load; requests: %s\n", qPrintable(server.requests.join('\n'))); return false;
        }
        int listed = -1, inspected = -1;
        for (int i = 0; i < server.requests.size(); ++i) {
            if (server.requests[i].startsWith("/apis/example.test/v1/widgets?")) listed = i;
            if (server.requests[i].contains("/widgets/custom-widget")) inspected = i;
        }
        if (listed < 0 || inspected != listed + 1) { std::fprintf(stderr, "Priority order %d -> %d; requests: %s\n", listed, inspected, qPrintable(server.requests.join('\n'))); return false; }
        return true;
    }
    const int expectedRows = scenario.startsWith("radar_water_") ? 4 : scenario.startsWith("workload_") ? 1 : scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_initial_population" || scenario == "radar_reopen_position" || scenario.startsWith("radar_pooled_") || scenario == "columns_pin_all" ? 1001 : scenario == "inspector_related_workspace" || scenario.startsWith("inspector_related_event_") || scenario.startsWith("inspector_related_table_events_") || scenario.startsWith("filter_event_") || scenario == "inspector_related_alias_distinct" || scenario == "inspector_related_alias_reuse" ? 5 : scenario.startsWith("table_") || scenario == "inspector_custom_secret" || scenario.startsWith("inspector_related_") ? 4 : 3;
    if (!waitFor([&] { return item(window, "resourceTable")->property("rows").toInt() == expectedRows && !workspace.loading(); }, 10000)) {
        std::fprintf(stderr, "Resource load failed: %s %s\n", qPrintable(text(window, "errorMessage")), qPrintable(text(window, "syncProblemMessage"))); return false;
    }
    if (scenario.startsWith("dock_")) {
        window->resize(scenario=="dock_narrow" ? 390 : 1440,900);
        if (!workspace.filter("alpha") || !waitFor([&] { return workspace.table()->rowCount()==1 && item(window,"cell_0_3"); })
            || !click(window,item(window,"cell_0_3")) || !waitFor([&] { return workspace.canEditYaml() && !workspace.loading(); })) return false;
        QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update(); if (!frames.wait(2000)) return false;
        auto* content=item(window,"workspaceContent");
        auto* heading=item(window,"inspectorResourceName");
        if (!content || !heading) return false;
        const auto bounds=[](const QQuickItem* target) { return QRectF(target->mapToScene({0,0}),target->size()); };
        const auto before=bounds(content);
        if (bounds(heading).top()<before.bottom() || before.width()<window->width()*0.55) {
            std::fputs("Inspector is not docked below the full-width workspace.\n",stderr); return false;
        }
        const auto calls=server.requests.size();
        if (scenario=="dock_keyboard") {
            auto* handle=item(window,"inspectorResizeHandle"); handle->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Up);
            window->update(); if (!frames.wait(2000) || bounds(content).height()>before.height()-10) return false;
            QTest::keyClick(window,Qt::Key_Down);
            window->update(); if (!frames.wait(2000) || qAbs(bounds(content).height()-before.height())>2) return false;
        }
        if (scenario=="dock_resize") {
            const auto handle=bounds(item(window,"inspectorResizeHandle")).center().toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,handle);
            QTest::mouseMove(window,handle-QPoint(0,70),100);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,handle-QPoint(0,70));
            window->update(); if (!frames.wait(2000)) return false;
            if (bounds(content).height()>=before.height()-20 || bounds(heading).top()<bounds(content).bottom()) return false;
        }
        if (scenario=="dock_page") {
            if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"settingsAppearanceSection"))) return false;
            window->update(); if (!frames.wait(2000)) return false;
            if (bounds(heading).top()<bounds(content).bottom() || workspace.inspectorName()!="alpha") return false;
            if (!click(window,item(window,"resourcesWorkspaceButton"))) return false;
        }
        const auto capture=qEnvironmentVariable("PODLORD_INSPECTOR_DOCK_FRAME");
        if (!capture.isEmpty() && !window->grabWindow().save(capture)) return false;
        if (!click(window,item(window,"closeInspector"))) return false;
        window->update(); if (!frames.wait(2000)) return false;
        return workspace.inspectorPath().isEmpty() && bounds(content).height()>before.height()+40
            && workspace.table()->rowCount()==1 && server.requests.size()==calls
            && (capture.isEmpty() || window->grabWindow().save(capture+".closed.png"));
    }
    if (scenario.startsWith("workload_")) {
        if (scenario == "workload_overview_zero_replicas") {
            if (!click(window,item(window,"cell_0_0")) || !waitFor([&] { return workspace.canEditYaml() && text(window,"overview_ready")=="0/0" && item(window,"overviewReadinessBar"); })) return false;
            const auto* fill=item(window,"overviewReadinessFill");
            auto* bar=item(window,"overviewReadinessBar");
            const auto* accessible = QAccessible::queryAccessibleInterface(bar);
            return fill && accessible && bar->property("value").toDouble()==0 && fill->property("color").value<QColor>()==workspace.appearanceColors()["unknown"].value<QColor>() && accessible->text(QAccessible::Name)=="Replicas ready: 0 of 0";
        }
        auto* model=workspace.table();
        const auto value = [&](const QString& field) {
            for (int c=0;c<model->columnCount();++c) if (model->headerData(c,Qt::Horizontal,Qt::UserRole)==field) return model->data(model->index(0,c)).toString();
            return QString("missing column");
        };
        const bool event=scenario=="workload_event_owner" || scenario=="workload_modern_event_owner";
        const QString image=event || scenario.startsWith("workload_status_node_") || scenario.startsWith("workload_custom_") || scenario=="workload_images_empty" ? "" : "registry.example/main:2, registry.example/sidecar:3";
        const QString ready=scenario=="workload_default_replicas" ? "0/1" : scenario=="workload_zero_replicas" ? "0/0" : scenario=="workload_missing_ready" ? "0/4"
            : scenario=="workload_status_replicaset_available" ? "4/4" : scenario=="workload_status_replicaset_unavailable" ? "0/4" : scenario=="workload_status_replicaset_zero" ? "0/0"
            : scenario=="workload_deployment" || scenario=="workload_replicaset" || scenario=="workload_statefulset" || scenario.startsWith("workload_status_deployment_") || scenario.startsWith("workload_status_replicaset_") ? "3/4" : "-";
        const QString status=scenario == "workload_status_deployment_available" || scenario == "workload_status_replicaset_available" ? "Available"
            : scenario == "workload_status_deployment_unavailable" || scenario == "workload_status_replicaset_unavailable" ? "Unavailable"
            : scenario == "workload_status_replicaset_zero" ? "ScaledZero" : scenario == "workload_status_replicaset_progressing" ? "Progressing"
            : scenario == "workload_status_node_ready" ? "Ready" : "NotReady";
        const bool correct=value("image")==image && value("ready")==ready && value("restarts")=="-" && (!event || value("owner")=="Pod/alpha")
            && (!scenario.startsWith("workload_status_") || value("status")==status);
        if (!correct) std::fprintf(stderr,"Workload table: image=%s; ready=%s; restarts=%s; owner=%s; status=%s\n",qPrintable(value("image")),qPrintable(value("ready")),qPrintable(value("restarts")),qPrintable(value("owner")),qPrintable(value("status")));
        return correct;
    }
    if (scenario.startsWith("sources_")) {
        auto* panel = item(window, "sourceManagementPanel");
        auto* toggle = item(window, "sourcesButton");
        auto* field = item(window, "sourcePath");
        auto* selector = item(window, "contexts");
        if (!panel || !toggle || !field || !selector || panel->isVisible() || field->isVisible() || selector->isVisible()) return false;
        const auto firstSession = workspace.currentSession();
        const auto rows = workspace.table()->rowCount();
        const auto calls = server.requests.size();
        if (scenario == "sources_collapsed") return toggle->isVisible() && toggle->isEnabled() && !toggle->property("checked").toBool();
        if (scenario == "sources_keyboard") {
            toggle->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window, Qt::Key_Space);
        } else if (!click(window, toggle)) return false;
        if (!waitFor([&] { return panel->isVisible() && field->isVisible() && selector->isVisible(); })) return false;
        if (scenario == "sources_open_context") {
            if (!click(window, selector)) return false;
            QTest::keyClick(window, selector->property("currentIndex").toInt() == 0 ? Qt::Key_End : Qt::Key_Home);
            QTest::keyClick(window, Qt::Key_Return);
            if (!click(window, item(window, "openContext"))) return false;
            return waitFor([&] { return workspace.currentSession() != firstSession && !workspace.busy() && !panel->isVisible() && workspace.table()->rowCount() > 0; });
        }
        if (scenario == "sources_invalid") {
            if (!type(window, field, temporary.filePath("missing.config")) || !click(window, item(window, "importButton"))) return false;
            return waitFor([&] { return !workspace.busy() && !workspace.error().isEmpty(); }) && panel->isVisible()
                && workspace.currentSession() == firstSession && workspace.table()->rowCount() == rows && server.requests.size() == calls;
        }
        if (scenario == "sources_narrow") {
            window->resize(680, 480); QTest::qWait(30);
            for (const auto& name : {"sourcePath", "importButton", "contexts", "openContext", "reloadSources", "toggleSidebar"}) {
                auto* control = item(window, name);
                if (!control || !control->isVisible() || control->width() <= 0) { std::fprintf(stderr,"Narrow source control unavailable: %s\n",name); return false; }
                const auto origin = control->mapToScene({0, 0});
                if (origin.x() < 0 || origin.x() + control->width() > window->width() + 1 || origin.y() + control->height() > window->height()) { std::fprintf(stderr,"Narrow source control out of bounds: %s (%g,%g %gx%g)\n",name,origin.x(),origin.y(),control->width(),control->height()); return false; }
            }
        }
        if (scenario == "sources_repeat") {
            for (int index = 0; index < 3; ++index) {
                if (!click(window, toggle) || panel->isVisible() || !click(window, toggle) || !panel->isVisible()) return false;
            }
        }
        if (scenario == "sources_keyboard") {
            field->forceActiveFocus(Qt::TabFocusReason);
            if (!field->hasActiveFocus()) return false;
            toggle->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window, Qt::Key_Space);
        } else if (!click(window, scenario == "sources_narrow" ? item(window, "closeSourceControls") : toggle)) { std::fprintf(stderr,"Cannot close source controls through the visible Sources action.\n"); return false; }
        return !panel->isVisible() && !field->isVisible() && !selector->isVisible() && workspace.currentSession() == firstSession
            && workspace.table()->rowCount() == rows && server.requests.size() == calls;
    }
    if (scenario.startsWith("session_rename_")) {
        if (!waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
        const auto id = workspace.currentSession();
        const auto before = podlord::SessionStore(profile).list();
        if (!std::holds_alternative<podlord::SessionCatalog>(before)) return false;
        const auto original = std::get<podlord::SessionCatalog>(before);
        if (scenario == "session_rename_filter" && !type(window, item(window, "resourceFilter"), "alpha")) return false;
        QString draft;
        if (scenario == "session_rename_draft") {
            if (!type(window, item(window, "resourceFilter"), "alpha")
                || !waitFor([&] { return item(window, "cell_0_0"); }) || !click(window, item(window, "cell_0_0"))
                || !click(window, item(window, "yamlButton")) || !waitFor([&] { return workspace.canEditYaml(); })
                || !click(window, item(window, "editYaml"))) return false;
            auto* editor = item(window, "inspectorYaml"); editor->forceActiveFocus();
            QTest::keySequence(window, QKeySequence::MoveToEndOfDocument); QTest::keyClick(window, Qt::Key_Return);
            for (const auto ch : QString("# preserve rename draft")) QTest::keyClick(window, ch.toLatin1());
            draft = workspace.yamlText();
        }
        if (scenario == "session_rename_context_menu") {
            if (!click(window, item(window, "activateSession_" + id), Qt::RightButton)
                || !waitFor([&] { return item(window, "renameTab_" + id) && item(window, "renameTab_" + id)->isVisible(); })
                || !click(window, item(window, "renameTab_" + id))) return false;
        } else if (!click(window, item(window, "renameCurrentSession"))) return false;
        if (!waitFor([&] { return item(window, "sessionNameInput") && item(window, "sessionNameInput")->isVisible(); })) return false;
        if (scenario == "session_rename_cancel" || scenario == "session_rename_escape") {
            if (!type(window, item(window, "sessionNameInput"), "Changed")) return false;
            if (scenario == "session_rename_cancel") { if (!click(window, item(window, "cancelSessionRename"))) return false; }
            else QTest::keyClick(window, Qt::Key_Escape);
            return waitFor([&] { return !item(window, "sessionNameInput")->isVisible(); }) && workspace.title() == "Unnamed"
                && std::get<podlord::SessionCatalog>(podlord::SessionStore(profile).list()) == original;
        }
        if (scenario == "session_rename_unnamed") {
            if (!type(window, item(window, "sessionNameInput"), "Renamed") || !click(window, item(window, "saveSessionRename"))
                || !waitFor([&] { return !workspace.busy() && workspace.title() == "Renamed"; })
                || !click(window, item(window, "renameCurrentSession")) || !waitFor([&] { return item(window, "sessionNameInput")->isVisible(); })
                || !type(window, item(window, "sessionNameInput"), "")) return false;
        } else if (scenario != "session_rename_repeat" && !type(window, item(window, "sessionNameInput"), scenario == "session_rename_conflict" ? "Taken" : "Renamed")) return false;
        QLockFile lock(profile + "/sessions.lock");
        if (scenario == "session_rename_busy" && !lock.tryLock(0)) return false;
        if (scenario == "session_rename_conflict") {
            const auto context = workspace.contexts().first().toMap().value("id").toString();
            if (!std::holds_alternative<podlord::SessionCatalog>(podlord::SessionStore(profile).create({context, {}}, "Taken"))) return false;
        }
        const auto calls = server.requests.size();
        if (scenario == "session_rename_keyboard") { item(window, "sessionNameInput")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return); }
        else if (!click(window, item(window, "saveSessionRename"))) return false;
        if (scenario == "session_rename_busy" || scenario == "session_rename_conflict") {
            return waitFor([&] { return !workspace.busy() && !text(window, "sessionRenameError").isEmpty(); })
                && item(window, "sessionNameInput")->isVisible() && workspace.title() == "Unnamed" && server.requests.size() == calls;
        }
        const auto expected = scenario == "session_rename_repeat" || scenario == "session_rename_unnamed" ? "Unnamed" : "Renamed";
        if (!waitFor([&] { return !workspace.busy() && workspace.title() == expected && !item(window, "sessionNameInput")->isVisible(); })) return false;
        QTest::qWait(50);
        if (workspace.currentSession() != id || server.requests.size() != calls) return false;
        const auto saved = podlord::SessionStore(profile).list();
        if (!std::holds_alternative<podlord::SessionCatalog>(saved)) return false;
        const auto catalog = std::get<podlord::SessionCatalog>(saved);
        if (catalog.sessions.size() != original.sessions.size() || catalog.sessions.first().usageAt != original.sessions.first().usageAt
            || catalog.sessions.first().id != original.sessions.first().id || catalog.sessions.first().config != original.sessions.first().config) return false;
        if (scenario == "session_rename_repeat") return catalog == original;
        if (scenario == "session_rename_filter") return workspace.filterText() == "alpha" && text(window, "resourceFilter") == "alpha";
        if (scenario == "session_rename_draft") return workspace.yamlEditing() && workspace.yamlDirty() && workspace.yamlText() == draft;
        if (scenario == "session_rename_restart") {
            window->close(); if (!waitFor([&] { return !window->isVisible(); })) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.busy() && restored.currentSession() == id && restored.title() == "Renamed"; });
        }
        return true;
    }
    if (scenario == "view_restore_scope") {
        const auto first = workspace.currentSession();
        if (!type(window, item(window, "resourceFilter"), "alpha")) return false;
        window->close(); if (!waitFor([&] { return !window->isVisible(); })) return false;
        const auto path = profile + "/views/" + first + ".json";
        QFile broken(path); if (!broken.open(QIODevice::WriteOnly) || broken.write("{") != 1) return false; broken.close();
        podlord::Workspace restored(profile); QQmlApplicationEngine secondEngine;
        secondEngine.rootContext()->setContextProperty("workspace", &restored);
        secondEngine.load(QUrl("qrc:/podlord/Main.qml"));
        if (secondEngine.rootObjects().isEmpty()) return false;
        auto* second = qobject_cast<QQuickWindow*>(secondEngine.rootObjects().first());
        if (!second || !waitFor([&] { return !restored.busy() && restored.viewStateFailed(); })
            || !click(second, item(second, "sourcesButton"))) return false;
        item(second, "contexts")->forceActiveFocus(); QTest::keyClick(second, Qt::Key_Down);
        if (!click(second, item(second, "openContext"))
            || !waitFor([&] { return !restored.busy() && restored.currentSession() != first && !restored.currentSession().isEmpty(); })
            || !type(second, item(second, "resourceFilter"), "bravo")) return false;
        podlord::TableSchemas schemas;
        for (const auto& entry : QList<QPair<QString, QAbstractItemModel*>>{{"resource", restored.table()}, {"event", restored.eventTable()},
            {"port", restored.portTable()}, {"inspectorEvent", restored.inspectorEventTable()}, {"inspectorLink", restored.inspectorLinkTable()}, {"value", restored.valuesTable()}}) {
            QStringList ids;
            for (int column = 0; column < entry.second->columnCount(); ++column) ids.append(entry.second->headerData(column, Qt::Horizontal, Qt::UserRole).toString());
            schemas[entry.first] = ids;
        }
        const auto id = restored.currentSession();
        const bool saved = waitFor([&] {
            const auto result = podlord::ViewStateStore(profile, schemas).load(id);
            const auto* value = std::get_if<podlord::TableViewStates>(&result);
            return value && value->value("resource").filter == "bravo";
        });
        if (!broken.open(QIODevice::ReadOnly)) return false;
        return saved && broken.readAll() == "{" && restored.viewStateFailed() && restored.filterText() == "bravo";
    }
    if (scenario.startsWith("view_restore_")) {
        if (!waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
        const auto session = workspace.currentSession();
        const auto path = profile + "/views/" + session + ".json";
        const bool event = scenario == "view_restore_event_filter" || scenario == "view_restore_event_sort";
        if (event && !click(window, item(window, "eventsWorkspaceButton"))) return false;
        const auto inputName = event ? "eventFilter" : "resourceFilter";
        const auto headerName = event ? "eventHeader_5" : "header_0";
        if (scenario == "view_restore_event_sort" && !waitFor([&] { return item(window, headerName) && item(window, headerName)->width() > 0; })) return false;
        const bool filterCase = scenario == "view_restore_filter" || scenario == "view_restore_clear" || scenario == "view_restore_event_filter";
        if (filterCase && !type(window, item(window, inputName), "alpha")) return false;
        if (scenario == "view_restore_clear" && !type(window, item(window, inputName), "")) return false;
        int clicks = scenario == "view_restore_asc" || scenario == "view_restore_event_sort" ? 1
            : scenario == "view_restore_desc" ? 2 : scenario == "view_restore_none" ? 3 : 0;
        for (int index = 0; index < clicks; ++index) if (!click(window, item(window, headerName))) return false;
        if (!filterCase && clicks == 0 && !type(window, item(window, "resourceFilter"), "alpha")) return false;
        window->close();
        if (!waitFor([&] { return !window->isVisible(); })) return false;
        podlord::Workspace restored(profile);
        QQmlApplicationEngine restoredEngine;
        restoredEngine.rootContext()->setContextProperty("workspace", &restored);
        restoredEngine.load(QUrl("qrc:/podlord/Main.qml"));
        if (restoredEngine.rootObjects().isEmpty()) return false;
        auto* reopened = qobject_cast<QQuickWindow*>(restoredEngine.rootObjects().first());
        if (!reopened || !waitFor([&] { return !restored.busy() && restored.currentSession() == session; })) return false;
        if (scenario == "view_restore_filter") return restored.filterText() == "alpha" && text(reopened, "resourceFilter") == "alpha";
        if (scenario == "view_restore_clear") return restored.filterText().isEmpty();
        if (scenario == "view_restore_asc") return restored.sortColumnIndex() == 0 && restored.sortDirection() == "ASC";
        if (scenario == "view_restore_desc") return restored.sortColumnIndex() == 0 && restored.sortDirection() == "DESC";
        if (scenario == "view_restore_none") return restored.sortColumnIndex() == -1;
        if (scenario == "view_restore_event_filter") return restored.eventFilterText() == "alpha";
        if (scenario == "view_restore_event_sort") return restored.eventSortColumnIndex() == 5 && restored.eventSortDirection() == "ASC";
        if (scenario == "view_restore_corrupt") {
            QFile broken(path); if (!broken.open(QIODevice::WriteOnly) || broken.write("{") != 1) return false; broken.close();
            if (!restored.reloadSavedViews() || !waitFor([&] { return !restored.busy() && restored.viewStateFailed(); })) return false;
            if (!type(reopened, item(reopened, "resourceFilter"), "bravo")) return false;
            QTest::qWait(30);
            if (!broken.open(QIODevice::ReadOnly) || broken.readAll() != "{") return false;
            return !restored.error().isEmpty() && restored.filterText() == "bravo";
        }
        if (scenario == "view_restore_conflict") {
            QFile changed(path); if (!changed.open(QIODevice::ReadOnly)) return false;
            auto document = QJsonDocument::fromJson(changed.readAll()).object(); changed.close();
            auto views = document["views"].toObject(), resource = views["resource"].toObject();
            resource["filter"] = "external"; views["resource"] = resource; document["views"] = views;
            if (!changed.open(QIODevice::WriteOnly) || changed.write(QJsonDocument(document).toJson()) < 0) return false; changed.close();
            if (!type(reopened, item(reopened, "resourceFilter"), "bravo") || !waitFor([&] { return restored.viewStateFailed(); })) return false;
            if (!click(reopened, item(reopened, "reloadSavedViews")) || !waitFor([&] { return !restored.busy() && !restored.viewStateFailed(); })) return false;
            return restored.filterText() == "external" && text(reopened, "resourceFilter") == "external";
        }
        if (scenario == "view_restore_close_stay" || scenario == "view_restore_close_discard" || scenario == "view_restore_close_escape") {
            QLockFile locked(path + ".lock"); if (!locked.tryLock(0)) return false;
            if (!type(reopened, item(reopened, "resourceFilter"), "bravo") || !waitFor([&] { return restored.viewStateFailed(); })) return false;
            reopened->close();
            if (!waitFor([&] { return restored.viewCloseNeedsDecision() && item(reopened, "stayViewClose")->isVisible(); })) return false;
            if (scenario == "view_restore_close_escape") { QTest::keyClick(reopened, Qt::Key_Escape); return waitFor([&] { return !restored.viewCloseNeedsDecision(); }) && reopened->isVisible(); }
            if (!click(reopened, item(reopened, scenario == "view_restore_close_stay" ? "stayViewClose" : "discardViewClose"))) return false;
            return waitFor([&] { return !restored.viewCloseNeedsDecision(); })
                && reopened->isVisible() == (scenario == "view_restore_close_stay") && restored.filterText() == "bravo";
        }
        return false;
    }
    if (scenario.startsWith("filter_event_")) {
        const int requests = server.requests.size();
        if (!type(window,item(window,"resourceFilter"),"\"alpha\"") || !click(window,item(window,"eventsWorkspaceButton"))) return false;
        if (!waitFor([&] { return item(window,"eventFilter")->isVisible() && workspace.totalEventCount()==2; })) return false;
        QString expression="\"Second event message\"", expected="Started";
        if (scenario=="filter_event_or") { expression="Scheduled Started"; expected="Scheduled,Started"; }
        else if (scenario=="filter_event_prefix") expression="~Started";
        else if (scenario=="filter_event_regex") expression="/Second.*message$/";
        else if (scenario=="filter_event_number") expression=">3";
        else if (scenario=="filter_event_invalid" || scenario=="filter_event_reset") { expression="/[/"; expected.clear(); }
        else if (scenario=="filter_event_sort") { expression="Scheduled Started"; expected="Started,Scheduled"; }
        if (!type(window,item(window,"eventFilter"),expression)) return false;
        if (scenario=="filter_event_sort") {
            if (!waitFor([&] { auto* header=item(window,"eventHeader_2"); return header && header->width()>0; }) || !click(window,item(window,"eventHeader_2")) || !click(window,item(window,"eventHeader_2"))) return false;
        }
        if (scenario=="filter_event_reset") {
            if (!item(window,"filterErrorMessage")->isVisible() || !click(window,item(window,"resetResourceFilters"))) return false;
            expression.clear(); expected="Scheduled,Started";
        }
        const auto names=[&] {
            QStringList result; const auto* model=workspace.eventTable();
            for(int row=0;row<model->rowCount();++row) result.append(model->data(model->index(row,2)).toString());
            return result.join(',');
        };
        if (!waitFor([&] { return names()==expected; })) {
            std::fprintf(stderr,"Event search UI: %s expected %s; error=%s\n",qPrintable(names()),qPrintable(expected),qPrintable(text(window,"filterErrorMessage"))); return false;
        }
        if (!click(window,item(window,"resourcesWorkspaceButton")) || !waitFor([&] { return item(window,"resourceFilter")->isVisible(); })) return false;
        if (workspace.filterText()!="\"alpha\"" || workspace.resourceCount()!=1 || item(window,"filterErrorMessage")->isVisible()) return false;
        if (!click(window,item(window,"eventsWorkspaceButton")) || !waitFor([&] { return item(window,"eventFilter")->isVisible(); })) return false;
        return names()==expected && workspace.eventFilterText()==expression
            && item(window,"filterErrorMessage")->isVisible()==(scenario=="filter_event_invalid") && server.requests.size()==requests;
    }
    if (scenario.startsWith("filter_search_")) {
        const int requests = server.requests.size();
        QString expression = "\"alpha\"", expected = "alpha";
        bool invalid = false;
        if (scenario == "filter_search_or") { expression = "alpha bravo"; expected = "alpha,bravo"; }
        else if (scenario == "filter_search_prefix") expression = "~ALP";
        else if (scenario == "filter_search_suffix") { expression = "widget~"; expected = "custom-widget"; }
        else if (scenario == "filter_search_regex") expression = "/^a.*a$/";
        else if (scenario == "filter_search_invalid") { expression = "/[/"; expected.clear(); invalid = true; }
        else if (scenario == "filter_search_unclosed") { expression = "\"alpha"; expected.clear(); invalid = true; }
        else if (scenario == "filter_search_reset") expression = "/[/";
        else if (scenario == "filter_search_sort") { expression = "alpha bravo"; expected = "bravo,alpha"; }
        auto* input = item(window, "resourceFilter");
        if (!type(window, input, expression)) return false;
        const auto names = [&] {
            QStringList result;
            for (int row = 0; row < workspace.table()->rowCount(); ++row)
                result.append(workspace.table()->data(workspace.table()->index(row,0)).toString());
            return result.join(',');
        };
        if (scenario == "filter_search_reset") {
            if (!waitFor([&] { return item(window,"filterErrorMessage")->isVisible(); }) || !click(window,item(window,"resetResourceFilters"))) return false;
            expected = "alpha,bravo,custom-widget"; expression.clear();
        }
        if (scenario == "filter_search_sort") {
            if (!click(window,item(window,"header_0")) || !click(window,item(window,"header_0"))) return false;
        }
        if (!waitFor([&] { return names() == expected; })) {
            std::fprintf(stderr,"Search UI: %s expected %s; error=%s\n",qPrintable(names()),qPrintable(expected),qPrintable(text(window,"filterErrorMessage"))); return false;
        }
        if (item(window,"filterErrorMessage")->isVisible() != invalid) return false;
        if (invalid && text(window,"filterErrorMessage").isEmpty()) return false;
        if (scenario == "filter_search_repeat") {
            if (!type(window,input,expression) || names() != expected) return false;
        }
        if (scenario == "filter_search_keyboard") {
            input->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Tab);
            if (!window->activeFocusItem() || window->activeFocusItem() == input) return false;
        }
        if (scenario == "filter_search_radar") {
            if (!click(window,item(window,"radarWorkspaceButton")) || !waitFor([&] { return workspace.workspacePage()=="resources"; })) return false;
            if (workspace.resourceCount()!=1 || workspace.totalResourceCount()!=3 || workspace.table()->data(workspace.table()->index(0,0)).toString()!="alpha") return false;
        }
        if (scenario == "filter_search_restore") {
            if (server.requests.size() != requests) {
                std::fprintf(stderr, "Editing a cached search sent a Kubernetes request\n"); return false;
            }
            const auto session = workspace.currentSession();
            podlord::TableSchemas schemas;
            for (const auto& entry : {std::pair{QString("resource"), workspace.table()}, std::pair{QString("event"), workspace.eventTable()},
                std::pair{QString("port"), workspace.portTable()}, std::pair{QString("inspectorEvent"), workspace.inspectorEventTable()},
                std::pair{QString("inspectorLink"), workspace.inspectorLinkTable()}, std::pair{QString("value"), workspace.valuesTable()}}) {
                QStringList columns;
                for (int column = 0; column < entry.second->columnCount(); ++column) columns.append(entry.second->headerData(column, Qt::Horizontal, Qt::UserRole).toString());
                schemas.insert(entry.first, columns);
            }
            if (!waitFor([&] {
                const auto loaded = podlord::ViewStateStore(profile, schemas).load(session);
                return std::holds_alternative<podlord::TableViewStates>(loaded) && std::get<podlord::TableViewStates>(loaded).value("resource").filter==expression;
            })) return false;
            podlord::Workspace restored(profile);
            if (!waitFor([&] { return !restored.busy() && !restored.loading() && restored.currentSession()==session
                && restored.filterText()==expression && restored.totalResourceCount()==3 && restored.resourceCount()==1; })) {
                std::fprintf(stderr, "Cold-start search restoration failed: filter=%s total=%d visible=%d error=%s\n",
                    qPrintable(restored.filterText()), restored.totalResourceCount(), restored.resourceCount(), qPrintable(restored.error()));
                return false;
            }
            return workspace.filterText()==expression;
        }
        return server.requests.size()==requests && workspace.filterText()==expression;
    }
    if (scenario.startsWith("language_cache_")) {
        const QString language = scenario.endsWith("_ar") ? "ar" : "de";
        if (!click(window, item(window, "cell_0_0")) || !waitFor([&] { return workspace.canEditYaml(); })) return false;
        const auto session = workspace.currentSession();
        const auto requests = server.requests.size();
        QStringList before;
        for (int row = 0; row < workspace.table()->rowCount(); ++row)
            for (int column = 0; column < workspace.table()->columnCount(); ++column)
                before.append(workspace.table()->data(workspace.table()->index(row, column)).toString());
        if (!click(window, item(window, "settingsWorkspaceButton")) || !click(window, item(window, "settingsAppearanceSection"))
            || !click(window, item(window, "inlineUiLanguage"))) return false;
        QTest::keyClick(window, Qt::Key_Home);
        const auto languages = workspace.uiLanguages();
        for (const auto& option : languages) {
            if (option.toMap()["code"] == language) break;
            QTest::keyClick(window, Qt::Key_Down);
        }
        QTest::keyClick(window, Qt::Key_Return);
        if (!waitFor([&] { return !workspace.busy() && workspace.uiLanguage() == language; })
            || !click(window, item(window, "resourcesWorkspaceButton"))) return false;
        QStringList after;
        for (int row = 0; row < workspace.table()->rowCount(); ++row)
            for (int column = 0; column < workspace.table()->columnCount(); ++column)
                after.append(workspace.table()->data(workspace.table()->index(row, column)).toString());
        if (before.isEmpty() || after != before || workspace.currentSession() != session || server.requests.size() != requests) return false;
        for (const auto& label : QList<QPair<QString, QString>>{{"overviewButton", "inspector.overview"}, {"yamlButton", "inspector.yaml"},
            {"inspectorEventsButton", "inspector.events"}, {"inspectorLinksButton", "inspector.links"}, {"logsButton", "inspector.logs"}}) {
            auto* control = item(window, label.first);
            if (!control || control->property("text") != workspace.uiText()[label.second]) {
                std::fprintf(stderr, "Inspector label %s did not apply language %s.\n", qPrintable(label.first), qPrintable(language));
                return false;
            }
        }
        auto* problems = item(window, "problemsOnly");
        if (!problems || problems->property("text") != workspace.uiText()["filters.problems"]
            || problems->property("mirrored").toBool() != (language == "ar")
            || !click(window, problems) || !workspace.problemsOnly()
            || !click(window, problems) || workspace.problemsOnly()) return false;
        return workspace.table()->rowCount() * workspace.table()->columnCount() == before.size()
            && server.requests.size() == requests && workspace.currentSession() == session;
    }
    if (scenario.startsWith("table_")) {
        auto* model = workspace.table();
        const auto column = [&](const QString& id) {
            for (int index = 0; index < model->columnCount(); ++index) if (model->headerData(index, Qt::Horizontal, Qt::UserRole) == id) return index;
            return -1;
        };
        const QStringList fields{"name", "kind", "namespace", "status", "node", "image", "cluster", "cpu", "memory", "storage", "createdAt", "ready", "restarts", "owner", "issue", "uid"};
        for (const auto& field : fields) if (column(field) < 0) { std::fprintf(stderr, "Missing resource column: %s\n", qPrintable(field)); return false; }
        if (!waitFor([&] { return model->data(model->index(0,column("cpu"))).toString() == (scenario == "table_zero" ? "0 mCPU" : "2000 mCPU"); })) {
            std::fprintf(stderr,"Table metrics unavailable: %s; %s; requests=%s\n", qPrintable(model->data(model->index(0,column("cpu"))).toString()), qPrintable(workspace.status()), qPrintable(server.requests.join('\n'))); return false;
        }
        const int requests = server.requests.size();
        auto* view = item(window, "resourceTable");
        const auto show = [&](int index) {
            QList<int> order;
            for (const auto& value:workspace.resourceColumns()) { const auto state=value.toMap(); if (state["visible"].toBool() && state["pinned"].toBool()) order.append(state["column"].toInt()); }
            for (const auto& value:workspace.resourceColumns()) { const auto state=value.toMap(); if (!state["visible"].toBool() || !state["pinned"].toBool()) order.append(state["column"].toInt()); }
            QQmlExpression position(qmlContext(view), view, QString("positionViewAtColumn(%1, TableView.Contain)").arg(order.indexOf(index))); position.evaluate();
            const bool shown = !position.hasError() && waitFor([&] {
                auto* cell = item(window, "cell_0_" + QString::number(index));
                if (!cell || !cell->isVisible() || cell->width() <= 0 || view->property("moving").toBool()) return false;
                const auto left = cell->mapToItem(view, {0,0}).x();
                return left >= -1 && left + cell->width() <= view->width() + 1;
            });
            if (!shown) {
                auto* cell = item(window, "cell_0_" + QString::number(index));
                std::fprintf(stderr, "Column %d is not displayed: viewport=%g x=%g cell=%g/%g moving=%d\n", index, view->width(), view->property("contentX").toDouble(),
                    cell ? cell->mapToItem(view, {0,0}).x() : -1, cell ? cell->width() : -1, view->property("moving").toBool());
            }
            return shown;
        };
        const auto value = [&](int row, const QString& field) { return model->data(model->index(row, column(field))).toString(); };
        if (scenario == "table_columns") return model->columnCount() == fields.size() && workspace.resourceColumns().size() == fields.size();
        if (scenario=="table_status_colors") {
            const QStringList colors{"success","warning","danger"};
            for (int row=0;row<3;++row) if (model->data(model->index(row,column("status")),Qt::ForegroundRole).value<QColor>()!=workspace.appearanceColors()[colors[row]].value<QColor>()) return false;
            return server.requests.size()==requests;
        }
        if (scenario.startsWith("table_overview_")) {
            if (scenario == "table_overview_custom") {
                if (!type(window,item(window,"resourceFilter"),"custom-widget") || !waitFor([&] { return model->rowCount()==1 && item(window,"cell_0_0"); })) return false;
            }
            if (!click(window,item(window,"cell_0_0")) || !waitFor([&] { return workspace.canEditYaml(); })) return false;
            const auto calls=server.requests.size();
            const auto* overview=item(window,"overviewScroll");
            if (!overview) return false;
            if (scenario == "table_overview_custom") return text(window,"overview_cluster")=="local" && !item(window,"overview_ready") && !item(window,"overviewReadinessBar") && server.requests.size()==calls;
            const QString expected=scenario=="table_overview_full" ? "10/10" : scenario=="table_overview_none" || scenario=="table_overview_pending" ? "0/10" : "2/10";
            if (!waitFor([&] { return text(window,"overview_ready")==expected && item(window,"overviewReadinessBar"); })) return false;
            auto* bar=item(window,"overviewReadinessBar");
            const auto fraction=scenario=="table_overview_full" ? 1.0 : scenario=="table_overview_none" || scenario=="table_overview_pending" ? 0.0 : 0.2;
            const QString color=scenario=="table_overview_full" ? "success" : scenario=="table_overview_none" || scenario=="table_overview_pending" ? "danger" : "warning";
            const auto* fill=item(window,"overviewReadinessFill");
            if (!fill || qAbs(bar->property("value").toDouble()-fraction)>0.001 || fill->property("color").value<QColor>() != workspace.appearanceColors()[color].value<QColor>()) return false;
            if (scenario=="table_overview_metadata") return text(window,"overview_cluster")=="local" && text(window,"overview_owner")=="ReplicaSet/workload" && text(window,"overview_restarts")=="10" && server.requests.size()==calls;
            if (scenario=="table_overview_copy") {
                item(window,"overview_ready")->forceActiveFocus(); QTest::keySequence(window,QKeySequence::SelectAll); QTest::keySequence(window,QKeySequence::Copy);
                return QGuiApplication::clipboard()->text()==expected && server.requests.size()==calls;
            }
            if (scenario=="table_overview_refresh") {
                const auto scope=workspace.inspectorScope();
                if (!click(window,item(window,"refreshButton")) || !waitFor([&] { return !workspace.loading() && text(window,"overview_ready")=="10/10"; })) return false;
                return workspace.inspectorScope()==scope && qAbs(item(window,"overviewReadinessBar")->property("value").toDouble()-1)<0.001;
            }
            if (scenario=="table_overview_metric_size") {
                auto* scroll=item(window,"overviewScroll"); scroll->forceActiveFocus(); QTest::keyClick(window,Qt::Key_End);
                return waitFor([&] { const auto* metric=item(window,"metric_memory_bar"); return metric && metric->isVisible() && metric->height()>=6 && metric->height()<=10; }) && server.requests.size()==calls;
            }
            return server.requests.size()==calls;
        }
        if (scenario == "table_cluster_switch") {
            const auto first=workspace.currentSession();
            const auto selection=item(window,"resourceTable")->property("selectionModel").value<QItemSelectionModel*>();
            if (!selection) return false;
            selection->setCurrentIndex(model->index(0,0),QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            if (!click(window,item(window,"sourcesButton"))) return false;
            auto* contexts=item(window,"contexts");
            const auto firstCluster=value(0,"cluster");
            const auto nextCluster=firstCluster=="local" ? "other" : "local";
            const int nextIndex=contexts->property("currentIndex").toInt()==0 ? 1 : 0;
            if (!click(window,contexts)) return false;
            QTest::keyClick(window,nextIndex==1 ? Qt::Key_End : Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return);
            if (!waitFor([&] { return contexts->property("currentIndex").toInt()==nextIndex; }) || !click(window,item(window,"openContext"))) { std::fprintf(stderr,"Context choice did not change: current=%d expected=%d\n",contexts->property("currentIndex").toInt(),nextIndex); return false; }
            if (!waitFor([&] { return workspace.currentSession()!=first && model->rowCount()==4 && !workspace.loading() && value(0,"cluster")==nextCluster && value(0,"cpu")=="2000 mCPU"; })) { std::fprintf(stderr,"Context table did not change: cluster=%s expected=%s rows=%d status=%s\n",qPrintable(value(0,"cluster")),nextCluster,model->rowCount(),qPrintable(workspace.status())); return false; }
            if (selection->currentIndex().isValid() || selection->hasSelection()) return false;
            if (!type(window,item(window,"resourceFilter"),nextCluster) || !waitFor([&] { return model->rowCount()==4; })) return false;
            const auto calls=server.requests.size();
            selection->setCurrentIndex(model->index(0,0),QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            if (!click(window,item(window,"activateSession_"+first))) return false;
            return waitFor([&] { return workspace.currentSession()==first && model->rowCount()==4 && value(0,"cluster")==firstCluster; }) && !selection->currentIndex().isValid() && !selection->hasSelection() && server.requests.size()==calls;
        }
        if (scenario == "table_values" || scenario == "table_zero") {
            const QString cpu = scenario == "table_zero" ? "0 mCPU" : "2000 mCPU", memory = scenario == "table_zero" ? "0 B" : "2 GiB";
            if (value(0, "cpu") != cpu || value(0, "memory") != memory || value(0, "storage") != "-" || value(0, "cluster") != "local"
                || value(0, "ready") != "2/10" || value(0, "restarts") != "10" || value(0, "owner") != "ReplicaSet/workload" || !value(0, "issue").contains("Ready 2/10")) {
                std::fprintf(stderr, "Table values: cpu=%s memory=%s storage=%s cluster=%s ready=%s restarts=%s owner=%s issue=%s\n", qPrintable(value(0,"cpu")), qPrintable(value(0,"memory")), qPrintable(value(0,"storage")), qPrintable(value(0,"cluster")), qPrintable(value(0,"ready")), qPrintable(value(0,"restarts")), qPrintable(value(0,"owner")), qPrintable(value(0,"issue"))); return false;
            }
            if (!click(window,item(window,"resourceColumnsButton"))) return false;
            item(window,"resourceColumnVisible_issue")->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
            item(window,"resourceColumnVisible_uid")->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
            if (!click(window,item(window,"resourceSaveColumns")) || !waitFor([&] { return !workspace.tableLayoutSaving(); })) return false;
            for (const auto& field : fields) if (!show(column(field)) || text(window, "cell_0_" + QString::number(column(field))) != value(0, field)) return false;
            return server.requests.size() == requests;
        }
        if (scenario.startsWith("table_sort_")) {
            const auto field = scenario.mid(QString("table_sort_").size());
            const int index = column(field);
            if (index < 0 || !show(index)) return false;
            const bool readiness = field == "ready";
            const QString first = readiness ? "charlie" : field == "createdAt" ? "alpha" : "bravo", second = readiness ? "alpha" : first == "alpha" ? "bravo" : "alpha", third = readiness ? "bravo" : "charlie";
            const auto order = [&](const QString& a, const QString& b, const QString& c) { return value(0,"name") == a && value(1,"name") == b && value(2,"name") == c && value(3,"name") == "custom-widget"; };
            if (!click(window, item(window, "header_" + QString::number(index))) || !waitFor([&] { return order(first, second, third) && workspace.sortDirection() == "ASC"; })) {
                std::fprintf(stderr, "Sort %s: header=%s requested=%d actual=%d/%s names=%s,%s,%s,%s\n", qPrintable(field), qPrintable(text(window, "header_" + QString::number(index))), index, workspace.sortColumnIndex(), qPrintable(workspace.sortDirection()), qPrintable(value(0,"name")), qPrintable(value(1,"name")), qPrintable(value(2,"name")), qPrintable(value(3,"name")));
                return false;
            }
            if (!click(window, item(window, "header_" + QString::number(index))) || !waitFor([&] { return order(readiness ? third : second, readiness ? second : first, readiness ? first : third) && workspace.sortDirection() == "DESC"; })) {
                std::fprintf(stderr, "Second sort %s: actual=%d/%s names=%s,%s,%s,%s\n", qPrintable(field), workspace.sortColumnIndex(), qPrintable(workspace.sortDirection()), qPrintable(value(0,"name")), qPrintable(value(1,"name")), qPrintable(value(2,"name")), qPrintable(value(3,"name")));
                return false;
            }
            return click(window, item(window, "header_" + QString::number(index))) && waitFor([&] { return workspace.sortColumnIndex() == -1 && order("alpha", "bravo", "charlie"); }) && server.requests.size() == requests;
        }
        if (scenario == "table_cluster_filter" || scenario == "table_owner_filter") {
            return type(window, item(window, "resourceFilter"), scenario == "table_cluster_filter" ? "local" : "ReplicaSet/workload")
                && waitFor([&] { return model->rowCount() == (scenario == "table_cluster_filter" ? 4 : 3); }) && server.requests.size() == requests;
        }
        if (scenario == "table_copy") {
            if (!show(column("memory"))) return false;
            item(window, "cell_0_" + QString::number(column("memory")))->forceActiveFocus(); QTest::keySequence(window, QKeySequence::Copy);
            return QGuiApplication::clipboard()->text() == "2 GiB" && workspace.inspectorPath().isEmpty() && server.requests.size() == requests;
        }
        if (scenario == "table_hover" || scenario == "table_keyboard_hover") {
            if (!show(column("image"))) return false;
            auto* cell = item(window, "cell_0_" + QString::number(column("image")));
            if (scenario == "table_hover") QTest::mouseMove(window, cell->mapToScene({cell->width()/2, cell->height()/2}).toPoint()); else cell->forceActiveFocus();
            const bool shown = waitFor([&] {
                for (auto* surface : QGuiApplication::allWindows()) if (auto* quick = qobject_cast<QQuickWindow*>(surface)) {
                    const auto* tip = item(quick,"plainTipText");
                    if (quick->isVisible() && tip && tip->isVisible() && tip->property("text").toString() == value(0,"image")) return true;
                }
                return false;
            });
            if (!shown) std::fprintf(stderr,"Table tooltip: hovered=%d hoverEnabled=%d focused=%d wantsTip=%d visible=%d text=%s\n",cell->property("hovered").toBool(),cell->property("hoverEnabled").toBool(),cell->hasActiveFocus(),cell->property("wantsTip").toBool(),cell->isVisible(),qPrintable(cell->property("text").toString()));
            return shown && server.requests.size() == requests;
        }
        if (scenario == "table_identity") {
            for (const auto& field : {QString("cluster"), QString("namespace"), QString("node"), QString("status"), QString("kind")}) {
                const auto a = model->data(model->index(0,column(field)), Qt::ForegroundRole).value<QColor>(), b = model->data(model->index(1,column(field)), Qt::ForegroundRole).value<QColor>();
                if (a.alpha() == 0 || a != b || !show(column(field)) || item(window,"cell_0_"+QString::number(column(field)))->property("identityColor").value<QColor>() != a) return false;
            }
            return model->data(model->index(0,column("cpu")), Qt::ForegroundRole).value<QColor>().alpha() == 0 && server.requests.size() == requests;
        }
        if (scenario == "table_refresh") {
            if (!show(column("image")) || !click(window,item(window,"cell_1_"+QString::number(column("image")))) || !waitFor([&] { return workspace.inspectorName() == "bravo" && workspace.canEditYaml(); })) {
                std::fprintf(stderr,"Table refresh selection failed: %s; %s; x=%g\n",qPrintable(workspace.inspectorName()),qPrintable(workspace.inspectorStatus()),view->property("contentX").toDouble()); return false;
            }
            QTest::qWait(50);
            const auto x = view->property("contentX").toDouble(), y = view->property("contentY").toDouble();
            const int previous = server.refresh;
            if (!click(window,item(window,"refreshButton")) || !waitFor([&] { return !workspace.loading() && server.refresh > previous; })) return false;
            const bool retained = workspace.inspectorName() == "bravo" && qAbs(view->property("contentX").toDouble()-x) < 1 && qAbs(view->property("contentY").toDouble()-y) < 1;
            if (!retained) std::fprintf(stderr,"Table refresh moved: %s x=%g->%g y=%g->%g\n",qPrintable(workspace.inspectorName()),x,view->property("contentX").toDouble(),y,view->property("contentY").toDouble());
            return retained;
        }
        return false;
    }
    if (scenario.startsWith("columns_")) {
        const auto requests = server.requests.size();
        const auto clickSetting = [&](const QString& name) {
            auto* target = item(window, name);
            if (!target) return false;
            for (int attempt = 0; attempt < 15; ++attempt) {
                QQuickItem* clipped = nullptr;
                QPointF local;
                for (auto* parent = target->parentItem(); parent; parent = parent->parentItem()) {
                    if (!parent->clip()) continue;
                    const auto point = target->mapToItem(parent, {target->width() / 2, target->height() / 2});
                    if (point.y() - target->height() / 2 < 4 || point.y() + target->height() / 2 > parent->height() - 4) { clipped = parent; local = point; break; }
                }
                if (!clipped) return click(window, target);
                const int direction = local.y() - target->height() / 2 < 4 ? 1 : -1;
                const auto position = clipped->mapToScene({clipped->width() / 2, clipped->height() / 2});
                QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), QPoint(0, direction * 150), QPoint(0, direction * 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &wheel); QTest::qWait(30);
                if (!waitFor([&] {
                    for (auto* parent = target->parentItem(); parent; parent = parent->parentItem()) if (parent->property("moving").toBool()) return false;
                    return true;
                })) return false;
            }
            return false;
        };
        const auto columns = [&](const QString& type) { return workspace.property((type + "Columns").toLatin1()).toList(); };
        const auto column = [&](const QString& type, const QString& id) {
            for (const auto& value : columns(type)) if (value.toMap().value("id") == id) return value.toMap();
            return QVariantMap{};
        };
        if (!click(window, item(window, "resourceColumnsButton")) || !waitFor([&] { return item(window, "resourceColumnVisible_kind") && item(window, "resourceColumnVisible_kind")->isVisible(); })) return false;
        if (scenario == "columns_keyboard") {
            item(window, "resourceColumnVisible_kind")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        } else if (scenario == "columns_pin" || scenario.startsWith("columns_pin_namespace")) {
            if (!click(window, item(window, scenario == "columns_pin" ? "resourceColumnPinned_name" : "resourceColumnPinned_namespace"))) return false;
        } else if (scenario == "columns_pin_all") {
            for (const auto& value : columns("resource")) {
                const auto name = "resourceColumnPinned_" + value.toMap()["id"].toString();
                auto* control = item(window, name);
                if (!control) return false;
                control->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
                if (!waitFor([&] { return item(window, name)->property("checked").toBool(); })) return false;
            }
        } else if (scenario == "columns_order" || scenario == "columns_sort_copy") {
            if (!click(window, item(window, "resourceColumnEarlier_kind"))) return false;
        } else if (scenario == "columns_width" || scenario == "columns_invalid_width") {
            if (!type(window, item(window, "resourceColumnWidth_name"), scenario == "columns_width" ? "450" : "")) return false;
            if (scenario == "columns_invalid_width") QTest::keyClick(window, Qt::Key_Backspace);
        } else if (scenario == "columns_last_visible") {
            for (const auto& value : columns("resource")) {
                if (!value.toMap()["visible"].toBool()) continue;
                const auto name = "resourceColumnVisible_" + value.toMap()["id"].toString();
                auto* control = item(window, name);
                if (!control) return false;
                control->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
                if (!waitFor([&] { return !item(window, name)->property("checked").toBool(); })) { std::fprintf(stderr, "Checkbox was not toggled: %s\n", qPrintable(name)); return false; }
            }
        } else if (!click(window, item(window, "resourceColumnVisible_kind"))) return false;
        if (scenario == "columns_cancel") {
            return click(window, item(window, "resourceCancelColumns")) && waitFor([&] { auto* control = item(window, "resourceColumnVisible_kind"); return !control || !control->isVisible(); }) && column("resource", "kind")["visible"].toBool() && server.requests.size() == requests;
        }
        QLockFile lock(QDir(profile).filePath("table-layouts.json.lock"));
        if (scenario == "columns_busy" && !lock.tryLock(0)) return false;
        if (scenario == "columns_conflict") {
            QJsonObject layouts;
            for (const auto& type : {QString("resource"), QString("event")}) {
                QJsonArray entries;
                for (const auto& value : columns(type)) {
                    const auto entry = value.toMap();
                    entries.append(QJsonObject{{"id", entry["id"].toString()}, {"visible", entry["visible"].toBool()}, {"pinned", entry["pinned"].toBool()}, {"width", type == "resource" && entry["id"] == "name" ? 430 : entry["width"].toInt()}});
                }
                layouts[type] = entries;
            }
            QFile changed(QDir(profile).filePath("table-layouts.json"));
            if (!changed.open(QIODevice::WriteOnly) || changed.write(QJsonDocument(QJsonObject{{"version", 1}, {"layouts", layouts}}).toJson()) < 0) return false;
        }
        if (scenario == "columns_keyboard") { item(window, "resourceSaveColumns")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return); }
        else if (!click(window, item(window, "resourceSaveColumns"))) return false;
        if (scenario == "columns_busy" || scenario == "columns_conflict" || scenario == "columns_invalid_width" || scenario == "columns_last_visible") {
            if (!waitFor([&] { return !workspace.property("tableLayoutError").toString().isEmpty(); })) {
                std::fprintf(stderr, "Save produced no validation error; saving=%d, columns=%s\n", workspace.property("tableLayoutSaving").toBool(), QJsonDocument(QJsonArray::fromVariantList(columns("resource"))).toJson(QJsonDocument::Compact).constData()); return false;
            }
            return item(window, "resourceColumnVisible_kind") && item(window, "resourceColumnVisible_kind")->isVisible() && column("resource", "kind")["visible"].toBool() && server.requests.size() == requests;
        }
        if (!waitFor([&] {
            auto* dialog = window->findChild<QObject*>("resourceColumnsDialog");
            return dialog && !dialog->property("visible").toBool() && !dialog->property("opened").toBool();
        })) return false;
        if (scenario == "columns_order" || scenario == "columns_sort_copy") {
            if (!waitFor([&] { return item(window, "header_1")->mapToScene({0, 0}).x() < item(window, "header_0")->mapToScene({0, 0}).x(); })) return false;
            if (scenario == "columns_order") return server.requests.size() == requests;
            if (!click(window, item(window, "header_1")) || !waitFor([&] { return workspace.sortColumnIndex() == 1; })) return false;
            auto* cell = item(window, "cell_0_1");
            if (!cell) return false;
            cell->forceActiveFocus();
            if (!waitFor([&] { return cell->hasActiveFocus(); })) return false;
            QTest::keySequence(window, QKeySequence::Copy);
            const bool copied = waitFor([&] { return QGuiApplication::clipboard()->text() == workspace.table()->data(workspace.table()->index(0, 1)).toString(); });
            if (!copied) {
                const auto* focused = window->activeFocusItem();
                std::fprintf(stderr, "Reordered copy: clipboard=%s expected=%s focus=%s/%s cellFocus=%d currentColumn=%d\n",
                    qPrintable(QGuiApplication::clipboard()->text()), qPrintable(workspace.table()->data(workspace.table()->index(0, 1)).toString()),
                    focused ? focused->metaObject()->className() : "none", focused ? qPrintable(focused->objectName()) : "none",
                    cell->hasActiveFocus(), item(window,"resourceTable")->property("currentColumn").toInt());
            }
            return copied && server.requests.size() == requests;
        }
        if (scenario == "columns_width") {
            const bool resized = waitFor([&] { return qAbs(item(window, "header_0")->width() - 450) < 1; });
            if (!resized) std::fprintf(stderr, "Name width: saved=%d header=%s/%g\n", column("resource", "name")["width"].toInt(), qPrintable(text(window,"header_0")), item(window,"header_0")->width());
            return resized && server.requests.size() == requests;
        }
        if (scenario == "columns_pin_all") {
            window->resize(760, 640); QTest::qWait(30);
            auto* scroll = item(window, "resourcePinnedTable");
            if (!scroll || !waitFor([&] { return text(window, "pinnedCell_0_0") == "radar-0000"; })) return false;
            const auto position = scroll->mapToScene({scroll->width() / 2, scroll->height() / 2});
            QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), QPoint(0, -360), QPoint(0, -720), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &wheel);
            if (!waitFor([&] { return scroll->property("topRow").toInt() > 0; })) return false;
            const auto row = scroll->property("topRow").toInt();
            return waitFor([&] { return text(window, "pinnedCell_" + QString::number(row) + "_0") == QString("radar-%1").arg(row, 4, 10, QChar('0')); }) && server.requests.size() == requests;
        }
        if (scenario == "columns_pin" || scenario.startsWith("columns_pin_namespace")) {
            const int pinned = scenario == "columns_pin" ? 0 : 2;
            const auto header = "pinnedHeader_" + QString::number(pinned);
            if (!waitFor([&] { return item(window, "resourcePinnedTable") && item(window, "resourcePinnedTable")->isVisible() && item(window, header); })) {
                for (auto* control : window->findChildren<QQuickItem*>())
                    if (control->objectName().contains("Header_") || control->objectName().startsWith("header_")) std::fprintf(stderr, "Rendered header %s: %s\n", qPrintable(control->objectName()), qPrintable(control->property("text").toString()));
                return false;
            }
            if (scenario.startsWith("columns_pin_namespace")) {
                if (!text(window, header).startsWith("Namespace") || text(window, "pinnedCell_0_2") != "default") {
                    std::fprintf(stderr, "Namespace presentation: header=%s cell=%s\n", qPrintable(text(window, header)), qPrintable(text(window, "pinnedCell_0_2"))); return false;
                }
                auto* cell = item(window, "pinnedCell_0_2");
                cell->forceActiveFocus();
                if (!waitFor([&] { return cell->hasActiveFocus(); })) return false;
                if (scenario != "columns_pin_namespace") {
                    auto* table = item(window, "resourcePinnedTable");
                    table->forceActiveFocus(Qt::TabFocusReason);
                    if (!waitFor([&] { return table->hasActiveFocus(); })) return false;
                    if (scenario.endsWith("_menu") || scenario.endsWith("_f10")) {
                        QTest::keyClick(window, scenario.endsWith("_f10") ? Qt::Key_F10 : Qt::Key_Menu,
                            scenario.endsWith("_f10") ? Qt::ShiftModifier : Qt::NoModifier);
                        if (!waitFor([&] { auto* menu = item(window, "menuCopy"); return menu && menu->isVisible(); })
                            || !click(window, item(window, "menuCopy"))) return false;
                    } else QTest::keySequence(window, QKeySequence::Copy);
                    return waitFor([&] { return QGuiApplication::clipboard()->text() == "default"; })
                        && server.requests.size() == requests;
                }
                QTest::keySequence(window, QKeySequence::Copy);
                if (!waitFor([&] { return QGuiApplication::clipboard()->text() == "default"; }) || !click(window, item(window, header)) || !waitFor([&] { return workspace.sortColumnIndex() == 2; })) {
                    const auto* focused = window->activeFocusItem();
                    const auto* current = item(window, "pinnedCell_0_2");
                    const auto* view = item(window, "resourcePinnedTable");
                    std::fprintf(stderr, "Namespace interaction: clipboard=%s sortedColumn=%d focus=%s/%s cellFocus=%d sameCell=%d currentColumn=%d windowActive=%d\n",
                        qPrintable(QGuiApplication::clipboard()->text()), workspace.sortColumnIndex(), focused ? focused->metaObject()->className() : "none",
                        focused ? qPrintable(focused->objectName()) : "none", current && current->hasActiveFocus(), current == cell,
                        view ? view->property("currentColumn").toInt() : -1, window->isActive()); return false;
                }
            }
            window->resize(760, 640); QTest::qWait(30);
            const auto before = item(window, header)->mapToScene({0, 0});
            auto* scroll = item(window, "resourceTable");
            const auto position = scroll->mapToScene({scroll->width() / 2, scroll->height() / 2});
            QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), QPoint(-240, 0), QPoint(-480, 0), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &wheel);
            return waitFor([&] { return scroll->property("contentX").toDouble() > 0; }) && item(window, header)->mapToScene({0, 0}) == before && server.requests.size() == requests;
        }
        if (column("resource", "kind")["visible"].toBool()) return false;
        if (scenario == "columns_restart") {
            podlord::Workspace restored(profile);
            return waitFor([&] {
                for (const auto& entry : restored.property("resourceColumns").toList()) if (entry.toMap()["id"] == "kind") return !entry.toMap()["visible"].toBool();
                return false;
            });
        }
        if (scenario == "columns_isolation") {
            if (!click(window, item(window, "eventsWorkspaceButton")) || !click(window, item(window, "eventColumnsButton")) || !waitFor([&] { return item(window, "eventColumnVisible_namespace") && item(window, "eventColumnVisible_namespace")->isVisible(); })) return false;
            if (!clickSetting("eventColumnVisible_namespace") || !click(window, item(window, "eventSaveColumns"))) return false;
            return waitFor([&] { return !column("event", "namespace")["visible"].toBool(); }) && column("resource", "namespace")["visible"].toBool() && !column("resource", "kind")["visible"].toBool() && server.requests.size() == requests;
        }
        if (scenario == "columns_reveal" || scenario == "columns_reset") {
            if (!click(window, item(window, "resourceColumnsButton"))) return false;
            if (!click(window, item(window, scenario == "columns_reset" ? "resourceResetColumns" : "resourceColumnVisible_kind")) || !click(window, item(window, "resourceSaveColumns"))) return false;
            return waitFor([&] { return column("resource", "kind")["visible"].toBool(); }) && server.requests.size() == requests;
        }
        return (scenario == "columns_hide" || scenario == "columns_keyboard") && server.requests.size() == requests;
    }
    if (scenario.startsWith("import_notice_")) {
        auto* notice = item(window, "sourceImportNotice");
        if (!notice || workspace.sourceImportNotice().isEmpty() || notice->isVisible()) return false;
        const auto calls = server.requests.size();
        if (scenario == "import_notice_hidden") return true;
        if (!click(window, item(window, "sourcesButton")) || !waitFor([&] { return notice->isVisible(); })) return false;
        if (scenario == "import_notice_reopen") {
            if (!click(window, item(window, "sourcesButton")) || !waitFor([&] { return !notice->isVisible(); })) return false;
            return click(window, item(window, "sourcesButton")) && waitFor([&] { return notice->isVisible(); }) && server.requests.size() == calls;
        }
        if (scenario != "import_notice_errors") return false;
        const auto directory = temporary.filePath("partial-import");
        if (!QDir().mkpath(directory) || !QFile::copy(temporary.filePath("source.config"), directory + "/config")) return false;
        QFile broken(directory + "/broken.config");
        const QByteArray invalid("not kubeconfig: [\n");
        if (!broken.open(QIODevice::WriteOnly) || broken.write(invalid) != invalid.size()) return false;
        broken.close();
        if (!type(window, item(window, "sourcePath"), directory)
            || !click(window, item(window, "importButton")) || !waitFor([&] { return !workspace.busy() && !workspace.sourceImportIssues().isEmpty(); })) return false;
        return click(window, item(window, "sourcesButton")) && waitFor([&] { return notice->isVisible() && item(window, "sourceImportDetailsButton")->isVisible(); })
            && server.requests.size() == calls;
    }
    if (scenario.startsWith("diagnostics_")) {
        const auto calls=server.requests.size();
        if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"settingsDiagnosticsSection"))) return false;
        const auto before=workspace.settingsDiagnostics();
        const auto audit=before["requests"].toList();
        if (audit.size()!=calls) return false;
        for (const auto& value:audit) { const auto row=value.toMap(); if (row["method"]!="GET" || row["status"]!="Success" || row["outcome"]!="HTTP 200" || row["duration"].toString().isEmpty()) return false; }
        const auto bytes=QJsonDocument::fromVariant(before).toJson();
        if (bytes.contains("local-test-token") || bytes.contains("secret-value")) return false;
        if (scenario=="diagnostics_snapshot") {
            if (!workspace.filter("no-matches")) return false;
            auto* table=item(window,"settingsRequestAudit");
            if (!table || !podlord::test::scrollIntoView(window,table)
                || !waitFor([&] { return table->property("count").toInt()==audit.size(); })) return false;
        }
        return click(window,item(window,"refreshSettingsDiagnostics")) && server.requests.size()==calls;
    }
    if (scenario.startsWith("radar_")) {
        if (!scenario.startsWith("radar_water_") && scenario!="radar_navigation_water") {
            if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"settingsGraphicsSection"))
                || !click(window,item(window,"inlineRadarWaterEnabled")) || !waitFor([&] { return !workspace.busy(); })
                || !click(window,item(window,"resourcesWorkspaceButton"))) return false;
        }
        if (scenario.startsWith("radar_pooled_")) {
            const auto defaults = workspace.alerts()->rules();
            for (const auto& entry : defaults) {
                auto disabled = entry.toMap(); disabled["enabled"] = false;
                if (!workspace.alerts()->saveRule(disabled) || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
            }
            if (!workspace.alerts()->duplicateRule(defaults.last().toMap()["id"].toString())
                || !waitFor([&] { return !workspace.alerts()->busy() && workspace.alerts()->rules().size() == 4; })) return false;
            auto rule = workspace.alerts()->rules().last().toMap();
            rule["enabled"] = true; rule["animation"] = scenario == "radar_pooled_animation" ? "pulse" : scenario.mid(QString("radar_pooled_").size()); rule["animationMode"] = "no-match";
            rule["groups"] = QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", "name"}, {"expression", "\"radar-0000\""}}})}.toVariantList();
            if (!workspace.alerts()->saveRule(rule) || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
        }
        if (!click(window, item(window, "radarWorkspaceButton"))) return false;
        auto* radar = item(window, "resourceRadar");
        if (!radar || !waitFor([&] { return radar->isVisible() && radar->width() > 0 && radar->height() > 0 && radar->property("count").toInt() == expectedRows; })) return false;
        if (scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_initial_population" || scenario == "radar_reopen_position" || scenario.startsWith("radar_island_")) {
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            if (!waitFor([&] { return !frames.isEmpty(); })) return false;
            const auto frame = qEnvironmentVariable("PODLORD_RADAR_FRAME");
            if (!frame.isEmpty() && !window->grabWindow().save(frame)) return false;
        }
        const auto calls = server.requests.size();
        if (scenario == "radar_compact_controls") {
            for (const auto* name : {"radarZoomOut", "radarZoom", "resetRadar"}) {
                auto* control = item(window, name);
                if (!control || !control->isVisible()
                    || control->property("implicitContentWidth").toReal() > control->property("availableWidth").toReal() + 0.5) {
                    std::fprintf(stderr, "Radar control is clipped: %s\n", name);
                    return false;
                }
            }
            return server.requests.size() == calls;
        }
        if (scenario.startsWith("radar_water_")) {
            auto* water = item(window,"radarWater");
            if (!water) { std::fprintf(stderr,"Radar water is missing.\n"); return false; }
            std::fprintf(stderr,"Water surface: visible=%d, playing=%d, speed=%d, window=%d, resources=%d\n",water->isVisible(),water->property("playing").toBool(),water->property("speedPercent").toInt(),int(window->visibility()),radar->property("count").toInt());
            const auto capture = [&](QRectF area) {
                const auto frame=window->grabWindow();
                const auto origin=radar->mapToScene(area.topLeft());
                const double ratio=double(frame.width())/window->width();
                return frame.copy(QRect(qRound(origin.x()*ratio),qRound(origin.y()*ratio),qRound(area.width()*ratio),qRound(area.height()*ratio)));
            };
            const auto still = [&] {
                QTest::qWait(500);
                const bool painted = radar->isVisible() && window->visibility()!=QWindow::Minimized;
                const auto before = painted ? capture(QRectF(8,8,100,80)) : QImage{};
                QTest::qWait(1400);
                const bool unchanged = !painted || before==capture(QRectF(8,8,100,80));
                if (!unchanged || water->property("playing").toBool() || server.requests.size()!=calls) std::fprintf(stderr,"Idle Radar: unchanged=%d playing=%d visible=%d calls=%d initial=%d\n",unchanged,water->property("playing").toBool(),water->isVisible(),int(server.requests.size()),int(calls));
                return unchanged && !water->property("playing").toBool() && server.requests.size()==calls;
            };
            if (scenario=="radar_water_rules_disabled") {
                if (!workspace.alerts()->reload() || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
                for (const auto& value:workspace.alerts()->rules()) if (value.toMap()["enabled"].toBool()) return false;
                return workspace.alerts()->rules().size()==3 && server.requests.size()==calls;
            }
            if (scenario=="radar_water_interaction") {
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Right); QTest::qWait(30);
                const auto paused=capture(QRectF(8,8,100,80)); QTest::qWait(120);
                if (paused!=capture(QRectF(8,8,100,80))) return false;
                QTest::qWait(300);
                return paused!=capture(QRectF(8,8,100,80)) && server.requests.size()==calls;
            }
            if (scenario.startsWith("radar_water_color_") || scenario=="radar_water_event_tile") {
                if (!workspace.alerts()->setPreferences(true,true) || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
                if (!waitFor([&] { return workspace.alerts()->effect("/apis/example.test/v1/namespaces/default/widgets/custom-widget").isEmpty(); },1000)) return false;
                if (scenario=="radar_water_event_tile" && (!type(window,item(window,"resourceFilter"),"radar-event") || !waitFor([&] { return radar->property("count").toInt()==1; }))) return false;
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_End);
                item(window,"resourceFilter")->forceActiveFocus();
                QTest::qWait(120);
                const auto name=scenario=="radar_water_event_tile" ? "radar-event" : "custom-widget";
                QQuickItem* target=nullptr;
                for (int index=0;index<expectedRows;++index) if (auto* tile=item(window,"radarTile_"+QString::number(index)); tile && tile->property("resourceName")==name) target=tile;
                if (!target || !displayedWithin(radar,target)) return false;
                const auto center=target->mapToItem(radar,QPointF(target->width()/2,target->height()/2));
                if (scenario=="radar_water_event_tile") {
                    const auto tileWidth=target->width();
                    const QRectF around(center-QPointF(18,18),QSizeF(36,36));
                    const auto before=capture(around);
                    if (!type(window,item(window,"resourceFilter"),"custom-widget") || !waitFor([&] { return radar->property("count").toInt()==1; })) return false;
                    QTest::qWait(100);
                    auto* widget=item(window,"radarTile_0");
                    if (!widget || widget->property("resourceName")!="custom-widget") return false;
                    const QRectF eventArea(center-QPointF(tileWidth/2,tileWidth/2),QSizeF(tileWidth,tileWidth));
                    const QRectF widgetArea(widget->mapToItem(radar,QPointF{}),widget->size());
                    const auto after=capture(around);
                    int changed=0;
                    const double ratio=double(before.width())/36;
                    for (int y=0;y<before.height();++y) for (int x=0;x<before.width();++x) {
                        const auto point=around.topLeft()+QPointF(x/ratio,y/ratio);
                        if (!eventArea.adjusted(-1.5,-1.5,1.5,1.5).contains(point)
                            && !widgetArea.adjusted(-1.5,-1.5,1.5,1.5).contains(point)
                            && before.pixel(x,y)!=after.pixel(x,y)) ++changed;
                    }
                    const auto middle=QPoint(before.width()/2,before.height()/2);
                    if (changed!=0 || before.pixelColor(middle)!=QColor("#1B4357") || before.pixel(middle)==after.pixel(middle)) std::fprintf(stderr,"Event tile: outside changes=%d before=%s after=%s width=%.2f\n",changed,qPrintable(before.pixelColor(middle).name()),qPrintable(after.pixelColor(middle).name()),tileWidth);
                    return changed==0 && before.pixelColor(middle)==QColor("#1B4357") && before.pixel(middle)!=after.pixel(middle) && server.requests.size()==calls;
                }
                const QHash<QString,QColor> colors{{"Node",QColor("#6B7378")},{"Namespace",QColor("#2E5941")},
                    {"ConfigMap",QColor("#4E6A43")},{"Secret",QColor("#4E6A43")},{"PersistentVolumeClaim",QColor("#4E6A43")},
                    {"Deployment",QColor("#665A3F")},{"Pod",QColor("#7D7048")},{"Service",QColor("#7D7048")},
                    {"Ingress",QColor("#286473")},{"Event",QColor("#1B4357")},{"Widget",QColor("#4E6A43")}};
                const auto image=capture(QRectF(center-QPointF(.5,.5),QSizeF(1,1)));
                const auto expected=colors.value(scenario.mid(QString("radar_water_color_").size()));
                if (!image.isNull() && image.pixelColor(0,0)!=expected) std::fprintf(stderr,"Terrain pixel: actual=%s expected=%s fill=%s terrain=%s focused=%d effect=%s\n",qPrintable(image.pixelColor(0,0).name()),qPrintable(expected.name()),qPrintable(target->property("alertColor").value<QColor>().name()),qPrintable(target->property("terrainColor").value<QColor>().name()),target->hasActiveFocus(),QJsonDocument(QJsonObject::fromVariantMap(target->property("alertEffect").toMap())).toJson(QJsonDocument::Compact).constData());
                return !image.isNull() && image.pixelColor(0,0)==expected && server.requests.size()==calls;
            }
            if (scenario=="radar_water_hidden") return click(window,item(window,"toggleSidebar")) && waitFor([&] { return !radar->isVisible(); }) && still() && server.requests.size()==calls;
            if (scenario=="radar_water_minimized") { window->setVisibility(QWindow::Minimized); return still() && server.requests.size()==calls; }
            if (scenario=="radar_water_reduced") return workspace.alerts()->setPreferences(true,true) && waitFor([&] { return !workspace.alerts()->busy(); }) && still() && server.requests.size()==calls;
            if (scenario=="radar_water_disabled" || scenario=="radar_water_zero" || scenario=="radar_water_settings") {
                if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"settingsGraphicsSection"))) return false;
                if (scenario=="radar_water_zero") {
                    auto* speed=item(window,"inlineRadarWaterSpeed"); if (!speed || !podlord::test::scrollIntoView(window,speed)) return false;
                    speed->forceActiveFocus(Qt::TabFocusReason);
                    for (int step=0; step<20 && workspace.property("radarWaterSpeedPercent").toInt()>0; ++step) {
                        QTest::keyClick(window,Qt::Key_Left);
                        if (!waitFor([&] { return !workspace.busy(); })) return false;
                    }
                    if (workspace.property("radarWaterSpeedPercent").toInt()!=0) return false;
                } else if (!click(window,item(window,"inlineRadarWaterEnabled"))) return false;
                if (!waitFor([&] { return !workspace.busy(); }) || !click(window,item(window,"resourcesWorkspaceButton")) || !still()) return false;
                if (scenario!="radar_water_settings") return server.requests.size()==calls;
                if (!workspace.reload() || !waitFor([&] { return !workspace.busy(); })) return false;
                return click(window,item(window,"settingsWorkspaceButton")) && click(window,item(window,"settingsGraphicsSection"))
                    && !item(window,"inlineRadarWaterEnabled")->property("checked").toBool();
            }
            QSignalSpy frames(window,&QQuickWindow::frameSwapped);
            const auto before=capture(QRectF(8,8,100,80));
            QTest::qWait(650);
            if (!waitFor([&] { return frames.size()>=2; },2000)) { std::fprintf(stderr,"Water did not present two frames.\n"); return false; }
            if (before==capture(QRectF(8,8,100,80)) || frames.size()>20 || server.requests.size()!=calls) {
                const auto evidence=qEnvironmentVariable("PODLORD_RADAR_ACTION_EVIDENCE");
                if (!evidence.isEmpty()) window->grabWindow().save(evidence+"/radar-water-failure.png");
                std::fprintf(stderr,"Water pixels/clock/transport failed: equal=%d frames=%lld calls=%d initial=%d\n",before==capture(QRectF(8,8,100,80)),static_cast<long long>(frames.size()),int(server.requests.size()),int(calls)); return false;
            }
            if (scenario=="radar_water_reopen") {
                if (!click(window,item(window,"toggleSidebar")) || !still() || !click(window,item(window,"toggleSidebar"))) return false;
                frames.clear(); QTest::qWait(650);
                return frames.size()>=2 && server.requests.size()==calls;
            }
            return true;
        }
        if (scenario.startsWith("radar_island_")) {
            auto* first = item(window, "radarTile_0");
            if (!displayedWithin(radar, first)) return false;
            const auto initial = first->mapToItem(radar, QPointF(first->width()/2, first->height()/2));
            const auto initialWidth = first->width();
            if (scenario == "radar_island_layout") {
                auto* second = item(window, "radarTile_1");
                return displayedWithin(radar, second)
                    && !QRectF(first->mapToItem(radar,QPointF{}),first->size()).intersects(QRectF(second->mapToItem(radar,QPointF{}),second->size()))
                    && server.requests.size() == calls;
            }
            radar->forceActiveFocus();
            if (scenario == "radar_island_selection") {
                QTest::keyClick(window,Qt::Key_Home);
                if (!type(window,item(window,"resourceFilter"),"bravo") || !waitFor([&] { return radar->property("count").toInt()==1; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Return); QTest::qWait(50);
                if (!workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) return false;
                QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return);
                return waitFor([&] { return workspace.inspectorName()=="bravo" && workspace.canEditYaml(); });
            }
            if (scenario == "radar_island_refresh") {
                if (!click(window,item(window,"refreshButton")) || !waitFor([&] { return !workspace.loading() && server.requests.size()>calls; })) return false;
                return waitFor([&] {
                    auto* tile=item(window,"radarTile_0");
                    return tile && QLineF(initial,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()<1;
                });
            }
            if (scenario == "radar_island_sort") {
                if (!click(window,item(window,"resourcesWorkspaceButton")) || !click(window,item(window,"header_0")) || !click(window,item(window,"header_0")) || !click(window,item(window,"radarWorkspaceButton"))) return false;
                return waitFor([&] { auto* tile=item(window,"radarTile_2"); return tile && tile->property("resourceName").toString()=="alpha" && QLineF(initial,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()<1; }) && server.requests.size()==calls;
            }
            if (scenario == "radar_island_session") {
                const auto session=workspace.currentSession(); QTest::keyClick(window,Qt::Key_Right); QTest::keyClick(window,Qt::Key_Down);
                const auto savedPose=radar->property("viewPose").toMap();
                if (savedPose["x"].toDouble()!=-24 || savedPose["y"].toDouble()!=-24 || savedPose["zoom"].toDouble()!=1) return false;
                if (!click(window,item(window,"sourcesButton"))) return false;
                item(window,"contexts")->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Down);
                if (!click(window,item(window,"openContext")) || !waitFor([&] { return workspace.currentSession()!=session && !workspace.busy() && !workspace.loading(); },10000)) return false;
                if (!click(window,item(window,"radarWorkspaceButton"))) return false;
                const auto view=workspace.radarView();
                if (view["x"].toDouble()!=0 || view["y"].toDouble()!=0 || view["zoom"].toDouble()!=1) return false;
                if (!click(window,item(window,"activateSession_"+session))) return false;
                if (!waitFor([&] { return workspace.currentSession()==session && workspace.radarView()==savedPose && radar->property("viewPose").toMap()==savedPose; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_0);
                return waitFor([&] { auto* tile=item(window,"radarTile_0"); return tile && QLineF(initial,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()<1; });
            }
            if (scenario == "radar_island_pan") {
                QTest::keyClick(window, Qt::Key_Right); QTest::keyClick(window, Qt::Key_Down);
                const auto expected = initial - QPointF(24,24);
                const bool remainsVisible = QRectF(expected - QPointF(initialWidth/2,initialWidth/2), QSizeF(initialWidth,initialWidth)).intersects(QRectF(QPointF{},radar->size()));
                if (!waitFor([&] {
                    auto* tile = item(window,"radarTile_0");
                    const auto pose = radar->property("viewPose").toMap();
                    return pose["x"].toDouble() == -24 && pose["y"].toDouble() == -24
                        && (tile ? QLineF(expected,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()<1 : !remainsVisible);
                })) {
                    const auto pose = radar->property("viewPose").toMap();
                    auto* tile = item(window,"radarTile_0");
                    const auto actual = tile ? tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2)) : QPointF{};
                    std::fprintf(stderr, "Radar key pan: focus=%d; pose=%g,%g; expected=%g,%g; actual=%g,%g; calls=%lld/%lld\n", radar->hasActiveFocus(), pose["x"].toDouble(), pose["y"].toDouble(), initial.x()-24, initial.y()-24, actual.x(), actual.y(), static_cast<long long>(server.requests.size()), static_cast<long long>(calls));
                    return false;
                }
                QTest::keyClick(window, Qt::Key_0);
                if (!waitFor([&] { auto* tile=item(window,"radarTile_0"); return tile && QLineF(initial,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()<1; })) return false;
                auto* search=item(window,"resourceFilter");
                if (!search->isVisible() && !click(window,item(window,"workspaceSearchButton"))) return false;
                search->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Left);
                auto* restored = item(window,"radarTile_0");
                return restored && QLineF(initial,restored->mapToItem(radar,QPointF(restored->width()/2,restored->height()/2))).length()<1 && server.requests.size()==calls;
            }
            if (scenario == "radar_island_filter") {
                auto* second=item(window,"radarTile_1");
                if (!displayedWithin(radar,second)) return false;
                const auto greyPosition=second->mapToItem(radar,QPointF(second->width()/2,second->height()/2));
                QPointF greyPoint;
                if (!type(window,item(window,"resourceFilter"),"alpha") || !waitFor([&] { return radar->property("count").toInt()==1; })) return false;
                auto* tile=item(window,"radarTile_0");
                if (!tile || QLineF(initial,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()>1) return false;
                if (!waitFor([&] { greyPoint=radar->mapToScene(greyPosition); const auto image=window->grabWindow(); const auto scale=double(image.width())/window->width(); return image.pixelColor(qRound(greyPoint.x()*scale),qRound(greyPoint.y()*scale))==QColor("#50575B"); })) return false;
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,greyPoint.toPoint());
                if (!workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) return false;
                return type(window,item(window,"resourceFilter"),"") && waitFor([&] { return radar->property("count").toInt()==expectedRows; }) && server.requests.size()==calls;
            }
            if (scenario == "radar_island_drag") {
                const auto start=radar->mapToScene(initial).toPoint();
                QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
                for (int step=1;step<=6;++step) { QTest::mouseMove(window,start+QPoint(step*10,step*5)); QTest::qWait(10); }
                QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(60,30));
                if (!waitFor([&] { auto* tile=item(window,"radarTile_0"); return tile && tile->mapToItem(radar,QPointF{}).x()>initial.x()+20; }) || !workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) return false;
                return click(window,item(window,"radarTile_0")) && waitFor([&] { return workspace.inspectorName()=="alpha" && workspace.canEditYaml(); });
            }
            const auto at=radar->mapToScene(initial);
            const bool trackpad=scenario=="radar_island_trackpad_zoom";
            const bool zoomOut=scenario=="radar_island_zoom_out";
            const bool horizontal=scenario=="radar_island_scroll_horizontal";
            QWheelEvent wheel(at,window->mapToGlobal(at.toPoint()),trackpad ? QPoint(18,120) : horizontal ? QPoint(18,0) : QPoint{},trackpad || horizontal ? QPoint{} : QPoint(0,zoomOut ? -120 : 120),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QCoreApplication::sendEvent(window,&wheel);
            return waitFor([&] {
                auto* tile=item(window,"radarTile_0");
                const bool widthMatches=tile && (horizontal ? qAbs(tile->width()-initialWidth)<.01 : zoomOut ? tile->width()<initialWidth/1.1 : tile->width()>initialWidth*1.1);
                return widthMatches && QLineF(initial,tile->mapToItem(radar,QPointF(tile->width()/2,tile->height()/2))).length()<1;
            }) && server.requests.size()==calls;
        }
        if (scenario.startsWith("radar_pooled_")) {
            const bool moving = scenario != "radar_pooled_none" && scenario != "radar_pooled_outline";
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            if (moving && !waitFor([&] { return frames.size() >= 4; })) return false;
            if (!moving) { QTest::qWait(250); frames.clear(); QTest::qWait(250); if (frames.size() > 1) return false; }
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(radar, item(window, "radarTile_1000")); })) return false;
            QTest::qWait(250); frames.clear(); QTest::qWait(250);
            if (frames.size() > 1) { std::fprintf(stderr, "Offscreen-only alarm caused %lld frames after settling.\n", static_cast<long long>(frames.size())); return false; }
            QTest::keyClick(window, Qt::Key_Home);
            if (!waitFor([&] { return displayedWithin(radar, item(window, "radarTile_0")); })) return false;
            QTest::qWait(100); frames.clear();
            if (moving && !waitFor([&] { return frames.size() >= 4; })) return false;
            if (!moving) { QTest::qWait(250); frames.clear(); QTest::qWait(250); if (frames.size() > 1) return false; }
            return server.requests.size() == calls;
        }
        if (scenario == "radar_initial_population") {
            auto* firstRow = item(window, "radarTile_0");
            return displayedWithin(radar, firstRow) && firstRow->property("resourceName").toString() == "radar-0000"
                && server.requests.size() == calls;
        }
        if (scenario == "radar_reopen_position") {
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(radar, item(window, "radarTile_1000")); })) return false;
            if (!click(window, item(window, "toggleSidebar")) || !waitFor([&] { return !radar->isVisible(); })) return false;
            if (!click(window, item(window, "radarWorkspaceButton"))) return false;
            return waitFor([&] { return displayedWithin(radar, item(window, "radarTile_1000")); }) && server.requests.size() == calls;
        }
        if ((scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_navigation_water") {
            QList<double> samples;
            QList<double> captures;
            radar->forceActiveFocus();
            for (int frame = 0; frame < 55; ++frame) {
                QSignalSpy presented(window, &QQuickWindow::frameSwapped);
                QElapsedTimer timer; timer.start();
                QTest::keyClick(window, frame % 2 ? Qt::Key_Home : Qt::Key_End);
                window->update();
                if (presented.isEmpty() && !presented.wait(1000)) return false;
                const auto elapsed = timer.nsecsElapsed() / 1000000.;
                if (window->grabWindow().isNull()) return false;
                if (frame >= 5) { samples.append(elapsed); captures.append(timer.nsecsElapsed() / 1000000.); }
            }
            std::sort(samples.begin(), samples.end());
            std::sort(captures.begin(), captures.end());
            if (qEnvironmentVariable("PODLORD_RADAR_BENCHMARK") == "1") std::printf("{\"boundary\":\"Radar keyboard navigation to frameSwapped\",\"backend\":\"%s\",\"platform\":\"%s\",\"resources\":%d,\"samples\":%lld,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,\"including_screenshot_p95_ms\":%.6f}\n", window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software ? "software" : window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal ? "metal" : "other", qPrintable(QGuiApplication::platformName()), expectedRows, static_cast<long long>(samples.size()), samples[24], samples[47], samples.last(), captures[47]);
            if (server.requests.size() != calls) return false;
        }
        if (scenario.startsWith("radar_animation_")) {
            auto* alerts = workspace.alerts();
            if (!waitFor([&] { return !alerts->busy(); })) return false;
            for (const auto& value : alerts->rules()) {
                auto rule = value.toMap(); rule["enabled"] = false;
                if (!alerts->saveRule(rule) || !waitFor([&] { return !alerts->busy(); })) return false;
            }
            if (!alerts->setPreferences(true, false) || !waitFor([&] { return !alerts->busy(); })) return false;
            QVariantMap rule{{"id", ""}, {"name", "Rendered radar action"}, {"description", "Local visual action"},
                {"enabled", true}, {"builtIn", false},
                {"groups", QVariantList{QVariantList{QVariantMap{{"field", "name"}, {"expression", "\"alpha\""}}}}},
                {"color", "#0088ff"}, {"colorMode", "no-match"}, {"colorSeconds", 5},
                {"animation", "none"}, {"animationMode", "no-match"}, {"animationSeconds", 5},
                {"zoom", 0}, {"sound", "none"}, {"soundMinimumMatches", 1}};
            if (!alerts->saveRule(rule) || !waitFor([&] { return !alerts->busy(); })) return false;
            auto* tile = item(window, "radarTile_0");
            if (!tile) return false;
            const auto path = workspace.table()->data(workspace.table()->index(0, 0), Qt::UserRole).toString();
            if (!waitFor([&] { return alerts->effect(path).value("color").toString() == "#0088ff"; })) return false;
            const auto renderedTile = [&] {
                const auto frame = window->grabWindow();
                if (frame.isNull()) return QImage{};
                const auto position = tile->mapToScene({0, 0});
                const double ratio = static_cast<double>(frame.width()) / window->width();
                return frame.copy(QRect(qRound(position.x() * ratio), qRound(position.y() * ratio),
                    qRound(tile->width() * ratio), qRound(tile->height() * ratio)));
            };
            QTest::qWait(80);
            const auto baseline = renderedTile();
            if (baseline.isNull()) return false;
            const QString animation = scenario.endsWith("pulse") ? "pulse" : scenario.endsWith("sweep") ? "sweep"
                : scenario.endsWith("outline") ? "outline" : "blink";
            rule = alerts->rules().last().toMap(); rule["animation"] = animation;
            if (!alerts->saveRule(rule) || !waitFor([&] { return !alerts->busy() && alerts->effect(path).value("animation").toString() == animation; })) return false;
            QTest::qWait(80);
            const auto first = renderedTile();
            bool changed = false;
            for (int sample = 0; sample < 8; ++sample) {
                QTest::qWait(80);
                const auto frame = renderedTile();
                if (frame.isNull()) return false;
                changed = changed || frame != first;
            }
            if (animation == "outline") return first != baseline && !changed && server.requests.size() == calls;
            if (!changed) return false;
            if (!scenario.startsWith("radar_animation_reduce_")) return server.requests.size() == calls;
            if (!alerts->setPreferences(true, true) || !waitFor([&] { return !alerts->busy() && alerts->reducedMotion(); })) return false;
            for (int sample = 0; sample < 4; ++sample) {
                QTest::qWait(80);
                const auto frame = renderedTile();
                if (frame != baseline) {
                    const auto evidence = qEnvironmentVariable("PODLORD_RADAR_ACTION_EVIDENCE");
                    if (!evidence.isEmpty()) {
                        baseline.save(evidence + "/" + scenario + "-baseline.png", "PNG");
                        frame.save(evidence + "/" + scenario + "-reduced.png", "PNG");
                    }
                    std::fprintf(stderr, "Reduced-motion frame did not restore the static action; resource=%s; effect=%s\n",
                        qPrintable(path), QJsonDocument(QJsonObject::fromVariantMap(alerts->effect(path))).toJson(QJsonDocument::Compact).constData());
                    return false;
                }
            }
            return server.requests.size() == calls;
        }
        if (scenario == "radar_refresh_layout") {
            auto* navigation = item(window, "radarWorkspaceButton");
            QSignalSpy presented(window,&QQuickWindow::frameSwapped); window->update();
            if (!presented.wait(2000)) return false;
            const auto position = navigation->mapToScene({0, 0});
            if (!click(window, item(window, "refreshButton"))) return false;
            QTest::qWait(40);
            const bool stableDuringSync = navigation->mapToScene({0, 0}) == position;
            if (!waitFor([&] { return !workspace.loading(); })) return false;
            QTest::qWait(40);
            const auto after = navigation->mapToScene({0, 0});
            if (!stableDuringSync || after != position) std::fprintf(stderr,"Radar refresh moved: before=(%.1f,%.1f) after=(%.1f,%.1f) stableDuring=%d\n",position.x(),position.y(),after.x(),after.y(),stableDuringSync);
            return stableDuringSync && after == position;
        }
        if (scenario == "radar_glyph" || scenario == "radar_glyph_reuse") {
            radar->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Home);
            for (int step = 0; step < 3; ++step)
                if (!click(window, item(window, "radarZoom"))) return false;
            auto* glyph = item(window, "radarGlyph_0");
            if (!glyph || !glyph->isVisible() || glyph->property("kind").toString() != "Pod") return false;
            if (scenario == "radar_glyph_reuse") {
                if (!type(window, item(window, "resourceFilter"), "custom-widget")
                    || !waitFor([&] { return radar->property("count").toInt() == 1; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
                if (!waitFor([&] { auto* next = item(window, "radarGlyph_0"); return next && next->isVisible() && next->property("kind").toString() == "Widget"; })) return false;
                if (!type(window, item(window, "resourceFilter"), "alpha")
                    || !waitFor([&] { return radar->property("count").toInt() == 1; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
                if (!waitFor([&] { auto* next = item(window, "radarGlyph_0"); return next && next->isVisible() && next->property("kind").toString() == "Pod"; })) return false;
            }
            return server.requests.size() == calls;
        }
        if (scenario == "radar_theme") {
            for (const auto& theme : workspace.themeNames()) for (const auto& variant : {QString("dark"), QString("light")}) {
                if (!workspace.saveAppearance(theme, variant) || !waitFor([&] { return !workspace.busy(); })) return false;
                auto* tile = item(window, "radarTile_0");
                const auto expected = workspace.table()->data(workspace.table()->index(0, 0), Qt::UserRole + 5).value<QColor>();
                if (!tile || !waitFor([&] { return tile->property("statusColor").value<QColor>() == expected; })) return false;
            }
            return server.requests.size() == calls;
        }
        if (scenario == "radar_keyboard") {
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Return);
            return waitFor([&] { return workspace.inspectorName() == "alpha" && workspace.canEditYaml(); });
        }
        if (scenario == "radar_zoom") {
            auto* zoom = item(window, "radarZoom"); if (!zoom) return false;
            const auto initial = radar->property("cellWidth").toDouble();
            if (!click(window, zoom)) return false;
            return waitFor([&] { return radar->property("cellWidth").toDouble() != initial; }) && server.requests.size() == calls;
        }
        if (scenario == "radar_sort") {
            if (!click(window, item(window, "resourcesWorkspaceButton")) || !click(window, item(window, "header_0")) || !click(window, item(window, "header_0"))
                || !click(window, item(window, "radarWorkspaceButton"))) return false;
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Return);
            return waitFor([&] { return workspace.inspectorName() == "custom-widget"; });
        }
        if (scenario == "radar_session") {
            const auto first = workspace.currentSession();
            if (!type(window, item(window, "resourceFilter"), "bravo")) return false;
            if (!click(window, item(window, "sourcesButton"))) return false;
            item(window, "contexts")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Down);
            if (!click(window, item(window, "openContext")) || !waitFor([&] { return workspace.currentSession() != first && !workspace.busy() && !workspace.loading(); }, 10000)) return false;
            if (!click(window, item(window, "radarWorkspaceButton")) || radar->property("count").toInt() != 3) return false;
            if (!click(window, item(window, "activateSession_" + first))) return false;
            return waitFor([&] { return workspace.currentSession() == first && radar->isVisible() && radar->property("count").toInt() == 1; }) && workspace.filterText() == "bravo";
        }
        if (scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water")) {
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(radar, item(window, "radarTile_1000")); })) return false;
            if (radar->property("count").toInt() != workspace.table()->rowCount()) return false;
        }
        const auto pattern = scenario == "radar_empty" ? "no-such-resource" : scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") ? "radar-0999" : "bravo";
        if (!type(window, item(window, "resourceFilter"), pattern)) return false;
        const int matches = scenario == "radar_empty" ? 0 : 1;
        if (!waitFor([&] { return radar->property("count").toInt() == matches; }) || workspace.table()->rowCount() != matches || server.requests.size() != calls) return false;
        if (scenario == "radar_empty") {
            if (!waitFor([&] { return item(window, "emptyRadar")->isVisible() && item(window, "resetResourceFilters")->isEnabled(); }) || !click(window, item(window, "resetResourceFilters"))) return false;
            return waitFor([&] { return radar->property("count").toInt() == expectedRows; }) && server.requests.size() == calls;
        }
        if (scenario == "radar_hidden") {
            if (!click(window, item(window, "toggleSidebar"))) return false;
            QTest::qWait(200); return !radar->isVisible() && server.requests.size() == calls;
        }
        return text(window, "resourceMatchCount").contains("visible: 1/" + QString::number(expectedRows));
    }
    if (scenario.startsWith("inspector_related_")) {
        if (scenario.startsWith("inspector_related_alias_")) {
            const int expectedEvents = scenario == "inspector_related_alias_distinct" || scenario == "inspector_related_alias_reuse" ? 2 : 1;
            if (workspace.totalEventCount() != expectedEvents || workspace.totalResourceCount() != expectedRows) return false;
            if (!click(window, item(window, "radarWorkspaceButton")) || !waitFor([&] { return item(window, "resourceRadar")->property("count").toInt() == expectedRows; })) return false;
            if (!click(window, item(window, "eventsWorkspaceButton")) || !waitFor([&] { return item(window, "eventTable")->property("rows").toInt() == expectedEvents; })) return false;
            if (!type(window, item(window, "eventFilter"), "\"Modern event message\"") || !waitFor([&] { return workspace.eventTable()->rowCount() == 1; })) return false;
            if (!waitFor([&] { return text(window, "eventCell_0_5") == "7"; })) {
                std::fprintf(stderr, "Modern Event count was not presented: %s; model=%s; filter=%s\n", qPrintable(text(window, "eventCell_0_5")), qPrintable(workspace.eventTable()->data(workspace.eventTable()->index(0, 5), Qt::DisplayRole).toString()), qPrintable(text(window, "eventFilter")));
                return false;
            }
            if (scenario == "inspector_related_alias_reuse") {
                const auto requests = server.requests.size();
                for (int pass = 0; pass < 10; ++pass) {
                    if (!type(window, item(window, "eventFilter"), "") || !waitFor([&] { return item(window, "eventTable")->property("rows").toInt() == 2; })) return false;
                    if (!type(window, item(window, "eventFilter"), "\"Modern event message\"")
                        || !waitFor([&] { return item(window, "eventTable")->property("rows").toInt() == 1 && text(window, "eventCell_0_5") == "7"; })) return false;
                }
                if (server.requests.size() != requests) return false;
            }
            if (scenario == "inspector_related_alias_legacy_time" && text(window, "eventCell_0_0") != "2026-10-03T10:03:00Z") return false;
            if (!click(window, item(window, "eventCell_0_2")) || !waitFor([&] { return workspace.inspectorName() == "alpha"; })) return false;
            return waitFor([&] { return workspace.inspectorEvents().size() == expectedEvents; });
        }
        if (scenario == "inspector_related_workspace" || scenario.startsWith("inspector_related_event_")) {
            const auto calls = server.requests.size();
            if (!click(window, item(window, "eventsWorkspaceButton")) || !waitFor([&] { return item(window, "eventTable")->isVisible() && item(window, "eventTable")->property("rows").toInt() == 2; })) return false;
            if (scenario == "inspector_related_event_sort") {
                if (!waitFor([&] { return item(window, "eventHeader_5") && item(window, "eventHeader_5")->width() > 0; })) return false;
                if (!click(window, item(window, "eventHeader_5")) || !waitFor([&] { return text(window, "eventCell_0_5") == "3"; })) return false;
                if (!click(window, item(window, "eventHeader_5")) || !waitFor([&] { return text(window, "eventCell_0_5") == "12"; })) return false;
                return click(window, item(window, "eventHeader_5")) && waitFor([&] { return text(window, "eventCell_0_5") == "3" && workspace.eventSortColumnIndex() == -1; }) && server.requests.size() == calls;
            }
            if (!type(window, item(window, "eventFilter"), "Scheduled") || !waitFor([&] { return workspace.eventTable()->rowCount() == 1 && text(window, "eventCell_0_2") == "Scheduled"; })) return false;
            if (server.requests.size() != calls) return false;
            if (scenario == "inspector_related_event_copy") {
                if (window->grabWindow().isNull()) return false;
                item(window, "eventCell_0_2")->forceActiveFocus(); QTest::keySequence(window, QKeySequence::Copy);
                const bool copied = QGuiApplication::clipboard()->text() == "Scheduled" && workspace.inspectorPath().isEmpty() && server.requests.size() == calls;
                if (!copied) std::fprintf(stderr, "Event copy: focus=%d; text=%s; clipboard=%s; inspector=%s; calls=%lld/%lld\n", item(window,"eventCell_0_2")->hasActiveFocus(), qPrintable(text(window,"eventCell_0_2")), qPrintable(QGuiApplication::clipboard()->text()), qPrintable(workspace.inspectorPath()), static_cast<long long>(server.requests.size()), static_cast<long long>(calls));
                return copied;
            }
            if (!click(window, item(window, "eventCell_0_2"))) return false;
            return waitFor([&] { return workspace.inspectorName() == (scenario == "inspector_related_event_missing" ? "related-event" : "alpha"); });
        }
        if (!type(window, item(window, "resourceFilter"), "alpha") || !waitFor([&] { return workspace.table()->rowCount() == 2; }) || !click(window, item(window, "cell_0_0"))) return false;
        if (!waitFor([&] { return workspace.canEditYaml(); })) {
            std::fprintf(stderr, "Related inspector not fresh: path=%s; summary=%s; detail=%s; requests=%s; cell-path=%s\n", qPrintable(workspace.inspectorPath()), qPrintable(workspace.inspected()), qPrintable(workspace.inspectorStatus()), qPrintable(server.requests.join('\n')), qPrintable(item(window, "cell_0_0")->property("resourcePath").toString())); return false;
        }
        const auto calls = server.requests.size();
        if (scenario.startsWith("inspector_related_table_")) {
            const bool event = scenario.contains("_events_");
            const auto prefix = event ? QString("inspectorEvent") : QString("inspectorLink");
            const auto name = [&](const QString& suffix) { return (prefix + suffix).toUtf8(); };
            if (!click(window, item(window, event ? "inspectorEventsButton" : "inspectorLinksButton"))) return false;
            auto* model = workspace.property(event ? "inspectorEventTable" : "inspectorLinkTable").value<QAbstractItemModel*>();
            if (!model || model->rowCount() != 2 || model->columnCount() != 5) return false;
            const auto operation = scenario.section('_', -1);
            const int column = event ? 3 : 0;
            const auto header = name("Header_" + QString::number(column));
            const auto cell = name("Cell_0_" + QString::number(column));
            if (!waitFor([&] { return item(window, cell.constData()); })) return false;
            if (operation == "sort") {
                if (!click(window, item(window, header.constData())) || model->data(model->index(0,column)).toString() != (event ? "3" : "alpha")) return false;
                if (!click(window, item(window, header.constData())) || model->data(model->index(0,column)).toString() != (event ? "12" : "custom-widget")) return false;
                return click(window, item(window, header.constData()))
                    && workspace.property(event ? "inspectorEventSortColumn" : "inspectorLinkSortColumn").toInt() == -1 && server.requests.size() == calls;
            }
            if (operation == "sortrestore") {
                bool reloaded = false;
                if (!click(window, item(window, header.constData())) || !waitFor([&] { return reloaded || (reloaded = workspace.reloadSavedViews()); })
                    || !waitFor([&] { return !workspace.busy(); })) return false;
                podlord::Workspace restored(profile);
                const bool passed = waitFor([&] { return !restored.busy() && restored.currentSession() == workspace.currentSession(); })
                    && restored.property(event ? "inspectorEventSortColumn" : "inspectorLinkSortColumn").toInt() == column;
                if (!passed) std::fprintf(stderr, "Inspector sort restart: actual=%d expected=%d session=%s/%s error=%s failed=%d original=%d\n",
                    restored.property(event ? "inspectorEventSortColumn" : "inspectorLinkSortColumn").toInt(), column,
                    qPrintable(restored.currentSession()), qPrintable(workspace.currentSession()), qPrintable(restored.error()), restored.viewStateFailed(),
                    workspace.property(event ? "inspectorEventSortColumn" : "inspectorLinkSortColumn").toInt());
                return passed;
            }
            if (operation == "copy") {
                item(window, cell.constData())->forceActiveFocus(); QTest::keySequence(window, QKeySequence::Copy);
                return QGuiApplication::clipboard()->text() == (event ? "3" : "alpha") && server.requests.size() == calls;
            }
            if (operation == "context" || operation == "keyboard") {
                const auto endpoint = name(event ? "Cell_0_2" : operation == "context" ? "Cell_1_2" : "Cell_1_0");
                auto* target = item(window, endpoint.constData());
                if (!target || !podlord::test::scrollIntoView(window, target)) return false;
                if (operation == "keyboard") { target->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return); }
                else {
                    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, target->mapToScene({target->width()/2, target->height()/2}).toPoint());
                    if (!waitFor([&] { return item(window, name("MenuInspector").constData()) && item(window, name("MenuInspector").constData())->isVisible(); })
                        || !click(window, item(window, name("MenuInspector").constData()))) return false;
                }
                return waitFor([&] { return workspace.inspectorName() == (!event && operation == "keyboard" ? "custom-widget" : "alpha"); });
            }
            if (operation == "invalid") {
                QGuiApplication::clipboard()->setText("retained");
                return !workspace.copyInspectorCell(prefix, "missing", 0) && !workspace.copyInspectorCell("unknown", "missing", 0)
                    && !workspace.copyInspectorCell(prefix, model->index(0,0).data(Qt::UserRole).toString(), -1)
                    && !workspace.copyInspectorCell(prefix, model->index(0,0).data(Qt::UserRole).toString(), 5)
                    && !workspace.sortInspectorColumn(prefix, -1) && !workspace.sortInspectorColumn(prefix, 5)
                    && QGuiApplication::clipboard()->text() == "retained" && server.requests.size() == calls;
            }
            if (operation == "find") {
                if (!click(window, item(window, name("FindButton").constData())) || !type(window, item(window, name("FindInput").constData()), event ? "message" : "default")) return false;
                if (!waitFor([&] { return text(window, name("FindCount").constData()) == "1/2"; }) || !click(window, item(window, name("FindNext").constData()))) return false;
                return text(window, name("FindCount").constData()) == "2/2" && model->rowCount() == 2 && server.requests.size() == calls;
            }
            if (operation == "narrow") window->setWidth(360);
            if (!click(window, item(window, name("ColumnsButton").constData()))) return false;
            const auto columnId = event ? QString("message") : QString("status");
            if (operation == "narrow") return waitFor([&] { return item(window, name("ColumnVisible_" + columnId).constData())->isVisible(); }) && server.requests.size() == calls;
            if (operation == "hide" && !click(window, item(window, name("ColumnVisible_" + columnId).constData()))) return false;
            if (operation == "pin" && !click(window, item(window, name("ColumnPinned_" + columnId).constData()))) return false;
            if (operation == "width" || operation == "restore") {
                if (!type(window, item(window, name("ColumnWidth_" + columnId).constData()), "222")) return false;
            }
            if (!click(window, item(window, name("SaveColumns").constData())) || !waitFor([&] { return !workspace.tableLayoutSaving(); }) || !workspace.tableLayoutError().isEmpty()) return false;
            auto columns = workspace.property(event ? "inspectorEventColumns" : "inspectorLinkColumns").toList();
            const auto saved = columns.last().toMap();
            if (operation == "hide") return !saved["visible"].toBool() && server.requests.size() == calls;
            if (operation == "pin") return saved["pinned"].toBool() && server.requests.size() == calls;
            if (operation == "restore") {
                if (server.requests.size() != calls) return false;
                podlord::Workspace restored(profile);
                if (!waitFor([&] { return !restored.busy(); })) return false;
                return restored.property(event ? "inspectorEventColumns" : "inspectorLinkColumns").toList().last().toMap()["width"].toInt() == 222;
            }
            return saved["width"].toInt() == 222 && server.requests.size() == calls;
        }
        if (scenario == "inspector_related_overview") {
            return text(window, "overview_name") == "alpha" && text(window, "overview_kind") == "Pod" && text(window, "overview_namespace") == "default"
                && text(window, "overview_uid") == "uid-alpha" && server.requests.size() == calls;
        }
        if (scenario == "inspector_related_events" || scenario == "inspector_related_wrong_uid") {
            if (!click(window, item(window, "inspectorEventsButton"))) return false;
            if (scenario == "inspector_related_wrong_uid") return item(window, "emptyInspectorEvents")->isVisible() && server.requests.size() == calls;
            if (!waitFor([&] { return text(window, "inspectorEventCell_0_2") == "Scheduled"; })) return false;
            auto* events = item(window, "inspectorEventTable");
            QQmlExpression reveal(qmlContext(events), events, "positionViewAtIndex(model.index(0, 4), TableView.Contain)");
            reveal.evaluate();
            return !reveal.hasError() && waitFor([&] { return text(window, "inspectorEventCell_0_4") == "Local event message"; }) && server.requests.size() == calls;
        }
        if (!click(window, item(window, "inspectorLinksButton")) || !waitFor([&] { return item(window, "inspectorLinkCell_0_2") && item(window, "inspectorLinkCell_1_2"); })) return false;
        if (server.requests.size() != calls) return false;
        return click(window, item(window, "inspectorLinkCell_0_2")) && waitFor([&] { return workspace.inspectorName() == "bravo"; });
    }
    if (scenario == "forbidden") return text(window, "syncProblemMessage").contains("Authorization denied");
    if (scenario == "relative_token") return server.authorization == "Bearer local-test-token";
    if (scenario == "basic") return server.authorization == "Basic " + QByteArray("local-user:local-password").toBase64();
    if (scenario == "markup") {
        if (!waitFor([&] { return item(window, "cell_0_3") && item(window, "cell_0_3")->width() > 0; })) return false;
        auto* cell = item(window, "cell_0_3");
        const auto before = server.requests.size();
        QTest::mouseMove(window, cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2)).toPoint());
        QTest::qWait(100); cell->forceActiveFocus(); QTest::qWait(100);
        return cell->property("text").toString().contains("<img") && server.requests.size() == before && !server.requests.contains("/presentation-leak");
    }
    if (scenario == "shutdown_pending") {
        if (!click(window, item(window, "closeSession")) || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
        const auto catalog = podlord::KubeconfigStore(profile).list();
        if (!std::holds_alternative<podlord::SourceCatalog>(catalog)) return false;
        const auto connection = podlord::KubeconfigStore(profile).connection(std::get<podlord::SourceCatalog>(catalog).sources.first().contexts.first().id);
        if (!std::holds_alternative<podlord::ClusterConnection>(connection)) return false;
        server.delayedReadDelivered = false; server.prematureDisconnect = false;
        const auto before = server.requests.size();
        auto client = std::make_unique<podlord::ResourceClient>();
        client->open("shutdown", std::get<podlord::ClusterConnection>(connection), {});
        if (!waitFor([&] { return server.requests.size() > before; })) return false;
        client.reset();
        return waitFor([&] { return server.prematureDisconnect; }) && server.requests.mid(before) == QStringList{"/api"};
    }
    if (scenario == "limit") {
        for (int i = 1; i < server.starts.size(); ++i) if (server.starts[i] - server.starts[i - 1] < 490) return false;
        return server.starts.size() >= 7;
    }
    if (scenario == "cache_expiry") {
        reference = reference.addSecs(86401);
        if (!click(window, item(window, "refreshButton"))) return false;
        return waitFor([&] { return workspace.table()->rowCount() == 0 && text(window, "syncProblemMessage").contains("HTTP 500"); });
    }
    if (scenario == "sync" || scenario == "periodic_sync" || scenario == "inactive_sync") {
        reference = reference.addSecs(26);
        if (!click(window, item(window, "closeSession")) || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
        // A public central-owner tick with the external reference clock, not a private cache edit.
        podlord::ResourceClient client(nullptr, [&] { return reference; });
        const auto sources = podlord::KubeconfigStore(profile).list();
        if (!std::holds_alternative<podlord::SourceCatalog>(sources)) return false;
        const auto contexts = std::get<podlord::SourceCatalog>(sources).sources.first().contexts;
        const auto visibleContext = std::find_if(contexts.cbegin(), contexts.cend(), [](const auto& context) { return context.name == "local"; });
        if (visibleContext == contexts.cend()) return false;
        const auto connection = podlord::KubeconfigStore(profile).connection(visibleContext->id);
        if (!std::holds_alternative<podlord::ClusterConnection>(connection)) return false;
        client.open("sync", std::get<podlord::ClusterConnection>(connection), {});
        if (scenario == "inactive_sync") {
            client.configure({0, 1}); client.showSession("inactive");
            const auto inactiveContext = std::find_if(contexts.cbegin(), contexts.cend(), [](const auto& context) { return context.name == "inactive"; });
            if (inactiveContext == contexts.cend()) return false;
            const auto other = podlord::KubeconfigStore(profile).connection(inactiveContext->id);
            if (!std::holds_alternative<podlord::ClusterConnection>(other)) return false;
            client.open("inactive", std::get<podlord::ClusterConnection>(other), {});
        }
        if (!waitFor([&] { return !client.loading("sync") && client.rows("sync").size() == 3; }, 10000)) return false;
        if (scenario == "inactive_sync") {
            if (!waitFor([&] { return !client.loading("inactive") && client.rows("inactive").size() == 3; }, 10000)) return false;
            client.showSession("sync");
            const auto inactiveCalls = server.authorizations.count("Bearer inactive-token");
            reference = reference.addSecs(26); client.synchronize();
            if (!waitFor([&] { return !client.loading("sync"); }) || server.authorizations.count("Bearer inactive-token") != inactiveCalls) return false;
            reference = reference.addSecs(35); client.synchronize();
            return waitFor([&] { return !client.loading("sync") && !client.loading("inactive"); })
                && server.authorizations.count("Bearer inactive-token") > inactiveCalls;
        }
        reference = reference.addSecs(26);
        const auto roots = server.requests.count("/api");
        if (scenario == "sync") client.synchronize();
        return waitFor([&] { return server.refresh >= 6 && !client.loading("sync"); }, scenario == "periodic_sync" ? 26000 : 7000)
            && server.requests.count("/api") == roots && client.rows("sync").size() == 3;
    }
    if (scenario == "sort") {
        if (!click(window, item(window, "header_0")) || !waitFor([&] { return workspace.sortDirection()=="ASC" && text(window, "cell_0_0") == "alpha"; })) return false;
        if (!click(window, item(window, "header_0")) || !waitFor([&] { return workspace.sortDirection()=="DESC" && text(window, "cell_0_0") == "custom-widget"; })) return false;
        return click(window, item(window, "header_0")) && waitFor([&] { return workspace.sortColumnIndex()==-1 && text(window, "header_0") == "Name" && text(window, "cell_0_0") == "alpha"; });
    }
    if (scenario == "filter") {
        const auto calls = server.requests.size();
        if (!type(window, item(window, "resourceFilter"), "bravo")) return false;
        return waitFor([&] { return item(window, "resourceTable")->property("rows").toInt() == 1; }) && server.requests.size() == calls;
    }
    if (scenario == "retain") {
        if (!click(window, item(window, "refreshButton"))) return false;
        return waitFor([&] { return text(window, "syncProblemMessage").contains("HTTP 500"); }) && item(window, "resourceTable")->property("rows").toInt() == 3;
    }
    if (scenario.startsWith("inspector_")) {
        QString otherSession;
        if (scenario.startsWith("inspector_edit_session_")) {
            const auto first = workspace.currentSession();
            if (!click(window, item(window, "sourcesButton"))) return false;
            item(window, "contexts")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End); QTest::keyClick(window, Qt::Key_Return);
            if (!click(window, item(window, "openContext")) || !waitFor([&] { return workspace.tabs().size() == 2 && workspace.currentSession() != first && !workspace.loading() && !workspace.busy(); }, 10000)) return false;
            otherSession = workspace.currentSession();
            if (!click(window, item(window, "activateSession_" + first)) || !waitFor([&] { return workspace.currentSession() == first && !workspace.busy() && !workspace.loading(); })) return false;
        }
        const bool secret = scenario.startsWith("inspector_secret_") || scenario == "inspector_edit_secret";
        if (!type(window, item(window, "resourceFilter"), secret ? "Secret" : scenario == "inspector_custom_secret" ? "custom-secret" : "bravo") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); })) return false;
        if (!click(window, item(window, "cell_0_0")) || !click(window, item(window, "yamlButton"))) return false;
        auto* yamlView = item(window, "inspectorYaml");
        if (!yamlView || !yamlView->isVisible() || !yamlView->property("readOnly").toBool() || !text(window, "inspectorReadStatus").contains("Refreshing")) return false;
        const auto expectedName = secret ? "alpha" : scenario == "inspector_custom_secret" ? "custom-secret" : "bravo";
        if (text(window, "inspectorResourceName") != expectedName) return false;
        if (scenario.startsWith("inspector_edit_")) {
            auto* edit = item(window, "editYaml");
            if (!edit || edit->isEnabled()) return false;
            if (scenario == "inspector_edit_late") {
                if (!waitFor([&] { return server.detailReads == 1; }) || !type(window, item(window, "resourceFilter"), "charlie") || !waitFor([&] { return workspace.table()->rowCount() == 1; }) || !click(window, item(window, "cell_0_0"))) return false;
                if (!waitFor([&] { return server.detailReads == 2; }) || edit->isEnabled() || !yamlView->property("readOnly").toBool() || text(window, "inspectorResourceName") != "charlie") return false;
                return waitFor([&] { return edit->isEnabled() && !workspace.loading(); }) && click(window, edit) && !yamlView->property("readOnly").toBool() && text(window, "inspectorYaml").contains("\"name\": \"charlie\"");
            }
        }
        if (scenario == "inspector_close_late") {
            if (!waitFor([&] { return server.detailReads == 1; }) || !click(window, item(window, "closeInspector"))) return false;
            if (!waitFor([&] { return !workspace.loading(); })) return false;
            return !yamlView->isVisible() && text(window, "inspectorYaml").isEmpty();
        }
        if (!waitFor([&] { return !text(window, "inspectorYaml").isEmpty() && !workspace.loading(); })) return false;
        if (text(window, "inspectorReadStatus").contains("Refreshing")) return false;
        const auto oldYaml = text(window, "inspectorYaml");
        if (scenario.startsWith("inspector_yaml_expiry_")) {
            if (!click(window, item(window, "overviewButton"))) return false;
            reference = reference.addSecs(301);
            if (!click(window, item(window, "refreshButton"))
                || !waitFor([&] { return !workspace.loading() && text(window, "inspectorYaml").isEmpty(); })) return false;
            if (!click(window, item(window, "yamlButton"))) return false;
            if (scenario == "inspector_yaml_expiry_repeat")
                for (int repeat = 0; repeat < 3; ++repeat) if (!click(window, item(window, "yamlButton"))) return false;
            if (!waitFor([&] { return server.detailReads == 2 && !workspace.loading(); })) return false;
            if (scenario == "inspector_yaml_expiry_failure" || scenario == "inspector_yaml_expiry_auth") {
                const auto error = scenario.endsWith("auth") ? "Authentication" : "HTTP 500";
                if (!text(window, "inspectorYaml").isEmpty() || item(window, "editYaml")->isEnabled()
                    || !text(window, "inspectorReadStatus").contains(error, Qt::CaseInsensitive)) return false;
                if (scenario.endsWith("auth")) {
                    const auto requests = server.requests.size();
                    if (!click(window, item(window, "overviewButton")) || !click(window, item(window, "yamlButton"))) return false;
                    return workspace.authenticationRequired() && server.requests.size() == requests;
                }
                return true;
            }
            return text(window, "inspectorYaml").contains("ordinary-value-v2") && item(window, "editYaml")->isEnabled()
                && yamlView->property("readOnly").toBool() && server.detailReads == 2;
        }
        if (scenario.startsWith("inspector_edit_")) {
            auto* edit = item(window, "editYaml");
            const auto noWrites = [&] { return std::all_of(server.methods.cbegin(), server.methods.cend(), [](const auto& method) { return method == "GET"; }); };
            if (scenario == "inspector_edit_identity") return !edit->isEnabled() && yamlView->property("readOnly").toBool() && noWrites();
            if (!edit->isEnabled()) return false;
            if (scenario == "inspector_edit_cached" || scenario == "inspector_edit_failure" || scenario == "inspector_edit_malformed" || scenario == "inspector_edit_auth" || scenario == "inspector_edit_same_version") {
                if (scenario == "inspector_edit_cached") {
                    if (!click(window, item(window, "closeInspector")) || !click(window, item(window, "cell_0_0")) || !click(window, item(window, "yamlButton"))) return false;
                } else if (!click(window, item(window, "refreshInspector"))) return false;
                if (edit->isEnabled() || !yamlView->property("readOnly").toBool() || text(window, "inspectorYaml") != oldYaml) return false;
                if (!waitFor([&] { return !workspace.loading(); })) return false;
                if (scenario == "inspector_edit_failure" || scenario == "inspector_edit_malformed" || scenario == "inspector_edit_auth") {
                    const auto before = server.requests.size(); QTest::qWait(100);
                    return !edit->isEnabled() && yamlView->property("readOnly").toBool() && text(window, "inspectorYaml") == oldYaml && server.requests.size() == before && noWrites();
                }
            }
            const auto beforeEdit = server.requests.size();
            if (!click(window, edit) || yamlView->property("readOnly").toBool() || server.requests.size() != beforeEdit) return false;
            if (scenario == "inspector_edit_fresh") return text(window, "inspectorYaml") == oldYaml && noWrites();
            if (scenario == "inspector_edit_clean") return click(window, item(window, "closeInspector")) && workspace.inspectorPath().isEmpty() && !item(window, "discardStay")->isVisible() && noWrites();
            if (scenario == "inspector_edit_clean_tab") {
                if (!click(window, item(window, "closeSession_" + workspace.currentSession())) || !yamlView->property("readOnly").toBool()) return false;
                return waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); }) && !item(window, "discardStay")->isVisible() && noWrites();
            }
            yamlView->forceActiveFocus(); QTest::keySequence(window, QKeySequence::MoveToEndOfDocument); QTest::keyClick(window, Qt::Key_Return);
            for (const auto ch : QString("# operator draft")) QTest::keyClick(window, ch.toLatin1());
            if (!text(window, "inspectorYaml").contains("# operator draft")) return false;
            if (scenario == "inspector_edit_empty") { QTest::keySequence(window, QKeySequence::SelectAll); QTest::keyClick(window, Qt::Key_Backspace); }
            const auto draft = text(window, "inspectorYaml");
            if (scenario == "inspector_edit_expiry_reopen") {
                reference = reference.addSecs(301);
                if (!click(window, item(window, "refreshButton"))
                    || !waitFor([&] { return !workspace.loading() && text(window, "yamlDraftStatus").contains("unavailable"); })) return false;
                const auto requests = server.requests.size();
                if (!click(window, item(window, "overviewButton")) || !click(window, item(window, "yamlButton"))) return false;
                return text(window, "inspectorYaml") == draft && !yamlView->property("readOnly").toBool()
                    && server.requests.size() == requests && noWrites();
            }
            if (scenario == "inspector_edit_secret") return draft.contains("[hidden]") && !draft.contains("YWxwaGEtcHJpdmF0ZS12YWx1ZQ==") && !draft.contains("alpha-private-value") && noWrites();
            if (scenario == "inspector_edit_cached") return draft.contains("ordinary-value-v2") && noWrites();
            if (scenario == "inspector_edit_revert") {
                QGuiApplication::clipboard()->setText(oldYaml); yamlView->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll); QTest::keySequence(window, QKeySequence::Paste);
                return text(window, "inspectorYaml") == oldYaml && click(window, item(window, "closeInspector")) && workspace.inspectorPath().isEmpty() && !item(window, "discardStay")->isVisible() && noWrites();
            }
            if (scenario == "inspector_edit_refresh" || scenario == "inspector_edit_same_version" || scenario == "inspector_edit_expiry") {
                if (scenario == "inspector_edit_expiry") { reference = reference.addSecs(301); if (!click(window, item(window, "refreshButton"))) return false; }
                else if (!click(window, item(window, "refreshInspector"))) return false;
                if (!waitFor([&] { return !workspace.loading(); }) || text(window, "inspectorYaml") != draft || yamlView->property("readOnly").toBool()) return false;
                const auto status = text(window, "yamlDraftStatus");
                return noWrites() && (scenario == "inspector_edit_refresh" ? status.contains("version changed") : scenario == "inspector_edit_same_version" ? !status.contains("version changed") : status.contains("unavailable"));
            }
            const auto originalSession = workspace.currentSession();
            if (scenario.startsWith("inspector_edit_resource_")) {
                if (!type(window, item(window, "resourceFilter"), "charlie") || !waitFor([&] { return workspace.table()->rowCount() == 1; }) || !click(window, item(window, "cell_0_0"))) return false;
            } else if (scenario.startsWith("inspector_edit_window_")) { if (window->close()) return false; }
            else if (scenario.startsWith("inspector_edit_tab_")) { if (!click(window, item(window, "closeSession_" + originalSession))) return false; }
            else if (scenario.startsWith("inspector_edit_session_")) { if (!click(window, item(window, "activateSession_" + otherSession))) return false; }
            else if (scenario.startsWith("inspector_edit_open_context_")) {
                if (!click(window, item(window, "sourcesButton"))) return false;
                item(window, "contexts")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End); QTest::keyClick(window, Qt::Key_Return);
                if (!click(window, item(window, "openContext"))) return false;
            } else if (!click(window, item(window, "closeInspector"))) return false;
            if (!waitFor([&] { return item(window, "discardStay") && item(window, "discardStay")->isVisible(); }) || text(window, "inspectorYaml") != draft || workspace.currentSession() != originalSession || workspace.inspectorPath().isEmpty() || !window->isVisible()) return false;
            const bool accept = scenario.endsWith("_accept") || scenario == "inspector_edit_resource_bound";
            if (scenario == "inspector_edit_resource_bound") workspace.filter("bravo");
            if (scenario == "inspector_edit_close_escape") QTest::keyClick(window, Qt::Key_Escape);
            else if (scenario == "inspector_edit_close_keyboard" || scenario == "inspector_edit_close_keypad" || scenario == "inspector_edit_close_space") {
                if (!waitFor([&] { return item(window, "discardStay")->hasActiveFocus() && item(window, "discardStay")->property("visualFocus").toBool(); })) {
                    std::fprintf(stderr, "Discard keyboard focus: active=%d visual=%d current=%s\n", item(window,"discardStay")->hasActiveFocus(), item(window,"discardStay")->property("visualFocus").toBool(), window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none");
                    return false;
                }
                QTest::keyClick(window, scenario.endsWith("_keypad") ? Qt::Key_Enter : scenario.endsWith("_space") ? Qt::Key_Space : Qt::Key_Return);
            }
            else if (!click(window, item(window, accept ? "discardAccept" : "discardStay"))) return false;
            if (!waitFor([&] { return !item(window, "discardStay")->isVisible(); })) return false;
            if (!accept) {
                const bool retained = text(window, "inspectorYaml") == draft && !yamlView->property("readOnly").toBool() && workspace.currentSession() == originalSession && !workspace.inspectorPath().isEmpty() && window->isVisible() && noWrites();
                if (!retained) std::fprintf(stderr, "Stay: draft=%d readOnly=%d session=%d inspector=%s visible=%d writes=%d\n", text(window,"inspectorYaml")==draft, yamlView->property("readOnly").toBool(), workspace.currentSession()==originalSession, qPrintable(workspace.inspectorPath()), window->isVisible(), !noWrites());
                return retained;
            }
            if (scenario.startsWith("inspector_edit_resource_")) return waitFor([&] { return text(window, "inspectorResourceName") == "charlie" && !workspace.loading(); }) && !text(window, "inspectorYaml").contains("# operator draft") && yamlView->property("readOnly").toBool() && noWrites();
            if (scenario == "inspector_edit_window_accept") return !window->isVisible() && noWrites();
            if (scenario == "inspector_edit_tab_accept") return waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); }) && noWrites();
            if (scenario == "inspector_edit_session_accept" || scenario == "inspector_edit_open_context_accept") return waitFor([&] { return workspace.currentSession() != originalSession && !workspace.busy(); }) && noWrites();
            return workspace.inspectorPath().isEmpty() && text(window, "inspectorYaml").isEmpty() && noWrites();
        }
        if (scenario == "inspector_list_wrong_version") {
            if (!click(window, item(window, "refreshButton")) || !waitFor([&] { return !workspace.loading() && text(window, "syncProblemMessage").contains("Invalid resource list entry"); })) return false;
            return workspace.table()->rowCount() == 1 && text(window, "inspectorYaml") == oldYaml;
        }
        const auto noLeak = [&] {
            const auto presentation = text(window, "inspectorYaml") + workspace.inspected() + text(window, "inspectorReadStatus") + text(window, "syncStatus") + text(window, "syncProblemMessage") + text(window, "errorMessage");
            return !presentation.contains("alpha-private-value") && !presentation.contains("beta-private-value")
                && !presentation.contains("YWxwaGEtcHJpdmF0ZS12YWx1ZQ==") && !presentation.contains("YmV0YS1wcml2YXRlLXZhbHVl") && !presentation.contains("string-private-value");
        };
        if (scenario.contains("_invalid_") || scenario == "inspector_wrong_version") {
            if (!click(window, item(window, "refreshInspector")) || !waitFor([&] { return !workspace.loading() && text(window, "inspectorReadStatus").contains("Invalid detail response"); })) return false;
            return text(window, "inspectorYaml") == oldYaml && noLeak();
        }
        if (scenario == "inspector_custom_secret") return oldYaml.contains("\"enabled\": true") && oldYaml.contains("\"count\": 42") && oldYaml.contains("\"ratio\": 0.125")
            && oldYaml.contains("- ~") && oldYaml.contains("- \"null\"") && !oldYaml.contains("[hidden]") && !item(window, "valuesButton")->isVisible();
        if (scenario == "inspector_yaml") {
            const auto requests = server.requests.size();
            if (!click(window, item(window, "copyYaml"))) return false;
            return QGuiApplication::clipboard()->text() == oldYaml && oldYaml.contains("\"numericText\": \"123\"")
                && oldYaml.contains("\"booleanText\": \"true\"") && oldYaml.contains("\"nullText\": \"null\"")
                && oldYaml.contains("\"immutable\": false") && server.requests.size() == requests;
        }
        if (scenario == "inspector_expiry") {
            reference = reference.addSecs(301);
            if (!click(window, item(window, "refreshButton"))) return false;
            return waitFor([&] { return text(window, "inspectorYaml").isEmpty(); }) && !text(window, "inspectorReadStatus").isEmpty();
        }
        if (scenario == "inspector_markup") {
            const auto requests = server.requests.size();
            yamlView->forceActiveFocus(); QTest::qWait(100);
            return oldYaml.contains("<img") && yamlView->property("textFormat").toInt() == 0 && server.requests.size() == requests && !server.requests.contains("/presentation-leak");
        }
        if (scenario == "inspector_cached" || scenario == "inspector_scroll" || scenario == "inspector_failure" || scenario == "inspector_malformed"
            || scenario == "inspector_forbidden" || scenario == "inspector_auth" || scenario == "inspector_redirect" || scenario == "inspector_backoff") {
            auto* scroll = item(window, "yamlScroll");
            auto* content = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
            if (!content) return false;
            double position = 0;
            if (scenario == "inspector_scroll") {
                QTest::qWait(30);
                const auto local = yamlView->mapToScene(QPointF(40, 100));
                QWheelEvent wheel(local, window->mapToGlobal(local.toPoint()), {}, QPoint(0, -1200), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &wheel);
                if (!waitFor([&] { return content->property("contentY").toDouble() > 0 && !content->property("moving").toBool(); })) {
                    std::fprintf(stderr, "YAML wheel did not settle: y=%.1f content=%.1f viewport=%.1f moving=%d\n", content->property("contentY").toDouble(), content->property("contentHeight").toDouble(), content->height(), content->property("moving").toBool()); return false;
                }
                position = content->property("contentY").toDouble();
            }
            if (!click(window, item(window, "refreshInspector")) || text(window, "inspectorYaml") != oldYaml || !text(window, "inspectorReadStatus").contains("Refreshing")) return false;
            if (!waitFor([&] { return !workspace.loading(); })) return false;
            if (scenario == "inspector_cached") return text(window, "inspectorYaml").contains("ordinary-value-v2");
            if (scenario == "inspector_scroll") {
                const bool retained = text(window, "inspectorYaml").contains("ordinary-value-v2") && waitFor([&] { return qAbs(content->property("contentY").toDouble() - position) < 2; });
                if (!retained) std::fprintf(stderr, "YAML refresh moved reading position: old=%.1f new=%.1f content=%.1f viewport=%.1f\n", position, content->property("contentY").toDouble(), content->property("contentHeight").toDouble(), content->height());
                return retained;
            }
            const auto status = text(window, "inspectorReadStatus");
            const auto failure = scenario == "inspector_failure" ? "HTTP 500" : scenario == "inspector_malformed" ? "Malformed" : scenario == "inspector_forbidden" ? "Authorization denied"
                : scenario == "inspector_auth" ? "Authentication failed" : scenario == "inspector_redirect" ? "Redirect refused" : "Rate limited";
            if (text(window, "inspectorYaml") != oldYaml || !status.contains(failure) || !status.contains("Cached") || !noLeak()) return false;
            const auto requests = server.requests.size(); QTest::qWait(100);
            return server.requests.size() == requests && !server.requests.contains("/credential-leak");
        }
        if (!click(window, item(window, "valuesButton"))) return false;
        if (scenario.startsWith("inspector_secret_table_")) {
            const auto before = server.requests.size();
            const auto action = scenario.mid(QString("inspector_secret_table_").size());
            auto* model = workspace.property("valuesTable").value<QAbstractItemModel*>();
            if (!model || model->columnCount() != 5 || model->rowCount() != 4) return false;
            if (!waitFor([&] { return item(window, "valueHeader_0") && item(window, "value_data/alpha"); })) return false;
            if (const auto frame = qEnvironmentVariable("PODLORD_VALUES_FRAME"); !frame.isEmpty()) window->grabWindow().save(frame);
            if (action == "layout") {
                const QStringList titles{"Key", "Encoding", "Value", "Copy", "Reveal"};
                for (int column = 0; column < titles.size(); ++column)
                    if (model->headerData(column, Qt::Horizontal).toString() != titles[column]) return false;
                return text(window, "value_data/alpha") == "[hidden]" && noLeak() && server.requests.size() == before;
            }
            if (action == "sort") {
                if (!click(window, item(window, "valueHeader_0")) || workspace.property("valuesSortDirection").toString() != "ASC") return false;
                if (!click(window, item(window, "valueHeader_0")) || model->index(0, 0).data().toString() != "empty") return false;
                return click(window, item(window, "valueHeader_0")) && workspace.property("valuesSortColumn").toInt() == -1
                    && model->index(0, 0).data().toString() == "alpha" && server.requests.size() == before;
            }
            if (action == "actions_unsorted") {
                return !workspace.sortInspectorColumn("value", 3) && !workspace.sortInspectorColumn("value", 4)
                    && !workspace.sortInspectorColumn("value", -1) && !workspace.sortInspectorColumn("value", 5)
                    && workspace.property("valuesSortColumn").toInt() == -1 && server.requests.size() == before;
            }
            if (action == "copy_key" || action == "copy_raw" || action == "copy_decoded") {
                const auto representation = action.mid(5);
                const auto menu = representation == "key" ? "copyKey_" : representation == "raw" ? "copyRaw_" : "copyDecoded_";
                if (!click(window, item(window, QString(menu) + "data/alpha"))) return false;
                const auto expected = representation == "key" ? "alpha" : representation == "raw" ? "YWxwaGEtcHJpdmF0ZS12YWx1ZQ==" : "alpha-private-value";
                return QGuiApplication::clipboard()->text() == expected && noLeak() && server.requests.size() == before;
            }
            if (action == "binary_decode") {
                if (!click(window, item(window, "copyDecoded_data/binary"))) return false;
                return text(window, "copyValueError").contains("raw") && noLeak() && server.requests.size() == before;
            }
            if (action == "context_copy") {
                if (!click(window, item(window, "value_data/alpha"), Qt::RightButton)
                    || !click(window, item(window, "valueMenuCopy"))) return false;
                return QGuiApplication::clipboard()->text() == "alpha-private-value" && noLeak() && server.requests.size() == before;
            }
            if (action == "hover") {
                if (!click(window, item(window, "reveal_data/alpha"))) return false;
                const auto full = QString("<b>literal secret</b>\n") + QString(600, 'x');
                if (!waitFor([&] { return text(window, "value_data/alpha") == full.left(256); })) return false;
                item(window, "value_data/alpha")->forceActiveFocus();
                auto* tooltip = window->findChild<QObject*>("valueValueTooltip");
                if (!tooltip || !waitFor([&] { return tooltip->property("visible").toBool() && tooltip->property("text").toString() == full; })) return false;
                QTest::keySequence(window, QKeySequence::Copy);
                return QGuiApplication::clipboard()->text() == full && noLeak() && server.requests.size() == before;
            }
            if (action == "find_masked") {
                if (!click(window, item(window, "valueFindButton")) || !type(window, item(window, "valueFindInput"), "alpha-private-value")) return false;
                if (!waitFor([&] { return text(window, "valueFindCount") == "0/0"; })) return false;
                return click(window, item(window, "reveal_data/alpha"))
                    && waitFor([&] { return text(window, "valueFindCount") == "1/1"; }) && noLeak() && server.requests.size() == before;
            }
            if (action == "sort_restart") {
                if (!click(window, item(window, "valueHeader_0")) || !click(window, item(window, "valueHeader_0"))) return false;
                const auto session = workspace.currentSession();
                window->close();
                if (!waitFor([&] { return !window->isVisible(); })) { std::fputs("Values restart: window did not close.\n", stderr); return false; }
                podlord::Workspace restored(profile);
                if (!waitFor([&] { return !restored.busy() && !restored.contexts().isEmpty(); })) { std::fputs("Values restart: reload did not complete.\n", stderr); return false; }
                if (restored.currentSession().isEmpty() && !restored.openContext(restored.contexts().first().toMap().value("id").toString())) { std::fputs("Values restart: session did not reopen.\n", stderr); return false; }
                if (!waitFor([&] { return !restored.busy() && restored.currentSession() == session; })) { std::fputs("Values restart: session identity changed.\n", stderr); return false; }
                const bool sorted = restored.property("valuesSortColumn").toInt() == 0 && restored.property("valuesSortDirection").toString() == "DESC";
                if (!sorted) std::fprintf(stderr, "Values restart: sort=%d/%s error=%s\n", restored.property("valuesSortColumn").toInt(), qPrintable(restored.property("valuesSortDirection").toString()), qPrintable(restored.error()));
                return sorted;
            }
            if (action == "legacy_presets") {
                if (!waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
                QFile presets(QDir(profile).filePath("filter-presets.json"));
                const auto bytes = QJsonDocument(QJsonArray{QJsonObject{{"name", "Secrets"}, {"search", "Secret"}, {"namespace", "default"}}}).toJson();
                if (!presets.open(QIODevice::WriteOnly) || presets.write(bytes) != bytes.size()) return false;
                presets.close();
                if (!workspace.reloadFilterPresets() || !waitFor([&] { return !workspace.filterPresetsBusy(); })
                    || !workspace.loadFilterPreset("Secrets")) return false;
                if (workspace.table()->rowCount() != 1 || workspace.resourceFieldFilters().value("namespace").toString() != "default") return false;
                if (!workspace.saveFilterPreset("Native copy") || !waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
                return workspace.filterPresets().contains("Secrets") && workspace.filterPresets().contains("Native copy")
                    && workspace.filterPresetsError().isEmpty() && server.requests.size() == before;
            }
            if (action == "import" || action == "import_conflict" || action == "import_url") {
                if (!waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
                if (action == "import_url") return !workspace.importFilterPresets(QUrl("https://example.test/filter-presets.json"))
                    && workspace.filterPresetsError().contains("local") && server.requests.size() == before;
                const auto source = temporary.filePath("saved filters %.json");
                QFile presets(source);
                const auto bytes = QJsonDocument(QJsonArray{QJsonObject{{"name", "Secrets"}, {"search", "Secret"}, {"namespace", "default"}}}).toJson();
                if (!presets.open(QIODevice::WriteOnly) || presets.write(bytes) != bytes.size()) return false;
                presets.close();
                if (action == "import_conflict" && (!workspace.saveFilterPreset("Secrets") || !waitFor([&] { return !workspace.filterPresetsBusy(); }))) return false;
                if (!workspace.importFilterPresets(QUrl::fromLocalFile(source)) || !waitFor([&] { return !workspace.filterPresetsBusy(); })) return false;
                if (!presets.open(QIODevice::ReadOnly) || presets.readAll() != bytes) return false;
                if (action == "import_conflict") return workspace.filterPresetsError().contains("already in use") && server.requests.size() == before;
                return workspace.filterPresetsError().isEmpty() && workspace.loadFilterPreset("Secrets") && workspace.table()->rowCount() == 1
                    && workspace.resourceFieldFilters().value("namespace").toString() == "default" && server.requests.size() == before;
            }
            if (action == "columns" || action == "restart") {
                if (!click(window, item(window, "valueColumnsButton"))
                    || !click(window, item(window, "valueColumnPinned_name"))
                    || !click(window, item(window, "valueColumnVisible_encoding"))
                    || !click(window, item(window, "valueSaveColumns"))
                    || !waitFor([&] { return !workspace.tableLayoutSaving(); })) return false;
                const auto columns = workspace.property("valuesColumns").toList();
                if (!columns[0].toMap().value("pinned").toBool() || columns[1].toMap().value("visible").toBool()) return false;
                if (action == "restart") {
                    podlord::Workspace restored(profile);
                    if (restored.property("valuesColumns").toList() != columns) return false;
                }
                return noLeak() && server.requests.size() == before;
            }
            return false;
        }
        if (scenario == "inspector_values_table_scroll") {
            auto* values = item(window, "resourceValues");
            auto* table = item(window, "valueTable");
            if (!waitFor([&] { return item(window, "valueTable")->property("rows").toInt() == 124
                && item(window, "valueTable")->height() > 0 && item(window, "value_data/booleanText"); })) return false;
            QSignalSpy frame(window, &QQuickWindow::frameSwapped);
            window->update();
            if (!frame.wait(1000)) return false;
            values->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(table, item(window, "value_data/numericText"))
                && text(window, "value_data/numericText") == "123"; })) return false;
            const auto position = values->property("contentY").toDouble();
            if (!click(window, item(window, "refreshInspector")) || !waitFor([&] { return server.detailReads == 2 && workspace.yamlText().contains("ordinary-value-v2"); })) return false;
            window->update();
            if (!frame.wait(1000)) return false;
            const bool retained = waitFor([&] { return std::abs(values->property("contentY").toDouble() - position) < 1
                && displayedWithin(table, item(window, "value_data/numericText")) && text(window, "value_data/numericText") == "123"; });
            if (!retained) std::fprintf(stderr, "Values refresh: position=%g -> %g content=%g root=%g viewport=%g\n", position, values->property("contentY").toDouble(), values->property("contentHeight").toDouble(), values->height(), table->height());
            return retained;
        }
        if (scenario == "inspector_configmap") {
            auto* values = item(window, "resourceValues");
            if (!waitFor([&] { return values->isVisible() && item(window, "valueTable")->height() > 0
                && values->property("contentHeight").toDouble() > values->height() && item(window, "value_data/booleanText"); })) return false;
            values->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            const bool visible = waitFor([&] { return text(window, "value_data/numericText")=="123"; });
            if (!visible) std::fprintf(stderr, "ConfigMap end navigation: y=%.1f content=%.1f viewport=%.1f focus=%s rows=%d\n", values->property("contentY").toDouble(), values->property("contentHeight").toDouble(), values->height(),
                window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none", item(window,"valueTable")->property("rows").toInt());
            return visible;
        }
        if (!noLeak() || !waitFor([&] { return item(window, "reveal_data/alpha"); })) return false;
        QGuiApplication::clipboard()->setText("clipboard-before-reveal");
        if (scenario == "inspector_secret_hidden") return text(window, "value_data/alpha") == "[hidden]" && text(window, "value_data/beta") == "[hidden]";
        const auto requests = server.requests.size();
        if (scenario == "inspector_secret_stringdata") {
            auto* values = item(window, "resourceValues");
            if (!waitFor([&] { return values->isVisible() && values->height() > 0; })) return false;
            values->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return item(window, "reveal_stringData/gamma") && item(window, "reveal_stringData/gamma")->isVisible(); })) return false;
            if (!click(window, item(window, "reveal_stringData/gamma")) || !waitFor([&] { return text(window, "value_stringData/gamma") == "string-private-value"; })) return false;
            return click(window, item(window, "copy_stringData/gamma")) && QGuiApplication::clipboard()->text() == "string-private-value"
                && !text(window, "inspectorYaml").contains("YWxwaGEtcHJpdmF0ZS12YWx1ZQ==") && server.requests.size() == requests;
        }
        if (scenario == "inspector_secret_copy") {
            if (!click(window, item(window, "copy_data/alpha"))) return false;
            return QGuiApplication::clipboard()->text() == "alpha-private-value" && noLeak() && text(window, "value_data/alpha") == "[hidden]" && server.requests.size() == requests;
        }
        if (scenario == "inspector_secret_binary") {
            auto* values=item(window,"resourceValues"); values->forceActiveFocus(); QTest::keyClick(window,Qt::Key_End);
            if (!waitFor([&] { return item(window,"reveal_data/binary"); })) { std::fputs("Binary reveal is not instantiated after End.\n",stderr); return false; }
            if (!click(window, item(window, "reveal_data/binary"))) { std::fputs("Binary reveal is not reachable by pointer and wheel.\n",stderr); return false; }
            if (!waitFor([&] { return text(window, "value_data/binary") == "AAH/"; })) {
                std::fputs("Binary revealed preview did not become visible.\n",stderr);
                const auto capture=qEnvironmentVariable("PODLORD_INSPECTOR_FAILURE_FRAME"); if (!capture.isEmpty()) window->grabWindow().save(capture);
                return false;
            }
            return click(window, item(window, "copy_data/binary")) && QGuiApplication::clipboard()->text() == "AAH/" && noLeak();
        }
        if (!click(window, item(window, "reveal_data/alpha")) || !waitFor([&] { return text(window, "value_data/alpha") == "alpha-private-value"; })) return false;
        if (scenario == "inspector_secret_yaml_copy") {
            if (!click(window, item(window, "yamlButton")) || !click(window, item(window, "copyYaml"))) return false;
            return QGuiApplication::clipboard()->text() == oldYaml && noLeak();
        }
        if (scenario == "inspector_secret_reveal") return text(window, "value_data/beta") == "[hidden]" && QGuiApplication::clipboard()->text() == "clipboard-before-reveal" && server.requests.size() == requests
            && !text(window, "inspectorYaml").contains("YWxwaGEtcHJpdmF0ZS12YWx1ZQ==") && !text(window, "inspectorYaml").contains("YmV0YS1wcml2YXRlLXZhbHVl");
        if (scenario == "inspector_secret_refresh") {
            item(window, "value_data/alpha")->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll);
            if (!click(window, item(window, "refreshInspector")) || !waitFor([&] { return server.detailReads == 2 && !workspace.loading(); })) return false;
            item(window, "value_data/alpha")->forceActiveFocus(); QTest::keySequence(window, QKeySequence::Copy);
            return text(window, "value_data/beta") == "[hidden]" && !text(window, "inspectorYaml").contains("YmV0YS1wcml2YXRlLXZhbHVl")
                && QGuiApplication::clipboard()->text() == "alpha-private-value";
        }
        if (scenario == "inspector_secret_reset") {
            if (!click(window, item(window, "closeInspector")) || !click(window, item(window, "cell_0_0")) || !click(window, item(window, "valuesButton"))) return false;
            return waitFor([&] { return text(window, "value_data/alpha") == "[hidden]"; }) && noLeak();
        }
        if (scenario == "inspector_secret_resource") {
            if (!type(window, item(window, "resourceFilter"), "bravo") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); }) || !click(window, item(window, "cell_0_0"))) return false;
            if (!type(window, item(window, "resourceFilter"), "Secret") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); }) || !click(window, item(window, "cell_0_0"))) return false;
            return waitFor([&] { return text(window, "value_data/alpha") == "[hidden]"; }) && noLeak();
        }
        if (scenario == "inspector_secret_window") {
            window->hide(); QTest::qWait(20); window->show();
            return waitFor([&] { return window->isVisible() && text(window, "value_data/alpha") == "[hidden]"; }) && noLeak();
        }
        if (scenario == "inspector_secret_tab_close") {
            const auto session = workspace.currentSession();
            if (!click(window, item(window, "closeSession")) || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
            if (!click(window, item(window, "openContext"))) return false;
            return waitFor([&] { return workspace.currentSession() == session && !workspace.busy() && text(window, "value_data/alpha") == "[hidden]"; }) && noLeak();
        }
        if (scenario == "inspector_secret_session") {
            const auto first = workspace.currentSession();
            if (!click(window, item(window, "sourcesButton"))) return false;
            item(window, "contexts")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Down);
            if (!click(window, item(window, "openContext")) || !waitFor([&] { return workspace.currentSession() != first && !workspace.busy() && !workspace.loading() && workspace.table()->rowCount() == 3; }, 10000)) return false;
            if (!type(window, item(window, "resourceFilter"), "Secret") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); })) return false;
            if (!click(window, item(window, "cell_0_0")) || !click(window, item(window, "valuesButton")) || !waitFor([&] { return !workspace.loading() && text(window, "value_data/alpha") == "[hidden]"; })) return false;
            if (!click(window, item(window, "reveal_data/alpha")) || !waitFor([&] { return text(window, "value_data/alpha") == "alpha-private-value"; })) return false;
            if (!click(window, item(window, "activateSession_" + first))) return false;
            return waitFor([&] { return workspace.currentSession() == first && text(window, "value_data/alpha") == "[hidden]"; }) && noLeak();
        }
        return false;
    }
    if (!waitFor([&] { return item(window, "cell_0_0") != nullptr; })) {
        auto* table = item(window, "resourceTable");
        std::fprintf(stderr, "Table geometry %g x %g, %d rows; window exposed=%d\n", table->width(), table->height(), table->property("rows").toInt(), window->isExposed());
        return false;
    }
    if (!click(window, item(window, "cell_0_0"))) return false;
    if (scenario == "coalesce") {
        if (!waitFor([&] { return server.requests.join('\n').contains("/pods/alpha"); })) return false;
        if (!click(window, item(window, "cell_0_0"))) return false;
        if (!waitFor([&] { return workspace.inspected().contains("fetchedAt"); })) return false;
        int details = 0; for (const auto& request : server.requests) if (request.contains("/pods/alpha")) ++details;
        return details == 1;
    }
    if (scenario == "detail_expiry") {
        if (!waitFor([&] { return workspace.inspected().contains("fetchedAt"); })) return false;
        reference = reference.addSecs(301);
        if (!click(window, item(window, "refreshButton"))) return false;
        return waitFor([&] { return !workspace.inspected().contains("fetchedAt"); }) && !workspace.inspected().isEmpty();
    }
    if (scenario == "slow_close") {
        if (!waitFor([&] { return server.requests.join('\n').contains("/api/v1/namespaces/default/pods/alpha"); })) return false;
        auto* close = item(window, "closeSession");
        if (!click(window, close) || !waitFor([&] { return workspace.currentSession().isEmpty(); })) return false;
        if (!waitFor([&] { return server.delayedReadDelivered || server.prematureDisconnect; })) return false;
        return server.delayedReadDelivered && !server.prematureDisconnect && workspace.inspected().isEmpty() && workspace.currentSession().isEmpty();
    }
    if (!waitFor([&] { return workspace.inspected().contains("fetchedAt"); })) {
        std::fprintf(stderr, "Inspector %s; requests %s\n", qPrintable(workspace.inspected()), qPrintable(server.requests.join('\n'))); return false;
    }
    if (scenario == "copy") {
        const auto before = workspace.inspected();
        const auto calls = server.requests.size();
        item(window, "cell_0_0")->forceActiveFocus();
        QTest::keySequence(window, QKeySequence::Copy);
        return QGuiApplication::clipboard()->text() == "alpha" && before == workspace.inspected() && calls == server.requests.size();
    }
    if (scenario == "context_menu") {
        if (!click(window, item(window, "cell_1_0"), Qt::RightButton)) return false;
        if (!waitFor([&] { return item(window, "menuInspector")->isVisible(); })) return false;
        return click(window, item(window, "menuInspector")) && waitFor([&] { return workspace.inspected().contains("bravo") && workspace.inspected().contains("fetchedAt"); });
    }
    return !workspace.inspected().contains("secret-value-must-not-appear")
        && server.requests.join('\n').contains("/apis/example.test/v1/widgets")
        && server.requests.join('\n').contains("continue=");
}
bool realCluster(const QString& source) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return false;
    const auto failed = [&](const char* stage) {
        std::fprintf(stderr, "Real inspector stage failed: %s; busy=%d loading=%d rows=%d\n", stage,
            workspace.busy(), workspace.loading(), workspace.table()->rowCount());
        const auto evidence = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
        if (!evidence.isEmpty()) { QTest::qWait(50); window->grabWindow().save(QFileInfo(evidence).absolutePath() + "/native-inspector-failure.png", "PNG"); }
        return false;
    };
    QTest::qWait(10);
    if (!waitFor([&] { return item(window, "importButton")->isEnabled(); })) return failed("import readiness");
    if (!type(window, item(window, "sourcePath"), source) || !click(window, item(window, "importButton"))) return failed("import action");
    if (!waitFor([&] { return item(window, "contexts")->property("count").toInt() == 1 && item(window, "openContext")->isEnabled(); })) return failed("context readiness");
    if (!click(window, item(window, "openContext"))) return failed("open context");
    if (!waitFor([&] { return item(window, "resourceFilter")->isEnabled(); })) return failed("filter readiness");
    if (!type(window, item(window, "resourceFilter"), "podlord-native-e2e")) return failed("filter action");
    if (!waitFor([&] { return item(window, "resourceTable")->property("rows").toInt() == 1 && item(window, "cell_0_0"); }, 60000)) {
        std::fprintf(stderr, "TLS backend: %s; built library: %s; runtime library: %s\n",
            qPrintable(QSslSocket::activeBackend()), qPrintable(QSslSocket::sslLibraryBuildVersionString()),
            qPrintable(QSslSocket::sslLibraryVersionString()));
        std::fprintf(stderr, "Real cluster load failed: %s %s\n", qPrintable(text(window, "errorMessage")), qPrintable(text(window, "syncProblemMessage"))); return false;
    }
    if (!click(window, item(window, "cell_0_0"))) return failed("open ConfigMap row");
    if (!waitFor([&] { return workspace.inspected().contains("fetchedAt"); })
        || !workspace.inspected().contains("ConfigMap") || !workspace.inspected().contains("podlord-native-e2e")) return failed("fresh ConfigMap detail");
    if (!click(window, item(window, "yamlButton")) || !waitFor([&] { return text(window, "inspectorYaml").contains("native-api"); }) || !item(window, "inspectorYaml")->property("readOnly").toBool()) return failed("ConfigMap YAML");
    if (!click(window, item(window, "valuesButton")) || !waitFor([&] { return text(window, "value_data/check") == "native-api"; })) return failed("ConfigMap values");
    const auto draftFailure = [&](const char* stage) {
        auto* edit = item(window, "editYaml");
        std::fprintf(stderr, "Real YAML draft stage failed: %s; busy=%d edit-visible=%d edit-enabled=%d read-only=%d; %s\n", stage, workspace.busy(), edit->isVisible(), edit->isEnabled(), item(window, "inspectorYaml")->property("readOnly").toBool(), qPrintable(text(window, "inspectorReadStatus")));
        const auto evidence = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
        if (!evidence.isEmpty()) { QTest::qWait(50); window->grabWindow().save(QFileInfo(evidence).absolutePath() + "/native-yaml-edit-failure.png", "PNG"); }
        return false;
    };
    if (!click(window, item(window, "yamlButton")) || !click(window, item(window, "editYaml")) || item(window, "inspectorYaml")->property("readOnly").toBool()) return draftFailure("begin editing");
    auto* editor = item(window, "inspectorYaml");
    editor->forceActiveFocus(); QTest::keySequence(window, QKeySequence::MoveToEndOfDocument); QTest::keyClick(window, Qt::Key_Return);
    for (const auto ch : QString("# local unapplied draft")) QTest::keyClick(window, ch.toLatin1());
    const auto draft = text(window, "inspectorYaml");
    if (!draft.contains("# local unapplied draft") || !click(window, item(window, "refreshInspector")) || !waitFor([&] { return !text(window, "inspectorReadStatus").contains("Refreshing"); }, 60000) || text(window, "inspectorYaml") != draft) return draftFailure("draft/refresh");
    if (!click(window, item(window, "leaveYamlEdit")) || !waitFor([&] { return item(window, "discardStay")->isVisible(); })) return draftFailure("discard prompt");
    const auto screenshotPath = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QTest::qWait(50);
        if (!window->grabWindow().save(QFileInfo(screenshotPath).absolutePath() + "/native-yaml-discard.png", "PNG")) return false;
    }
    if (!click(window, item(window, "discardStay")) || text(window, "inspectorYaml") != draft || !click(window, item(window, "leaveYamlEdit")) || !waitFor([&] { return item(window, "discardAccept")->isVisible(); }) || !click(window, item(window, "discardAccept"))) return draftFailure("stay/discard");
    if (!editor->property("readOnly").toBool() || text(window, "inspectorYaml").contains("# local unapplied draft") || !item(window, "editYaml")->isEnabled() || !click(window, item(window, "valuesButton"))) return draftFailure("return to server snapshot");
    if (!type(window, item(window, "resourceFilter"), "podlord-native-secret-e2e") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); }, 60000)) return failed("filter Secret");
    if (!click(window, item(window, "cell_0_0")) || !waitFor([&] { return text(window, "value_data/alpha") == "[hidden]" && text(window, "value_data/beta") == "[hidden]"; })) return failed("Secret values");
    const auto encoded = QString::fromLatin1(QByteArray("podlord-alpha-real-secret").toBase64());
    if (text(window, "inspectorYaml").contains(encoded) || text(window, "inspectorYaml").contains("podlord-alpha-real-secret")) return failed("Secret YAML masking");
    if (!click(window, item(window, "reveal_data/alpha")) || !waitFor([&] { return text(window, "value_data/alpha") == "podlord-alpha-real-secret"; })) return failed("Secret reveal");
    if (text(window, "value_data/beta") != "[hidden]" || !click(window, item(window, "copy_data/alpha")) || QGuiApplication::clipboard()->text() != "podlord-alpha-real-secret") return failed("Secret copy");
    const auto screenshot = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        if (!click(window, item(window, "reveal_data/alpha")) || !waitFor([&] { return text(window, "value_data/alpha") == "[hidden]"; }) || !click(window, item(window, "yamlButton"))) return false;
        QTest::qWait(50);
        if (!window->grabWindow().save(screenshot, "PNG")) return false;
    }
    return true;
}
bool realSearchCluster(const QString& source) {
    QTemporaryDir temporary; if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return !workspace.busy() && item(window,"importButton")->isEnabled(); })) return false;
    if (!workspace.saveReadSettings(1200,0,"5") || !waitFor([&] { return !workspace.busy() && workspace.requestLimit()==1200; })) return false;
    if (!type(window,item(window,"sourcePath"),source) || !click(window,item(window,"importButton"))) return false;
    if (!waitFor([&] { return item(window,"contexts")->property("count").toInt()==1 && item(window,"openContext")->isEnabled(); })) return false;
    if (!click(window,item(window,"openContext")) || !waitFor([&] { return workspace.totalResourceCount()>=1051 && !workspace.loading(); },120000)) {
        std::fprintf(stderr,"Real search load: total=%d; %s; %s\n",workspace.totalResourceCount(),qPrintable(workspace.error()),qPrintable(workspace.status())); return false;
    }
    const QList<QPair<QString,int>> queries{{"~visual-config-",512},{"~visual-secret-",256},
        {"/^visual-config-000[1-3]$/",24},{"\"visual-config-0001\" \"visual-secret-0001\"",16},
        {"/[/",0},{"not-a-resource-in-this-owned-cluster",0}};
    for (const auto& query:queries) {
        if (!type(window,item(window,"resourceFilter"),query.first)
            || !waitFor([&] { return workspace.resourceCount()==query.second; })) {
            std::fprintf(stderr,"Real search %s: count=%d expected=%d; %s\n",qPrintable(query.first),workspace.resourceCount(),query.second,qPrintable(workspace.filterError())); return false;
        }
        if ((query.first=="/[/") != item(window,"filterErrorMessage")->isVisible()) return false;
        if (!click(window,item(window,"radarWorkspaceButton")) || !waitFor([&] { return item(window,"resourceRadar")->property("count").toInt()==query.second; })) return false;
        if (!click(window,item(window,"resourcesWorkspaceButton"))) return false;
    }
    if (!click(window,item(window,"resetResourceFilters")) || !waitFor([&] { return workspace.resourceCount()==workspace.totalResourceCount() && workspace.resourceCount()>=1051; })) return false;
    if (!type(window,item(window,"resourceFilter"),"/^visual-config-000[1-3]$/") || !click(window,item(window,"radarWorkspaceButton"))) return false;
    if (!waitFor([&] { return item(window,"resourceRadar")->property("count").toInt()==24; })) return false;
    const auto screenshot=qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
    if (!screenshot.isEmpty()) { QTest::qWait(50); if (!window->grabWindow().save(screenshot,"PNG")) return false; }
    std::printf("Real Kubernetes search: 512 ConfigMaps, 256 Secrets, regex 24, alternatives 16; radar/table agree; invalid/empty/reset verified.\n");
    return true;
}
bool applicationBoundary(const QString& scenario, const QString& binary) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    QStringList arguments;
    int expected = 2;
    if (scenario == "app.help") { arguments = {"--help"}; expected = 0; }
    else if (scenario == "app.version") { arguments = {"--version"}; expected = 0; }
    else if (scenario == "app.relative") arguments = {"--profile", "relative"};
    else if (scenario == "app.repeated") arguments = {"--profile", temporary.path(), "--profile", temporary.path()};
    else if (scenario == "app.extra") arguments = {"--profile", temporary.path(), "unexpected"};
    else return false;
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", qEnvironmentVariable("PODLORD_TEST_QPA_PLATFORM", "offscreen"));
    process.setProcessEnvironment(environment);
    process.start(binary, arguments);
    if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(); return false; }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2 && argc != 3) return 2;
    const QString scenario = QString::fromLocal8Bit(argv[1]);
    const bool passed = argc == 3 ? (scenario.startsWith("app.") ? applicationBoundary(scenario, QString::fromLocal8Bit(argv[2]))
        : scenario == "real_search" ? realSearchCluster(QString::fromLocal8Bit(argv[2])) : scenario == "real" && realCluster(QString::fromLocal8Bit(argv[2]))) : execute(scenario);
    if (!passed) std::fprintf(stderr, "UI scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
