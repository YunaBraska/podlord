#include "workspace.h"
#include "radar_island.h"
#include "background_http.h"
#include "platform_tls.h"
#include <QWebSocket>
#include <QMimeData>
#include <memory>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QScopeGuard>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <cmath>
#include <QInputMethodEvent>
#include <QInputMethod>
#include "container_terminal.h"
#include "ui_input.h"
#include "kind_glyph.h"
#include <QPainter>
#include <QClipboard>
#include <QAccessible>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QLockFile>
#include <QGuiApplication>
#include <QLocale>
#include <limits>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <QWheelEvent>
#include <QItemSelectionModel>
#include <QAbstractItemModelTester>
#include <cstdio>
#include <ctime>
#include <functional>
#include <algorithm>

namespace {
using podlord::test::radarPoint;
using podlord::test::radarPointVisible;
using podlord::test::radarTarget;
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
bool saveShellFrame(QQuickWindow* window,const QString& scenario,QQuickItem* target = nullptr) {
    const auto output=qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
    if (output.isEmpty()) return true;
    QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
    if (!waitFor([&] { return !frames.isEmpty(); }) || !QDir().mkpath(output)) return false;
    const auto path = output+"/"+scenario+"-"+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png";
    if (!target) return window->grabWindow().save(path);
    const auto grab = target->grabToImage();
    return grab && waitFor([&] { return !grab->image().isNull(); }) && grab->image().save(path);
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
    bool inspectorResourceRecreated = false;
    int valueLists = 0;
    bool delayedReadDelivered = false;
    bool prematureDisconnect = false;
    bool refuseCollections = false;
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
                    if (refuseCollections && (path == "/api/v1/pods" || path == "/api/v1/events" || path == "/apis/example.test/v1/widgets")) status = 500;
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
                                if (scenario == "inspector_auto_secret" && detailReads > 1) {
                                    auto data = document["data"].toObject();
                                    data["beta"] = QString::fromLatin1(QByteArray("beta-renewed-private-value").toBase64());
                                    document["data"] = data;
                                }
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
                                if (scenario == "inspector_failure" || scenario == "inspector_edit_failure" || scenario == "inspector_yaml_expiry_failure" || scenario == "inspector_auto_failure") status = 500;
                                if (scenario == "inspector_forbidden") status = 403;
                                if (scenario == "inspector_auth" || scenario == "inspector_edit_auth" || scenario == "inspector_yaml_expiry_auth" || scenario == "inspector_auto_auth") status = 401;
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
                    if (scenario=="shell_text_system" || scenario=="shell_text_terminal_invalid") {
                        if (path=="/api/v1") {
                            auto endpoints=document["resources"].toArray();
                            endpoints.append(QJsonObject{{"name","pods/exec"},{"kind","Pod"},{"namespaced",true},{"verbs",QJsonArray{"create"}}});
                            document["resources"]=endpoints;
                        }
                        if (path.startsWith("/api/v1/namespaces/default/pods/"))
                            document["spec"]=QJsonObject{{"containers",QJsonArray{QJsonObject{{"name","main"},{"image","example.test/app:1"}}}}};
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
                    if (scenario == "radar_quick_info") {
                        if (path == "/apis") {
                            auto groups=document["groups"].toArray();
                            groups.append(QJsonObject{{"preferredVersion",QJsonObject{{"groupVersion","metrics.k8s.io/v1beta1"}}}});
                            document["groups"]=groups;
                        } else if (path == "/apis/metrics.k8s.io/v1beta1") {
                            status=200; document={{"resources",QJsonArray{QJsonObject{{"name","pods"},{"kind","PodMetrics"},{"namespaced",true},{"verbs",QJsonArray{"get","list"}}}}}};
                        } else if (path == "/apis/metrics.k8s.io/v1beta1/pods") {
                            auto value=pod("alpha","PodMetrics","metrics.k8s.io/v1beta1");
                            value["timestamp"]=measured.toString(Qt::ISODateWithMs); value["window"]="15s";
                            value["containers"]=QJsonArray{QJsonObject{{"name","main"},{"usage",QJsonObject{{"cpu","0"},{"memory","0"}}}}};
                            status=200; document={{"metadata",QJsonObject{}},{"items",QJsonArray{value}}};
                        }
                    }
                    if (scenario == "radar_quick_info" && path == "/api/v1/pods") {
                        auto target = pod("alpha");
                        auto metadata=target["metadata"].toObject();
                        metadata["creationTimestamp"]=measured.addSecs(-60).toString(Qt::ISODateWithMs);
                        target["metadata"]=metadata;
                        target["spec"] = QJsonObject{{"nodeName", "worker-a"}, {"containers", QJsonArray{QJsonObject{
                            {"name", "main"}, {"image", "example.test/app:1"}, {"resources", QJsonObject{
                                {"requests", QJsonObject{{"cpu", "100m"}, {"memory", "32Mi"}, {"ephemeral-storage", "1Gi"}}},
                                {"limits", QJsonObject{{"cpu", "500m"}}}}}}}}};
                        target["status"] = QJsonObject{{"phase", "Running"}, {"containerStatuses", QJsonArray{QJsonObject{
                            {"name", "main"}, {"ready", false}, {"restartCount", 12}, {"state", QJsonObject{
                                {"waiting", QJsonObject{{"reason", "CrashLoopBackOff"}, {"message", "Back-off restarting failed container"}}}}}}}}};
                        document["items"] = QJsonArray{target, pod("bravo")};
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
                    if ((scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_initial_population" || scenario == "radar_reopen_position" || scenario.startsWith("radar_pooled_") || scenario.startsWith("radar_accessibility_") || scenario == "columns_pin_all") && path == "/api/v1/pods") {
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
                            event["involvedObject"] = QJsonObject{{"uid", scenario == "inspector_related_wrong_uid" || scenario == "inspector_related_event_missing" ? "replaced-alpha" : "uid-alpha"}, {"kind", "Pod"}, {"name", "alpha"}, {"namespace", "default"}};
                            event["reason"] = "Scheduled"; event["message"] = "Local event message"; event["type"] = "Normal"; event["count"] = 3;
                            if (scenario.contains("_color_")) event["type"] = scenario.endsWith("normal") ? "Normal" : scenario.endsWith("warning") ? "Warning" : scenario.endsWith("empty") ? "" : "Unrecognized";
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
                                const int desired = scenario == "table_auto_image_literal" ? 1 : index == 0 ? 10 : index == 1 ? 2 : 1;
                                const int ready = index == 0 && (scenario == "table_overview_full" || ((scenario == "table_overview_refresh" || scenario == "table_overview_stable_refresh") && refresh > 1)) ? 10
                                    : index == 0 && scenario == "table_overview_none" ? 0 : index == 0 ? 2 : 1;
                                for (int container = 0; container < desired; ++container) {
                                    const auto containerName = "worker" + QString::number(container);
                                    if (metrics) containers.append(QJsonObject{{"name", containerName}, {"usage", QJsonObject{
                                        {"cpu", container > 0 || scenario == "table_zero" || scenario == "table_auto_zero" ? "0" : index == 0 ? "2" : "500m"},
                                        {"memory", container > 0 || scenario == "table_zero" || scenario == "table_auto_zero" ? "0" : index == 0 ? "2Gi" : "1024Mi"}}}});
                                    else containers.append(QJsonObject{{"name", containerName}, {"image", scenario == "table_auto_image_literal" ? "-" : "registry.example/" + QString(90, 'a') + ":" + QString::number(refresh)},
                                        {"resources", QJsonObject{{"requests", QJsonObject{{"cpu", "10m"}, {"memory", "16Mi"}, {"ephemeral-storage", "1Gi"}}}}}});
                                    states.append(QJsonObject{{"name", containerName}, {"ready", container < ready},
                                        {"restartCount", scenario == "table_auto_zero" ? 0 : container == 0 ? index == 0 ? 10 : 2 : 0}});
                                }
                                if (metrics) { value["timestamp"] = measured.toString(Qt::ISODateWithMs); value["window"] = "15s"; value["containers"] = containers; }
                                else { value["spec"] = QJsonObject{{"containers", containers}, {"nodeName", "local-node"}};
                                    value["status"] = QJsonObject{{"phase", scenario=="table_status_colors" ? index==0 ? "Running" : index==1 ? "Pending" : "Failed" : scenario == "table_overview_pending" && index == 0 ? "Pending" : "Running"}, {"containerStatuses", index == 2 || (scenario == "table_overview_pending" && index == 0) ? QJsonArray{} : states}}; }
                                if ((scenario == "table_auto_refresh" || scenario == "table_auto_focus") && refresh > 1 && !metrics) {
                                    value.remove("spec"); value["status"] = QJsonObject{{"phase", "Running"}};
                                    metadata.remove("ownerReferences"); value["metadata"] = metadata;
                                }
                                if (scenario == "table_auto_namespace" && index == 0) {
                                    metadata["namespace"] = "cluster"; value["metadata"] = metadata;
                                }
                                values.append(value);
                            }
                            document = overviewDetail ? values.first().toObject() : QJsonObject{{"metadata", QJsonObject{}}, {"items", values}};
                        }
                        if (scenario == "table_auto_namespace" && path == "/apis/example.test/v1") {
                            auto endpoints = document["resources"].toArray(); auto endpoint = endpoints.first().toObject();
                            endpoint["namespaced"] = false; endpoints[0] = endpoint; document["resources"] = endpoints;
                        }
                        if (scenario == "table_auto_namespace" && path == "/apis/example.test/v1/widgets") {
                            auto target = pod("custom-widget", "Widget", "example.test/v1");
                            auto metadata = target["metadata"].toObject(); metadata.remove("namespace"); target["metadata"] = metadata;
                            document["items"] = QJsonArray{target};
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
                    if (scenario == "inspector_auto_uid" && inspectorResourceRecreated) {
                        const auto recreate = [](QJsonObject resource) {
                            auto metadata = resource["metadata"].toObject();
                            if (resource["kind"] == "ConfigMap" && metadata["name"] == "bravo") {
                                metadata["uid"] = "uid-bravo-recreated"; resource["metadata"] = metadata;
                            }
                            return resource;
                        };
                        if (document.contains("items")) {
                            QJsonArray items;
                            for (const auto& resource : document["items"].toArray()) items.append(recreate(resource.toObject()));
                            document["items"] = items;
                        } else document = recreate(document);
                    }
                    if (scenario == "table_overview_live_updates" && path.startsWith("/api/v1/namespaces/default/pods/")) {
                        auto metadata = document["metadata"].toObject();
                        metadata["resourceVersion"] = QString::number(requests.size());
                        document["metadata"] = metadata;
                    }
                    if (scenario=="shell_empty_cache" && document.contains("items")) document["items"]=QJsonArray{};
                    if (raw.isEmpty()) raw = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    const auto response = "HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nRetry-After: 1\r\nLocation: http://127.0.0.1:9/credential-leak\r\nContent-Length: " + QByteArray::number(raw.size()) + "\r\n\r\n" + raw;
                    const QPointer<QTcpSocket> live(socket);
                    int delay = scenario.startsWith("inspector_") && path.contains("/namespaces/default/") ? 600 : (scenario == "slow_close" && path.startsWith("/api/v1/namespaces/default/pods/"))
                        || ((scenario == "coalesce" || scenario == "detail_expiry") && path.startsWith("/api/v1/namespaces/default/pods/"))
                        || ((scenario == "hidden_followup" || scenario == "shutdown_pending") && path == "/api") ? 400 : 0;
                    if (scenario=="map_preview_deferred" && path=="/api/v1/pods" && url.query().contains("continue=") && authorization=="Bearer inactive-token") delay=1800;
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
            if (auto* cell = podlord::test::tableCell(view, parts[1].toInt(), parts[2].toInt())) return cell;
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
bool revealTableCell(QQuickWindow* window, const QString& tableName, int row, int column) {
    auto* view = item(window, tableName);
    if (!view || !view->isVisible()) return false;
    QQmlExpression reveal(qmlContext(view), view,
        QString("forceLayout(); positionViewAtCell(cellAtIndex(model.index(%1, %2)), TableView.Contain)").arg(row).arg(column));
    reveal.evaluate();
    if (reveal.hasError()) {
        std::fprintf(stderr, "Table cell reveal failed: %s\n", qPrintable(reveal.error().toString()));
        return false;
    }
    QQmlExpression lookup(qmlContext(view), view,
        QString("itemAtIndex(model.index(%1, %2))").arg(row).arg(column));
    return waitFor([&] {
        auto* cell = lookup.evaluate().value<QQuickItem*>();
        return cell && cell->isVisible() && cell->width() > 0 && cell->height() > 0
            && !podlord::test::visibleBounds(window, cell).isEmpty();
    });
}
bool click(QQuickWindow* window, QQuickItem* target, Qt::MouseButton button = Qt::LeftButton) {
    const auto name = target ? target->objectName() : QString{};
    if (!podlord::test::revealWorkspaceAction(window, name)) return false;
    if (!name.isEmpty()) target = item(window, name);
    if (target && !target->isVisible()) {
        const auto name=target->objectName();
        if (name=="sourcesButton" || name=="renameCurrentSession")
            if (!click(window,item(window,"toggleSidebar"))) return false;
    }
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    const auto drawerSettled = [&] {
        auto* drawer = window->findChild<QObject*>("sidebarDrawer");
        return !drawer || !drawer->property("visible").toBool() || drawer->property("opened").toBool();
    };
    if (!waitFor(drawerSettled, 2000)) return false;
    // Menu actions need activation confirmation, while table cells keep their view-owned lookup.
    if (!name.isEmpty() && target->metaObject()->indexOfSignal("triggered()") >= 0)
        return podlord::test::clickVisible(window,name,button) && waitFor(drawerSettled,2000);
    QList<QQuickItem*> ancestors;
    for (auto* parent = target; parent; parent = parent->parentItem()) ancestors.prepend(parent);
    for (auto* parent : ancestors) parent->ensurePolished();
    QCoreApplication::processEvents();
    QSignalSpy frame(window, &QQuickWindow::frameSwapped);
    window->update();
    if (frame.isEmpty() && !frame.wait(1000)) {
        qWarning() << "Pointer target frame unavailable:" << name << "exposed/active" << window->isExposed() << window->isActive();
        return false;
    }
    if (!name.isEmpty()) target = item(window, name);
    if ((!target || !target->isVisible()) && !name.isEmpty())
        return podlord::test::clickVisible(window,name,button) && waitFor(drawerSettled,2000);
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    if (!podlord::test::scrollIntoView(window, target, true)) return false;
    if (!name.isEmpty() && target->objectName() != name) {
        std::fprintf(stderr, "Pointer target recycled: %s -> %s\n", qPrintable(name), qPrintable(target->objectName()));
        return false;
    }
    const auto bounds = podlord::test::visibleBounds(window, target);
    if (bounds.isEmpty()) return false;
    if (name == "renameCurrentContext" || name.startsWith("renameTab_")) {
        auto* hit = window->contentItem();
        while (auto* child = hit->childAt(hit->mapFromScene(bounds.center()).x(), hit->mapFromScene(bounds.center()).y())) hit = child;
        qInfo() << "Context alias pointer:" << name << "target" << target->mapRectToScene(target->boundingRect())
            << "visible" << bounds << "hit" << hit->objectName() << hit->metaObject()->className();
    }
        QTest::mouseMove(window, bounds.center().toPoint(), 0);
        QCoreApplication::processEvents();
        QTest::mouseClick(window, button, Qt::NoModifier, bounds.center().toPoint());
    QCoreApplication::processEvents();
    return waitFor(drawerSettled, 2000);
}
bool type(QQuickWindow* window, QQuickItem* target, const QString& value) {
    if (target && !target->isVisible() && (target->objectName() == "resourceFilter" || target->objectName() == "eventFilter"))
        if (!click(window, item(window, "workspaceSearchButton")) || !waitFor([&] { return target->isVisible(); })) return false;
    if (!target || !target->isVisible() || !target->isEnabled()) return false;
    target->forceActiveFocus();
    QTest::keySequence(window, QKeySequence::SelectAll);
    if (value.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
    for (const auto c : value) QTest::keyClick(window, c.toLatin1());
    if (target->objectName() == "sourceAliasName" && target->property("text").toString() != value) {
        qWarning() << "Context alias input mismatch:" << target->property("text") << "expected" << value
            << "focus" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{});
        return false;
    }
    return true;
}
bool showEventColumn(QQuickWindow* window, podlord::Workspace& workspace, const QString& column) {
    if (!click(window, item(window, "eventColumnsButton"))) return false;
    auto* dialog = window->findChild<QObject*>("eventColumnsDialog");
    if (!dialog || !waitFor([&] { return dialog->property("opened").toBool(); })) {
        qWarning() << "Event columns dialog did not open:" << column;
        return false;
    }
    auto* visible = item(window, "eventColumnVisible_" + column);
    if (!visible || !visible->isVisible()) return false;
    if (!visible->property("checked").toBool()
        && (!click(window, visible) || !waitFor([&] { return visible->property("checked").toBool(); }))) return false;
    return click(window, item(window, "eventSaveColumns"))
        && waitFor([&] { return !workspace.tableLayoutSaving() && !dialog->property("visible").toBool(); })
        && workspace.tableLayoutError().isEmpty();
}
bool execute(const QString& requestedScenario) {
    bool mapRadar=requestedScenario.startsWith("radar_map_");
    const bool initialFit=requestedScenario=="radar_autofit" || requestedScenario=="radar_map_autofit";
    const QString scenario=initialFit ? QStringLiteral("radar_initial_population") : mapRadar ? "radar_"+requestedScenario.mid(10) : requestedScenario;
    const bool twoContexts = scenario == "session_tabs_controls" || scenario == "context_alias_tab_menu" || scenario == "context_chooser_tabs" || (scenario.startsWith("map_") && scenario!="map_source_enter") || scenario == "shell_map_session" || scenario == "view_restore_scope" || scenario == "quick_open_open_context" || scenario == "table_cluster_switch" || scenario == "radar_session" || scenario == "radar_island_session" || scenario == "inactive_sync" || scenario == "inspector_secret_session" || scenario.startsWith("inspector_edit_session_") || scenario.startsWith("inspector_edit_open_context_") || scenario.startsWith("inspector_edit_map_history_");
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    KubernetesBoundary server(scenario == "shell_metric_zero" ? "radar_quick_info" : scenario == "radar_context_identity" ? "radar_initial_population" : scenario == "shell_search_events" || scenario=="columns_isolation" || scenario=="shell_sidebar" || scenario.startsWith("view_restore_event_") ? "filter_event_message" : scenario == "query_event" ? "find_event" : scenario);
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
    if (scenario == "gke_prompt") credentials = "exec: {command: gke-gcloud-auth-plugin, apiVersion: client.authentication.k8s.io/v1beta1}";
    if (scenario == "basic") credentials = "username: local-user\n    password: local-password";
    if (scenario == "invalid_ca") credentials = "token: local-test-token";
    auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:" + QByteArray::number(server.serverPort())
        + (scenario == "invalid_ca" ? "\n    certificate-authority-data: bm90LWNlcnRpZmljYXRl" : "")
        + (scenario == "table_cluster_switch" ? "\n- name: other\n  cluster:\n    server: http://127.0.0.1:"+QByteArray::number(server.serverPort()) : QByteArray{})
        + "\nusers:\n- name: local\n  user:\n    " + credentials
        + (twoContexts ? "\n- name: inactive\n  user: {token: inactive-token}" : "")
        + "\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n"
        + (scenario == "table_cluster_switch" ? "- name: inactive\n  context: {cluster: other, user: inactive}\n" : twoContexts ? "- name: inactive\n  context: {cluster: local, user: inactive}\n" : "");
    if (scenario.startsWith("map_") && twoContexts) yaml += "- name: staging\n  context: {cluster: local, user: local}\n";
    if (file.write(yaml) != yaml.size()) return false;
    file.close();
    const auto profile = temporary.filePath("profile");
    QDateTime reference = QDateTime::currentDateTimeUtc();
    if (scenario == "settings_invalid") {
        if (!QDir().mkpath(profile)) return false;
        QFile invalid(QDir(profile).filePath("read-settings.json"));
        if (!invalid.open(QIODevice::WriteOnly) || invalid.write("{\"version\":1,\"requestHardLimitPerMinute\":-1,\"inactiveSyncMinutes\":0}") < 0) return false;
    }
    const auto previousFont = QGuiApplication::font();
    const auto restoreFont = qScopeGuard([&] { if (scenario.startsWith("shell_text_")) QGuiApplication::setFont(previousFont); });
    if (scenario.startsWith("shell_text_")) {
        auto font = previousFont; font.setPixelSize(scenario=="shell_text_initial" ? 28 : 14);
        QGuiApplication::setFont(font);
    }
    podlord::Workspace workspace(profile, nullptr, [&] { return reference; });
    if ((scenario == "session_tabs_controls" || scenario.startsWith("radar_") || scenario.startsWith("map_") || scenario=="inspector_malformed")
        && !podlord::test::muteAlerts(workspace.alerts())) return false;
    QList<qint64> dispatched;
    if (scenario == "limit") QObject::connect(&workspace, &podlord::Workspace::requestStarted, &workspace,
        [&](const QString&, const QString&, qint64 at) { dispatched.append(at); });
    if (scenario == "session_tabs_controls" || scenario.startsWith("accessibility_") || scenario.startsWith("radar_accessibility_") || scenario == "shell_touch_search_tooltip" || scenario=="inspector_secret_table_accessibility" || scenario=="inspector_auto_secret" || scenario=="settings_inline_alerts" || scenario=="settings_inline_graphics" || scenario=="settings_inline_diagnostic_audit_copy") QAccessible::setActive(true);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return window->isVisible() && window->isExposed(); })) {
        std::fprintf(stderr,"UI render surface unavailable: platform=%s window=%d visible=%d exposed=%d active=%d\n",
            qPrintable(QGuiApplication::platformName()),bool(window),window && window->isVisible(),window && window->isExposed(),window && window->isActive());
        return false;
    }
    QString renameStage = "source import/open";
    bool renamePassed = false;
    const auto renameDiagnostics = qScopeGuard([&] {
        if (!scenario.startsWith("context_alias_") || renamePassed) return;
        auto* dialog = window->findChild<QObject*>("sourceAliasDialog");
        auto* input = item(window, "sourceAliasName");
        qWarning() << "Context alias stage:" << renameStage << "title" << workspace.title()
            << "busy" << workspace.busy() << "error" << workspace.error()
            << "dialog visible/opened" << (dialog ? dialog->property("visible") : QVariant{}) << (dialog ? dialog->property("opened") : QVariant{})
            << "input visible/enabled" << (input && input->isVisible()) << (input && input->isEnabled())
            << "text" << (input ? input->property("text") : QVariant{})
            << "selection" << (input ? input->property("selectedText") : QVariant{})
            << "focus" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{});
        const auto evidence = qEnvironmentVariable("PODLORD_RENAME_EVIDENCE");
        if (!evidence.isEmpty()) window->grabWindow().save(evidence + '/' + scenario + '-'
            + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + '-' + QString::number(QCoreApplication::applicationPid()) + ".png");
    });
#ifdef Q_OS_IOS
    if (!podlord::test::orientIosTestWindow(window)) return false;
#endif
    QTest::qWait(10);
    if (scenario.startsWith("drop_config_")) {
        QMimeData mime;
        std::unique_ptr<QLockFile> ownedLock;
        if (scenario.startsWith("drop_config_locked_")) {
            if (!QDir().mkpath(profile+"/kubeconfigs")
                || !QFile::setPermissions(profile,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)
                || !QFile::setPermissions(profile+"/kubeconfigs",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)) return false;
            ownedLock=std::make_unique<QLockFile>(profile+"/kubeconfigs/sources.lock");
            if (!ownedLock->tryLock(0)) return false;
        }
        if (scenario == "drop_config_invalid") {
            QFile invalid(source);
            if (!invalid.open(QIODevice::WriteOnly | QIODevice::Truncate) || invalid.write("apiVersion: [\n") < 0) return false;
        }
        QList<QUrl> urls{QUrl::fromLocalFile(source)};
        if (scenario == "drop_config_multiple") {
            const auto second = QFileInfo(source).absolutePath() + "/second.yaml";
            QFile file(second); auto content = yaml; content.replace("local", "second");
            if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) return false;
            file.close(); urls.append(QUrl::fromLocalFile(second));
        }
        if (scenario == "drop_config_repeat") urls.append(urls.first());
        if (scenario == "drop_config_missing") urls={QUrl::fromLocalFile(temporary.filePath("missing.yaml"))};
        if (scenario.startsWith("drop_config_locked_")) {
            const auto invalidPath=temporary.filePath("invalid.yaml");
            QFile invalid(invalidPath);
            if (!invalid.open(QIODevice::WriteOnly) || invalid.write("apiVersion: [\n") < 0) return false;
            invalid.close();
            if (scenario.endsWith("invalid_first")) urls.prepend(QUrl::fromLocalFile(invalidPath));
            else urls.append(QUrl::fromLocalFile(invalidPath));
        }
        mime.setUrls(urls);
        const QPoint position(window->width() / 2, window->height() / 2);
        QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        if (scenario.startsWith("drop_config_locked_")) return enter.isAccepted() && drop.isAccepted()
            && waitFor([&] { return !workspace.busy() && workspace.error().contains("lock"); })
            && workspace.contexts().isEmpty() && workspace.currentSession().isEmpty() && server.requests.isEmpty();
        const int expected = scenario == "drop_config_invalid" || scenario == "drop_config_missing" ? 0 : scenario == "drop_config_multiple" ? 2 : 1;
        return enter.isAccepted() && drop.isAccepted()
            && waitFor([&] { return !workspace.busy() && workspace.contexts().size() == expected; })
            && workspace.currentSession().isEmpty() && workspace.tabs().isEmpty() && server.requests.isEmpty()
            && workspace.sourceImportNotice().isEmpty() && workspace.error().isEmpty()
            && (!item(window,"mapBack") || !item(window,"mapBack")->isVisible())
            && (!item(window,"mapForward") || !item(window,"mapForward")->isVisible());
    }
    if (scenario == "map_empty" || scenario == "map_empty_phone" || scenario == "accessibility_empty" || scenario == "empty_brand" || scenario == "shell_footer_empty" || scenario == "shell_compact_portrait" || scenario == "shell_compact_empty") {
        if (scenario.endsWith("_phone") || scenario == "shell_compact_portrait") window->resize(320,640);
        if (scenario == "shell_compact_empty") window->resize(640,360);
        return waitFor([&] { return !workspace.busy() && workspace.workspacePage()=="map" && workspace.mapOverview()
            && item(window,"mapImportSource") && displayedWithin(window->contentItem(),item(window,"mapImportSource")); })
            && !item(window,"resourcesWorkspaceButton")->isVisible() && !item(window,"eventsWorkspaceButton")->isVisible()
            && !item(window,"portForwardTasksButton")->isVisible() && !item(window,"mapBack")
            && !item(window,"workspaceFooter")->isVisible()
            && workspace.contexts().isEmpty() && workspace.currentSession().isEmpty() && server.requests.isEmpty() && saveShellFrame(window,scenario);
    }
    if (!waitFor([&] { return !workspace.busy(); })) return false;
    if (scenario == "import_files_empty" || scenario == "import_files_empty_folder") {
        const auto folder=temporary.filePath("empty folder");
        if (!QDir().mkpath(folder)) return false;
        if (scenario == "import_files_empty") {
            if (workspace.importFiles({})) return false;
        } else if (!workspace.importFiles({folder}) || !waitFor([&] { return !workspace.busy(); })) return false;
        return workspace.contexts().isEmpty() && workspace.tabs().isEmpty() && workspace.currentSession().isEmpty()
            && workspace.error().isEmpty() && workspace.sourceImportNotice().isEmpty() && server.requests.isEmpty();
    }
    if (scenario == "ui_density_empty_settings") {
        if (!click(window,item(window,"settingsWorkspaceButton"))) return false;
        for (const auto size:{QSize(1440,900),QSize(390,720),QSize(640,320)}) {
            window->resize(size);
            if (!waitFor([&] { return workspace.workspacePage()=="settings"
                && !item(window,"workspaceSidebar")->isVisible()
                && !item(window,"workspaceSidebarHost")->isVisible()
                && !item(window,"toggleSidebar")->isVisible()
                && !window->findChild<QObject*>("sidebarDrawer")->property("visible").toBool(); })) return false;
        }
        return workspace.currentSession().isEmpty() && workspace.contexts().isEmpty() && server.requests.isEmpty();
    }
    if (scenario == "shell_compact_alerts") {
        window->resize(320, 640);
        if (!click(window, item(window, "settingsWorkspaceButton"))
            || !waitFor([&] { const auto* editor = item(window, "alertEditor"); return editor && editor->isVisible(); })
            || podlord::test::visibleItem(window->contentItem(), "saveAlert")
            || !click(window, item(window, "addAlert"))) return false;
        return waitFor([&] {
            auto* table = item(window, "alertRulesTable");
            auto* add = item(window, "addAlert");
            auto* save = item(window, "saveAlert");
            auto* editor = item(window, "alertEditor");
            if (!table || !add || !save || !editor || !table->isVisible() || !add->isVisible() || !editor->isVisible()) return false;
            const QRectF screen(0, 0, window->width(), window->height());
            const auto bounds = [](QQuickItem* control) { return control->mapRectToScene(control->boundingRect()); };
            auto* description = item(window, "alertDescription");
            return description && description->property("cursorPosition").toInt() == 0
                && screen.contains(bounds(add)) && screen.contains(bounds(save))
                && bounds(table).bottom() <= bounds(add).top() + 1
                && bounds(editor).contains(bounds(save))
                && server.requests.isEmpty();
        });
    }
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
        if (!waitFor([&] { return !workspace.busy() && (workspace.requestLimit() == 120 || !workspace.error().isEmpty()); })) return false;
        if (scenario == "settings_busy" || scenario == "settings_conflict") return !workspace.error().isEmpty() && workspace.requestLimit() == 0;
        if (workspace.requestLimit() != 120 || workspace.inactiveSyncMinutes() != 1 || !workspace.error().isEmpty()) return false;
        if (scenario == "settings_save") return server.requests.isEmpty() && QFileInfo(QDir(profile).filePath("read-settings.json")).permissions() == (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
        if (scenario == "settings_restore") { podlord::Workspace restored(profile); return waitFor([&] { return restored.requestLimit() == 120 && restored.inactiveSyncMinutes() == 1 && !restored.busy(); }) && server.requests.isEmpty(); }
        if (!click(window, item(window, "mapWorkspaceButton"))) return false;
    }
    if (scenario == "empty") {
        return waitFor([&] { auto* import = item(window, "mapImportSource");
            return workspace.mapOverview() && import && displayedWithin(window->contentItem(), import); })
            && !item(window,"resourcesWorkspaceButton")->isVisible()
            && !item(window,"eventsWorkspaceButton")->isVisible()
            && !item(window,"portForwardTasksButton")->isVisible() && server.requests.isEmpty();
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
        }
        bool admitted = false;
        if (!QMetaObject::invokeMethod(&workspace, "quickImportFile", Q_RETURN_ARG(bool, admitted), Q_ARG(QString, input))
            || !admitted || !waitFor([&] { return !workspace.busy(); })) return false;
        if (scenario == "quick_file_invalid" || scenario == "quick_file_missing")
            return workspace.contexts().isEmpty() && workspace.tabs().isEmpty() && workspace.error().isEmpty()
                && workspace.sourceImportNotice().isEmpty() && server.requests.isEmpty();
        return workspace.contexts().size() == 1 && workspace.tabs().isEmpty() && workspace.currentSession().isEmpty()
            && server.requests.isEmpty();
    }
    if (workspace.appStore() || scenario == "gke_prompt") {
        if (!click(window, item(window, "settingsWorkspaceButton"))
            || !waitFor([&] { auto* section = item(window, "settingsSourcesSection"); return section && section->isVisible(); })
            || !click(window, item(window, "settingsSourcesSection"))
            || !click(window, item(window, "sourceActionsMenuButton")) || !click(window, item(window, "pasteSourceButton"))
            || !waitFor([&] { auto* input = item(window, "pasteSourceYaml"); return input && input->isVisible(); })) return false;
        auto* origin = item(window, "pasteSourceOrigin");
        if (!origin || origin->isVisible() == workspace.appStore() || origin->property("text").toString().isEmpty()) {
            std::fputs("Paste must retain its origin and expose external path entry only outside the Store channel.\n", stderr); return false;
        }
        item(window, "pasteSourceYaml")->forceActiveFocus();
        QInputMethodEvent input;
        input.setCommitString(QString::fromUtf8(yaml));
        QCoreApplication::sendEvent(item(window, "pasteSourceYaml"), &input);
        if (!click(window, item(window, "importPastedSource"))
            || !waitFor([&] { return !workspace.busy() && !item(window, "pasteSourceYaml")->isVisible(); })
            || !click(window, item(window, "mapWorkspaceButton"))) return false;
    } else if (scenario == "native_file_import" || scenario == "native_folder_import") {
        QString selected=source;
        const bool folder=scenario=="native_folder_import";
        if (folder) {
            selected=QFileInfo(source).dir().filePath("picked-folder");
            if (!QDir().mkpath(selected) || !QFile::copy(source,selected+"/config.yaml")) return false;
        }
        auto* picker=window->findChild<QObject*>(folder ? "kubeconfigFolderDialog" : "kubeconfigFileDialog");
        if (folder && picker) {
            picker->setProperty("currentFolder",QUrl::fromLocalFile(selected));
            if (!QMetaObject::invokeMethod(picker,"open") || !waitFor([&] { return picker->property("visible").toBool(); })) return false;
        }
        if (!picker || !picker->setProperty(folder ? "selectedFolder" : "selectedFile",QUrl::fromLocalFile(selected))
            || !QMetaObject::invokeMethod(picker,"accepted",Qt::DirectConnection)) return false;
        if (folder) QMetaObject::invokeMethod(picker,"close");
        const bool imported=waitFor([&] { return !workspace.busy() && workspace.contexts().size()==1; });
        if (!imported) std::fprintf(stderr,"Picker import failed: selected=%s contexts=%lld error=%s\n",qPrintable(picker->property(folder ? "selectedFolder" : "selectedFile").toUrl().toString()),static_cast<long long>(workspace.contexts().size()),qPrintable(workspace.error()));
        return imported && workspace.tabs().isEmpty() && server.requests.isEmpty();
    } else if (scenario == "map_source_enter") {
        if (!click(window, item(window,"settingsWorkspaceButton")) || !podlord::test::selectSettingsSection(window,"sources")
            || !type(window, item(window,"settingsSourcePath"), source)) return false;
        QTest::keyClick(window, Qt::Key_Return);
    } else if (!workspace.quickImportFile(source)) return false;
    if (scenario.startsWith("settings_inline_")) {
        if (!waitFor([&] { return !workspace.busy(); }) || !click(window,item(window,"settingsWorkspaceButton"))) return false;
        const auto section=scenario.mid(QString("settings_inline_").size());
        const auto destination=section=="invalid" || section=="save" ? QString("Sync")
            : section=="narrow" || section=="graphics" || section=="workspace" ? QString("Appearance")
            : section=="privacy" || section.startsWith("runtime") || section.startsWith("diagnostic_") ? QString("Diagnostics")
            : section.left(1).toUpper()+section.mid(1);
        if (!click(window,item(window,section=="alerts" ? QString("alertsWorkspaceButton") : "settings"+destination+"Section"))) return false;
        if (item(window,"sourceManagementPanel")) return false;
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
                if (audit) {
                    const auto timestamp=model->index(0,0).data(Qt::UserRole+6).toLongLong();
                    const auto expectedTime=QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(timestamp).toLocalTime(),QLocale::ShortFormat);
                    auto* pathCell=item(window,"auditCell_0_2");
                    auto* accessible=pathCell ? QAccessible::queryAccessibleInterface(pathCell) : nullptr;
                    if (model->index(0,0).data().toString()!=expectedTime || !accessible
                        || accessible->text(QAccessible::Name)!=expectedTime+", Path, "+model->index(0,2).data().toString()) {
                        std::fprintf(stderr,"Request time and spoken identity must use the system-local display, not an internal ID.\n"); return false;
                    }
                }
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
            if (section == "runtime_keyboard") {
                auto* sectionControl = item(window, "settingsSyncSection");
                sectionControl->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window, Qt::Key_Space);
                if (!waitFor([&] { auto* current = item(window, "diagnosticValue_rss"); return !current || !current->isVisible(); })) return false;
                auto* diagnosticsModel = workspace.property("diagnosticTable").value<QAbstractItemModel*>();
                if (!diagnosticsModel) return false;
                QStringList hiddenValues;
                for (int row = 0; row < diagnosticsModel->rowCount(); ++row) hiddenValues.append(diagnosticsModel->index(row, 1).data().toString());
                QTest::qWait(5200);
                QStringList afterHidden;
                for (int row = 0; row < diagnosticsModel->rowCount(); ++row) afterHidden.append(diagnosticsModel->index(row, 1).data().toString());
                if (afterHidden != hiddenValues) return false;
                auto* diagnostics = item(window, "settingsDiagnosticsSection");
                diagnostics->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window, Qt::Key_Space);
            }
            return waitFor([&] { auto* current = item(window, "diagnosticValue_rss"); return current && current->property("text").toString().section(' ', 0, 0).toDouble() > value + 40; })
                && allocation.size() == 64 * 1024 * 1024 && server.requests.isEmpty();
        }
        if (section=="save") {
            auto* input=item(window,"inlineRequestLimit")->property("contentItem").value<QQuickItem*>();
            if (!type(window,input,"120")) return false;
            input->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Tab);
            if (!type(window,item(window,"inlineLogLimit"),"7") || !waitFor([&] { return !workspace.busy() && workspace.logLimitMb()==7; })) return false;
            podlord::Workspace restored(profile);
            return waitFor([&] { return !restored.busy() && restored.requestLimit()==120 && restored.logLimitMb()==7; }) && server.requests.isEmpty();
        }
        if (section=="invalid") return type(window,item(window,"inlineLogLimit"),"0") && waitFor([&] { return !workspace.error().isEmpty(); }) && workspace.logLimitMb()==5 && server.requests.isEmpty();
        if (section=="alerts") {
            if (!waitFor([&] { return item(window,"alertRule_0"); })) return false;
            auto* row=QAccessible::queryAccessibleInterface(item(window,"alertRule_0"));
            if (!row || row->text(QAccessible::Name)!="Problem color, Name") {
                qWarning() << "Alert row accessible name:" << (row ? row->text(QAccessible::Name) : QString("missing")); return false;
            }
            for (const auto& pair : {qMakePair("alertZoomEnabled","Auto-focus"),qMakePair("alertSoundEnabled","Sound"),
                    qMakePair("previewAlertZoom","Auto-focus: Preview"),qMakePair("previewAlertSound","Sound: Preview")}) {
                auto* accessible=QAccessible::queryAccessibleInterface(item(window,pair.first));
                if (!accessible || accessible->text(QAccessible::Name)!=QLatin1String(pair.second)) {
                    qWarning() << "Alert control accessible name:" << pair.first << (accessible ? accessible->text(QAccessible::Name) : QString("missing")); return false;
                }
            }
            return server.requests.isEmpty();
        }
        if (section=="graphics") {
            auto* motion=QAccessible::queryAccessibleInterface(item(window,"inlineReducedMotion"));
            if (!motion || motion->text(QAccessible::Name)!="Reduced motion") return false;
            auto* speed = item(window,"inlineRadarWaterSpeed");
            if (item(window,"inlineRadarWaterEnabled") || !speed || !podlord::test::scrollIntoView(window,speed)) return false;
            speed->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Left);
            return waitFor([&] { return !workspace.busy() && workspace.radarWaterSpeedPercent()==0; }) && server.requests.isEmpty();
        }
        if (section=="diagnostics") {
            if (!waitFor([&] { return workspace.settingsDiagnostics()["metrics"].toList().size() >= 11; })) return false;
            auto* metrics = workspace.property("diagnosticTable").value<QAbstractItemModel*>();
            bool hasMemory = false;
            if (metrics) for (int row = 0; row < metrics->rowCount(); ++row)
                hasMemory = hasMemory || metrics->index(row,0).data(Qt::UserRole).toString() == "rss";
            return hasMemory && item(window,"settingsRequestAudit")->property("count").toInt()==0
                && workspace.settingsDiagnostics()["metrics"].toList().size()>=11 && server.requests.isEmpty();
        }
        if (section=="privacy") {
            if (item(window,"privacyTelemetry")) return false;
            for (const auto* label : {"Telemetry enabled", "Secret values", "Request audit retention"})
                for (auto* entry : window->findChildren<QQuickItem*>())
                    if (entry->isVisible() && entry->property("text").toString()==QLatin1String(label)) return false;
            return type(window,item(window,"inlineYamlLimit"),"4") && click(window,item(window,"inlineSaveYamlLimit"))
                && waitFor([&] { return !workspace.busy() && workspace.yamlLimitMiB()==4; }) && server.requests.isEmpty();
        }
        if (section=="workspace") return !item(window,"settingsWorkspaceSection") && !item(window,"settingsResourceColumns") && item(window,"inlineWorkspaceRestore")->isVisible() && server.requests.isEmpty();
        if (section=="sources") return item(window,"settingsSourcePath")->isVisible()
            && waitFor([&] { return item(window,"settingsSourceList")->property("count").toInt()==1; }) && server.requests.isEmpty();
        if (section=="about") return item(window,"settingsVersion")->isVisible() && server.requests.isEmpty();
        if (section=="sync") return item(window,"inlineRequestLimit")->isVisible() && server.requests.isEmpty();
        if (section=="narrow") window->setWidth(360);
        return waitFor([&] { auto* control=item(window,"inlineAppearanceTheme"); return control && displayedWithin(window->contentItem(),control); }) && server.requests.isEmpty();
    }
    if (!waitFor([&] { return !workspace.busy() && workspace.contexts().size() == (scenario.startsWith("map_") ? (twoContexts ? 3 : 1) : twoContexts ? 2 : 1); })) {
        std::fprintf(stderr,"Imported context unavailable: %s\n",qPrintable(workspace.error())); return false;
    }
    if (scenario.startsWith("map_") && !click(window,item(window,"mapWorkspaceButton"))) return false;
    if (scenario=="map_overview" || scenario=="map_alias" || scenario=="map_phone" || scenario=="map_landscape") {
        if (!waitFor([&] { return !workspace.busy() && workspace.contexts().size()==3; })) return false;
        if (scenario=="map_phone") window->resize(320,640);
        if (scenario=="map_landscape") window->resize(640,320);
        const auto first=workspace.contexts().first().toMap();
        if (!waitFor([&] { auto* cards=item(window,"contextMapCards"); auto* card=item(window,"contextMapCard_"+first["id"].toString());
            return workspace.workspacePage()=="map" && workspace.mapOverview() && cards && cards->property("count").toInt()==3
                && card && displayedWithin(window->contentItem(),card); })) return false;
        if (scenario=="map_landscape") {
            if (!waitFor([&] { return window->contentItem()->width()==640 && window->contentItem()->height()==320; }) || window->grabWindow().isNull()) return false;
            auto* cards=item(window,"contextMapCards"); auto* rename=item(window,"renameMapContext_"+first["id"].toString());
            const QRectF viewport(cards->mapToScene(QPointF{}),cards->size());
            if (!rename || !viewport.contains(rename->mapRectToScene(rename->boundingRect()))) {
                saveShellFrame(window,scenario); std::fprintf(stderr,"Landscape context caption/action clipped: viewport=%gx%g\n",cards->width(),cards->height()); return false;
            }
        }
        if (scenario=="map_alias") {
            if (!click(window,item(window,"renameMapContext_"+first["id"].toString()))
                || !waitFor([&] { return item(window,"sourceAliasName") && item(window,"sourceAliasName")->isVisible(); })
                || !type(window,item(window,"sourceAliasName"),"My development")
                || !click(window,item(window,"saveSourceAlias"))
                || !waitFor([&] { return !workspace.busy() && workspace.contexts().first().toMap()["name"]=="My development"; })) return false;
            if (workspace.contexts().first().toMap()["id"]!=first["id"] || workspace.contexts().first().toMap()["context"]!=first["context"]) return false;
            QFile original(source); if (!original.open(QIODevice::ReadOnly) || original.readAll()!=yaml) return false;
        }
        return workspace.currentSession().isEmpty() && workspace.tabs().isEmpty() && server.requests.isEmpty() && saveShellFrame(window,scenario);
    }
    if (scenario == "map_open_options" || scenario == "map_alias_modal") {
        if (!workspace.setWorkspacePage("map") || !workspace.showMapOverview()) return false;
        const auto context = workspace.contexts().first().toMap()["id"].toString();
        if (!waitFor([&] { return item(window, "contextMapCard_" + context); })) return false;
        if (scenario == "map_open_options") {
            if (!click(window, item(window, "openMapContextOptions_" + context))
                || !waitFor([&] { auto* open = item(window, "openMapContextInTab"); return open && open->isVisible(); })
                || !workspace.currentSession().isEmpty() || !server.requests.isEmpty()
                || !click(window, item(window, "openMapContextInTab"))) return false;
            return waitFor([&] { return !workspace.busy() && !workspace.loading() && !workspace.currentSession().isEmpty()
                && workspace.workspacePage() == "map" && !workspace.mapOverview() && workspace.totalResourceCount() == 3; });
        }
        if (!click(window, item(window, "renameMapContext_" + context))
            || !waitFor([&] { return item(window, "sourceAliasName")->isVisible(); })) return false;
        auto* card = item(window, "contextMapCard_" + context);
        const auto point = card->mapToScene(QPointF(12, 12)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        if (!workspace.currentSession().isEmpty() || !server.requests.isEmpty() || !item(window, "sourceAliasName")->isVisible()) return false;
        return click(window, item(window, "cancelSourceAlias"))
            && waitFor([&] { return !item(window, "sourceAliasName")->isVisible(); })
            && workspace.currentSession().isEmpty() && server.requests.isEmpty();
    }
    if (scenario == "settings_invalid") return waitFor([&] { return !workspace.error().isEmpty() && !workspace.busy(); })
        && workspace.currentSession().isEmpty() && workspace.tabs().isEmpty() && server.requests.isEmpty();
    const auto openAction=scenario.startsWith("map_") || scenario=="gke_prompt" || workspace.appStore() ? "contextMapCard_"+workspace.contexts().first().toMap()["id"].toString() : "contextMapCard_"+workspace.contexts().first().toMap()["id"].toString();
    if (!workspace.setWorkspacePage("map")) return false;
    if (!waitFor([&] { auto* action=item(window,openAction); return action && action->isVisible(); }) || !click(window,item(window,openAction))) { std::fputs("Open context action unavailable.\n",stderr); return false; }
    if (!waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); },10000)) return false;
    if (scenario == "hidden_followup") {
        if (!waitFor([&] { return server.requests.contains("/api"); })) return false;
        if (!click(window,item(window,"closeCurrentSession"))
            || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
        QTest::qWait(600);
        return server.requests == QStringList{"/api"} && server.delayedReadDelivered && !server.prematureDisconnect;
    }
    if (!scenario.startsWith("map_")) {
        if (!waitFor([&] { return workspace.totalResourceCount() > 0 || !workspace.loading(); }, 15000)) return false;
        if (workspace.totalResourceCount() > 0 && !click(window,item(window,"resourcesWorkspaceButton"))) return false;
    }
    if (scenario == "filters_all_resources") {
        if (!waitFor([&] { return !workspace.loading() && !workspace.syncLoading() && workspace.totalResourceCount() == 3; })) return false;
        const auto calls = server.requests.size();
        if (!click(window, item(window, "filterPreset")) || !item(window, "allResourceFilters")->property("highlighted").toBool()) return false;
        if (!workspace.filter("alpha") || !waitFor([&] { return workspace.table()->rowCount() == 1; })
            || item(window, "allResourceFilters")->property("highlighted").toBool()
            || !click(window, item(window, "allResourceFilters"))) return false;
        return waitFor([&] { return workspace.table()->rowCount() == 3 && workspace.filterText().isEmpty(); })
            && server.requests.size() == calls && workspace.resourceFieldFilters().isEmpty();
    }
    if (scenario=="map_preview_deferred") {
        const auto first=workspace.currentSession(),context=workspace.contexts().first().toMap()["id"].toString();
        const auto failed=[&](const char* stage) { std::fprintf(stderr,"Deferred preview: %s, pods=%d delivered=%d disconnected=%d total=%d calls=%lld\n",stage,server.refresh,server.delayedReadDelivered,server.prematureDisconnect,workspace.totalResourceCount(),static_cast<long long>(server.requests.size())); return false; };
        if (!waitFor([&] { return server.refresh==2 && std::any_of(server.requests.cbegin(),server.requests.cend(),[](const auto& request) { return QUrl(request).path()=="/apis/example.test/v1/widgets"; }) && !server.delayedReadDelivered; })) return failed("A final page in flight");
        if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString())
            || !waitFor([&] { return !workspace.busy() && !workspace.syncLoading() && workspace.currentSession()!=first && workspace.totalResourceCount()==3 && server.delayedReadDelivered; },15000)
            || !workspace.mapPreviewUrl(context).isEmpty() || server.prematureDisconnect) return failed("B visible, A accepted in background");
        const auto calls=server.requests.size();
        if (!workspace.activate(first) || !waitFor([&] { return !workspace.busy() && workspace.totalResourceCount()==3 && !workspace.mapPreviewUrl(context).isEmpty(); })) return failed("A cached revisit preview");
        return server.requests.size()==calls && !server.prematureDisconnect;
    }
    if (scenario.startsWith("map_")) {
        if (!waitFor([&] { return !workspace.loading() && !workspace.syncLoading() && workspace.totalResourceCount()==3
            && workspace.workspacePage()=="map" && !workspace.mapOverview(); },15000)) return false;
        const auto session=workspace.currentSession(),context=workspace.contexts().first().toMap()["id"].toString();
        if (!waitFor([&] { return !workspace.mapPreviewUrl(context).isEmpty(); })) {
            std::fprintf(stderr,"Map preview did not persist: %s needed=%d\n",qPrintable(workspace.mapPreviewError()),workspace.mapPreviewNeeded()); return false;
        }
        const auto path=QUrl(workspace.mapPreviewUrl(context)).toLocalFile();
        QImage saved(path);
        if (saved.size()!=QSize(320,200) || !saved.textKeys().isEmpty()) return false;
        const auto calls=server.requests.size();
        if (scenario=="map_caption_copy" || scenario=="map_caption_touch") {
            const auto identity=workspace.mapContextIdentity();
            auto* caption=item(window,"mapTitle");
            if (!caption) return false;
            std::unique_ptr<QPointingDevice> touch(scenario=="map_caption_touch" ? QTest::createTouchDevice() : nullptr);
            const auto openMenu=[&] {
                if (!touch) return click(window,caption,Qt::RightButton);
                const auto point=caption->mapToScene(caption->boundingRect().center()).toPoint();
                QTest::touchEvent(window,touch.get()).press(0,point,window).commit();
                const bool opened=waitFor([&] { const auto* action=item(window,"copyMapContextName"); return action && action->isVisible(); });
                QTest::touchEvent(window,touch.get()).release(0,point,window).commit();
                return opened;
            };
            if (!openMenu()
                || !waitFor([&] { auto* action=item(window,"copyMapContextName"); return action && action->isVisible(); })
                || !click(window,item(window,"copyMapContextName"))
                || QGuiApplication::clipboard()->text()!=identity.value("name").toString()
                || !openMenu()
                || !click(window,item(window,"copyMapClusterName"))
                || QGuiApplication::clipboard()->text()!=identity.value("cluster").toString()) return false;
            QGuiApplication::clipboard()->setText("unchanged");
            return !workspace.copyMapIdentity("id") && QGuiApplication::clipboard()->text()=="unchanged"
                && server.requests.size()==calls && saveShellFrame(window,scenario);
        }
        if (scenario=="map_repeat_navigation") {
            auto* overview=item(window,"mapOverview");
            if (!overview || overview->property("showText").toBool() || !click(window,item(window,"mapWorkspaceButton"))
                || !workspace.mapOverview() || workspace.currentSession()!=session
                || !workspace.setWorkspacePage("resources") || !click(window,item(window,"mapWorkspaceButton"))) return false;
            return workspace.workspacePage()=="map" && !workspace.mapOverview() && workspace.currentSession()==session
                && server.requests.size()==calls;
        }
        if (scenario=="map_menu_remove") {
            if (!workspace.showMapOverview()
                || !waitFor([&] { return item(window,"openMapContextOptions_"+context); })
                || !click(window,item(window,"openMapContextOptions_"+context))
                || !waitFor([&] { auto* action=item(window,"renameMapContextMenu"); return action && action->isVisible(); })
                || !click(window,item(window,"removeMapContextMenu"))
                || !waitFor([&] { auto* dialog=window->findChild<QObject*>("sourceRemovalDialog"); return dialog && dialog->property("opened").toBool(); })
                || workspace.sourceRemoval().value("contextId")!=context
                || !click(window,item(window,"cancelSourceRemoval"))) return false;
            if (!workspace.sourceRemoval().isEmpty() || workspace.contexts().size()!=3
                || !click(window,item(window,"openMapContextOptions_"+context))
                || !click(window,item(window,"removeMapContextMenu"))
                || !click(window,item(window,"confirmSourceRemoval"))) return false;
            return waitFor([&] { return !workspace.busy() && workspace.contexts().size()==2 && workspace.tabs().isEmpty(); })
                && server.requests.size()==calls;
        }
        if (scenario=="map_overview_stability" || scenario=="map_overview_lifecycle") {
            const auto catalog=podlord::KubeconfigStore(profile).list();
            if (!std::holds_alternative<podlord::SourceCatalog>(catalog)) return false;
            const auto endpoint=std::get<podlord::SourceCatalog>(catalog).sources.first().contexts.first().server;
            QByteArray many="apiVersion: v1\nkind: Config\nclusters:\n- name: lifecycle\n  cluster: {server: '"+endpoint.toUtf8()+"'}\nusers:\n- name: local\n  user: {}\ncontexts:\n";
            for (int index=0;index<1000;++index) many+="- name: context-"+QByteArray::number(index)+"\n  context: {cluster: lifecycle, user: local}\n";
            if (!workspace.importText(QFileInfo(source).absolutePath()+"/lifecycle.config",QString::fromUtf8(many))
                || !waitFor([&] { return !workspace.busy() && workspace.contexts().size()==1003; })) return false;
            const auto requests=server.requests.size();
            const int cycles=scenario=="map_overview_stability" ? 1 : 20;
            for (int cycle=0;cycle<cycles;++cycle) {
                if (!workspace.showMapOverview() || !waitFor([&] { auto* cards=item(window,"contextMapCards"); return cards && cards->isVisible() && cards->property("count").toInt()==1003; })) return false;
                QPointer<QQuickItem> cards=item(window,"contextMapCards");
                if (!QMetaObject::invokeMethod(cards,"positionViewAtEnd",Qt::DirectConnection) || window->grabWindow().isNull()) return false;
                const auto offset=cards->property("contentY").toDouble();
                if (offset<=0) return false;
                if (scenario=="map_overview_stability") {
                    if (!workspace.setResourceLimit("1") || window->grabWindow().isNull()) return false;
                    const auto after=cards->property("contentY").toDouble();
                    if (std::abs(after-offset)>1) {
                        std::fprintf(stderr,"Cached-view change reset context-card scroll: before=%g after=%g\n",offset,after); return false;
                    }
                    return workspace.mapOverview() && cards->property("count").toInt()==1003 && workspace.currentSession()==session && server.requests.size()==requests;
                }
                if (!workspace.activate(workspace.currentSession())) return false;
                if (cards && !QMetaObject::invokeMethod(cards,"positionViewAtBeginning",Qt::DirectConnection)) return false;
                window->resize(cycle%2 ? 1120 : 960,720);
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                if (workspace.mapOverview() || workspace.currentSession()!=session || window->grabWindow().isNull()) return false;
            }
            return server.requests.size()==requests && saveShellFrame(window,scenario);
        }
        if (scenario=="map_current_versions" || scenario=="map_current_removed" || scenario=="map_catalog_warning" || scenario=="map_catalog_unreadable") {
            const podlord::KubeconfigStore store(profile);
            auto catalog=store.list();
            if (!std::holds_alternative<podlord::SourceCatalog>(catalog)) return false;
            const auto imported=std::get<podlord::SourceCatalog>(catalog).sources.first().importedAt;
            if (!waitFor([&] { return QDateTime::currentDateTimeUtc()>imported; })) return false;
            QFile original(source);
            const auto changed=yaml+"\n# new source revision\n";
            if (!original.open(QIODevice::WriteOnly|QIODevice::Truncate) || original.write(changed)!=changed.size()) return false;
            original.close();
            if (!workspace.importFile(source) || !waitFor([&] { return !workspace.busy() && workspace.contexts().size()==6; })
                || !workspace.showMapOverview() || !waitFor([&] { return item(window,"contextMapCards"); })) return false;
            window->grabWindow();
            if (item(window,"contextMapCards")->property("count").toInt()!=3) {
                std::fputs("Historical source versions created duplicate context cards.\n",stderr); return false;
            }
            catalog=store.list();
            if (!std::holds_alternative<podlord::SourceCatalog>(catalog)) return false;
            const auto latest=std::get<podlord::SourceCatalog>(catalog).sources.first();
            if (scenario=="map_catalog_warning" || scenario=="map_catalog_unreadable") {
                QFile previewFile(path);
                if (!previewFile.open(QIODevice::ReadOnly)) return false;
                const auto previewBytes=previewFile.readAll(); previewFile.close();
                const auto directory=QFileInfo(latest.ownedPath).absolutePath(), backup=directory+"-retained";
                QByteArray record;
                QFile owned(latest.ownedPath);
                if (scenario=="map_catalog_warning") {
                    if (!owned.open(QIODevice::ReadOnly)) return false;
                    record=owned.readAll(); owned.close();
                    if (!owned.open(QIODevice::WriteOnly|QIODevice::Truncate) || owned.write("broken source snapshot")<0) return false;
                    owned.close();
                } else {
                    if (!QDir().rename(directory,backup)) return false;
                    QFile blocked(directory);
                    if (!blocked.open(QIODevice::WriteOnly) || blocked.write("not a directory")<0) return false;
                }
                if (!workspace.reload() || !waitFor([&] { return !workspace.busy(); })
                    || workspace.property("mapCatalogWarning").toString().isEmpty() || !text(window,"mapTitle").contains("older")
                    || item(window,"contextMapCards")->property("count").toInt()!=3) return false;
                const auto rows=window->property("mapContexts").toList();
                if (rows.size()!=3 || !click(window,item(window,"contextMapCard_"+rows.first().toMap()["id"].toString()))
                    || !waitFor([&] { return !workspace.busy() && !workspace.mapOverview(); })
                    || workspace.property("mapCatalogWarning").toString().isEmpty() || !workspace.showMapOverview()) return false;
                if (scenario=="map_catalog_warning") {
                    if (!owned.open(QIODevice::WriteOnly|QIODevice::Truncate) || owned.write(record)!=record.size()) return false;
                    owned.close();
                } else if (!QFile::remove(directory) || !QDir().rename(backup,directory)) return false;
                if (!workspace.reload() || !waitFor([&] { return !workspace.busy() && workspace.property("mapCatalogWarning").toString().isEmpty(); })) return false;
                return previewFile.open(QIODevice::ReadOnly) && previewFile.readAll()==previewBytes
                    && item(window,"contextMapCards")->property("count").toInt()==3;
            }
            if (scenario=="map_current_removed") {
                for (const auto& entry:latest.contexts)
                    if (!std::holds_alternative<podlord::SessionCatalog>(store.removeContext(entry.id,{}))) return false;
                podlord::Workspace restored(profile);
                QQmlApplicationEngine restoredEngine;
                restoredEngine.rootContext()->setContextProperty("workspace",&restored);
                restoredEngine.load(QUrl("qrc:/podlord/Main.qml"));
                if (restoredEngine.rootObjects().isEmpty()) return false;
                auto* restoredWindow=qobject_cast<QQuickWindow*>(restoredEngine.rootObjects().first());
                return waitFor([&] { auto* cards=item(restoredWindow,"contextMapCards"); return !restored.busy() && restored.contexts().size()==3 && cards && cards->property("count").toInt()==0; })
                    && restored.currentSession()==session && restored.mapOverview() && server.requests.size()==calls;
            }
            if (!waitFor([&] { return QDateTime::currentDateTimeUtc()>latest.importedAt; })
                || !original.open(QIODevice::WriteOnly|QIODevice::Truncate) || original.write(yaml)!=yaml.size()) return false;
            original.close();
            if (!workspace.importFile(source) || !waitFor([&] { return !workspace.busy() && workspace.contexts().size()==6; })) return false;
            const auto savedSessions=podlord::SessionStore(profile).list();
            if (!std::holds_alternative<podlord::SessionCatalog>(savedSessions)) return false;
            const auto& sessions=std::get<podlord::SessionCatalog>(savedSessions).sessions;
            return item(window,"contextMapCards")->property("count").toInt()==3
                && item(window,"contextMapCard_"+context) && workspace.currentSession()==session && workspace.mapOverview()
                && sessions.size()==1 && sessions.first().config.contextId==context && server.requests.size()==calls;
        }
        if (scenario=="map_boundaries") {
            if (workspace.metaObject()->indexOfProperty("canMapBack") >= 0 || workspace.metaObject()->indexOfProperty("canMapForward") >= 0
                || workspace.metaObject()->indexOfMethod("navigateMap(int)") >= 0) return false;
            if (!workspace.showMapOverview() || !workspace.showMapOverview() || !workspace.mapOverview()
                || !workspace.activate(session) || workspace.mapOverview() || workspace.currentSession() != session
                || !workspace.showMapOverview() || !workspace.mapOverview()) return false;
            return workspace.currentSession()==session && server.requests.size()==calls;
        }
        if (scenario=="map_loaded_landscape") {
            window->resize(640,320);
            if (!click(window,item(window,"mapOverview"))
                || !waitFor([&] { return window->contentItem()->width()==640 && window->contentItem()->height()==320 && item(window,"contextMapCards"); })
                || window->grabWindow().isNull()) return false;
            auto* cards=item(window,"contextMapCards"); auto* card=item(window,"contextMapCard_"+context);
            const QRectF viewport(cards->mapToScene(QPointF{}),cards->size());
            if (!card || !viewport.contains(card->mapRectToScene(card->boundingRect()))) {
                saveShellFrame(window,scenario); std::fputs("Loaded landscape context card clipped after rendering.\n",stderr); return false;
            }
            auto* preview=item(window,"contextMapPreview_"+context);
            if (!preview || !waitFor([&] { return preview->property("status").toInt()==1; }) || window->grabWindow().isNull()
                || preview->property("paintedWidth").toDouble()<=0 || preview->property("paintedHeight").toDouble()<=0) {
                std::fprintf(stderr,"Loaded landscape preview lost usable image height: card=%gx%g image=%gx%g status=%d\n",card->width(),card->height(),preview ? preview->property("paintedWidth").toDouble() : 0,preview ? preview->property("paintedHeight").toDouble() : 0,preview ? preview->property("status").toInt() : -1); return false;
            }
            return workspace.mapOverview() && workspace.currentSession()==session && server.requests.size()==calls && saveShellFrame(window,scenario);
        }
        if (scenario=="map_search") {
            if (!waitFor([&] { return !workspace.radarView().isEmpty(); })) return false;
            const auto camera=workspace.radarView();
            const auto failed=[&](const char* stage) {
                qWarning()<<"Map search:"<<stage<<"count"<<workspace.resourceCount()<<"counter"<<text(window,"queryMatchCount")
                    <<"camera before"<<camera<<"camera after"<<workspace.radarView();
                saveShellFrame(window,QStringLiteral("map-search-failure")); return false;
            };
            if (!workspace.setResourceLimit("1") || !click(window,item(window,"workspaceSearchButton"))
                || !type(window,item(window,"resourceFilter"),"Pod")
                || !waitFor([&] { return workspace.resourceCount()==2 && text(window,"queryMatchCount")=="0/2"; })
                || workspace.radarView()!=camera) return failed("typed query retains camera and selection");
            if (!click(window,item(window,"queryNext")) || !waitFor([&] { return text(window,"queryMatchCount")=="1/2"; })
                || !click(window,item(window,"queryNext")) || !waitFor([&] { return text(window,"queryMatchCount")=="2/2"; })
                || !click(window,item(window,"queryNext")) || !waitFor([&] { return text(window,"queryMatchCount")=="1/2"; })
                || !click(window,item(window,"queryPrevious")) || !waitFor([&] { return text(window,"queryMatchCount")=="2/2"; })) return failed("explicit result navigation");
            return workspace.displayedResourceCount()==1 && workspace.radarView()!=camera && workspace.inspectorPath().isEmpty() && server.requests.size()==calls;
        }
        if (scenario=="map_preview_expiry") {
            QFile before(path); if (!before.open(QIODevice::ReadOnly)) return false;
            const auto bytes=before.readAll(); before.close();
            server.refuseCollections=true; reference=reference.addSecs(25*60*60);
            if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && !workspace.syncLoading() && server.requests.size()>calls; },15000)) return false;
            QFile after(path);
            return workspace.totalResourceCount()==0 && !workspace.mapPreviewNeeded() && after.open(QIODevice::ReadOnly) && after.readAll()==bytes;
        }
        if (scenario=="map_cache_settings") {
            if (!click(window,item(window,"settingsWorkspaceButton")) || !podlord::test::selectSettingsSection(window,"sync")
                || !waitFor([&] { return workspace.cachePayloadBytes()>0 && item(window,"inlineCollectionCacheHours"); })) return false;
            auto* hours=item(window,"inlineCollectionCacheHours");
            auto* editor=hours->property("contentItem").value<QQuickItem*>();
            if (hours->property("value").toInt()!=24 || !type(window,editor,"48")) return false;
            QTest::keyClick(window,Qt::Key_Return);
            if (!waitFor([&] { return !workspace.busy() && workspace.collectionCacheHours()==48; })) return false;
            const auto stored=podlord::ReadSettingsStore(profile).load();
            return std::holds_alternative<podlord::ReadSettings>(stored) && std::get<podlord::ReadSettings>(stored).collectionCacheHours==48
                && text(window,"collectionCacheUsage").contains("MiB") && server.requests.size()==calls && saveShellFrame(window,scenario);
        }
        if (scenario=="map_cleanup") {
            if (!workspace.requestSourceRemoval(context) || !workspace.confirmSourceRemoval()
                || !waitFor([&] { return !workspace.busy() && workspace.contexts().size()==2 && workspace.mapPreviewUrl(context).isEmpty() && !QFileInfo::exists(path); })) return false;
            return workspace.mapOverview();
        }
        if (scenario=="map_active_tab") {
            if (!click(window,item(window,"mapOverview")) || !workspace.mapOverview()
                || !click(window,item(window,"quickOpenDropdown"))
                || !click(window,item(window,"quickContext_"+workspace.sessionConfiguration(session).value("contextId").toString()))) return false;
            return waitFor([&] { return !workspace.mapOverview() && workspace.workspacePage()=="map"; }) && server.requests.size()==calls;
        }
        if (scenario=="map_preview_restart" || scenario=="map_restore_tab" || scenario=="map_restore_metadata") {
            window->hide();
            podlord::Workspace restored(profile,nullptr,[&] { return reference; });
            QQmlApplicationEngine reopened;
            reopened.rootContext()->setContextProperty("workspace",&restored); reopened.load(QUrl("qrc:/podlord/Main.qml"));
            auto* second=reopened.rootObjects().isEmpty()?nullptr:qobject_cast<QQuickWindow*>(reopened.rootObjects().first());
            if (!second || !waitFor([&] { return !restored.busy() && restored.mapOverview() && !restored.mapPreviewUrl(context).isEmpty()
                && item(second,"contextMapPreview_"+context) && item(second,"contextMapPreview_"+context)->property("status").toInt()==1; },10000)) return false;
            if (restored.tabs().size()!=workspace.tabs().size() || server.requests.size()!=calls) return false;
            if (!saveShellFrame(second,scenario)) return false;
            if (scenario=="map_restore_metadata") {
                QSignalSpy completed(&restored,&podlord::Workspace::sourceImportFinished);
                if (!restored.refreshSources() || !waitFor([&] { return !completed.isEmpty() && !restored.busy(); })) { std::fprintf(stderr,"Offline sources reload failed: %s\n",qPrintable(restored.error())); return false; }
                completed.clear();
                if (!restored.renameSourceContext(context,"Offline alias") || !waitFor([&] { return !completed.isEmpty() && !restored.busy(); })) { std::fprintf(stderr,"Offline alias failed: %s\n",qPrintable(restored.error())); return false; }
                if (!restored.reload() || !waitFor([&] { return !restored.busy(); })) return false;
                QTest::qWait(1000);
                if (server.requests.size()!=calls) std::fprintf(stderr,"Offline metadata connected: calls=%lld -> %lld\n",static_cast<long long>(calls),static_cast<long long>(server.requests.size()));
                return restored.mapOverview() && server.requests.size()==calls;
            }
            if (!click(second,item(second,"quickOpenDropdown")) || !click(second,item(second,"quickContext_"+restored.sessionConfiguration(session).value("contextId").toString()))
                || !waitFor([&] { return !restored.busy() && !restored.loading() && restored.totalResourceCount()==3; },10000)) return false;
            if (scenario!="map_restore_tab" && !click(second,item(second,"resourcesWorkspaceButton"))) return false;
            return server.requests.size()>calls && restored.currentSession()==session
                && (scenario!="map_restore_tab" || (!restored.mapOverview() && restored.workspacePage()=="map"));
        }
        if (scenario=="map_navigation") {
            const auto failed=[&](const char* stage) { std::fprintf(stderr,"Map navigation: %s, viewport=%dx%d page=%s overview=%d calls=%lld -> %lld drawer=%d\n",stage,window->width(),window->height(),qPrintable(workspace.workspacePage()),workspace.mapOverview(),static_cast<long long>(calls),static_cast<long long>(server.requests.size()),window->findChild<QObject*>("sidebarDrawer")->property("visible").toBool()); saveShellFrame(window,scenario+"-failure"); return false; };
            if (!click(window,item(window,"resourcesWorkspaceButton")) || workspace.currentSession()!=session || workspace.workspacePage()!="resources") return failed("Resources");
            const bool drawer=!window->property("sidebarDocked").toBool();
            if (drawer && !click(window,item(window,"toggleSidebar"))) return failed("open sidebar");
            if (!waitFor([&] { auto* radar=item(window,"resourceRadar"); return radar && radar->width()<720 && radar->isVisible(); })) return failed("mini radar");
            if (drawer) { QTest::keyClick(window,Qt::Key_Escape); if (!waitFor([&] { return !window->findChild<QObject*>("sidebarDrawer")->property("visible").toBool(); })) return failed("close sidebar"); }
            if (workspace.totalEventCount() > 0) {
                if (!click(window,item(window,"eventsWorkspaceButton"))) return failed("Events");
            } else if (item(window,"eventsWorkspaceButton")->isVisible()) return failed("Empty Events visible");
            if (item(window,"portForwardTasksButton")->isVisible()) return failed("Empty Connections visible");
            if (!click(window,item(window,"mapWorkspaceButton"))) return failed("Map");
            if (!click(window,item(window,"mapOverview")) || !workspace.mapOverview()) return failed("All Contexts");
            if (item(window,"mapBack") || item(window,"mapForward")) return failed("Obsolete map history");
            return (workspace.tabs().size()==1 && server.requests.size()==calls) || failed("retained session/cache");
        }
        return workspace.currentSession()==session && workspace.workspacePage()=="map" && !workspace.mapOverview() && saveShellFrame(window,scenario);
    }
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
        } else if (scenario == "query_next" && selection->currentIndex().data(Qt::UserRole).toString().isEmpty()) return false;
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
        const auto selected = [&] { return selection->currentIndex().data(Qt::UserRole).toString(); };
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
    if (scenario.startsWith("accessibility_")) {
        if (!waitFor([&] { return workspace.totalResourceCount() == 3 && !workspace.loading(); })) return false;
        const auto calls = server.requests.size();
        if (!accessibleName(window,"More actions")) return false;
        if (scenario == "accessibility_populated") {
            window->hide(); QTest::qWait(20); window->show(); window->requestActivate();
            auto* summary=QAccessible::queryAccessibleInterface(item(window,"sessionHealthStrip"));
            if (!summary || summary->role()!=QAccessible::StaticText || !summary->text(QAccessible::Name).startsWith("Context health: ")) {
                std::fputs("Context health is not exposed as a named status in the accessibility tree.\n",stderr); return false;
            }
            return waitFor([&] { return window->isVisible(); }) && accessibleName(window,"More actions") && server.requests.size() == calls;
        }
        if (scenario == "accessibility_settings") return click(window, item(window, "settingsWorkspaceButton"))
            && click(window, item(window, "settingsSourcesSection"))
            && accessibleName(window,"Kubeconfig file or folder") && server.requests.size() == calls;
        if (scenario == "accessibility_dialog") {
            QTest::keySequence(window, QKeySequence(Qt::CTRL | Qt::Key_K));
            if (!waitFor([&] { return item(window, "commandPaletteSearch")->isVisible(); })
                || !accessibleName(window, "Find a command")) return false;
            QTest::keyClick(window, Qt::Key_Escape);
            return waitFor([&] { return !item(window, "commandPaletteSearch")->isVisible(); })
                && accessibleName(window,"More actions") && server.requests.size() == calls;
        }
        return false;
    }
    if (scenario == "gke_prompt") {
        window->resize(320, 640);
        if (!waitFor([&] { return window->contentItem()->width() == 320 && window->contentItem()->height() == 640; })) return false;
        if (!waitFor([&] { return workspace.authenticationRequired(); }) || !click(window, item(window, "authenticationButton"))) return false;
        auto* accept = item(window, "authenticationAccept");
        if (!accept || !waitFor([&] { return accept->isVisible(); })) return false;
        const auto evidence = qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
        if (!evidence.isEmpty() && (!QDir().mkpath(evidence)
            || !window->grabWindow().save(evidence + "/gke-confirmation-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png"))) return false;
        const auto prompt = workspace.authenticationPrompt();
        const bool native = workspace.appStore() || QGuiApplication::platformName() == "ios" || QGuiApplication::platformName() == "android";
        const bool correct = native ? prompt.contains("native Google authorization") && prompt.contains("No imported command runs")
            : prompt.contains("gke-gcloud-auth-plugin") && prompt.contains("This runs the imported configuration");
        const bool fits = waitFor([&] {
            for (auto* label : window->findChildren<QQuickItem*>())
                if (label->isVisible() && label->property("text").toString() == prompt)
                    return window->contentItem()->mapRectToScene(window->contentItem()->boundingRect())
                        .contains(label->mapRectToScene(label->boundingRect()));
            return false;
        });
        if (!fits) std::fputs("Authentication prompt is clipped at 320 pixels.\n", stderr);
        if (!evidence.isEmpty()) {
            window->resize(1440, 900);
            if (!waitFor([&] { return window->contentItem()->width() == 1440 && window->contentItem()->height() == 900; })
                || !window->grabWindow().save(evidence + "/gke-confirmation-wide-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
        }
        QTest::keyClick(window, Qt::Key_Escape);
        return correct && fits && waitFor([&] { return !accept->isVisible(); }) && server.requests.isEmpty() && !workspace.authenticationRunning();
    }
    if (scenario == "plugin" || scenario == "invalid_ca" || scenario == "empty_token")
        return waitFor([&] { return !text(window, "errorMessage").isEmpty(); }) && server.requests.isEmpty();
    if (scenario == "auth") {
        if (!waitFor([&] { return text(window, "syncProblemMessage").contains("Authentication failed"); })) return false;
        const int count = server.requests.size();
        QTest::qWait(100);
        return server.requests.size() == count && item(window, "authenticationButton")->isVisible() && !item(window, "refreshButton");
    }
    if (scenario == "redirect") return waitFor([&] { return text(window, "syncProblemMessage").contains("Redirect refused"); }) && !server.requests.contains("/credential-leak");
    if (scenario == "malformed") return waitFor([&] { return text(window, "syncProblemMessage").contains("invalid response"); }) && !text(window, "syncProblemMessage").contains("secret-value");
    if (scenario == "invalid_discovery") return waitFor([&] { return text(window, "syncProblemMessage").contains("Invalid core API version"); }) && !server.requests.contains("/escape");
    if (scenario == "invalid_list") {
        const bool reported=waitFor([&] { return text(window, "syncProblemMessage").contains("invalid resource"); });
        if (!reported) std::fprintf(stderr, "Invalid list response was not reported; visible status: %s; requests: %s\n", qPrintable(text(window, "syncProblemMessage")), qPrintable(server.requests.join('\n')));
        return reported;
    }
    if (scenario=="shell_empty_cache") {
        if (!waitFor([&] { return !workspace.loading() && !workspace.busy(); })) return false;
        return waitFor([&] {
            auto* empty=item(window,"emptyRadar");
            return workspace.workspacePage()=="map" && !workspace.mapOverview()
                && workspace.totalResourceCount()==0 && empty && empty->isVisible()
                && text(window,"emptyRadar")=="No resources to display.";
        });
    }
    if (scenario.startsWith("shell_")) {
        if (!waitFor([&] { return workspace.totalResourceCount()==(scenario == "shell_search_events" || scenario=="shell_sidebar" ? 5 : 3) && !workspace.loading(); },10000)) {
            std::fprintf(stderr, "Shell cache failed: resources=%d loading=%d busy=%d error=%s\n", workspace.totalResourceCount(), workspace.loading(), workspace.busy(), qPrintable(text(window, "errorMessage")));
            return false;
        }
        const auto calls=server.requests.size();
        auto* radar=item(window,"resourceRadar");
        if (scenario=="shell_text_renderer_invalid") {
            if (!radar) return false;
            const auto pose=radar->property("viewPose");
            QSignalSpy changes(radar,SIGNAL(textSizesChanged()));
            for (const auto* property:{"captionPixelSize","headingPixelSize"}) {
                const auto original=radar->property(property).toDouble();
                for (const auto invalid:{qreal(0),qreal(-1),std::numeric_limits<qreal>::quiet_NaN()}) {
                    changes.clear(); radar->setProperty(property,invalid);
                    if (radar->property(property).toDouble()!=original || !changes.isEmpty()) return false;
                }
                radar->setProperty(property,original*2);
                if (radar->property(property).toDouble()!=original*2) return false;
                changes.clear(); radar->setProperty(property,original*2);
                if (!changes.isEmpty()) return false;
                radar->setProperty(property,original);
            }
            return radar->property("viewPose")==pose && server.requests.size()==calls;
        }
        if (scenario=="shell_text_terminal_invalid") {
            if (!workspace.inspectPath("/api/v1/namespaces/default/pods/alpha")
                || !waitFor([&] { return workspace.canStartTerminal(); }) || !workspace.setInspectorPage("terminal")
                || !waitFor([&] { return item(window,"terminalSurface")!=nullptr; })) return false;
            auto* surface=item(window,"terminalSurface");
            const auto original=surface->property("fontPixelSize").toInt();
            const auto requests=server.requests.size();
            QSignalSpy changes(surface,SIGNAL(colorsChanged()));
            for (const int invalid:{0,-1}) {
                surface->setProperty("fontPixelSize",invalid);
                if (surface->property("fontPixelSize").toInt()!=original || !changes.isEmpty()) return false;
            }
            surface->setProperty("fontPixelSize",original*2);
            if (surface->property("fontPixelSize").toInt()!=original*2) return false;
            changes.clear(); surface->setProperty("fontPixelSize",original*2);
            if (!changes.isEmpty()) return false;
            surface->setProperty("fontPixelSize",original);
            return surface->property("fontPixelSize").toInt()==original && server.requests.size()==requests;
        }
        if (scenario=="shell_text_system") {
            const auto expected=qEnvironmentVariable("PODLORD_SYSTEM_TEXT_MINIMUM").toDouble();
            const auto actualFont=[&](QQuickItem* control) { return control ? control->property("font").value<QFont>().pixelSize() : 0; };
            const auto fontFailure=[&](const char* stage) { qWarning()<<"System text probe"<<stage<<"minimum"<<expected<<"body"<<window->property("font")<<"cell"<<actualFont(item(window,"cell_0_0")); saveShellFrame(window,"system-text-failure"); return false; };
            if (expected<14 || !waitFor([&] {
                return window->property("font").value<QFont>().pixelSize()>=expected && actualFont(item(window,"cell_0_0"))>=expected*13/14-1;
            })) return fontFailure("initial size");
            const int body=window->property("font").value<QFont>().pixelSize();
            const int cell=actualFont(item(window,"cell_0_0"));
            if (!saveShellFrame(window,"system-text-resources") || !click(window,item(window,"settingsWorkspaceButton"))
                || !waitFor([&] { return item(window,"settingsSectionPicker") && item(window,"settingsSectionPicker")->isVisible(); })
                || !saveShellFrame(window,"system-text-settings") || !click(window,item(window,"resourcesWorkspaceButton"))
                || !workspace.inspectPath("/api/v1/namespaces/default/pods/alpha")
                || !waitFor([&] { return workspace.canStartTerminal(); }) || !saveShellFrame(window,"system-text-inspector")
                || !workspace.setInspectorPage("terminal") || !waitFor([&] { return item(window,"terminalSurface")!=nullptr; })) return fontFailure("view journey");
            const int terminal=item(window,"terminalSurface")->property("fontPixelSize").toInt();
            if (terminal!=body || !saveShellFrame(window,"system-text-terminal") || !workspace.closeInspector()
                || !workspace.setWorkspacePage("map") || !saveShellFrame(window,"system-text-map")) return fontFailure("view journey");
            const auto* map=item(window,"resourceRadar");
            const auto caption=map ? map->property("captionPixelSize").toDouble() : 0;
            const auto directory=qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
            QFile report(directory+"/system-text.json");
            const auto bytes=QJsonDocument(QJsonObject{{"platform",QGuiApplication::platformName()}, {"bodyPixels",body},
                {"cellPixels",cell}, {"terminalPixels",terminal}, {"radarCaptionPixels",caption},
                {"width",window->width()}, {"height",window->height()}, {"minimumExpected",expected}}).toJson();
            return caption>=11 && !directory.isEmpty() && report.open(QIODevice::WriteOnly) && report.write(bytes)==bytes.size();
        }
        if (scenario.startsWith("shell_text_")) {
            if (scenario=="shell_text_resize") { auto font=QGuiApplication::font(); font.setPixelSize(28); QGuiApplication::setFont(font); }
            if (!waitFor([&] {
                auto* cell=item(window,"cell_0_2");
                return window->property("font").value<QFont>().pixelSize()==28 && cell
                    && cell->property("font").value<QFont>().pixelSize()==26 && cell->height()>=44;
            })) { qWarning()<<"System font enlargement did not reach application and table"; return false; }
            window->setMinimumHeight(0);
            for (const auto size:{QSize(320,640),QSize(640,320),QSize(1440,920)}) {
                window->resize(size);
                if (!waitFor([&] { return displayedWithin(item(window,"workspaceToolbar"),item(window,"workspaceActionsButton")); })
                    || !saveShellFrame(window,scenario+"-"+QString::number(size.width())+"x"+QString::number(size.height()))) return false;
            }
            auto enlarged=QGuiApplication::font(); enlarged.setPixelSize(39); QGuiApplication::setFont(enlarged);
            window->resize(320,640);
            if (!click(window,item(window,"settingsWorkspaceButton")) || !podlord::test::selectSettingsSection(window,"appearance")
                || !waitFor([&] {
                auto* box=item(window,"inlineWorkspaceRestore");
                auto* label=box ? box->property("contentItem").value<QQuickItem*>() : nullptr;
                return label && box->width()<=box->parentItem()->width()+1 && box->property("font").value<QFont>().pixelSize()==39 && label->width()>0 && label->property("paintedWidth").toReal()>0 && label->property("paintedWidth").toReal()<=label->width()+1;
            }) || !saveShellFrame(window,scenario+"-largest-settings") || !click(window,item(window,"resourcesWorkspaceButton"))) return false;
            window->resize(1440,920);
            auto font=QGuiApplication::font(); font.setPixelSize(14); QGuiApplication::setFont(font);
            return waitFor([&] { auto* restored=item(window,"cell_0_2"); return restored && window->property("font").value<QFont>().pixelSize()==14
                && restored->property("font").value<QFont>().pixelSize()==13; })
                && workspace.currentSession()!="" && server.requests.size()==calls;
        }
        if (scenario == "shell_navigation_overflow") {
            window->setMinimumHeight(0);
            const auto session = workspace.currentSession();
            for (const auto size : {QSize(320,640), QSize(390,720), QSize(640,320), QSize(1440,920)}) {
                window->resize(size);
                if (!waitFor([&] {
                    auto* header = item(window,"workspaceToolbar");
                    auto* menu = item(window,"workspaceActionsButton");
                    if (!header || !menu || header->height() > 60 || !displayedWithin(header,menu)) return false;
                    const auto y = menu->mapToScene({0,menu->height()/2}).y();
                    for (const auto* name : {"workspaceSearchButton","quickOpenDropdown","mapWorkspaceButton","resourcesWorkspaceButton",
                        "eventsWorkspaceButton","portForwardTasksButton","settingsWorkspaceButton","toggleSidebar","pulse_pods","pulse_nodes"}) {
                        auto* control = item(window,name);
                        if (!control || !control->isVisible()) continue;
                        if (!displayedWithin(header,control) || qAbs(control->mapToScene({0,control->height()/2}).y()-y)>1) return false;
                    }
                    return item(window,"workspaceFooter")->isVisible() == (size.width() >= 600 && size.height() >= 600);
                }) || !saveShellFrame(window,scenario+"-"+QString::number(size.width())+"x"+QString::number(size.height()))) return false;
            }
            window->resize(320,640);
            if (!waitFor([&] { return !item(window,"settingsWorkspaceButton")->isVisible(); })
                || !click(window,item(window,"settingsWorkspaceButton")) || workspace.workspacePage()!="settings"
                || !waitFor([&] { return item(window,"workspaceActionsButton")->property("activeIndicator").toBool(); })
                || !click(window,item(window,"workspaceActionsButton"))) return false;
            auto* selected = podlord::test::visibleItem(window->contentItem(),"settingsWorkspaceButton");
            if (!selected || !selected->property("checkable").toBool() || !selected->property("checked").toBool()) return false;
            if (!click(window,selected) || !waitFor([&] {
                return workspace.workspacePage()=="settings" && item(window,"settingsWorkspaceButton")->property("checked").toBool()
                    && item(window,"workspaceActionsButton")->property("activeIndicator").toBool();
            })) return false;
            QTest::keyClick(window,Qt::Key_Escape);
            if (!click(window,item(window,"workspaceActionsButton"))) return false;
            auto* metric = podlord::test::visibleItem(window->contentItem(),"overflowMetric_pods");
            if (!metric || !click(window,metric) || workspace.workspacePage()!="dashboard") return false;
            window->resize(1440,920);
            return click(window,item(window,"resourcesWorkspaceButton")) && click(window,item(window,"resourcesWorkspaceButton"))
                && item(window,"resourcesWorkspaceButton")->property("checked").toBool()
                && workspace.currentSession()==session && server.requests.size()==calls;
        }
        if (scenario == "shell_metric_empty") {
            if (!workspace.filter("no-matching-resource")) return false;
            return waitFor([&] {
                auto* metrics = item(window,"headerMetrics");
                return workspace.resourceCount()==0 && metrics && metrics->property("visibleCount").toInt()==0
                    && metrics->property("overflowMetrics").toList().isEmpty()
                    && !podlord::test::visibleItem(window->contentItem(),"pulse_cpu")
                    && !podlord::test::visibleItem(window->contentItem(),"pulse_pods")
                    && !podlord::test::visibleItem(window->contentItem(),"pulse_nodes");
            }) && saveShellFrame(window,scenario) && server.requests.size()==calls;
        }
        if (scenario == "shell_metric_zero") {
            if (!workspace.filter("alpha")) return false;
            return waitFor([&] {
                auto* cpu = podlord::test::visibleItem(window->contentItem(),"pulse_cpu");
                auto* memory = podlord::test::visibleItem(window->contentItem(),"pulse_memory");
                auto* cpuValue = item(window,"pulse_cpu_usage");
                auto* memoryValue = item(window,"pulse_memory_usage");
                auto* cpuGlyph = podlord::test::visibleItem(window->contentItem(),"pulse_cpu_glyph");
                auto* memoryGlyph = podlord::test::visibleItem(window->contentItem(),"pulse_memory_glyph");
                return cpu && memory && text(window,"pulse_cpu_usage").startsWith("0") && text(window,"pulse_memory_usage").startsWith("0")
                    && cpuValue && memoryValue && !cpuValue->property("truncated").toBool() && !memoryValue->property("truncated").toBool()
                    && cpuValue->implicitWidth() <= cpuValue->width()+1 && memoryValue->implicitWidth() <= memoryValue->width()+1
                    && cpuGlyph && memoryGlyph && cpuGlyph->property("kind")=="Cpu" && memoryGlyph->property("kind")=="Memory"
                    && !podlord::test::visibleItem(window->contentItem(),"pulse_storage")
                    && !podlord::test::visibleItem(window->contentItem(),"pulse_nodes")
                    && qAbs(cpu->width()-memory->width())<.5;
            }) && saveShellFrame(window,scenario) && server.requests.size()==calls;
        }
        if (scenario.startsWith("shell_map")) {
            const auto session = workspace.currentSession();
            const auto miniWidth = radar->width();
            const auto selectView = [&](const char* name) {
                return click(window, item(window, name));
            };
            if (scenario == "shell_map_footer") {
                if (!workspace.setResourceLimit("1")
                    || !waitFor([&] { return text(window,"resourceMatchCount")=="visible: 1/3"; })
                    || !selectView("mapWorkspaceButton")
                    || !waitFor([&] { return text(window,"resourceMatchCount")=="matches: 3/3"; })) return false;
                for (const auto& query:{QString("alpha"),QString("no-matching-resource"),QString{}}) {
                    const int matches=query.isEmpty() ? 3 : query=="alpha" ? 1 : 0;
                    if (!workspace.filter(query) || !waitFor([&] {
                        return text(window,"resourceMatchCount")==QString("matches: %1/3").arg(matches);
                    })) return false;
                }
                if (!workspace.setWorkspacePage("events")
                    || !waitFor([&] { return text(window,"resourceMatchCount")=="visible: 1/3"; })
                    || !workspace.setWorkspacePage("map")
                    || !waitFor([&] { return text(window,"resourceMatchCount")=="matches: 3/3"; })
                    || !selectView("resourcesWorkspaceButton")) return false;
                return waitFor([&] { return text(window,"resourceMatchCount")=="visible: 1/3"; })
                    && workspace.currentSession()==session && workspace.resourceLimit()==1 && server.requests.size()==calls;
            }
            if (!workspace.filter("alpha") || !selectView("mapWorkspaceButton")) return false;
            if (!waitFor([&] { return (workspace.workspacePage()=="map" && !workspace.mapOverview())
                && item(window, "resourceRadar") == radar && radar->isVisible() && radar->width() > miniWidth + 100; })) return false;
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Right);
            const auto pose = workspace.radarView();
            if (scenario == "shell_map_inspector") {
                window->setMinimumHeight(0); window->resize(640,320);
                QTest::keyClick(window,Qt::Key_Return);
                if (!waitFor([&] { return workspace.inspectorName()=="alpha" && workspace.canEditYaml()
                    && !radar->isVisible() && !radar->hasActiveFocus(); })) return false;
                if (!click(window,item(window,"closeInspector"))) return false;
                return waitFor([&] { return workspace.inspectorPath().isEmpty() && (workspace.workspacePage()=="map" && !workspace.mapOverview())
                    && radar->isVisible() && !displayedWithin(item(window,"workspaceSidebarHost"),radar)
                    && workspace.radarView()==pose && workspace.filterText()=="alpha"; });
            }
            if (scenario == "shell_map_session") {
                if (!workspace.openContext(workspace.contexts().last().toMap().value("id").toString())
                    || !waitFor([&] { return !workspace.loading() && !workspace.busy() && workspace.currentSession() != session; })
                    || !(workspace.workspacePage()=="map" && !workspace.mapOverview())
                    || !workspace.activate(session)
                    || !waitFor([&] { return !workspace.busy() && workspace.currentSession() == session && (workspace.workspacePage()=="map" && !workspace.mapOverview()); })) return false;
                return waitFor([&] { return radar->isVisible() && workspace.radarView() == pose && workspace.filterText() == "alpha"; });
            }
            for (const auto size : {QSize(640,320), QSize(390,780), QSize(1440,920)}) {
                window->setMinimumHeight(0); window->resize(size);
                if (!waitFor([&] { return radar->isVisible() && radar->width() > 200 && radar->height() > 60; })
                    || workspace.radarView() != pose || workspace.filterText() != "alpha") return false;
            }
            const auto capture = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
            if (!capture.isEmpty() && !window->grabWindow().save(capture)) return false;
            if (!selectView("resourcesWorkspaceButton") || !waitFor([&] { return !(workspace.workspacePage()=="map" && !workspace.mapOverview()) && radar->width() <= miniWidth + 1; })) return false;
            return workspace.currentSession() == session && workspace.radarView() == pose
                && workspace.filterText() == "alpha" && server.requests.size() == calls;
        }
        if (scenario == "shell_landscape_search_error") {
            window->setMinimumHeight(0);
            window->resize(640, 272);
            if (!workspace.filter("alpha") || !click(window, item(window, "workspaceSearchButton"))) {
                std::fputs("Landscape search control unavailable.\n", stderr); return false;
            }
            if (podlord::test::visibleItem(window->contentItem(),"pulse_cpu") || podlord::test::visibleItem(window->contentItem(),"pulse_nodes")) return false;
            for (const auto* name : {"pulse_pods_usage"}) {
                auto* usage = item(window, name);
                if (!usage || !usage->isVisible() || usage->property("truncated").toBool()) {
                    std::fputs("Landscape resource count is truncated.\n", stderr); return false;
                }
            }
            const auto session = workspace.currentSession();
            workspace.openContext("ctx:" + QString(64, '0'));
            if (!waitFor([&] { return !workspace.busy() && !text(window, "errorMessage").isEmpty(); })
                || workspace.currentSession() != session || workspace.filterText() != "alpha") {
                std::fprintf(stderr, "Landscape validation error unavailable: same-session=%d filter=%s error=%s\n", workspace.currentSession() == session,
                    qPrintable(workspace.filterText()), qPrintable(text(window, "errorMessage"))); return false;
            }
            if (!waitFor([&] {
                auto* cell = item(window, "cell_0_0");
                return cell && cell->height() > 0 && displayedWithin(window->contentItem(), cell);
            })) {
                std::fprintf(stderr, "Landscape search and error leave no complete resource row.\n");
                return false;
            }
            if (server.requests.size() != calls || !click(window, item(window, "cell_0_0"))) return false;
            return waitFor([&] { return workspace.inspectorName() == "alpha"; });
        }
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
            const auto context = workspace.sessionConfiguration(session).value("contextId").toString();
            if (context.isEmpty() || !workspace.close(session)
                || !waitFor([&] { return !workspace.busy() && workspace.currentSession().isEmpty(); })
                || !text(window, "syncStatus").isEmpty()
                || !workspace.openContext(context) || !waitFor([&] {
                    const auto active = workspace.currentSession();
                    return !workspace.busy() && !workspace.loading() && !active.isEmpty() && active != session
                        && workspace.sessionConfiguration(active).value("contextId").toString() == context;
                })) return false;
            const bool preserved = text(window, "syncStatus") == QString("API: %1/min  Synced: 0s ago").arg(calls) && server.requests.size() == calls;
            if (!preserved) qWarning() << "Reopened context footer:" << text(window,"syncStatus") << "requests" << calls << server.requests;
            return preserved;
        }
        if (scenario == "shell_landscape" || scenario == "shell_landscape_filters" || scenario == "shell_landscape_rotate") {
            const auto rotationFailure = [&](const char* stage) {
                qWarning() << "Responsive navigation failure:" << stage << window->size() << "pose" << radar->property("viewPose") << "filter" << workspace.filterText();
                saveShellFrame(window,"landscape-rotation-failure");
                return false;
            };
            if (scenario != "shell_landscape_filters") {
                if (!workspace.filter("alpha")) return rotationFailure("filter request");
                if (!waitFor([&] { return item(window,"radarZoom")->isVisible(); })) return rotationFailure("zoom availability");
                if (!click(window,item(window,"radarZoom"))) return rotationFailure("zoom input");
            }
            const auto pose = radar->property("viewPose").toMap();
            window->resize(800,390);
            if (!waitFor([&] { return item(window, "workspaceToolbar")->isVisible() && !radar->isVisible()
                && !item(window, "sidebarFilterScroll")->isVisible(); })) return false;
            if (scenario == "shell_landscape_filters") {
                const auto failed = [&](const char* stage, QQuickItem* control) {
                    qWarning() << "Landscape filter failure:" << stage << window->size() << "requests" << server.requests.size() << calls;
                    for (auto* ancestor = control; ancestor; ancestor = ancestor->parentItem())
                        if (ancestor == control || ancestor->clip())
                            qWarning() << ancestor->objectName() << QRectF(ancestor->mapToScene(QPointF{}), ancestor->size())
                                       << "clip" << ancestor->clip() << "moving" << ancestor->property("moving");
                    const auto capture = qEnvironmentVariable("PODLORD_UI_FAILURE_FRAME");
                    if (!capture.isEmpty()) window->grabWindow().save(capture);
                    return false;
                };
                for (const auto size : {QSize(800,390), QSize(640,320), QSize(640,272)}) {
                    window->resize(size);
                    const bool problemsBefore = workspace.problemsOnly();
                    auto* toggle = item(window, "toggleLandscapeFilters");
                    auto* problems = item(window, "problemsOnly");
                    if (!podlord::test::scrollIntoView(window, toggle) || !click(window, toggle)
                        || !waitFor([&] { return item(window, "sidebarFilterScroll")->isVisible(); })
                        || !podlord::test::scrollIntoView(window, problems)) return failed("expand and reveal", problems);
                    const QRectF bounds(problems->mapToScene(QPointF{}), problems->size());
                    if (!QRectF(0, 0, window->width(), window->height()).contains(bounds)) return failed("window clipping", problems);
                    for (auto* parent = problems->parentItem(); parent; parent = parent->parentItem())
                        if (parent->clip() && !QRectF(parent->mapToScene(QPointF{}), parent->size()).contains(bounds)) return failed("ancestor clipping", problems);
                    if (!click(window, problems) || workspace.problemsOnly() == problemsBefore
                        || !podlord::test::scrollIntoView(window, toggle) || !click(window, toggle)
                        || !waitFor([&] { return !item(window, "sidebarFilterScroll")->isVisible(); })
                        || workspace.problemsOnly() == problemsBefore || server.requests.size() != calls) return failed("toggle and retain", problems);
                }
                return true;
            }
            const auto evidence = qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
            const QList<QSize> sizes = scenario == "shell_landscape_rotate" ? QList<QSize>{QSize(800,390),QSize(390,720),QSize(800,390),QSize(1440,920)}
                : QList<QSize>{QSize(600,360),QSize(640,360),QSize(800,390),QSize(896,414),QSize(1080,480)};
            for (const auto size : sizes) {
                window->resize(size);
                const bool landscape = size.width() >= 600 && size.height() < 600;
                if (!waitFor([&] {
                    auto* header = item(window, "workspaceToolbar");
                    auto* host = item(window, "workspaceSidebarHost");
                    return header->isVisible() && header->height() <= 60 && (landscape ? host->isVisible() && host->width() <= 80 && !radar->isVisible() : true)
                        && item(window, "resourceTable")->height() >= 32;
                })) return false;
                if (scenario == "shell_landscape_rotate") {
                    auto* drawer = window->findChild<QObject*>("sidebarDrawer");
                    if (!drawer) return false;
                    if (size.width() == 390) {
                        const QPointer<QQuickItem> toggle(item(window,"toggleSidebar"));
                        const bool activated = click(window,toggle);
                        if (!activated || !waitFor([&] { return drawer->property("visible").toBool() && radar->isVisible(); })) {
                            qWarning() << "Portrait filters action:" << activated << "target" << (toggle ? toggle->objectName() : QString{})
                                << "drawer visible/opened" << drawer->property("visible") << drawer->property("opened");
                            return rotationFailure("open portrait filters");
                        }
                    } else if (!waitFor([&] { return !drawer->property("visible").toBool(); })) return false;
                }
                if (landscape) for (const auto* name : {"workspaceSearchButton","resourcesWorkspaceButton","settingsWorkspaceButton","workspaceActionsButton","toggleSidebar"}) {
                    if (!podlord::test::revealWorkspaceAction(window,name)) return rotationFailure("navigation availability");
                    auto* control = item(window,name);
                    if (!control || control->width() < 44 || control->height() < 44 || !podlord::test::scrollIntoView(window, control)
                        || !displayedWithin(window->contentItem(),control)) return rotationFailure("navigation bounds");
                    QTest::keyClick(window,Qt::Key_Escape);
                }
                if (radar != item(window,"resourceRadar") || radar->property("viewPose").toMap() != pose || workspace.filterText() != "alpha"
                    || workspace.resourceCount() != 1 || server.requests.size() != calls) return rotationFailure("retained state");
                if (!evidence.isEmpty() && !window->grabWindow().save(evidence + "/landscape-" + QString::number(size.width()) + "x" + QString::number(size.height()) + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
            }
            return true;
        }
        if (scenario == "shell_touch_search_tooltip") {
            window->resize(640, 320);
            auto* resources = item(window, "resourcesWorkspaceButton");
            auto* search = item(window, "workspaceSearchButton");
            auto* input = item(window, "resourceFilter");
            const auto tooltipVisible = [&](const QString& label) {
                QList<QAccessibleInterface*> pending{QAccessible::queryAccessibleInterface(window)};
                for (qsizetype index = 0; index < pending.size() && index < 10000; ++index) {
                    auto* entry = pending[index];
                    if (!entry || !entry->isValid() || entry->state().invisible) continue;
                    if (entry->role() == QAccessible::ToolTip && entry->text(QAccessible::Name) == label && !entry->rect().isEmpty()) return true;
                    for (int child = 0; child < entry->childCount(); ++child) pending.append(entry->child(child));
                }
                return false;
            };
            const auto failed = [&](const char* stage) {
                qWarning() << "Touch search tooltip failure:" << stage
                    << "focus" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{})
                    << "search checked" << search->property("checked")
                    << "Resources tooltip" << tooltipVisible(resources->property("text").toString());
                const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
                if (!evidence.isEmpty()) window->grabWindow().save(evidence + "/shell-touch-search-tooltip-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png");
                return false;
            };
            if (!resources || !search || !input || !waitFor([&] {
                return item(window, "workspaceToolbar")->isVisible()
                    && displayedWithin(window->contentItem(), resources) && displayedWithin(window->contentItem(), search);
            })) return false;
#ifdef Q_OS_MACOS
            // Qt's macOS buttons are Tab-only. Exercise the Android/Linux touch-focus policy.
            resources->setFocusPolicy(Qt::StrongFocus);
#endif
            QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2));
            search->forceActiveFocus(Qt::TabFocusReason);
            if (!search->hasActiveFocus() || !waitFor([&] { return tooltipVisible(search->property("text").toString()); }))
                return failed("keyboard-focused Search tooltip unavailable");
            std::fputs("Keyboard-focused Search exposes its visible tooltip.\n", stdout);
            std::unique_ptr<QPointingDevice> touch(QTest::createTouchDevice());
            const auto tap = [&](QQuickItem* control) {
                const auto center = control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint();
                QTest::touchEvent(window, touch.get()).press(0, center, window).commit();
                QTest::touchEvent(window, touch.get()).release(0, center, window).commit();
            };
            tap(resources);
            if (!waitFor([&] { return resources->hasActiveFocus() && workspace.workspacePage() == "resources"; }))
                return failed("Resources touch did not select and focus Resources");
            QQmlExpression delay(qmlContext(resources), resources, QStringLiteral("ToolTip.delay"));
            const int tooltipDelay = delay.evaluate().toInt();
            if (delay.hasError()) return failed("tooltip delay unavailable");
            QTest::qWait(qMax(0, tooltipDelay) + 1);
            QSignalSpy frame(window, &QQuickWindow::frameSwapped);
            window->update();
            if (!frame.wait(1000)) return failed("Resources touch frame unavailable");
            tap(search);
            if (!waitFor([&] { return input->isVisible() && input->hasActiveFocus() && search->property("checked").toBool(); }))
                return failed("Search touch after Resources did not open and focus the filter");
            return server.requests.size() == calls;
        }
        if (scenario == "shell_search_map_queued") {
            const auto failed = [](const char* stage) { qWarning() << "Map search stage:" << stage; return false; };
            if (!click(window, item(window, "mapWorkspaceButton"))
                || !waitFor([&] { return workspace.workspacePage() == "map" && !workspace.mapOverview(); })) return failed("map search setup");
            auto* input = item(window, "resourceFilter");
            auto* toggle = item(window, "workspaceSearchButton");
            if (!input || input->isVisible() || !toggle) return failed("map search starts hidden");
            const auto bindings = QKeySequence::keyBindings(QKeySequence::Find);
            if (bindings.isEmpty()) return failed("platform Find binding");
            bool closeQueued = false;
            bool focusedBeforeClose = false;
            QRectF queuedBounds;
            class QueuedPointerClick final : public QObject {
            public:
                QQuickWindow* target = nullptr;
                QPoint position;
                bool event(QEvent* incoming) override {
                    if (incoming->type() != QEvent::User) return QObject::event(incoming);
                    QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, position);
                    return true;
                }
            } closePointer;
            closePointer.target = window;
            QSignalSpy closedByPointer(toggle, SIGNAL(clicked()));
            const auto focused = QObject::connect(input, &QQuickItem::activeFocusChanged, window, [&] {
                if (input->hasActiveFocus() && closedByPointer.isEmpty()) focusedBeforeClose = true;
            });
            const auto revealed = QObject::connect(input, &QQuickItem::visibleChanged, window, [&] {
                if (!input->isVisible() || closeQueued) return;
                closeQueued = true;
                queuedBounds = podlord::test::visibleBounds(window, toggle);
                closePointer.position = queuedBounds.center().toPoint();
                // Schedule the complete public QtTest click ahead of normal
                // deferred focus, without reentering the visibility binding.
                QCoreApplication::postEvent(&closePointer, new QEvent(QEvent::User), Qt::HighEventPriority);
            });
            QTest::keySequence(window, bindings.first());
            QObject::disconnect(revealed);
            QCoreApplication::sendPostedEvents();
            QObject::disconnect(focused);
            if (!closeQueued || focusedBeforeClose || closedByPointer.size() != 1 || input->isVisible() || toggle->property("checked").toBool()) {
                qWarning() << "Queued Map search state: closeQueued=" << closeQueued << "clicked=" << closedByPointer.size()
                    << "focusedBeforeClose=" << focusedBeforeClose
                    << "inputVisible=" << input->isVisible() << "inputFocused=" << input->hasActiveFocus()
                    << "toggleChecked=" << toggle->property("checked").toBool() << "toggleFocused=" << toggle->hasActiveFocus()
                    << "toggleBounds=" << podlord::test::visibleBounds(window, toggle)
                    << "queuedBounds=" << queuedBounds << "queuedPointInToggle=" << toggle->mapFromScene(queuedBounds.center())
                    << "focus=" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{})
                    << "requests=" << server.requests.size() << "baseline=" << calls;
                return failed("queued pointer closes Find before focus delivery");
            }
            return waitFor([&] { return !input->isVisible() && !input->hasActiveFocus() && toggle->hasActiveFocus(); })
                && workspace.resourceCount() == 3 && server.requests.size() == calls;
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
                    || item(window, "eventFilter")->isVisible() || item(window, "eventFilter")->hasActiveFocus() || workspace.eventFilterText() != "Started"
                    || !click(window, item(window, "resourcesWorkspaceButton"))) return false;
            } else if (!click(window, item(window, "workspaceSearchButton"))) return false;
            if (input->isVisible() || input->hasActiveFocus() || workspace.resourceCount() != 1 || !click(window, item(window, "workspaceSearchButton"))
                || !waitFor([&] { return input->hasActiveFocus(); }) || text(window, "resourceFilter") != "\"alpha\"") return false;
            QTest::keyClick(window, Qt::Key_Escape);
            auto* toggle = item(window, "workspaceSearchButton");
            if (input->isVisible() || workspace.resourceCount() != 1 || !toggle->hasActiveFocus()) return false;
            const auto point = podlord::test::visibleBounds(window, toggle).center();
            const QPointF global = window->mapToGlobal(point.toPoint());
            for (int repeat = 0; repeat < 2; ++repeat) {
                QCoreApplication::postEvent(window, new QMouseEvent(QEvent::MouseButtonPress, point, point, global,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
                QCoreApplication::postEvent(window, new QMouseEvent(QEvent::MouseButtonRelease, point, point, global,
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
            }
            QCoreApplication::sendPostedEvents(window);
            QCoreApplication::sendPostedEvents();
            return waitFor([&] { return !input->isVisible() && !input->hasActiveFocus() && !toggle->property("checked").toBool(); })
                && workspace.resourceCount() == 1 && server.requests.size() == calls;
        }
        if (scenario == "shell_health_refresh") {
            auto* health = item(window, "sessionHealthSegments");
            if (!health || !waitFor([&] { return qAbs(health->height() - health->parentItem()->height()) < 1; })) return false;
            if (!workspace.refresh()) return false;
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
            const auto context = workspace.sessionConfiguration(session).value("contextId").toString();
            if (context.isEmpty()) return false;
            if (scenario == "shell_quick_reopen" && (!workspace.close(session) || !waitFor([&] { return !workspace.busy(); }))) return false;
            if (!click(window, item(window, "quickOpenDropdown"))) return false;
            const auto target = "quickContext_" + (scenario == "shell_quick_context" ? workspace.contexts().first().toMap()["id"].toString() : context);
            if (!waitFor([&] { return item(window, target) && item(window, target)->isVisible(); })
                || !click(window, item(window, target)) || !waitFor([&] {
                    return !workspace.busy() && !workspace.loading() && !workspace.currentSession().isEmpty();
                })) return false;
            const auto active = workspace.currentSession();
            if (server.requests.size() != calls) qWarning() << "Quick context reopen requests:" << calls << server.requests;
            return (scenario == "shell_quick_reopen"
                ? active != session && workspace.sessionConfiguration(active).value("contextId").toString() == context
                : active == session) && server.requests.size() == calls;
        }
        if (scenario == "shell_quick_narrow") {
            window->resize(320, 720);
            if (!waitFor([&] {
                for (const auto& name : {"quickOpenDropdown"}) {
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
            if (item(window,"refreshButton") || item(window,"commandPaletteButton")) return false;
            for (const auto size : {QSize(1440,920), QSize(320,640)}) {
                window->resize(size);
                if (!click(window,item(window,"workspaceActionsButton"))) return false;
                if (!waitFor([&] {
                    const auto* find = item(window,"resourceFindButton");
                    const auto* columns = item(window,"resourceColumnsButton");
                    return find && columns && find->isVisible() && columns->isVisible()
                        && displayedWithin(window->contentItem(),const_cast<QQuickItem*>(find))
                        && displayedWithin(window->contentItem(),const_cast<QQuickItem*>(columns));
                })) return false;
                QTest::keyClick(window,Qt::Key_Escape);
            }
            return server.requests.size()==calls;
        }
        if (scenario == "shell_footer_narrow") {
            const auto context = workspace.sessionConfiguration(workspace.currentSession()).value("contextId").toString();
            if (!workspace.renameSourceContext(context, QString(96, 'x')) || !waitFor([&] { return !workspace.busy(); })) return false;
            for (const auto size : {QSize(320,360), QSize(390,720), QSize(1440,920)}) {
                window->resize(size);
                if (!waitFor([&] {
                    auto* footer = item(window, "workspaceFooter");
                    if (!footer || qAbs(footer->height()-24) > 0.5) return false;
                    const bool shown = size.width() >= 600 && size.height() >= 600;
                    if (footer->isVisible() != shown) return false;
                    const QRectF scene(0,0,window->width(),window->height());
                    for (const auto* name : {"resourceMatchCount", "syncStatus"}) {
                        auto* label = item(window, name);
                        if (!label || (shown && (label->height() > 24 || !scene.adjusted(-0.5,-0.5,0.5,0.5).contains(label->mapRectToScene(label->boundingRect()))))) return false;
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
            for (const auto* name : {"eventsWorkspaceButton", "portForwardTasksButton"}) {
                auto* control = item(window,name);
                if (control && control->isVisible()) return false;
            }
            for (const auto* name : {"workspaceSearchButton", "resourcesWorkspaceButton", "settingsWorkspaceButton", "workspaceActionsButton", "toggleSidebar"}) {
                if (!podlord::test::revealWorkspaceAction(window,name)) return false;
                auto* control = item(window, name);
                if (!control || !waitFor([&] { return control->width() >= 44 && control->height() >= 44; })) return false;
                const auto bounds = control->mapRectToScene(control->boundingRect());
                if (bounds.left() < 0 || bounds.right() > 320.5) return false;
                if (control->metaObject()->indexOfSignal("triggered()") < 0) {
                    for (const auto previous : controls) if (bounds.intersects(previous)) return false;
                    controls.append(bounds);
                }
                QTest::keyClick(window,Qt::Key_Escape);
            }
            const auto evidence = qEnvironmentVariable("PODLORD_UI_EVIDENCE_DIR");
            if (!evidence.isEmpty()) {
                QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
                if (!frames.wait(1000) || !window->grabWindow().save(evidence + "/shell-phone-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png")) return false;
            }
            if (!click(window, item(window, "toggleSidebar")) || !waitFor([&] { return radar->isVisible(); })) return false;
            for (const auto* name : {"radarZoomOut", "radarZoom"}) {
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
            return !item(window,"portForwardTasksButton")->isVisible() && workspace.workspacePage()=="events"
                && radar->isVisible() && server.requests.size()==calls;
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
        if (scenario=="shell_metric_missing") return waitFor([&] {
            auto* pods = podlord::test::visibleItem(window->contentItem(),"pulse_pods");
            auto* nodes = podlord::test::visibleItem(window->contentItem(),"pulse_nodes");
            return !podlord::test::visibleItem(window->contentItem(),"pulse_cpu")
                && !podlord::test::visibleItem(window->contentItem(),"pulse_memory")
                && !podlord::test::visibleItem(window->contentItem(),"pulse_storage")
                && pods && !nodes && text(window,"pulse_pods_usage")=="2";
        }) && server.requests.size()==calls;
        return false;
    }
    if (scenario == "compact_toolbar") {
        const auto originalSize=window->size();
        if (QGuiApplication::platformName()!="ios" && QGuiApplication::platformName()!="android") {
            for (const auto width : {1440,1920}) {
                window->resize(width,originalSize.height());
                if (!waitFor([&] {
                    auto* header=item(window,"workspaceToolbar");
                    auto* metrics=item(window,"headerMetrics");
                    auto* last=item(window,"pulse_pods");
                    if (!header || !metrics || !last) return false;
                    auto right=header->width()-6;
                    for (const auto* name:{"toggleSidebar","closeCurrentSession"}) {
                        const auto* action=item(window,name);
                        if (action && action->isVisible()) right=std::min(right,action->mapToItem(header,QPointF{}).x()-6);
                    }
                    return qAbs(metrics->mapToItem(header,QPointF(metrics->width(),0)).x()-right)<=1
                        && qAbs(last->mapToItem(header,QPointF(last->width(),0)).x()-right)<=1;
                })) {
                    std::fprintf(stderr,"Header metric group is not right-aligned at width %d.\n",width);
                    return false;
                }
            }
            window->resize(originalSize);
        }
        if (!click(window,item(window,"workspaceSearchButton"))) return false;
        return waitFor([&] {
            const auto* tabs=item(window,"sessionTabs");
            const auto* toolbar=item(window,"workspaceToolbar");
            const auto* filter=item(window,"resourceFilter");
            if (!toolbar || !filter) return false;
            const auto gap=filter->mapToScene(QPointF{}).y()-toolbar->mapToScene(QPointF(0,toolbar->height())).y();
            return (!tabs || !tabs->isVisible()) && filter->isVisible() && gap>=0 && gap<=8;
        });
    }
    if (scenario.startsWith("problems_reference_")) {
        if (!waitFor([&] { return workspace.totalResourceCount()==3 && !workspace.loading(); })) return false;
        const auto calls=server.requests.size();
        if (!workspace.setFilterMode("problems")) return false;
        const bool problem=scenario.endsWith("unready") || scenario.endsWith("failed") || scenario.endsWith("pending") || scenario.endsWith("terminating");
        return workspace.resourceCount()==(problem ? 1 : 0) && (!problem || workspace.table()->data(workspace.table()->index(0,0),Qt::UserRole+1)=="target") && server.requests.size()==calls;
    }
    if (scenario == "repeated_page") return waitFor([&] { return text(window, "syncProblemMessage").contains("repeated a resource page"); });
    if (scenario == "rate_limit") return waitFor([&] { return text(window, "syncProblemMessage").contains("limiting requests"); });
    if (scenario == "priority") {
        if (!waitFor([&] { const auto* cell = item(window, "cell_0_0"); return workspace.table()->rowCount() > 0 && cell && cell->width() > 0 && cell->height() > 0; })) return false;
        if (!click(window, item(window, "cell_0_0")) || !waitFor([&] { return workspace.inspected().contains("fetchedAt"); })) {
            std::fprintf(stderr, "Priority inspector did not load; requests: %s\n", qPrintable(server.requests.join('\n'))); return false;
        }
        if (!waitFor([&] { return !workspace.loading(); })) return false;
        const auto discovered = server.requests.indexOf("/apis/example.test/v1");
        const auto inspected = server.requests.indexOf("/api/v1/namespaces/default/pods/alpha");
        if (inspected < 0 || discovered <= inspected) {
            std::fprintf(stderr, "Inspector should precede queued discovery. Requests: %s\n", qPrintable(server.requests.join('\n')));
            return false;
        }
        return true;
    }
    const int expectedRows = scenario.startsWith("radar_water_") ? 4 : scenario.startsWith("workload_") ? 1 : scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_initial_population" || scenario == "radar_context_identity" || scenario == "radar_reopen_position" || scenario.startsWith("radar_pooled_") || scenario.startsWith("radar_accessibility_") || scenario == "columns_pin_all" ? 1001 : scenario == "inspector_related_workspace" || scenario.startsWith("inspector_related_event_") || scenario.startsWith("inspector_related_table_events_") || scenario.startsWith("filter_event_") || scenario.startsWith("view_restore_event_") || scenario=="columns_isolation" || scenario=="shell_sidebar" || scenario == "inspector_related_alias_distinct" || scenario == "inspector_related_alias_reuse" ? 5 : scenario.startsWith("table_") || scenario == "inspector_custom_secret" || scenario.startsWith("inspector_related_") ? 4 : 3;
    if (!waitFor([&] { return item(window, "resourceTable")->property("rows").toInt() == std::min(expectedRows, workspace.resourceLimit())
        && workspace.resourceCount() == expectedRows && !workspace.loading(); }, 10000)) {
        std::fprintf(stderr, "Resource load failed: %s %s\n", qPrintable(text(window, "errorMessage")), qPrintable(text(window, "syncProblemMessage"))); return false;
    }
    if (scenario.startsWith("dock_")) {
        const bool compact = scenario=="dock_narrow" || scenario=="dock_compact";
        const bool systemWindow=QGuiApplication::platformName()=="ios" || QGuiApplication::platformName()=="android";
        if (!systemWindow) window->resize(compact ? 390 : 1440,900);
        if (!workspace.filter("alpha") || !waitFor([&] { return workspace.table()->rowCount()==1 && item(window,"cell_0_0"); })
            || !click(window,item(window,"cell_0_0")) || !waitFor([&] { return workspace.canEditYaml() && !workspace.loading(); })) {
            std::fprintf(stderr,"Cannot open filtered row: window=%dx%d rows=%d inspector=%s error=%s\n",window->width(),window->height(),workspace.table()->rowCount(),qPrintable(workspace.inspectorPath()),qPrintable(workspace.error()));
            const auto evidence=qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
            if (!evidence.isEmpty()) { QDir().mkpath(evidence); window->grabWindow().save(evidence+"/inspector-open-failure.png"); }
            return false;
        }
        QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update(); if (!frames.wait(2000)) { std::fputs("Inspector frame unavailable.\n",stderr); return false; }
        auto* content=item(window,"workspaceContent");
        auto* heading=item(window,"inspectorResourceName");
        if (!content || !heading) return false;
        const auto bounds=[](const QQuickItem* target) { return QRectF(target->mapToScene({0,0}),target->size()); };
        const auto before=bounds(content);
        if (compact) {
            const auto session=workspace.currentSession();
            const auto pose=workspace.radarView();
            const auto calls=server.requests.size();
            const auto evidence=qEnvironmentVariable("PODLORD_SHELL_EVIDENCE");
            const auto failed=[&](const char* stage) {
                std::fprintf(stderr,"Compact inspector failed: %s, window=%dx%d content-visible=%d\n",stage,window->width(),window->height(),content->isVisible());
                if (!evidence.isEmpty()) { QDir().mkpath(evidence); window->grabWindow().save(evidence+"/inspector-failure-"+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png"); }
                return false;
            };
            if (!waitFor([&] { return !content->isVisible(); })) return failed("take over workspace");
            if (scenario=="dock_compact") {
                if (!click(window,item(window,"yamlButton")) || !click(window,item(window,"editYaml"))) return failed("begin YAML edit");
                auto* editor=item(window,"inspectorYaml");
                editor->forceActiveFocus(); QTest::keySequence(window,QKeySequence::MoveToEndOfDocument); QTest::keyClick(window,Qt::Key_Return);
                for (const auto c : QString("# compact draft")) QTest::keyClick(window,c.toLatin1());
                const auto draft=text(window,"inspectorYaml");
                if (!draft.contains("# compact draft")) return failed("enter draft");
                const auto sizes=systemWindow ? QList<QSize>{window->size()} : QList<QSize>{{390,780},{320,640},{780,390},{640,360},{1440,920},{390,780}};
                for (const auto size : sizes) {
                    if (!systemWindow) window->resize(size);
                    const bool combined=size.width()>=600 && size.height()>=600;
                    if (!waitFor([&] {
                        auto* scroll=item(window,"yamlScroll");
                        auto* viewport=scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
                        auto* panel=item(window,"workspaceInspector");
                        const QRectF screen(0,0,window->width(),window->height());
                        return content->isVisible()==combined && viewport && viewport->height()>=editor->property("cursorRectangle").toRectF().height()
                            && panel && screen.contains(bounds(panel)) && text(window,"inspectorYaml")==draft && !editor->property("readOnly").toBool();
                    })) return failed("readable draft after size change");
                    for (const auto* name : {"checkYaml","previewYaml","yamlScroll"})
                        if (!podlord::test::scrollIntoView(window,item(window,name))) return failed("reach draft controls");
                    if (!evidence.isEmpty() && (!QDir().mkpath(evidence) || !window->grabWindow().save(evidence+"/inspector-draft-"+QString::number(size.width())+"x"+QString::number(size.height())+"-"+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png"))) return false;
                }
                if (!click(window,item(window,"closeInspector")) || !waitFor([&] { return item(window,"discardStay")->isVisible(); })
                    || !click(window,item(window,"discardStay")) || text(window,"inspectorYaml")!=draft || content->isVisible()) return failed("retain draft on stay");
                if (!click(window,item(window,"closeInspector")) || !waitFor([&] { return item(window,"discardAccept")->isVisible(); })
                    || !click(window,item(window,"discardAccept"))) return failed("confirm draft discard");
            } else if (!click(window,item(window,"closeInspector"))) return failed("close inspector");
            const bool restored=waitFor([&] { return workspace.inspectorPath().isEmpty() && content->isVisible(); })
                && workspace.currentSession()==session && workspace.filterText()=="alpha" && workspace.table()->rowCount()==1
                && workspace.radarView()==pose && server.requests.size()==calls;
            if (!restored) return failed("restore table, session, filter and radar without requests");
            return true;
        }
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
    if (scenario.startsWith("quick_open_") || scenario == "sources_invalid") {
        const auto firstSession = workspace.currentSession();
        const auto rows = workspace.table()->rowCount();
        const auto calls = server.requests.size();
        auto* toggle = item(window,"quickOpenDropdown");
        if (!toggle || item(window,"sourceManagementPanel")) return false;
        if (scenario == "quick_open_collapsed") return !window->findChild<QObject*>("quickOpenMenu")->property("visible").toBool();
        if (scenario == "sources_invalid") {
            if (!click(window,item(window,"settingsWorkspaceButton")) || !podlord::test::selectSettingsSection(window,"sources")
                || !type(window,item(window,"settingsSourcePath"),temporary.filePath("missing.config"))
                || !click(window,item(window,"settingsImportSource"))) return false;
            return waitFor([&] { return !workspace.busy() && !workspace.sourceImportError().isEmpty(); })
                && workspace.currentSession()==firstSession && workspace.table()->rowCount()==rows && server.requests.size()==calls;
        }
        if (scenario == "quick_open_narrow") window->resize(680,480);
        if (scenario == "quick_open_keyboard") { toggle->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Space); }
        else if (!click(window,toggle)) return false;
        if (!waitFor([&] { return window->findChild<QObject*>("quickOpenMenu")->property("visible").toBool(); })) return false;
        if (scenario == "quick_open_open_context") {
            const auto next = workspace.contexts().last().toMap()["id"].toString();
            if (!click(window,item(window,"quickContext_"+next))) return false;
            return waitFor([&] { return workspace.currentSession()!=firstSession && !workspace.busy() && workspace.table()->rowCount()>0; });
        }
        const int repeats = scenario == "quick_open_repeat" ? 3 : 1;
        for (int index=0; index<repeats; ++index) {
            QTest::keyClick(window,Qt::Key_Escape);
            if (!waitFor([&] { return !window->findChild<QObject*>("quickOpenMenu")->property("visible").toBool(); })) return false;
            if (index+1<repeats && !click(window,toggle)) return false;
        }
        return workspace.currentSession()==firstSession && workspace.table()->rowCount()==rows && server.requests.size()==calls;
    }
    if (scenario.startsWith("context_alias_")) {
        renameStage = "prepare rename";
        if (!waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
        const auto id = workspace.currentSession();
        if (scenario == "context_alias_tab_menu") {
            const auto context = workspace.sessionConfiguration(id).value("contextId").toString();
            QString other;
            for (const auto& entry : workspace.contexts()) {
                const auto candidate = entry.toMap().value("id").toString();
                if (candidate != context) { other = candidate; break; }
            }
            if (other.isEmpty() || !click(window,item(window,"quickOpenDropdown"))
                || !click(window,item(window,"quickContext_"+other))
                || !waitFor([&] { return !workspace.busy() && workspace.tabs().size()==2; })
                || !click(window,item(window,"activateSession_"+id))
                || !waitFor([&] { return workspace.currentSession()==id && !workspace.busy() && !workspace.loading(); })) return false;
        }
        const auto contextId = workspace.sessionConfiguration(id).value("contextId").toString();
        auto contextInfo = [&] {
            for (const auto& value : workspace.contexts()) if (value.toMap().value("id").toString() == contextId) return value.toMap();
            return QVariantMap{};
        };
        const auto originalName = contextInfo().value("name").toString();
        if (scenario == "context_alias_filter" && !type(window, item(window, "resourceFilter"), "alpha")) return false;
        QString draft;
        if (scenario == "context_alias_draft") {
            if (!type(window, item(window, "resourceFilter"), "alpha")
                || !waitFor([&] { return item(window, "cell_0_0"); }) || !click(window, item(window, "cell_0_0"))
                || !click(window, item(window, "yamlButton")) || !waitFor([&] { return workspace.canEditYaml(); })
                || !click(window, item(window, "editYaml"))) return false;
            auto* editor = item(window, "inspectorYaml"); editor->forceActiveFocus();
            QTest::keySequence(window, QKeySequence::MoveToEndOfDocument); QTest::keyClick(window, Qt::Key_Return);
            for (const auto ch : QString("# preserve alias draft")) QTest::keyClick(window, ch.toLatin1());
            draft = workspace.yamlText();
        }
        renameStage = "open initial dialog";
        if (scenario == "context_alias_tab_menu") {
            if (!click(window, item(window, "activateSession_" + id), Qt::RightButton)
                || !waitFor([&] { return item(window, "renameTab_" + id) && item(window, "renameTab_" + id)->isVisible(); })
                ) return false;
            const auto* anchor = item(window, "activateSession_" + id);
            const auto* entry = item(window, "renameTab_" + id);
            if (qAbs(entry->mapToScene({0,0}).x()-anchor->mapToScene({0,0}).x()) > 220
                || qAbs(entry->mapToScene({0,0}).y()-anchor->mapToScene({0,0}).y()) > 100
                || !click(window, item(window, "renameTab_" + id))) return false;
        } else if (!click(window, item(window, "renameCurrentContext"))) return false;
        renameStage = "wait initial dialog opened";
        if (!waitFor([&] { auto* dialog = window->findChild<QObject*>("sourceAliasDialog");
                return dialog && dialog->property("opened").toBool() && item(window, "sourceAliasName")
                    && item(window, "sourceAliasName")->isVisible(); })) return false;
        renameStage = "type rename";
        if (scenario == "context_alias_cancel" || scenario == "context_alias_escape") {
            if (!type(window, item(window, "sourceAliasName"), "Changed")) return false;
            if (scenario == "context_alias_cancel") { if (!click(window, item(window, "cancelSourceAlias"))) return false; }
            else QTest::keyClick(window, Qt::Key_Escape);
            return renamePassed = waitFor([&] { return !item(window, "sourceAliasName")->isVisible(); })
                && workspace.title() == originalName && contextInfo().value("name").toString() == originalName;
        }
        if (!type(window, item(window, "sourceAliasName"), "Renamed")) return false;
        const auto calls = server.requests.size();
        renameStage = "submit alias";
        if (scenario == "context_alias_keyboard") { item(window, "sourceAliasName")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return); }
        else if (!click(window, item(window, "saveSourceAlias"))) return false;
        renameStage = "wait alias updated and dialog closed";
        if (!waitFor([&] { return !workspace.busy() && workspace.title() == "Renamed"
            && window->title() == QStringLiteral("Podlord - Renamed") && !item(window, "sourceAliasName")->isVisible(); })) {
            qWarning() << "Context alias did not settle:" << workspace.title() << workspace.error() << workspace.busy()
                << "input" << item(window, "sourceAliasName")->property("text");
            return false;
        }
        renameStage = "verify context alias";
        QTest::qWait(50);
        if (workspace.currentSession() != id || server.requests.size() != calls) return false;
        if (contextInfo().value("name").toString() != "Renamed") return false;
        if (scenario == "context_alias_filter") return renamePassed = workspace.filterText() == "alpha" && text(window, "resourceFilter") == "alpha";
        if (scenario == "context_alias_draft") return renamePassed = workspace.yamlEditing() && workspace.yamlDirty() && workspace.yamlText() == draft;
        if (scenario == "context_alias_restart") {
            window->close(); if (!waitFor([&] { return !window->isVisible(); })) return false;
            podlord::Workspace restored(profile);
            return renamePassed = waitFor([&] { return !restored.busy() && restored.currentSession() == id && restored.title() == "Renamed"; });
        }
        return renamePassed = true;
    }
    if (scenario == "radar_context_identity") {
        const auto first=workspace.currentSession();
        const auto context=workspace.sessionConfiguration(first)["contextId"].toString();
        QPointer<podlord::RadarIsland> radar;
        QString stage="context lookup";
        bool passed=false;
        int cachedCalls=-1;
        const auto diagnostics=qScopeGuard([&] {
            if (passed) return;
            qWarning() << "Context island parity stage:" << stage << "map" << mapRadar
                << "session" << workspace.currentSession() << "context" << context
                << "page" << workspace.workspacePage() << "busy/loading" << workspace.busy() << workspace.loading()
                << "resources/tiles" << workspace.totalResourceCount() << (radar ? radar->tiles()->rowCount() : -1)
                << "remembered pose" << workspace.radarView() << "rendered pose" << (radar ? radar->viewPose() : QVariantMap{})
                << "requests before/after" << cachedCalls << server.requests.size();
            saveShellFrame(window,requestedScenario+"-failure");
        });
        if (context.isEmpty()) return false;
        const auto show=[&] {
            if ((workspace.workspacePage() != (mapRadar ? "map" : "resources") || (mapRadar && workspace.mapOverview()))
                && !click(window,item(window,mapRadar ? "mapWorkspaceButton" : "resourcesWorkspaceButton"))) return false;
            return waitFor([&] {
                radar=window->findChild<podlord::RadarIsland*>("resourceRadar");
                return radar && radar->isVisible() && !workspace.busy() && !workspace.loading() && workspace.totalResourceCount()>=1000;
            },10000);
        };
        const auto points=[&] {
            QMap<QString,QPointF> result;
            if (!radar) return result;
            auto* tiles=radar->tiles(); const auto roles=tiles->roleNames();
            for (int row=0;row<tiles->rowCount();++row) {
                const auto tile=tiles->index(row,0);
                result.insert(tile.data(roles.key("resourcePath")).toString(),
                    QPointF(tile.data(roles.key("worldX")).toDouble(),tile.data(roles.key("worldY")).toDouble()));
            }
            return result;
        };
        stage="show original island";
        if (!show()) return false;
        const QVariantMap firstPose{{"x",100},{"y",-40},{"zoom",.01}};
        stage="original population and camera";
        radar->setViewPose(firstPose);
        if (!waitFor([&] { return radar->tiles()->rowCount()==workspace.totalResourceCount() && workspace.radarView()==firstPose; })) return false;
        stage="preview capture and explicit context tab";
        const auto original=points(); const auto preview=radar->previewImage();
        const auto firstTabCount=workspace.tabs().size();
        if (preview.isNull() || original.size()<1000 || !workspace.openContext(context,false,true)
            || !waitFor([&] { return !workspace.busy() && workspace.tabs().size()==firstTabCount+1
                && workspace.currentSession()!=first; })) return false;
        stage="locate explicit context tab";
        const auto second=workspace.currentSession();
        if (second.isEmpty() || second==first) return false;
        if (workspace.sessionConfiguration(second)["contextId"]!=context || !workspace.activate(second)
            || !waitFor([&] { return workspace.currentSession()==second && !workspace.busy() && !workspace.loading(); },10000)
            || !show()) return false;
        stage="duplicate coordinate and preview parity";
        radar->setViewPose({{"x",0},{"y",0},{"zoom",.01}});
        if (!waitFor([&] { return points()==original; }) || radar->previewImage()!=preview) {
            std::fprintf(stderr,"Same-context sessions changed resource coordinates or preview: first=%s second=%s context=%s\n",
                qPrintable(first),qPrintable(second),qPrintable(context));
            return false;
        }
        const auto calls=server.requests.size();
        cachedCalls=calls;
        stage="rename alias without geometry changes";
        if (!workspace.renameSourceContext(context,"Local island alias") || !waitFor([&] { return !workspace.busy(); })
            || points()!=original || radar->previewImage()!=preview) return false;
        stage="return to original session";
        if (!click(window,item(window,"activateSession_"+first))
            || !waitFor([&] { return workspace.currentSession()==first && !workspace.busy() && !workspace.loading(); })
            || !show()) return false;
        stage="restore camera, geometry and preview without transport";
        return passed=waitFor([&] { return workspace.radarView()==firstPose && radar->viewPose()==firstPose && points()==original; })
            && radar->previewImage()==preview && server.requests.size()==calls;
    }
    if (scenario.startsWith("ui_density_")) {
        const auto calls = server.requests.size();
        if (scenario == "ui_density_alignment") {
            if (item(window,"settingsWorkspaceButton")->property("glyph")!="Settings"
                || item(window,"toggleLandscapeFilters")->property("glyph")!="Filters") return false;
            window->setMinimumSize(QSize(200,200));
            bool aligned = true;
            for (const auto& mode : {QString("dark"),QString("light")}) {
                if (!workspace.saveAppearance("Imperial Ledger",mode) || !waitFor([&] { return !workspace.busy(); })) return false;
                for (const auto size : {QSize(1440,920),QSize(640,320),QSize(320,640)}) {
                    window->resize(size);
                    const bool drawer = size.width() < 600;
                    if (drawer && !click(window,item(window,"toggleSidebar"))) return false;
                    QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
                    if (!waitFor([&] { return !frames.isEmpty(); })) return false;
                    int checked = 0;
                    for (const auto* name : {"workspaceSearchButton","quickOpenDropdown","workspaceActionsButton","toggleSidebar",
                        "renameCurrentSession","masterAudioMute","resetResourceFilters","filterPreset","saveFilterPreset",
                        "reloadFilterPresets","importFilterPresets","renameFilterPreset","deleteFilterPreset",
                        "radarZoomOut","radarZoom","toggleLandscapeFilters"}) {
                        auto* control = item(window,name);
                        if (!control || !control->isVisible() || !control->property("glyph").isValid()
                            || control->property("glyph").toString().isEmpty() || control->property("showText").toBool()) continue;
                        auto* content = control->property("contentItem").value<QQuickItem*>();
                        QQuickItem* glyph = nullptr;
                        if (content) for (auto* child : content->childItems())
                            if (child->isVisible() && child->property("kind") == control->property("glyph")) glyph = child;
                        if (!glyph) return false;
                        const auto center = glyph->mapToItem(control,QPointF(glyph->width()/2,glyph->height()/2));
                        const auto offset = center-QPointF(control->width()/2,control->height()/2);
                        if (qAbs(offset.x())>1 || qAbs(offset.y())>1) {
                            std::fprintf(stderr,"Uncentered icon %s: offset=(%.2f,%.2f), target=%.1fx%.1f, window=%dx%d\n",
                                name,offset.x(),offset.y(),control->width(),control->height(),size.width(),size.height());
                            aligned = false;
                        }
                        ++checked;
                    }
                    if (checked < 3) return false;
                    auto* source = item(window,"sourcesButton");
                    if (source && source->isVisible()) {
                        auto* label = source->property("contentItem").value<QQuickItem*>();
                        if (!label) return false;
                        const auto w = qMin(label->width(),label->property("contentWidth").toDouble());
                        const auto h = qMin(label->height(),label->property("contentHeight").toDouble());
                        const auto horizontal = label->property("horizontalAlignment").toInt();
                        const auto vertical = label->property("verticalAlignment").toInt();
                        const auto x = horizontal==Qt::AlignHCenter ? (label->width()-w)/2 : horizontal==Qt::AlignRight ? label->width()-w : 0;
                        const auto y = vertical==Qt::AlignVCenter ? (label->height()-h)/2 : vertical==Qt::AlignBottom ? label->height()-h : 0;
                        const auto center = label->mapToItem(source,QPointF(x+w/2,y+h/2));
                        if (qAbs(center.x()-source->width()/2)>1 || qAbs(center.y()-source->height()/2)>1) {
                            std::fprintf(stderr,"Uncentered source-button text at %dx%d: center=(%.2f,%.2f), target=%.1fx%.1f\n",
                                size.width(),size.height(),center.x(),center.y(),source->width(),source->height());
                            aligned = false;
                        }
                    }
                    if (!saveShellFrame(window,scenario+"-"+mode+"-"+QString::number(size.width())+"x"+QString::number(size.height()))) return false;
                    if (drawer) QTest::keyClick(window,Qt::Key_Escape);
                }
            }
            return aligned && server.requests.size()==calls;
        }
        if (scenario == "ui_density_radar_adaptive") {
            window->setMinimumSize(QSize(200,200));
            if (!window->property("searchOpen").toBool() && !click(window,item(window,"workspaceSearchButton"))) return false;
            if (!type(window,item(window,"resourceFilter"),"alpha") || !click(window,item(window,"workspaceSearchButton"))) return false;
            auto* radar = item(window,"resourceRadar");
            if (!radar || !radar->isVisible()) return false;
            radar->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Plus); QTest::keyClick(window,Qt::Key_Right);
            if (!waitFor([&] { return !workspace.property("radarView").toMap().isEmpty(); })) return false;
            const auto pose = workspace.property("radarView").toMap();
            const auto session = workspace.currentSession();
            const auto total = workspace.totalResourceCount();
            window->resize(640,320);
            const bool compact = waitFor([&] {
                auto* host = item(window,"workspaceSidebarHost");
                auto* sidebar = item(window,"workspaceSidebar");
                return host && sidebar && !radar->isVisible()
                    && qAbs(host->width()-sidebar->property("collapsedFilterWidth").toDouble())<1
                    && item(window,"toggleLandscapeFilters")->isVisible();
            });
            auto* host = item(window,"workspaceSidebarHost");
            auto* primary = item(window,"workspaceInspectorSplit");
            std::fprintf(stderr,"Compact workspace: table=%.1f sidebar=%.1f radarVisible=%d\n",
                primary ? primary->width() : -1,host ? host->width() : -1,radar->isVisible());
            if (!saveShellFrame(window,scenario+"-compact") || !compact) return false;
            QTest::keyClick(window,Qt::Key_Right);
            if (radar->hasActiveFocus() || radar->property("viewPose").toMap()!=pose) {
                std::fputs("Hidden Radar must not retain keyboard input.\n",stderr); return false;
            }
            if (window->findChild<QObject*>("radarWorkspaceButton")) return false;
            if (!click(window,item(window,"toggleLandscapeFilters")) || !waitFor([&] {
                auto* contextName = item(window,"renameCurrentContext");
                return !radar->isVisible() && contextName && contextName->isVisible() && host->width()>100;
            }) || !saveShellFrame(window,scenario+"-filters")) return false;
            if (!click(window,item(window,"toggleLandscapeFilters")) || !click(window,item(window,"mapWorkspaceButton"))) return false;
            if (!waitFor([&] { return radar->isVisible() && radar->height()>44 && radar->property("viewPose").toMap()==pose; })
                || !saveShellFrame(window,scenario+"-map")) return false;
            if (!click(window,item(window,"resourcesWorkspaceButton")) || !waitFor([&] { return !radar->isVisible(); })) return false;
            window->resize(1440,920);
            return waitFor([&] { return radar->isVisible() && item(window,"resourceRadar")==radar
                && radar->property("viewPose").toMap()==pose && workspace.currentSession()==session
                && workspace.totalResourceCount()==total && workspace.filterText()=="alpha"; })
                && server.requests.size()==calls && saveShellFrame(window,scenario+"-restored");
        }
        if (scenario == "ui_density_single_tab") {
            const auto* tabs = item(window, "sessionTabs");
            if (!(workspace.tabs().size() == 1 && tabs && !tabs->isVisible()
                && item(window, "quickOpenDropdown") && item(window, "quickOpenDropdown")->isVisible()
                && server.requests.size() == calls && saveShellFrame(window, scenario))) return false;
            return click(window, item(window, "quickOpenDropdown"))
                && click(window, item(window, "closeCurrentSession"))
                && waitFor([&] { return workspace.tabs().isEmpty() && workspace.currentSession().isEmpty(); })
                && server.requests.size() == calls;
        }
        if (scenario == "ui_density_filters") {
            auto* reset = item(window,"resetResourceFilters");
            auto* limit = item(window,"resourceLimit");
            if (!reset || !limit || reset->isEnabled()) {
                std::fputs("Reset filters must be disabled for the unchanged default view.\n",stderr);
                return false;
            }
            limit->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Up);
            if (!waitFor([&] { return workspace.resourceLimit()==1025 && !workspace.busy() && reset->isEnabled(); })
                || !click(window,reset)
                || !waitFor([&] { return workspace.resourceLimit()==1024 && !reset->isEnabled(); })) return false;
            const auto originalSize = window->size();
            window->resize(402,778);
            if (!waitFor([&] { auto* toggle=item(window,"toggleSidebar"); return toggle
                && !podlord::test::visibleBounds(window,toggle).isEmpty(); })
                || !click(window,item(window,"toggleSidebar"))) { std::fputs("Row-limit drawer action unavailable.\n",stderr); return false; }
            auto* drawer = window->findChild<QObject*>("sidebarDrawer");
            if (!drawer || !waitFor([&] { return drawer->property("opened").toBool(); })) { std::fputs("Row-limit drawer did not open.\n",stderr); return false; }
            limit = item(window,"resourceLimit");
            if (!limit || !limit->setProperty("locale",QVariant::fromValue(QLocale(QLocale::German,QLocale::Germany)))) { std::fputs("Row-limit locale was not available.\n",stderr); return false; }
            for (const auto value : {QString("1024"),QString("5000")}) {
                if (!workspace.setResourceLimit(value) || !waitFor([&] { return !workspace.busy(); })) { qWarning() << "Row-limit value rejected" << value << workspace.error(); return false; }
                auto* number = limit->property("contentItem").value<QQuickItem*>();
                if (!number || !waitFor([&] {
                    return number->property("contentWidth").toReal() <= number->width()
                        - number->property("leftPadding").toReal() - number->property("rightPadding").toReal() + 0.5;
                })) {
                    qWarning() << "Resource row limit is clipped:" << value << "control" << limit->width()
                        << "text" << (number ? number->property("contentWidth").toReal() : 0)
                        << "editor" << (number ? number->width() : 0);
                    saveShellFrame(window,scenario+"-limit-clipped");
                    return false;
                }
            }
            QTest::keyClick(window,Qt::Key_Escape);
            if (!waitFor([&] { return !drawer->property("visible").toBool(); })) { std::fputs("Row-limit drawer did not close.\n",stderr); return false; }
            QSignalSpy restoredFrames(window,&QQuickWindow::frameSwapped);
            window->resize(originalSize);
            window->update();
            if (!waitFor([&] { return !restoredFrames.isEmpty(); })) return false;
            if (!workspace.setResourceLimit("1024") || !waitFor([&] { return !workspace.busy(); })) { std::fputs("Row-limit default restoration failed.\n",stderr); return false; }
            if (!click(window, item(window, "sidebarField_cluster"))) { std::fputs("Cluster field is unavailable after restoring the window.\n",stderr); return false; }
            auto* editor = item(window, "fieldFiltersInline");
            auto* field = item(window, "sidebarField_cluster");
            if (!editor || !field || !waitFor([&] { return editor->isVisible()
                && editor->mapToScene(QPointF{}).y() >= field->mapToScene(QPointF(0, field->height())).y(); })
                || window->findChild<QObject*>("fieldFiltersPopup") || item(window, "refreshFieldFilterValues")) { std::fputs("Inline cluster field did not open.\n",stderr); return false; }
            if (!saveShellFrame(window, scenario + "-open")
                || !type(window, item(window, "fieldFilterExpression"), "__absent__")
                || !waitFor([&] { return workspace.resourceCount() == 0; })
                || !click(window, field) || !waitFor([&] { return !item(window, "fieldFiltersInline"); })
                || !click(window, field)) { std::fputs("Inline cluster filter could not roundtrip.\n",stderr); return false; }
            const auto* input = item(window, "fieldFilterExpression");
            const bool retained = input && input->property("text").toString() == "__absent__"
                && server.requests.size() == calls && saveShellFrame(window, scenario);
            if (!retained) qWarning() << "Inline cluster filter changed after reopening:" << (input ? input->property("text").toString() : QString{})
                << "requests" << calls << server.requests.size();
            return retained;
        }
        if (scenario == "ui_density_tabs") {
            if (!click(window,item(window,"cell_0_0")) || !waitFor([&] { return !workspace.inspectorPath().isEmpty(); })) return false;
            for (const auto width:{320,390,1440}) {
                window->resize(width,844);
                if (!waitFor([&] {
                    int visible=0;
                    for (const auto* name:{"overviewButton","yamlButton","inspectorEventsButton","inspectorLinksButton","valuesButton","logsButton","terminalButton"}) {
                        const auto* tab=item(window,name);
                        if (!tab || !tab->isVisible()) continue;
                        ++visible;
                        const auto bounds=tab->mapRectToScene(tab->boundingRect());
                        if (tab->width()<44 || podlord::test::visibleBounds(window,const_cast<QQuickItem*>(tab)).width()+1<bounds.width()) return false;
                    }
                    return visible>=5;
                })) {
                    for (const auto* name:{"overviewButton","yamlButton","inspectorEventsButton","inspectorLinksButton","valuesButton","logsButton","terminalButton"}) {
                        auto* tab=item(window,name);
                        if (tab && tab->isVisible()) qWarning() << "Tab bounds" << name << tab->size() << tab->mapToScene(QPointF{}) << podlord::test::visibleBounds(window,tab);
                    }
                    saveShellFrame(window,scenario+"-failure"); return false;
                }
            }
            return saveShellFrame(window,scenario);
        }
        if (scenario == "ui_density_close") {
            QTest::keySequence(window, QKeySequence(Qt::CTRL | Qt::Key_K));
            if (!waitFor([&] { auto* close = item(window, "commandPaletteClose"); return close && close->isVisible(); })) return false;
            const auto* close = item(window, "commandPaletteClose");
            const auto* search = item(window, "commandPaletteSearch");
            if (!close || !search || close->property("glyph").toString() != "Close"
                || close->mapToScene(QPointF{}).y() >= search->mapToScene(QPointF{}).y()) return false;
            return saveShellFrame(window, scenario) && click(window, const_cast<QQuickItem*>(close)) && server.requests.size() == calls;
        }
        return false;
    }
    if (scenario == "session_tabs_controls") {
        if (!item(window,"sessionTabs") || item(window,"sessionTabs")->isVisible()) return false;
        const auto first=workspace.currentSession();
        for (const auto& value:workspace.contexts()) {
            if (!workspace.openContext(value.toMap()["id"].toString())
                || !waitFor([&] { return !workspace.busy() && !workspace.loading(); })) return false;
            if (workspace.currentSession()!=first) break;
        }
        const auto second=workspace.currentSession();
        if (first==second || workspace.tabs().size()!=2) return false;
        QStringList order;
        for (const auto& tab:workspace.tabs()) order.append(tab.toMap()["id"].toString());
        const auto calls=server.requests.size();
        auto* firstTab=item(window,"activateSession_"+first);
        auto* secondTab=item(window,"activateSession_"+second);
        if (!firstTab || !secondTab || qAbs(secondTab->mapToScene(QPointF{}).x()-firstTab->mapToScene(QPointF(firstTab->width(),0)).x())>1) {
            std::fputs("Session tabs do not form one contiguous strip.\n",stderr); return false;
        }
        for (auto* tab:{firstTab,secondTab}) {
            auto* accessible=tab ? QAccessible::queryAccessibleInterface(tab) : nullptr;
            if (!accessible || accessible->role()!=QAccessible::PageTab || accessible->text(QAccessible::Name).isEmpty()) {
                std::fputs("Session entry is not an accessible page tab.\n",stderr); return false;
            }
        }
        firstTab->forceActiveFocus(Qt::TabFocusReason);
        for (const auto& action:QList<QPair<Qt::Key,QString>>{{Qt::Key_Home,first},{Qt::Key_End,second},{Qt::Key_Left,first},{Qt::Key_Right,second}}) {
            QTest::keyClick(window,action.first);
            if (!waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.currentSession()==action.second; })) return false;
            auto* active=item(window,"activateSession_"+action.second);
            if (!active || !active->property("checked").toBool()) return false;
        }
        const auto originalSize=window->size();
        window->resize(390,844);
        QTest::qWait(30);
        item(window,"activateSession_"+first)->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window,Qt::Key_End);
        if (!waitFor([&] {
            const auto* active=item(window,"activateSession_"+second);
            const auto* close=item(window,"closeSession_"+second);
            return active && close && workspace.currentSession()==second
                && QRectF(0,0,window->width(),window->height()).contains(active->mapRectToScene(QRectF(QPointF{},active->size())))
                && close->width()>=44 && close->height()>=44;
        })) { saveShellFrame(window,scenario+"-phone-failure"); std::fputs("Selected session tab or touch action is clipped or undersized.\n",stderr); return false; }
        if (!saveShellFrame(window,scenario+"-phone")) return false;
        window->resize(originalSize);
        QTest::qWait(30);
        QStringList remainingOrder;
        for (const auto& tab:workspace.tabs()) remainingOrder.append(tab.toMap()["id"].toString());
        if (!saveShellFrame(window,scenario) || remainingOrder!=order || server.requests.size()!=calls
            || !click(window,item(window,"closeSession_"+first))) return false;
        return waitFor([&] { return !workspace.busy() && workspace.tabs().size()==1 && !item(window,"sessionTabs")->isVisible(); })
            && workspace.currentSession()==second && server.requests.size()==calls;
    }
    if (scenario == "context_chooser_tabs") {
        const auto originalTab = workspace.currentSession();
        const auto contextId = workspace.sessionConfiguration(originalTab).value("contextId").toString();
        const auto originalContextName = workspace.title();
        if (contextId.isEmpty() || !click(window,item(window,"quickOpenDropdown"))) return false;
        auto* quickMenu = window->findChild<QObject*>("quickOpenMenu");
        if (!quickMenu || !waitFor([&] { return quickMenu->property("visible").toBool()
            && item(window,"quickContext_"+contextId) && item(window,"quickContext_"+contextId)->isVisible(); })
            || item(window,"quickSession_"+originalTab) || item(window,"quickWindowSession_"+originalTab)) return false;
        if (!window->findChild<QObject*>("quickOpenTabMenu")
            || quickMenu->property("count").toInt() > workspace.contexts().size() + 5
            || !saveShellFrame(window,scenario+"-menu")) return false;
        QTest::keyClick(window,Qt::Key_Escape);
        if (!click(window,item(window,"settingsWorkspaceButton"))
            || !podlord::test::selectSettingsSection(window,"sources")
            || item(window,"settingsManageSessions") || window->findChild<QObject*>("sessionManagementDialog")) return false;
        const int beforeTabs = workspace.tabs().size();
        const auto chooserSize = window->size();
        window->resize(390,844);
        if (!podlord::test::revealWorkspaceAction(window,"quickOpenDropdown")
            || !click(window,item(window,"quickOpenDropdown"))
            || !click(window,item(window,"quickOpenTab"))
            || !waitFor([&] { auto* menu = window->findChild<QObject*>("quickOpenTabMenu");
                return menu && menu->property("opened").toBool(); })
            || !waitFor([&] {
                const auto* entry = item(window,"quickContextNewTab_"+contextId);
                return entry && entry->height() >= 44 && QRectF(0,0,window->width(),window->height())
                    .contains(entry->mapRectToScene(entry->boundingRect()));
            }) || !saveShellFrame(window,scenario+"-phone-menu")
            || !click(window,item(window,"quickContextNewTab_"+contextId))
            || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.tabs().size()==beforeTabs+1; })) return false;
        const auto newTab = workspace.currentSession();
        if (newTab.isEmpty() || newTab==originalTab || workspace.sessionConfiguration(newTab).value("contextId").toString()!=contextId
            || workspace.title()!=originalContextName) return false;
        window->resize(chooserSize);
        if (!waitFor([&] { auto* rename = item(window,"renameCurrentContext"); return rename && rename->isVisible(); })) return false;
        const auto tabWithId = [&](const QString& id) {
            for (const auto& tab : workspace.tabs()) if (tab.toMap().value("id").toString()==id) return tab.toMap();
            return QVariantMap{};
        };
        if (tabWithId(newTab).value("name").toString()!=originalContextName
            || !click(window,item(window,"renameCurrentContext"))
            || !type(window,item(window,"sourceAliasName"),"Chooser alias")
            || !click(window,item(window,"saveSourceAlias"))
            || !waitFor([&] { return !workspace.busy() && workspace.title()=="Chooser alias"; })) return false;
        return saveShellFrame(window,scenario) && workspace.currentSession()==newTab && !tabWithId(newTab).isEmpty()
            && tabWithId(newTab).value("name").toString()=="Chooser alias"
            && tabWithId(originalTab).value("id").toString()==originalTab;
    }
    if (scenario == "view_restore_scope") {
        const auto first = workspace.currentSession();
        if (!type(window, item(window, "resourceFilter"), "alpha")) return false;
        bool drained = false;
        if (!waitFor([&] { return drained || (drained = workspace.reloadSavedViews()); })
            || !waitFor([&] { return !workspace.busy(); })) return false;
        const auto contextId = workspace.sessionConfiguration(first).value("contextId").toString();
        window->close(); if (!waitFor([&] { return !window->isVisible(); })) return false;
        const auto path = profile + "/views/" + podlord::contextViewName(contextId);
        QFile broken(path); if (!broken.open(QIODevice::WriteOnly) || broken.write("{") != 1) return false; broken.close();
        podlord::Workspace restored(profile); QQmlApplicationEngine secondEngine;
        secondEngine.rootContext()->setContextProperty("workspace", &restored);
        secondEngine.load(QUrl("qrc:/podlord/Main.qml"));
        if (secondEngine.rootObjects().isEmpty()) return false;
        auto* second = qobject_cast<QQuickWindow*>(secondEngine.rootObjects().first());
        if (!second || !waitFor([&] { return !restored.busy() && restored.viewStateFailed(); })) return false;
        const auto firstContext = restored.sessionConfiguration(first).value("contextId").toString();
        QString nextContext;
        for (const auto& context : restored.contexts()) {
            const auto id = context.toMap().value("id").toString();
            if (id != firstContext) { nextContext = id; break; }
        }
        if (nextContext.isEmpty() || !click(second,item(second,"quickOpenDropdown"))
            || !click(second,item(second,"quickContext_" + nextContext))
            || !waitFor([&] { return !restored.busy() && restored.currentSession() != first && !restored.currentSession().isEmpty(); })
            || !type(second, item(second, "resourceFilter"), "bravo")) return false;
        podlord::TableSchemas schemas;
        for (const auto& entry : QList<QPair<QString, QAbstractItemModel*>>{{"resource", restored.table()}, {"event", restored.eventTable()},
            {"port", restored.portTable()}, {"inspectorEvent", restored.inspectorEventTable()}, {"inspectorLink", restored.inspectorLinkTable()}, {"value", restored.valuesTable()}}) {
            QStringList ids;
            for (int column = 0; column < entry.second->columnCount(); ++column) ids.append(entry.second->headerData(column, Qt::Horizontal, Qt::UserRole).toString());
            schemas[entry.first] = ids;
        }
        const auto id = restored.sessionConfiguration(restored.currentSession()).value("contextId").toString();
        const bool saved = waitFor([&] {
            const auto result = podlord::ViewStateStore(profile, schemas).load(id);
            const auto* value = std::get_if<podlord::TableViewStates>(&result);
            return value && value->value("resource").filter == "bravo";
        });
        if (!broken.open(QIODevice::ReadOnly)) return false;
        return saved && broken.readAll() == "{" && restored.viewStateFailed() && restored.filterText() == "bravo";
    }
    if (scenario == "view_save_quiet") {
        // Reload admission is the public barrier for draining pending writes.
        bool initialReload = false;
        if (!waitFor([&] { return initialReload || (initialReload = workspace.reloadSavedViews()); })
            || !waitFor([&] { return !workspace.busy(); })) return false;
        const auto calls = server.requests.size();
        if (!workspace.filter("alpha")) return false;
        QSignalSpy changes(&workspace, &podlord::Workspace::changed);
        QSignalSpy readiness(&workspace, &podlord::Workspace::sessionWindowActionsReadyChanged);
        int completionChanges = -1;
        int completionReadiness = -1;
        bool reloaded = false;
        if (!waitFor([&] {
            if (reloaded) return true;
            const int beforeReload = changes.size();
            const int beforeReadiness = readiness.size();
            if (!workspace.reloadSavedViews()) return false;
            reloaded = true;
            completionChanges = beforeReload;
            completionReadiness = beforeReadiness;
            return true;
        }) || !waitFor([&] { return !workspace.busy(); })) return false;
        if (completionChanges != 0)
            std::fprintf(stderr, "Successful view save invalidated unchanged UI %d times.\n", completionChanges);
        return completionChanges == 0 && completionReadiness > 0 && workspace.filterText() == "alpha"
            && !workspace.viewStateFailed() && server.requests.size() == calls;
    }
    if (scenario.startsWith("view_restore_")) {
        if (!waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
        const auto session = workspace.currentSession();
        const auto contextId = workspace.sessionConfiguration(session).value("contextId").toString();
        const auto path = profile + "/views/" + podlord::contextViewName(contextId);
        const bool event = scenario == "view_restore_event_filter" || scenario == "view_restore_event_sort";
        if (event && !click(window, item(window, "eventsWorkspaceButton"))) return false;
        const auto inputName = event ? "eventFilter" : "resourceFilter";
        const auto headerName = event ? "eventHeader_1" : "header_0";
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
        if ((scenario=="view_restore_corrupt" || scenario=="view_restore_conflict" || scenario.startsWith("view_restore_close_"))
            && (!click(reopened,item(reopened,"quickOpenDropdown"))
                || !click(reopened,item(reopened,"quickContext_"+restored.sessionConfiguration(session).value("contextId").toString()))
                || !waitFor([&] { return !restored.busy() && !restored.loading() && restored.totalResourceCount()==3; })
                || !click(reopened,item(reopened,"resourcesWorkspaceButton")))) return false;
        if (scenario == "view_restore_filter") return restored.filterText() == "alpha" && text(reopened, "resourceFilter") == "alpha";
        if (scenario == "view_restore_clear") return restored.filterText().isEmpty();
        if (scenario == "view_restore_asc") return restored.sortColumnIndex() == 0 && restored.sortDirection() == "ASC";
        if (scenario == "view_restore_desc") return restored.sortColumnIndex() == 0 && restored.sortDirection() == "DESC";
        if (scenario == "view_restore_none") return restored.sortColumnIndex() == -1;
        if (scenario == "view_restore_event_filter") return restored.eventFilterText() == "alpha";
        if (scenario == "view_restore_event_sort") return restored.eventSortColumnIndex() == 1 && restored.eventSortDirection() == "ASC";
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
            if (!waitFor([&] { return !restored.viewCloseNeedsDecision(); })) return false;
            if (scenario == "view_restore_close_stay") return reopened->isVisible() && restored.filterText() == "bravo";
            return !reopened->isVisible() && restored.currentSession().isEmpty()
                && restored.resourceCount() == 0 && restored.logRows()->rowCount() == 0;
        }
        return false;
    }
    const auto eventColor = [&](const char* name) {
        const auto expected = workspace.appearanceColors()[scenario.endsWith("normal") ? "success" : scenario.endsWith("warning") ? "warning" : scenario.endsWith("empty") ? "text" : "unknown"].value<QColor>();
        return waitFor([&] {
            auto* cell = item(window, name);
            auto* caption = cell ? cell->property("contentItem").value<QObject*>() : nullptr;
            return caption && caption->property("color").value<QColor>() == expected;
        });
    };
    if (scenario.startsWith("filter_event_")) {
        const int requests = server.requests.size();
        if (!type(window,item(window,"resourceFilter"),"\"alpha\"") || !click(window,item(window,"eventsWorkspaceButton"))) return false;
        if (!waitFor([&] { return item(window,"eventFilter")->isVisible() && workspace.totalEventCount()==2; })) return false;
        if (scenario.contains("_color_")) return eventColor("eventCell_0_1") && server.requests.size() == requests;
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
            if (!podlord::test::focusResourceRadar(window) || !waitFor([&] { return workspace.workspacePage()=="resources"; })) return false;
            if (workspace.resourceCount()!=1 || workspace.totalResourceCount()!=3 || workspace.table()->data(workspace.table()->index(0,0)).toString()!="alpha") return false;
        }
        if (scenario == "filter_search_restore") {
            if (server.requests.size() != requests) {
                std::fprintf(stderr, "Editing a cached search sent a Kubernetes request\n"); return false;
            }
            const auto session = workspace.currentSession();
            const auto context = workspace.sessionConfiguration(session).value("contextId").toString();
            if (context.isEmpty()) return false;
            podlord::TableSchemas schemas;
            for (const auto& entry : {std::pair{QString("resource"), workspace.table()}, std::pair{QString("event"), workspace.eventTable()},
                std::pair{QString("port"), workspace.portTable()}, std::pair{QString("inspectorEvent"), workspace.inspectorEventTable()},
                std::pair{QString("inspectorLink"), workspace.inspectorLinkTable()}, std::pair{QString("value"), workspace.valuesTable()}}) {
                QStringList columns;
                for (int column = 0; column < entry.second->columnCount(); ++column) columns.append(entry.second->headerData(column, Qt::Horizontal, Qt::UserRole).toString());
                schemas.insert(entry.first, columns);
            }
            if (!waitFor([&] {
                const auto loaded = podlord::ViewStateStore(profile, schemas).load(context);
                return std::holds_alternative<podlord::TableViewStates>(loaded) && std::get<podlord::TableViewStates>(loaded).value("resource").filter==expression;
            })) return false;
            podlord::Workspace restored(profile);
            if (!waitFor([&] { return !restored.busy() && restored.currentSession()==session && restored.filterText()==expression; })
                || restored.totalResourceCount()!=0 || server.requests.size()!=requests || !restored.activate(session)) return false;
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
        if (scenario == "table_light_contrast") {
            const auto originalTheme=workspace.themeName();
            const auto originalColor=model->index(0,column("namespace")).data(Qt::ForegroundRole).value<QColor>();
            const auto luminance=[](QColor color) {
                const auto linear=[](double value) { return value<=0.04045 ? value/12.92 : std::pow((value+0.055)/1.055,2.4); };
                return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
            };
            const auto readable=[&](QColor color) {
                for (const auto& key : {"panel","inset","selection","hover"}) {
                    const double a=luminance(color),b=luminance(workspace.appearanceColors()[key].value<QColor>());
                    if ((std::max(a,b)+0.05)/(std::min(a,b)+0.05)<4.5) return false;
                }
                return true;
            };
            if (!workspace.saveAppearance("Imperial Ledger","light") || !waitFor([&] { return !workspace.busy() && workspace.themeVariant()=="light"; })) return false;
            if (!readable(model->index(0,column("namespace")).data(Qt::ForegroundRole).value<QColor>())
                || !readable(model->index(0,column("status")).data(Qt::ForegroundRole).value<QColor>())) return false;
            if (!workspace.inspectPath(model->index(0,0).data(Qt::UserRole).toString()) || !waitFor([&] { return workspace.canEditYaml(); })) return false;
            const auto calls=server.requests.size();
            for (const auto& field : {"name","namespace","status"}) {
                if (!waitFor([&] { auto* value=item(window,"overview_"+QString::fromLatin1(field)); return value && value->isVisible() && readable(value->property("color").value<QColor>()); })) return false;
            }
            if (!saveShellFrame(window,scenario,item(window,"workspaceInspector"))
                || !workspace.saveAppearance(originalTheme,"dark") || !waitFor([&] { return !workspace.busy() && workspace.themeVariant()=="dark"; })) return false;
            return model->index(0,column("namespace")).data(Qt::ForegroundRole).value<QColor>()==originalColor && server.requests.size()==calls;
        }
        if (scenario.startsWith("table_auto_")) {
            const auto calls = server.requests.size();
            const auto state = [&](const QString& id) {
                for (const auto& value : workspace.resourceColumns()) if (value.toMap()["id"] == id) return value.toMap();
                return QVariantMap{};
            };
            const auto shown = [&](const QString& id) {
                const auto entry = state(id); return entry.value("shown", entry.value("visible")).toBool();
            };
            const auto filter = [&](const QString& name) {
                return type(window, item(window, "resourceFilter"), name)
                    && waitFor([&] { return model->rowCount() == (name.isEmpty() ? 4 : name == "missing-resource" ? 0 : 1); });
            };
            if (scenario == "table_auto_zero") {
                if (!filter("alpha") || !waitFor([&] { return model->index(0,column("cpu")).data().toString() == "0 mCPU"; })) return false;
                return shown("cpu") && shown("memory") && shown("restarts") && shown("ready") && !shown("storage")
                    && model->index(0,column("restarts")).data().toString() == "0" && server.requests.size() == calls;
            }
            if (scenario == "table_auto_namespace") {
                return filter("custom-widget") && waitFor([&] { return !shown("namespace"); })
                    && filter("alpha") && waitFor([&] { return shown("namespace"); })
                    && model->index(0,column("namespace")).data() == "cluster" && server.requests.size() == calls;
            }
            if (scenario == "table_auto_image_literal") {
                if (!filter("alpha")) return false;
                const auto value=model->index(0,column("image")).data().toString();
                if (!shown("image") || value != "-") { qWarning() << "Literal image:" << value << state("image"); return false; }
                return server.requests.size() == calls;
            }
            if (scenario == "table_auto_focus") {
                if (!filter("alpha")) return false;
                auto* view=item(window,"resourceTable");
                auto* selection=view->property("selectionModel").value<QItemSelectionModel*>();
                if (!selection) return false;
                QQmlExpression position(qmlContext(view),view,QString("positionViewAtColumn(columnAtIndex(model.index(0,%1)),TableView.Contain)").arg(column("node")));
                if (!waitFor([&] { position.evaluate(); auto* cell=item(window,"cell_0_"+QString::number(column("node"))); return !position.hasError() && cell && cell->isVisible() && cell->width()>0; })) return false;
                item(window,"cell_0_"+QString::number(column("node")))->forceActiveFocus();
                if (!waitFor([&] { return selection->currentIndex().column() == column("node"); }) || !workspace.refresh()
                    || !waitFor([&] { return !workspace.loading() && !shown("node") && selection->currentIndex().isValid() && selection->currentIndex().column()!=column("node"); })) return false;
                view->forceActiveFocus(); QTest::keySequence(window,QKeySequence::Copy);
                if (QGuiApplication::clipboard()->text()!=model->data(model->index(0,selection->currentIndex().column())).toString()) return false;
                if (!click(window,item(window,"resourceFindButton")) || !type(window,item(window,"resourceFindInput"),"alpha")
                    || !waitFor([&] { return text(window,"resourceFindCount")=="1/1"; })
                    || !click(window,item(window,"resourceFindNext")) || !click(window,item(window,"resourceFindClose"))) return false;
                view->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Return);
                return waitFor([&] { return workspace.inspectorName()=="alpha" && workspace.canEditYaml(); });
            }
            if (scenario == "table_auto_limit") {
                return workspace.sortColumn(column("name")) && workspace.sortColumn(column("name"))
                    && workspace.setResourceLimit("1") && waitFor([&] {
                        return workspace.displayedResourceCount() == 1 && workspace.visibleResourceTable()->index(0,0).data() == "custom-widget" && !shown("cpu");
                    }) && workspace.sortColumn(column("name")) && workspace.sortColumn(column("name"))
                    && waitFor([&] { return workspace.visibleResourceTable()->index(0,0).data() == "alpha" && shown("cpu"); })
                    && server.requests.size() == calls;
            }
            if (scenario == "table_auto_refresh") {
                if (!filter("alpha") || !shown("node") || !workspace.refresh()) return false;
                return waitFor([&] { return !workspace.loading() && !shown("node") && !shown("image") && !shown("owner"); })
                    && state("node")["visible"].toBool();
            }
            if (scenario == "table_auto_filter") {
                return filter("alpha") && waitFor([&] { return shown("cpu"); })
                    && filter("custom-widget") && waitFor([&] { return !shown("cpu"); })
                    && filter("alpha") && waitFor([&] { return shown("cpu"); })
                    && filter("missing-resource") && waitFor([&] { return !shown("namespace") && !shown("cpu") && shown("name"); })
                    && server.requests.size() == calls;
            }
            if (!filter("custom-widget") || !waitFor([&] { return !shown("cpu"); })) return false;
            if (scenario == "table_auto_empty") {
                for (const auto& field : QStringList{"cpu","memory","storage","ready","restarts","node","image","owner"}) {
                    if (shown(field) || !state(field)["visible"].toBool()) {
                        qWarning() << "Empty resource column was shown or its saved preference changed:" << field; return false;
                    }
                }
                return shown("namespace") && shown("name") && shown("kind") && server.requests.size() == calls
                    && saveShellFrame(window, scenario, item(window,"workspaceContent"));
            }
            if (!click(window, item(window,"resourceColumnsButton"))) return false;
            if (scenario == "table_auto_pin" || scenario == "table_auto_hidden") {
                const auto control = scenario == "table_auto_pin" ? "resourceColumnPinned_cpu" : "resourceColumnVisible_cpu";
                auto* target = item(window,control);
                if (!target) return false;
                target->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
            } else if (scenario == "table_auto_save") {
                if (!type(window,item(window,"resourceColumnWidth_name"),"450")) return false;
            } else if (scenario == "table_auto_last") {
                for (const auto& value : workspace.resourceColumns()) {
                    const auto entry=value.toMap();
                    if (entry["id"] == "cpu" || !entry["visible"].toBool()) continue;
                    auto* target=item(window,"resourceColumnVisible_"+entry["id"].toString());
                    if (!target) return false;
                    target->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
                }
            } else return false;
            if (!click(window,item(window,"resourceSaveColumns"))
                || !waitFor([&] { return !workspace.tableLayoutSaving() && !window->findChild<QObject*>("resourceColumnsDialog")->property("visible").toBool(); })
                || !workspace.tableLayoutError().isEmpty()) return false;
            if (scenario == "table_auto_last") {
                int count=0;
                for (const auto& value : workspace.resourceColumns()) if (value.toMap()["shown"].toBool()) ++count;
                return count == 1 && shown("cpu") && !shown("name") && server.requests.size() == calls;
            }
            if (scenario == "table_auto_pin") {
                return shown("cpu") && state("cpu")["pinned"].toBool()
                    && waitFor([&] { auto* cell=item(window,"pinnedCell_0_"+QString::number(column("cpu"))); return cell && cell->isVisible() && cell->width()>0; })
                    && server.requests.size() == calls;
            }
            if (scenario == "table_auto_hidden") {
                return !state("cpu")["visible"].toBool() && filter("alpha")
                    && !shown("cpu") && server.requests.size() == calls;
            }
            podlord::Workspace restored(profile);
            if (!waitFor([&] { return !restored.busy(); })) return false;
            for (const auto& value : restored.resourceColumns()) {
                const auto entry=value.toMap();
                if (entry["id"] == "cpu" && !entry["visible"].toBool()) return false;
                if (entry["id"] == "name" && entry["width"].toInt() != 450) return false;
            }
            return state("cpu")["visible"].toBool() && !shown("cpu") && filter("alpha") && waitFor([&] { return shown("cpu"); })
                && server.requests.size() == calls;
        }
        if (!waitFor([&] { return model->data(model->index(0,column("cpu"))).toString() == (scenario == "table_zero" ? "0 mCPU" : "2000 mCPU"); })) {
            std::fprintf(stderr,"Table metrics unavailable: %s; %s; requests=%s\n", qPrintable(model->data(model->index(0,column("cpu"))).toString()), qPrintable(workspace.status()), qPrintable(server.requests.join('\n'))); return false;
        }
        const int requests = server.requests.size();
        auto* view = item(window, "resourceTable");
        const auto show = [&](int index) {
            QQmlExpression position(qmlContext(view), view,
                QString("positionViewAtColumn(columnAtIndex(model.index(0, %1)), TableView.Contain)").arg(index));
            const bool shown = waitFor([&] {
                position.evaluate();
                if (position.hasError()) return false;
                auto* cell = item(window, "cell_0_" + QString::number(index));
                if (!cell || !cell->isVisible() || cell->width() <= 0 || view->property("moving").toBool()) return false;
                auto* header = item(window, "header_" + QString::number(index));
                if (!header || !header->isVisible() || header->width() <= 0) return false;
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
        if (scenario == "table_pointer_drag" || scenario == "table_pointer_touch") {
            if (!show(column("name"))) return false;
            auto* cell = item(window, "cell_0_" + QString::number(column("name")));
            const auto start = cell->mapToScene({cell->width() / 2, cell->height() / 2}).toPoint();
            const auto previousX = view->property("contentX").toDouble();
            const bool touchInput = scenario == "table_pointer_touch";
            std::unique_ptr<QPointingDevice> touch(touchInput ? QTest::createTouchDevice() : nullptr);
            if (touchInput) QTest::touchEvent(window, touch.get()).press(0, start, window).commit();
            else QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            for (int step = 1; step <= 6; ++step) {
                const auto point = start - QPoint(step * 10, 0);
                if (touchInput) QTest::touchEvent(window, touch.get()).move(0, point, window).commit();
                else QTest::mouseMove(window, point);
                QTest::qWait(10);
            }
            if (touchInput) QTest::touchEvent(window, touch.get()).release(0, start - QPoint(60, 0), window).commit();
            else QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start - QPoint(60, 0));
            if (!waitFor([&] { return view->property("contentX").toDouble() > previousX + 20 && !view->property("moving").toBool(); })
                || !workspace.inspectorPath().isEmpty() || server.requests.size() != requests) return false;
            if (!show(column("name"))) return false;
            cell = item(window, "cell_0_" + QString::number(column("name")));
            if (touchInput) {
                const auto point = cell->mapToScene({cell->width() / 2, cell->height() / 2}).toPoint();
                QTest::touchEvent(window, touch.get()).press(0, point, window).commit();
                QTest::touchEvent(window, touch.get()).release(0, point, window).commit();
            } else if (!click(window, cell)) return false;
            return waitFor([&] { return workspace.inspectorName() == "alpha" && workspace.canEditYaml(); });
        }
        if (scenario == "table_columns") return model->columnCount() == fields.size() && workspace.resourceColumns().size() == fields.size();
        if (scenario == "table_compact_tools") {
            auto* content = item(window, "workspaceContent");
            auto* header = item(window, "resourceHeaderView");
            if (!content || !header) return false;
            const auto gap = header->mapToScene({0, 0}).y() - content->mapToScene({0, 0}).y();
            if (gap > 1) {
                std::fprintf(stderr, "Unused toolbar gap above resource table: %g\n", gap);
                return false;
            }
            return server.requests.size() == requests;
        }
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
            auto* overview=item(window,"overviewScroll");
            if (!overview) return false;
            if (scenario == "table_overview_custom") return text(window,"overview_cluster")=="local" && !item(window,"overview_ready") && !item(window,"overviewReadinessBar") && server.requests.size()==calls;
            const QString expected=scenario=="table_overview_full" ? "10/10" : scenario=="table_overview_none" || scenario=="table_overview_pending" ? "0/10" : "2/10";
            int navigationKeys=0;
            const auto navigateToField=[&](const QString& id) {
                auto* rows=overview->property("model").value<QAbstractItemModel*>();
                if (!rows) return;
                const auto current=overview->property("currentIndex").toInt();
                for (int row=0; row<rows->rowCount(); ++row) {
                    if (rows->data(rows->index(row,0),Qt::UserRole).toMap()["id"]!=id || row==current) continue;
                    overview->forceActiveFocus(Qt::TabFocusReason);
                    QTest::keyClick(window,current<row ? Qt::Key_Down : Qt::Key_Up);
                    ++navigationKeys;
                    break;
                }
            };
            if (!waitFor([&] {
                const bool ready=text(window,"overview_ready")==expected && item(window,"overviewReadinessBar");
                if (!ready && scenario=="table_overview_metadata") navigateToField("ready");
                return ready;
            })) {
                std::fprintf(stderr,"Overview readiness: scenario=%s expected=%s actual=%s keys=%d current=%d count=%d contentY=%g height=%g\n",
                    qPrintable(scenario),qPrintable(expected),qPrintable(text(window,"overview_ready")),navigationKeys,
                    overview->property("currentIndex").toInt(),overview->property("count").toInt(),
                    overview->property("contentY").toDouble(),overview->height());
                return false;
            }
            auto* bar=item(window,"overviewReadinessBar");
            const auto fraction=scenario=="table_overview_full" ? 1.0 : scenario=="table_overview_none" || scenario=="table_overview_pending" ? 0.0 : 0.2;
            const QString color=scenario=="table_overview_full" ? "success" : scenario=="table_overview_none" || scenario=="table_overview_pending" ? "danger" : "warning";
            const auto* fill=item(window,"overviewReadinessFill");
            if (!fill || qAbs(bar->property("value").toDouble()-fraction)>0.001 || fill->property("color").value<QColor>() != workspace.appearanceColors()[color].value<QColor>()) return false;
            if (scenario=="table_overview_metadata") {
                const auto createdAt = server.measured.addDays(-2);
                const auto locale = QLocale::system();
                const QStringList ids{"cluster","restarts","owner","createdAt","fetchedAt"},
                    values{"local","10","ReplicaSet/workload",locale.toString(createdAt.toLocalTime(),QLocale::ShortFormat),
                        locale.toString(reference.toLocalTime(),QLocale::ShortFormat)};
                qsizetype next=0;
                const bool visible=waitFor([&] {
                    if (next==ids.size()) return true;
                    auto* field=item(window,"overview_"+ids[next]);
                    if (field && field->isVisible() && field->width()>0 && field->height()>0
                        && podlord::test::visibleBounds(window,field).contains(field->mapRectToScene(QRectF(QPointF{},field->size())))
                        && field->property("text")==values[next]) {
                        if (++next==ids.size()) return true;
                    }
                    navigateToField(ids[next]);
                    return false;
                });
                if (!visible) std::fprintf(stderr,"Overview metadata: missing visible field=%s actual=%s keys=%d current=%d count=%d contentY=%g height=%g\n",
                    qPrintable(ids[next]),qPrintable(text(window,"overview_"+ids[next])),navigationKeys,
                    overview->property("currentIndex").toInt(),overview->property("count").toInt(),
                    overview->property("contentY").toDouble(),overview->height());
                return visible && workspace.yamlText().contains(createdAt.toString(Qt::ISODateWithMs))
                    && server.requests.size()==calls && saveShellFrame(window,scenario+"-local-time",item(window,"workspaceInspector"));
            }
            if (scenario=="table_overview_copy") {
                const QPointer<QQuickItem> field = item(window,"overview_ready");
                if (!field || !click(window,field)) return false;
                QTest::keySequence(window,QKeySequence::SelectAll); QTest::keySequence(window,QKeySequence::Copy);
                if (QGuiApplication::clipboard()->text()!=expected) return false;
                QTest::keyClick(window,Qt::Key_Tab);
                if (!field || field->hasActiveFocus()) return false;
                QTest::keyClick(window,Qt::Key_Tab,Qt::ShiftModifier);
                if (!waitFor([&] { return field && field->hasActiveFocus(); })) return false;
                QTest::keySequence(window,QKeySequence::SelectAll);
                QTest::keyClick(window,Qt::Key_Delete);
                if (text(window,"overview_ready")!=expected) return false;
                QGuiApplication::clipboard()->setText("unexpected replacement");
                QTest::keySequence(window,QKeySequence::Paste);
                if (text(window,"overview_ready")!=expected || !accessibleName(window,"Ready: "+expected)) return false;
                QTest::keySequence(window,QKeySequence::SelectAll);
                return text(window,"overview_ready")==expected && server.requests.size()==calls
                    && saveShellFrame(window,scenario,item(window,"workspaceInspector"));
            }
            if (scenario=="table_overview_stable_refresh") {
                QPointer<QQuickItem> name = item(window,"overview_name");
                QPointer<QQuickItem> readiness = bar;
                if (!name || !QMetaObject::invokeMethod(name,"select",Q_ARG(int,0),Q_ARG(int,5))
                    || name->property("selectedText") != "alpha") return false;
                const auto scope = workspace.inspectorScope();
                if (!workspace.refresh()
                    || !waitFor([&] { return !workspace.loading() && text(window,"overview_ready")=="10/10"; })) return false;
                const bool retained = name && readiness && name == item(window,"overview_name")
                    && readiness == item(window,"overviewReadinessBar") && name->property("selectedText") == "alpha"
                    && workspace.inspectorScope() == scope && qAbs(readiness->property("value").toDouble()-1)<0.001;
                if (!retained) std::fprintf(stderr,"Metadata refresh discarded unchanged text selection or readiness row.\n");
                return retained;
            }
            if (scenario=="table_overview_stable_lifecycle") {
                auto* fields = overview->property("model").value<QAbstractItemModel*>();
                if (!fields || fields->rowCount() == 0 || fields->rowCount(fields->index(0,0)) != 0
                    || fields->data({}).isValid() || fields->data(fields->index(0,0),-1).isValid()
                    || fields->data(model->index(0,0),Qt::UserRole).isValid()) return false;
                QAbstractItemModelTester contract(fields,QAbstractItemModelTester::FailureReportingMode::Fatal);
                const auto path = workspace.inspectorPath();
                const auto retired = fields->index(fields->rowCount()-1,0);
                if (!click(window,item(window,"closeInspector")) || fields->rowCount() != 0 || fields->data(retired,Qt::UserRole).isValid()) {
                    std::fprintf(stderr,"Metadata close: path=%s rows=%d retired=%d\n",qPrintable(workspace.inspectorPath()),fields->rowCount(),fields->data(retired,Qt::UserRole).isValid());
                    return false;
                }
                if (!workspace.inspectPath(path) || !waitFor([&] { return text(window,"overview_name")=="alpha"; })) {
                    std::fprintf(stderr,"Metadata reopen: path=%s name=%s page=%s modelRows=%d viewCount=%d field=%s error=%s\n",qPrintable(workspace.inspectorPath()),qPrintable(workspace.inspectorName()),qPrintable(workspace.inspectorPage()),fields->rowCount(),overview->property("count").toInt(),qPrintable(text(window,"overview_name")),qPrintable(workspace.error()));
                    saveShellFrame(window,scenario+"-reopen-failure",item(window,"workspaceInspector"));
                    return false;
                }
                QString customPath;
                for (int row = 0; row < model->rowCount(); ++row)
                    if (model->data(model->index(row,column("name"))) == "custom-widget") customPath = model->data(model->index(row,0),Qt::UserRole).toString();
                if (overview->property("model").value<QAbstractItemModel*>() != fields || fields->rowCount() == 0
                    || customPath.isEmpty() || !workspace.inspectPath(customPath) || !waitFor([&] { return workspace.canEditYaml(); })) {
                    std::fprintf(stderr,"Metadata custom open: modelAttached=%d rows=%d customPath=%s current=%s editable=%d error=%s\n",overview->property("model").value<QAbstractItemModel*>()==fields,fields->rowCount(),qPrintable(customPath),qPrintable(workspace.inspectorPath()),workspace.canEditYaml(),qPrintable(workspace.error()));
                    return false;
                }
                if (!waitFor([&] {
                    if (text(window,"overview_kind") != "Widget" || overview->property("count").toInt() != fields->rowCount()) return false;
                    for (int row = 0; row < fields->rowCount(); ++row)
                        if (fields->data(fields->index(row,0),Qt::UserRole).toMap().value("id") == "ready") return false;
                    return true;
                })) {
                    std::fprintf(stderr,"Custom resource retained Pod metadata: name=%s kind=%s ready=%s\n",qPrintable(workspace.inspectorName()),qPrintable(text(window,"overview_kind")),qPrintable(text(window,"overview_ready")));
                    return false;
                }
                return workspace.inspectPath(path) && waitFor([&] { return text(window,"overview_name")=="alpha"
                    && text(window,"overview_ready")=="2/10" && item(window,"overviewReadinessBar"); })
                    && overview->property("model").value<QAbstractItemModel*>() == fields;
            }
            if (scenario=="table_overview_live_updates") {
                const bool profiling = qgetenv("PODLORD_INSPECTOR_PROFILE") == "1";
                QPointer<QQuickItem> name = item(window,"overview_name");
                if (!name || !QMetaObject::invokeMethod(name,"select",Q_ARG(int,0),Q_ARG(int,5))) return false;
                QList<double> cpu;
                bool retained = true;
                for (int update = 0; update < (profiling ? 35 : 3); ++update) {
                    QSignalSpy presentation(&workspace,&podlord::Workspace::inspectorPresentationChanged);
                    const auto started = profiling ? std::clock() : 0;
                    if (!workspace.refreshInspector() || !waitFor([&] { return !presentation.isEmpty() && workspace.canEditYaml() && !workspace.loading(); })) return false;
                    for (int drain = 0; drain < 2; ++drain) {
                        QSignalSpy rendered(window,&QQuickWindow::frameSwapped); window->update();
                        if (rendered.isEmpty() && !rendered.wait(2000)) return false;
                    }
                    if (profiling && update >= 5) cpu.append(1000.0 * (std::clock()-started)/CLOCKS_PER_SEC);
                    retained = retained && name && name == item(window,"overview_name") && name->property("selectedText") == "alpha";
                }
                if (profiling) {
                    std::sort(cpu.begin(),cpu.end());
                    double total = 0;
                    for (const auto sample : cpu) total += sample;
                    std::fprintf(stdout,"%s\n",QJsonDocument(QJsonObject{{"type","metadata-update-cpu"},{"diagnosticOnly",true},
                        {"samples",cpu.size()},{"totalCpuMs",total},{"p95CpuMs",cpu[28]},{"retainedSelection",retained},
                        {"scope","Process CPU for explicit HTTP detail refresh and two rendered drain frames, five warmups; not release frame clearance"}}).toJson(QJsonDocument::Compact).constData());
                }
                return retained && text(window,"overview_ready")=="2/10";
            }
            if (scenario=="table_overview_refresh") {
                const auto scope=workspace.inspectorScope();
                if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && text(window,"overview_ready")=="10/10"; })) return false;
                return workspace.inspectorScope()==scope && qAbs(item(window,"overviewReadinessBar")->property("value").toDouble()-1)<0.001;
            }
            if (scenario=="table_overview_metric_size") {
                auto* scroll=item(window,"overviewScroll");
                if (!podlord::test::scrollIntoView(window,scroll)) return false;
                scroll->forceActiveFocus(); QTest::keyClick(window,Qt::Key_End);
                const bool compact=waitFor([&] { const auto* metric=item(window,"metric_storage_bar"); return metric && metric->isVisible() && metric->height()>=6 && metric->height()<=10; });
                if (!compact) {
                    const auto* metric=item(window,"metric_storage_bar");
                    std::fprintf(stderr,"Rendered metric: present=%d visible=%d height=%g rows=%d scroll=%g/%g\n",metric!=nullptr,metric && metric->isVisible(),metric ? metric->height() : -1,scroll->property("count").toInt(),scroll->property("contentY").toDouble(),scroll->property("contentHeight").toDouble());
                }
                return compact && server.requests.size()==calls
                    && saveShellFrame(window,scenario,item(window,"workspaceInspector"));
            }
            return server.requests.size()==calls && saveShellFrame(window,scenario,item(window,"workspaceInspector"));
        }
        if (scenario == "table_cluster_switch") {
            const auto first=workspace.currentSession();
            const auto selection=item(window,"resourceTable")->property("selectionModel").value<QItemSelectionModel*>();
            if (!selection) return false;
            selection->setCurrentIndex(model->index(0,0),QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            const auto firstCluster=value(0,"cluster");
            const auto nextCluster=firstCluster=="local" ? "other" : "local";
            if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return !workspace.busy(); })
                || !workspace.setWorkspacePage("resources")) return false;
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
            auto* display = qobject_cast<QAbstractItemModel*>(view->property("model").value<QObject*>());
            if (!display) return false;
            for (const auto& field : fields) {
                if (field == "storage") {
                    for (const auto& entry : workspace.resourceColumns()) if (entry.toMap()["id"] == field && entry.toMap()["shown"].toBool()) return false;
                    continue;
                }
                if (!show(column(field)) || text(window, "cell_0_" + QString::number(column(field))) != display->data(display->index(0, column(field))).toString()) return false;
            }
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
            if (!workspace.refresh()) {
                std::fprintf(stderr,"Table refresh input failed: busy=%d auth=%d session=%s status=%s\n",
                    workspace.busy(),workspace.authenticationRequired(),qPrintable(workspace.currentSession()),qPrintable(workspace.status()));
                return false;
            }
            // Refresh completion is an asynchronous functional boundary, not a frame-time assertion.
            if (!waitFor([&] { return !workspace.loading() && server.refresh > previous; }, 30000)) {
                std::fprintf(stderr,"Table refresh did not complete: requests=%d->%d loading=%d status=%s\n",
                    previous,server.refresh,workspace.loading(),qPrintable(workspace.status()));
                return false;
            }
            const bool retained = workspace.inspectorName() == "bravo" && qAbs(view->property("contentX").toDouble()-x) < 1 && qAbs(view->property("contentY").toDouble()-y) < 1;
            if (!retained) std::fprintf(stderr,"Table refresh moved: %s x=%g->%g y=%g->%g\n",qPrintable(workspace.inspectorName()),x,view->property("contentX").toDouble(),y,view->property("contentY").toDouble());
            return retained;
        }
        return false;
    }
    if (scenario.startsWith("columns_")) {
        const auto requests = server.requests.size();
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
            for (const auto& direction : {QString("Earlier"), QString("Later")}) {
                auto* control = item(window, "resourceColumn" + direction + "_kind");
                auto* content = control ? control->property("contentItem").value<QQuickItem*>() : nullptr;
                if (!content || content->property("text").toString() != (direction=="Earlier" ? QStringLiteral("\u2191") : QStringLiteral("\u2193"))) return false;
            }
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
            podlord::Workspace writer(profile);
            if (!waitFor([&] { return !writer.busy(); })) return false;
            auto changed = writer.resourceColumns();
            for (auto& value : changed) {
                auto entry = value.toMap();
                if (entry["id"] == "name") entry["width"] = 430;
                value = entry;
            }
            if (!writer.saveTableLayout("resource", changed) || !waitFor([&] {
                for (const auto& value : writer.resourceColumns())
                    if (value.toMap()["id"] == "name") return value.toMap()["width"] == 430;
                return false;
            })) return false;
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
            if (!click(window, item(window, "eventsWorkspaceButton")) || !click(window, item(window, "eventColumnsButton")) || !waitFor([&] { return item(window, "eventColumnVisible_namespace") && item(window, "eventColumnVisible_namespace")->isVisible(); })) {
                std::fprintf(stderr, "Event columns dialog did not open.\n"); return false;
            }
            if (!podlord::test::clickVisible(window, "eventColumnVisible_namespace")) {
                std::fprintf(stderr, "Event namespace checkbox could not be toggled.\n"); return false;
            }
            if (!click(window, item(window, "eventSaveColumns"))) {
                std::fprintf(stderr, "Event namespace column save button could not be activated.\n"); return false;
            }
            const bool passed = waitFor([&] { return column("event", "namespace")["visible"].toBool(); })
                && column("resource", "namespace")["visible"].toBool() && !column("resource", "kind")["visible"].toBool() && server.requests.size() == requests;
            if (!passed) std::fprintf(stderr, "Column isolation: event namespace=%d resource namespace=%d resource kind=%d calls=%lld/%lld error=%s\n",
                column("event", "namespace")["visible"].toBool(), column("resource", "namespace")["visible"].toBool(), column("resource", "kind")["visible"].toBool(),
                static_cast<long long>(server.requests.size()), static_cast<long long>(requests), qPrintable(workspace.tableLayoutError()));
            return passed;
        }
        if (scenario == "columns_reveal" || scenario == "columns_reset") {
            if (!click(window, item(window, "resourceColumnsButton"))) return false;
            if (!click(window, item(window, scenario == "columns_reset" ? "resourceResetColumns" : "resourceColumnVisible_kind")) || !click(window, item(window, "resourceSaveColumns"))) return false;
            return waitFor([&] { return column("resource", "kind")["visible"].toBool(); }) && server.requests.size() == requests;
        }
        return (scenario == "columns_hide" || scenario == "columns_keyboard") && server.requests.size() == requests;
    }
    if (scenario.startsWith("import_notice_")) {
        const auto calls = server.requests.size();
        if (!workspace.sourceImportNotice().isEmpty() || !workspace.sourceImportIssues().isEmpty()) return false;
        if (scenario == "import_notice_hidden") return true;
        if (!click(window, item(window, "settingsWorkspaceButton"))
            || !podlord::test::selectSettingsSection(window, "sources")) return false;
        if (scenario == "import_notice_reopen") {
            if (!click(window, item(window, "mapWorkspaceButton"))
                || !click(window, item(window, "settingsWorkspaceButton"))
                || !podlord::test::selectSettingsSection(window, "sources")) return false;
            return workspace.sourceImportNotice().isEmpty() && workspace.sourceImportIssues().isEmpty()
                && server.requests.size() == calls;
        }
        if (scenario != "import_notice_errors") return false;
        const auto directory = temporary.filePath("partial-import");
        if (!QDir().mkpath(directory) || !QFile::copy(temporary.filePath("source.config"), directory + "/config")) return false;
        QFile broken(directory + "/broken.config");
        const QByteArray invalid("not kubeconfig: [\n");
        if (!broken.open(QIODevice::WriteOnly) || broken.write(invalid) != invalid.size()) return false;
        broken.close();
        if (!workspace.importFile(directory) || !waitFor([&] { return !workspace.busy() && !workspace.sourceImportIssues().isEmpty(); })
            || !click(window, item(window, "settingsSourceImportDetails"))) return false;
        auto* details = window->findChild<QObject*>("sourceImportDetails");
        return details && waitFor([&] { return details->property("opened").toBool(); })
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
        return !workspace.refreshSettingsDiagnostics().isEmpty() && server.requests.size()==calls;
    }
    if (scenario.startsWith("radar_")) {
        const bool shortLandscape = scenario.startsWith("radar_info_panel_short_landscape");
        // The real 640x320 Android capture has 24-pixel system bars at both edges.
        const QSize landscapeSurface(640, scenario.endsWith("_insets") ? 272 : 320);
        const auto failed = [&](const char* stage) {
            const auto* alerts = workspace.alerts();
            std::fprintf(stderr, "Radar setup failed: stage=%s resources=%d busy=%d water=%d alerts_busy=%d error=%s evaluation=%s\n",
                stage, workspace.resourceCount(), workspace.busy(), workspace.radarWaterEnabled(), alerts->busy(),
                qPrintable(alerts->error()), qPrintable(alerts->evaluationError()));
            if (shortLandscape) {
                std::fprintf(stderr,"Short landscape radar: scenario=%s window=%dx%d content=%gx%g backend=%d\n",
                    qPrintable(requestedScenario),window->width(),window->height(),window->contentItem()->width(),window->contentItem()->height(),
                    int(window->rendererInterface()->graphicsApi()));
                const auto geometry = [&](const char* name, QQuickItem* entry) {
                    if (!entry) { std::fprintf(stderr,"Radar geometry %s: missing\n",name); return; }
                    const auto scene=entry->mapRectToScene(entry->boundingRect());
                    const auto exposed=podlord::test::visibleBounds(window,entry);
                    std::fprintf(stderr,"Radar geometry %s: type=%s scene=(%g,%g %gx%g) exposed=(%g,%g %gx%g) visible=%d clip=%d opacity=%g\n",
                        name,entry->metaObject()->className(),scene.x(),scene.y(),scene.width(),scene.height(),
                        exposed.x(),exposed.y(),exposed.width(),exposed.height(),entry->isVisible(),entry->clip(),entry->opacity());
                };
                auto* map=item(window,"resourceRadar");
                auto* pane=map;
                while (pane && !pane->property("compact").isValid()) pane=pane->parentItem();
                geometry("source-sidebar",window->findChild<QQuickItem*>("workspaceSidebarHost"));
                geometry("host",pane ? pane->parentItem() : nullptr);
                geometry("pane",pane);
                geometry("map-layout",map ? map->parentItem() : nullptr);
                geometry("map",map);
                geometry("water",item(window,"radarWater"));
                geometry("info",item(window,"radarInfoPanel"));
                auto* source=map ? map->property("source").value<QAbstractItemModel*>() : nullptr;
                std::fprintf(stderr,"Radar source: rows=%d count=%d expanded=%d\n",source ? source->rowCount() : -1,
                    map ? map->property("count").toInt() : -1,pane && pane->property("expanded").toBool());
                const auto capture=qEnvironmentVariable("PODLORD_UI_FAILURE_FRAME");
                if (!capture.isEmpty()) window->grabWindow().save(capture);
            }
            const auto evidence = qEnvironmentVariable("PODLORD_RADAR_ACTION_EVIDENCE");
            if (!evidence.isEmpty()) window->grabWindow().save(evidence + "/" + scenario + "-"
                + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + "-setup.png", "PNG");
            return false;
        };
        const auto stopWater = [&] {
            auto* speed = item(window,"inlineRadarWaterSpeed");
            if (!speed || !podlord::test::scrollIntoView(window,speed)) return false;
            speed->forceActiveFocus(Qt::TabFocusReason);
            for (int step=0; step<20 && speed->property("value").toInt()>0; ++step) {
                QTest::keyClick(window,Qt::Key_Left);
                if (!waitFor([&] { return !workspace.busy(); })) return false;
            }
            return waitFor([&] { return !workspace.busy() && workspace.radarWaterSpeedPercent()==0; });
        };
        if (!scenario.startsWith("radar_water_") && scenario!="radar_navigation_water") {
            if (!click(window,item(window,"settingsWorkspaceButton"))) return failed("open settings");
            if (!podlord::test::selectSettingsSection(window,"appearance")) return failed("open graphics");
            if (!stopWater()) return failed("freeze background");
            if (!click(window,item(window,"resourcesWorkspaceButton"))) return failed("open resources");
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
        if (scenario.startsWith("radar_info_panel_")) {
            window->resize(scenario.endsWith("_phone") ? QSize(390,844) : shortLandscape ? landscapeSurface : QSize(640,320));
            QSignalSpy resizedFrame(window,&QQuickWindow::frameSwapped); window->update();
            if (!waitFor([&] { return !resizedFrame.isEmpty(); })) return failed("resized radar layout");
        }
        mapRadar = mapRadar || window->property("compactLandscape").toBool();
        if (!podlord::test::focusResourceRadar(window,mapRadar)) return failed("open radar");
        auto* radar = item(window, "resourceRadar");
        if (!radar || !waitFor([&] { return radar->isVisible() && radar->width() > 0 && radar->height() > 0 && radar->property("count").toInt() == expectedRows; })) return failed("populate radar");
        // Existing input/paint scenarios deliberately start from the public 100% reset.
        if (!initialFit) { radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_0); }
        if (scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water") || scenario == "radar_initial_population" || scenario == "radar_reopen_position" || scenario.startsWith("radar_island_")) {
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            if (!waitFor([&] { return !frames.isEmpty(); })) return false;
            const auto frame = qEnvironmentVariable("PODLORD_RADAR_FRAME");
            if (!frame.isEmpty() && !window->grabWindow().save(frame)) return false;
        }
        const auto calls = server.requests.size();
        if (scenario == "radar_overlay_navigation") {
            if (item(window,"resetRadar")) return failed("removed reset control must not exist");
            const auto* navigation=item(window,"radarNavigationOverlay");
            if (!navigation) return failed("radar navigation remains available");
            for (const auto* control : navigation->findChildren<QQuickItem*>())
                if (control->isVisible() && control->property("text").toString().trimmed().endsWith('%'))
                    return failed("zoom percentage must not occupy navigation space");
            const auto* detail = item(window, "radarInfoPanel");
            if (detail && detail->isVisible()) return failed("empty resource details must not reserve space");
            if (radar->boundingRect() != radar->parentItem()->boundingRect()) return failed("drawing area must fill its host");
            const auto* history = item(window, "mapBack");
            if (history || item(window,"mapForward")) return failed("obsolete map history");
            const auto viewport = radar->mapRectToScene(radar->boundingRect());
            for (const auto* name : {"radarZoomOut", "radarZoom"}) {
                auto* control = item(window, name);
                if (!control || !control->isVisible() || !viewport.contains(control->mapRectToScene(control->boundingRect())))
                    return failed("navigation must overlay the drawing area");
            }
            if (mapRadar) for (const auto* name : {"mapOverview"}) {
                auto* control = item(window, name);
                if (!control || !control->isVisible() || !viewport.contains(control->mapRectToScene(control->boundingRect())))
                    return failed("map history must overlay the drawing area");
            }
            const auto before = radar->property("viewPose").toMap()["zoom"].toDouble();
            return click(window, item(window, "radarZoom"))
                && radar->property("viewPose").toMap()["zoom"].toDouble() > before
                && server.requests.size() == calls && saveShellFrame(window, requestedScenario);
        }
        if (scenario == "radar_quick_info") {
            int row = -1;
            for (int index = 0; index < workspace.table()->rowCount(); ++index)
                if (workspace.table()->index(index, 0).data(Qt::UserRole + 1).toString() == "alpha") row = index;
            auto* island = qobject_cast<podlord::RadarIsland*>(radar);
            if (!island || row < 0 || !island->selectResource(row) || !island->focusResource(row)) return failed("problem resource selection");
            radar->forceActiveFocus();
            if (!waitFor([&] {
                const auto* title = item(window, "radarInfoText");
                const auto* status = item(window, "radarInfoStatus");
                const auto* issue = item(window, "radarInfoIssue");
                const auto* readiness = item(window, "radarInfoReadiness");
                return title && title->isVisible() && title->property("text").toString() == "Pod alpha"
                    && status && status->isVisible() && status->property("text").toString().contains("CrashLoopBackOff")
                    && issue && issue->isVisible() && issue->property("text").toString().contains("CrashLoopBackOff")
                    && readiness && readiness->isVisible() && readiness->property("text").toString().contains("0/1")
                    && readiness->property("text").toString().contains("12");
            })) {
                for (const auto* name : {"radarInfoText", "radarInfoStatus", "radarInfoIssue", "radarInfoReadiness"}) {
                    const auto* field = item(window, name);
                    qWarning() << "Radar quick info:" << name << "visible" << (field && field->isVisible())
                        << "text" << (field ? field->property("text") : QVariant{});
                }
                qWarning() << "Radar cached quick details:" << workspace.table()->index(row, 0).data(Qt::UserRole + 15);
                return failed("structured type, status, problem and readiness");
            }
            for (const auto* name : {"metric_cpu_bar", "metric_cpu_marker_request", "metric_cpu_marker_limit", "metric_memory_bar"})
                if (!item(window, name)) return failed("configured metric reference markers");
            for (const auto* name : {"metric_cpu_usage", "metric_memory_usage"}) {
                const auto* usage=item(window,name);
                if (!usage || !usage->isVisible() || !usage->property("text").toString().startsWith('0')) return failed("observed zero remains visible");
            }
            const auto* references=item(window,"metric_memory_references");
            if (!references || !references->isVisible() || !references->property("text").toString().contains("request:")
                || references->property("text").toString().contains("limit:") || references->property("text").toString().contains("not set"))
                return failed("only present metric references are displayed");
            const auto* storage=item(window,"metric_storage_bar");
            const auto* storageUsage=item(window,"metric_storage_usage");
            const auto* storageMeasurement=item(window,"metric_storage_measurement");
            const auto* storageReferences=item(window,"metric_storage_references");
            if (!storage || !storage->isVisible() || !storageUsage || storageUsage->isVisible()
                || !storageMeasurement || storageMeasurement->isVisible() || !storageReferences || !storageReferences->isVisible()
                || !storageReferences->property("text").toString().contains("request:") || storageReferences->property("text").toString().contains("not set"))
                return failed("configured reference remains without inventing usage");
            return server.requests.size() == calls && saveShellFrame(window, requestedScenario)
                && saveShellFrame(window,requestedScenario+"-details",item(window,"radarInfoPanel"));
        }
        if (shortLandscape) {
            auto* water=item(window,"radarWater");
            auto* info=window->findChild<QQuickItem*>("radarInfoPanel");
            auto* pane=radar;
            while (pane && !pane->property("compact").isValid()) pane=pane->parentItem();
            const auto renderedFrame=[&] {
                QSignalSpy frames(window,&QQuickWindow::frameSwapped);
                window->update();
                return waitFor([&] { return !frames.isEmpty(); });
            };
            if (!renderedFrame()) return failed("renderer/requested frame missing");
            if (window->size()!=landscapeSurface || window->contentItem()->size()!=QSizeF(landscapeSurface)
                || !window->property("compactLandscape").toBool()) return failed("geometry/exact short landscape composition");
            if (!water || !info || !pane || pane->property("expanded").toBool()!=mapRadar)
                return failed("geometry/radar composition owner");
            const auto scene=[](QQuickItem* entry) { return entry->mapRectToScene(entry->boundingRect()); };
            const auto geometryValid=[&](bool requireDetails) {
                const auto map=scene(radar),background=scene(water);
                const QRectF windowBounds(0,0,window->width(),window->height());
                if (!(radar->isVisible() && water->isVisible() && map.width()>0 && map.height()>0
                    && background==map && windowBounds.contains(map)
                    && podlord::test::visibleBounds(window,radar).contains(map)
                    && podlord::test::visibleBounds(window,water).contains(background))) return false;
                if (!requireDetails) return !info->isVisible();
                const auto details=scene(info);
                const auto* filterHost=item(window,"radarDetailsOverlayHost");
                const auto* navigation=item(window,"radarNavigationOverlay");
                return info->isVisible() && details.width()>0 && details.height()>0 && windowBounds.contains(details)
                    && podlord::test::visibleBounds(window,info).contains(details)
                    && (filterHost && filterHost->isVisible() ? scene(const_cast<QQuickItem*>(filterHost)).contains(details) && !map.intersects(details)
                        : !map.intersects(details) || (mapRadar && map.contains(details) && navigation && !scene(const_cast<QQuickItem*>(navigation)).intersects(details)));
            };
            if (!geometryValid(false)) return failed("geometry/starved, clipped or reserved empty info");
            if (mapRadar) {
                auto* host=item(window,"workspaceSidebarHost");
                auto* toggle=item(window,"toggleLandscapeFilters");
                const QPointer<QQuickItem> back=item(window,"mapOverview");
                const auto compactTools=[&] {
                    auto* zoom=item(window,"radarZoom");
                    if (!host || !toggle || !back || !zoom || host->width()>80
                        || toggle->width()<44 || radar->height()<zoom->height()
                        || std::abs(scene(back).top()-scene(zoom).top())>1) return false;
                    const auto bounds=scene(pane);
                    for (const auto* name : {"mapOverview",
                            "radarZoomOut","radarZoom"}) {
                        auto* control=item(window,name);
                        if (!control || !control->isVisible() || !bounds.contains(scene(control))
                            || !podlord::test::visibleBounds(window,control).contains(scene(control))) return false;
                    }
                    return true;
                };
                if (!waitFor(compactTools)) return failed("geometry/compact map toolbar or collapsed filter rail");
                const auto camera=radar->property("viewPose");
                if (!click(window,toggle) || !waitFor([&] { return host->width()>=228; }))
                    return failed("geometry/expanded filter controls unavailable");
                if (!back || item(window,"mapOverview")!=back || radar->property("viewPose")!=camera)
                    return failed("geometry/filter expansion replaced navigation or camera");
                if (!click(window,toggle) || !waitFor(compactTools) || radar->property("viewPose")!=camera
                    || server.requests.size()!=calls)
                    return failed("geometry/filter collapse changed camera or requested resources");
            }
            const auto initialMap=scene(radar);
            const QColor background("#061621");
            const auto painted=[&](int index) {
                const auto point=radarPoint(radar,index);
                if (!point || !radarPointVisible(radar,index)
                    || point->path!=workspace.table()->index(index,0).data(Qt::UserRole).toString())
                    return failed("geometry/real resource point outside map");
                const auto frame=window->grabWindow();
                if (frame.isNull()) return failed("renderer/empty window capture");
                const double ratio=double(frame.width())/window->width();
                const auto area=QRectF(scene(radar).topLeft()*ratio,radar->size()*ratio).toAlignedRect().intersected(frame.rect());
                int waterPixels=0;
                for (int y=area.top();y<=area.bottom();++y) for (int x=area.left();x<=area.right();++x)
                    if (frame.pixelColor(x,y)==background) ++waterPixels;
                const auto resourcePixel=podlord::test::radarPixel(window,index);
                if (waterPixels==0 || !resourcePixel.isValid() || resourcePixel==background) {
                    std::fprintf(stderr,"Short landscape paint: resource=%d waterPixels=%d mapPixels=%dx%d resourcePixel=%s\n",
                        index,waterPixels,area.width(),area.height(),qPrintable(resourcePixel.name(QColor::HexArgb)));
                    return failed(waterPixels==0 ? "renderer/water background missing" : "renderer/real resource point missing");
                }
                return true;
            };
            for (const int index : {0,expectedRows-1}) {
                radar->forceActiveFocus();
                QTest::keyClick(window,index==0 ? Qt::Key_Home : Qt::Key_End);
                const auto name=workspace.table()->index(index,0).data(Qt::UserRole+1).toString();
                if (!waitFor([&] {
                    auto* heading=item(window,"radarInfoText");
                    return radarPointVisible(radar,index) && heading && heading->isVisible()
                        && heading->property("text").toString().contains(name) && info->property("hasDetails").toBool();
                })) return failed("geometry/focused real resource and external info unavailable");
                const auto point=radarPoint(radar,index);
                if (!point) return failed("geometry/focused resource point missing");
                QTest::mouseMove(window,radar->mapToScene(point->bounds.center()).toPoint());
                if (!renderedFrame()) return failed("renderer/hover frame missing");
                if (!geometryValid(true) || scene(radar)!=initialMap)
                    return failed("geometry/focus or hover resized map or misplaced overlay");
                if (!painted(index)) return false;
            }
            if (server.requests.size()!=calls || workspace.resourceCount()!=expectedRows || !workspace.inspectorPath().isEmpty())
                return failed("cached radar composition changed requests, filters or inspector");
            return saveShellFrame(window,requestedScenario);
        }
        const auto typeRadarQuery=[&](const QString& value, Qt::Key key=Qt::Key_unknown) {
            // On a phone, leave the modal mini-radar before using the main search,
            // then return through the same public sidebar button a user uses.
            auto* drawer=window->findChild<QObject*>("sidebarDrawer");
            const bool reopen=drawer && drawer->property("visible").toBool();
            if (reopen) {
                QTest::keyClick(window,Qt::Key_Escape);
                if (!waitFor([&] { return !drawer->property("visible").toBool(); })) return false;
            }
            if (!type(window,item(window,"resourceFilter"),value)) return false;
            if (key!=Qt::Key_unknown) QTest::keyClick(window,key);
            if (reopen && !click(window,item(window,"toggleSidebar"))) return false;
            QSignalSpy frame(window,&QQuickWindow::frameSwapped);
            window->update();
            return frame.wait(2000);
        };
        if (scenario=="radar_metadata_focus") {
            QStringList loops;
            QObject observer;
            QObject::connect(&engine,&QQmlEngine::warnings,&observer,[&](const QList<QQmlError>& errors) {
                for (const auto& error:errors) if (error.description().contains("Binding loop detected")) loops.append(error.toString());
            });
            for (const auto& name:{QString("alpha"),QString("bravo"),QString("alpha"),QString("bravo")}) {
                if (!workspace.filterField("name",'"'+name+'"') || !waitFor([&] { return workspace.resourceCount()==1; })) return false;
                const auto path=workspace.table()->index(0,0).data(Qt::UserRole).toString();
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Home);
                if (!waitFor([&] { const auto* target=radarTarget(window,0); return target && target->property("text").toString().contains(name); })) return failed("focused resource metadata");
                if (!workspace.resetResourceFilters() || !waitFor([&] { return workspace.resourceCount()==expectedRows; })) return false;
                if (!waitFor([&] { const auto* target=radarTarget(window,workspace.alertResourceIndex(path));
                    return target && target->property("resourcePath")==path && target->property("text").toString().contains(name); })) return failed("focused identity survives proxy index change");
            }
            if (!loops.isEmpty()) { qWarning() << loops; return failed("acyclic resource metadata updates"); }
            return workspace.inspectorPath().isEmpty() && server.requests.size()==calls;
        }
        if (scenario=="radar_pulse_focus_refresh") {
            auto* metric=item(window,"pulse_pods");
            if (!metric) return failed("available Pod summary control");
            metric->forceActiveFocus();
            const auto focused=[&] { return window->activeFocusItem() && window->activeFocusItem()->objectName()=="pulse_pods"; };
            if (!workspace.filterField("name","\"alpha\"") || !waitFor([&] { return workspace.resourceCount()==1; })
                || !focused()) return failed("metric update preserves keyboard focus");
            if (!workspace.resetResourceFilters() || !waitFor([&] { return workspace.resourceCount()==expectedRows; })
                || !focused() || server.requests.size()!=calls) return failed("repeated metric update preserves focus without requests");
            QTest::keyClick(window,Qt::Key_Space);
            return waitFor([&] { return workspace.workspacePage()=="dashboard"; });
        }
        if (scenario=="radar_tab_traversal") {
            QStringList expected;
            for (int row=0;row<workspace.table()->rowCount();++row) expected.append(workspace.table()->index(row,0).data(Qt::UserRole).toString());
            expected.sort();
            radar->forceActiveFocus();
            for (const auto& path:expected) {
                QTest::keyClick(window,Qt::Key_Tab);
                if (!waitFor([&] { const auto* focused=window->activeFocusItem(); return focused && focused->property("resourcePath")==path; })) return failed("forward virtual resource tab order");
            }
            QTest::keyClick(window,Qt::Key_Backtab);
            if (!waitFor([&] { const auto* focused=window->activeFocusItem(); return focused && focused->property("resourcePath")==expected[expected.size()-2]; })) return failed("reverse virtual resource tab order");
            QTest::keyClick(window,Qt::Key_Tab); QTest::keyClick(window,Qt::Key_Tab);
            if (!waitFor([&] { const auto* focused=window->activeFocusItem(); return focused && !focused->property("resourcePath").isValid(); })) return failed("resource tab traversal exits radar");
            radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Tab); QTest::keyClick(window,Qt::Key_Backtab);
            return waitFor([&] { return radar->hasActiveFocus(); }) && workspace.inspectorPath().isEmpty() && server.requests.size()==calls;
        }
        if (scenario=="radar_touch_inspect") {
            const auto before=radarPoint(radar,0); if (!before) return false;
            const auto start=radar->mapToScene(before->bounds.center()).toPoint();
            std::unique_ptr<QPointingDevice> touch(QTest::createTouchDevice());
            QTest::touchEvent(window,touch.get()).press(0,start,window).commit();
            for (int step=1;step<=6;++step) { QTest::touchEvent(window,touch.get()).move(0,start+QPoint(step*10,step*5),window).commit(); QTest::qWait(10); }
            QTest::touchEvent(window,touch.get()).release(0,start+QPoint(60,30),window).commit();
            if (!waitFor([&] { const auto moved=radarPoint(radar,0); return moved && moved->bounds.center().x()>before->bounds.center().x()+20; })
                || !workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) return failed("touch drag only pans");
            const auto after=radarPoint(radar,0); if (!after) return false;
            const auto point=radar->mapToScene(after->bounds.center()).toPoint();
            QTest::touchEvent(window,touch.get()).press(0,point,window).commit();
            QTest::touchEvent(window,touch.get()).release(0,point,window).commit();
            return waitFor([&] { return workspace.inspectorName()=="alpha" && workspace.canEditYaml(); });
        }
        if (initialFit) {
            const auto completeIsland=[&] {
                auto* tiles=radar->property("tiles").value<QAbstractItemModel*>();
                if (!tiles || tiles->rowCount()!=expectedRows || workspace.radarView().isEmpty()) return false;
                const auto pose=radar->property("viewPose").toMap();
                if (pose!=workspace.radarView()) return false;
                int xRole=-1,yRole=-1;
                const auto roles=tiles->roleNames();
                for (auto role=roles.cbegin();role!=roles.cend();++role) {
                    if (role.value()=="worldX") xRole=role.key();
                    if (role.value()=="worldY") yRole=role.key();
                }
                if (xRole<0 || yRole<0) return false;
                const double zoom=pose.value("zoom").toDouble();
                if (zoom<.01 || zoom>3.4 || (!mapRadar && zoom>=.55)) return false;
                const QRectF viewport(0,0,radar->width(),radar->height());
                for (int row=0;row<tiles->rowCount();++row) {
                    const auto index=tiles->index(row,0);
                    const QPointF center(radar->width()/2+(index.data(xRole).toDouble()+pose.value("x").toDouble())*zoom,
                        radar->height()/2+(index.data(yRole).toDouble()+pose.value("y").toDouble())*zoom);
                    if (!viewport.contains(center)) return false;
                }
                return true;
            };
            if (!waitFor(completeIsland) || !saveShellFrame(window,requestedScenario+"-initial")) return failed("initial whole island fit");
            int inputTargets=0;
            for (auto* child:radar->childItems()) {
                const auto name=child->objectName();
                if (name=="radarFocusTarget" || name=="radarHoverTarget" || name.startsWith("radarTile_")) ++inputTargets;
            }
            if (inputTargets!=2) return failed("bounded input targets for complete island");
            const auto initial=workspace.radarView();
            radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Right); QTest::keyClick(window,Qt::Key_Plus);
            auto chosen=workspace.radarView();
            if (chosen==initial || !workspace.filterField("name","\"radar-0999\"") || !waitFor([&] { return workspace.resourceCount()==1; })
                || workspace.radarView()!=chosen || !workspace.filterField("name","") || !waitFor([&] { return workspace.resourceCount()==expectedRows; })) return failed("fit preserves filtered camera");
            if (!click(window,item(window,"resourcesWorkspaceButton")) || !click(window,item(window,"mapWorkspaceButton"))
                || !typeRadarQuery("radar-0999") || !waitFor([&] { return workspace.resourceCount()==1; })
                || workspace.radarView()!=chosen) return failed("map typing preserves camera");
            QTest::keyClick(window,Qt::Key_Return);
            if (!waitFor([&] { return radar->property("currentIndex").toInt()>=0 && workspace.radarView()!=chosen; })
                || !workspace.inspectorPath().isEmpty()) return failed("explicit map search navigation");
            chosen=workspace.radarView();
            if (!typeRadarQuery("") || !waitFor([&] { return workspace.resourceCount()==expectedRows; })
                || workspace.radarView()!=chosen || (!mapRadar && !click(window,item(window,"resourcesWorkspaceButton")))) return false;
            window->resize(760,640); QTest::qWait(30); window->resize(1440,920);
            if (!waitFor([&] { return radar->property("viewPose").toMap()==chosen; }) || server.requests.size()!=calls) return failed("fit preserves resized camera");
            const auto before=server.requests.size();
            if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && server.requests.size()>before; })
                || workspace.radarView()!=chosen) return failed("fit preserves refreshed camera");
            radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_0);
            const QVariantMap reset{{"x",0},{"y",0},{"zoom",1}};
            QTest::qWait(50);
            return workspace.radarView()==reset && radar->property("viewPose").toMap()==reset
                && saveShellFrame(window,requestedScenario+"-reset");
        }
        if (scenario=="radar_label_contrast") {
            if (!click(window,item(window,"radarZoomOut"))) return failed("label contrast setup");
            const auto luminance=[](const QColor& color) {
                const auto linear=[](double value) { return value<=.04045 ? value/12.92 : std::pow((value+.055)/1.055,2.4); };
                return .2126*linear(color.redF())+.7152*linear(color.greenF())+.0722*linear(color.blueF());
            };
            const auto output=qEnvironmentVariable("PODLORD_RADAR_ACTION_EVIDENCE");
            if (!output.isEmpty() && !QDir().mkpath(output)) return false;
            for (const auto& theme:workspace.themeNames()) for (const auto& variant:{QString("dark"),QString("light")}) {
                if (!workspace.saveAppearance(theme,variant) || !waitFor([&] { return !workspace.busy(); })) return false;
                for (const bool dimmed:{false,true}) {
                    if (!typeRadarQuery(dimmed ? "no-such-resource" : "")) return false;
                    const auto foreground=dimmed ? QColor("#DDE7EC").darker(140) : QColor("#DDE7EC");
                    const auto contrast=(luminance(foreground)+.05)/(luminance(QColor("#061621"))+.05);
                    if (contrast<4.5) return failed("label contrast ratio");
                    if (mapRadar) {
                        const auto* caption = item(window,"mapTitle");
                        if (!caption) return failed("map caption");
                        const auto ink = luminance(caption->property("color").value<QColor>());
                        const auto water = luminance(QColor("#061621"));
                        if ((std::max(ink,water)+.05)/(std::min(ink,water)+.05)<4.5) {
                            qWarning() << "Map caption contrast" << theme << variant << caption->property("color");
                            return failed("map caption contrast ratio");
                        }
                    }
                    QImage image;
                    const bool rendered=waitFor([&] {
                        image=window->grabWindow();
                        if (image.isNull()) return false;
                        const double scale=double(image.width())/window->width();
                        const auto area=QRectF(radar->mapToScene(QPointF{})*scale,radar->size()*scale).toAlignedRect().intersected(image.rect());
                        int ink=0;
                        for (int y=area.top();y<=area.bottom();++y) for (int x=area.left();x<=area.right();++x)
                            if (image.pixel(x,y)==foreground.rgba()) ++ink;
                        return ink>5;
                    });
                    if (!rendered) { qWarning()<<"Missing rendered radar label"<<theme<<variant<<dimmed; return false; }
                    if (!output.isEmpty() && !image.save(output+"/"+requestedScenario+"-"+QString(theme).replace(' ','-')+"-"+variant
                        +(dimmed ? "-dimmed-" : "-active-")+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png")) return false;
                }
            }
            return server.requests.size()==calls;
        }
        if (scenario.startsWith("radar_info_panel")) {
            if (mapRadar && !waitFor([&] {
                const auto* host=item(window,"resourceMapHost");
                if (!host || !host->isVisible()) return false;
                const auto expected=host->mapRectToScene(QRectF(QPointF{},host->size())).adjusted(4,4,-4,-4);
                const auto actual=radar->mapRectToScene(QRectF(QPointF{},radar->size()));
                return qAbs(actual.x()-expected.x())<=1 && qAbs(actual.y()-expected.y())<=1
                    && qAbs(actual.width()-expected.width())<=1 && qAbs(actual.height()-expected.height())<=1
                    && !window->grabWindow().isNull();
            })) return failed("expanded map renders at its full visible size");
            const auto initialViewport=radar->mapRectToScene(QRectF(QPointF{},radar->size()));
            const auto first=radarPoint(radar,0);
            if (!first) return failed("first resource hover target");
            const auto firstName=workspace.table()->index(0,0).data(Qt::UserRole+1).toString();
            QTest::mouseMove(window,radar->mapToScene(first->bounds.center()).toPoint());
            if (!waitFor([&] { const auto* heading=item(window,"radarInfoText"); return heading && heading->property("text").toString().contains(firstName); })) return failed("pointer reveals cached details");
            if (window->width() >= 1000) {
                const auto point=radar->mapToScene(first->bounds.center());
                if (!waitFor([&] {
                    const auto* details=item(window,"radarInfoPanel");
                    if (!details || !details->isVisible()) return false;
                    const auto bounds=details->mapRectToScene(details->boundingRect());
                    const auto nearest=QPointF(std::clamp(point.x(),bounds.left(),bounds.right()),
                        std::clamp(point.y(),bounds.top(),bounds.bottom()));
                    return !bounds.contains(point) && QLineF(point,nearest).length()<=20
                        && QRectF(0,0,window->width(),window->height()).contains(bounds)
                        && radar->mapRectToScene(QRectF(QPointF{},radar->size()))==initialViewport;
                })) return failed("wide-screen pointer details stay near the resource without covering it");
                if (!saveShellFrame(window,requestedScenario+"-pointer")) return false;
            }
            QTest::mouseMove(window,radar->mapToScene(QPointF(8,8)).toPoint());
            QTest::qWait(60);
            auto* reader=item(window,"radarInfoScroll");
            if (!reader) return failed("external info reader");
            QTest::mouseMove(window,QPoint(1,1),0);
            if (!waitFor([&] { const auto* details=item(window,"radarInfoPanel"); return !details || !details->isVisible(); }))
                return failed("hover-only details close when the pointer leaves the radar");
            for (const auto* name : {"metric_cpu_bar","metric_memory_bar","metric_storage_bar"})
                if (item(window,name)) return failed("missing metrics do not create placeholder gauges");
            radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Home);
            auto* tip=item(window,"radarInfoPanel");
            if (!tip || !waitFor([&] { const auto* text=item(window,"radarInfoText"); return text && text->isVisible() && !text->property("text").toString().isEmpty(); })) return failed("cached info appears outside map");
            auto* scroll=item(window,"radarInfoScroll");
            auto* tile=radarTarget(window,0);
            if (!scroll || !tile) return failed("info public controls");
            if (!waitFor([&] {
                const auto popup=tip->mapRectToScene(QRectF(QPointF{},tip->size()));
                const auto viewport=radar->mapRectToScene(QRectF(QPointF{},radar->size()));
                const auto* filterHost=item(window,"radarDetailsOverlayHost");
                const auto* navigation=item(window,"radarNavigationOverlay");
                const bool outsideMap=filterHost && filterHost->isVisible() ? filterHost->mapRectToScene(QRectF(QPointF{},filterHost->size())).contains(popup) && !viewport.intersects(popup)
                    : !viewport.intersects(popup) || (mapRadar && viewport.contains(popup) && navigation && !navigation->mapRectToScene(QRectF(QPointF{},navigation->size())).intersects(popup));
                return viewport==initialViewport && outsideMap && QRectF(0,0,window->width(),window->height()).contains(popup);
            })) {
                const auto* host=item(window,"radarDetailsOverlayHost");
                qWarning() << "Map info geometry" << initialViewport
                    << radar->mapRectToScene(QRectF(QPointF{},radar->size()))
                    << tip->mapRectToScene(QRectF(QPointF{},tip->size()))
                    << (host && host->isVisible()) << (host ? host->mapRectToScene(QRectF(QPointF{},host->size())) : QRectF{});
                saveShellFrame(window,requestedScenario+"-failure");
                return failed("info keeps the entire map unobscured and stable");
            }
            auto* contents=scroll->property("contentItem").value<QQuickItem*>();
            if (!contents) return failed("info scroll surface");
            const auto pose=radar->property("viewPose");
            if (contents->property("contentHeight").toDouble()>contents->height()+1) {
                const auto before=contents->property("contentY").toDouble();
                scroll->forceActiveFocus(Qt::TabFocusReason);
                if (!waitFor([&] { return scroll->hasActiveFocus() && tip->property("visible").toBool(); })) return failed("info reader focus");
                QTest::keyClick(window,Qt::Key_Down);
                if (!waitFor([&] { return contents->property("contentY").toDouble()>before; })) return failed("info keyboard scrolling");
                const auto afterKey=contents->property("contentY").toDouble();
                const auto point=scroll->mapToScene(QPointF(scroll->width()/2,scroll->height()/2));
                QTest::mouseMove(window,point.toPoint());
                // A short overflow can already be at its end after one key step.
                QElapsedTimer wheelClock; wheelClock.start();
                QWheelEvent rewind(point,window->mapToGlobal(point.toPoint()),{},QPoint(0,1200),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                rewind.setTimestamp(static_cast<ulong>(wheelClock.msecsSinceReference()));
                QCoreApplication::sendEvent(window,&rewind);
                if (!waitFor([&] { return contents->property("contentY").toDouble()<afterKey
                        && qAbs(contents->property("contentY").toDouble())<0.5 && !contents->property("moving").toBool(); })
                    || radar->property("viewPose")!=pose) {
                    std::fprintf(stderr,"Info rewind: keyY=%g currentY=%g content=%g viewport=%g cameraChanged=%d focus=%s target=%d\n",
                        afterKey,contents->property("contentY").toDouble(),contents->property("contentHeight").toDouble(),contents->height(),
                        radar->property("viewPose")!=pose,qPrintable(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}),tip->property("hasDetails").toBool());
                    return failed("info reverse wheel isolates camera");
                }
                const auto beforeWheel=contents->property("contentY").toDouble();
                QWheelEvent wheel(point,window->mapToGlobal(point.toPoint()),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                wheel.setTimestamp(std::max<quint64>(rewind.timestamp()+1,static_cast<quint64>(wheelClock.msecsSinceReference()+wheelClock.elapsed())));
                QCoreApplication::sendEvent(window,&wheel);
                const bool scrolled=waitFor([&] { return contents->property("contentY").toDouble()>beforeWheel; });
                if (!scrolled || radar->property("viewPose")!=pose) {
                    std::fprintf(stderr,"Info wheel: start=%g end=%g content=%g viewport=%g moving=%d accepted=%d cameraChanged=%d visible=%d\n",
                        beforeWheel,contents->property("contentY").toDouble(),contents->property("contentHeight").toDouble(),contents->height(),
                        contents->property("moving").toBool(),wheel.isAccepted(),radar->property("viewPose")!=pose,tip->property("visible").toBool());
                    return failed("info wheel isolates camera");
                }
            }
            scroll->forceActiveFocus(Qt::TabFocusReason);
            if (!waitFor([&] { return scroll->hasActiveFocus(); })) return failed("info can receive keyboard focus");
            const auto next=radarPoint(radar,1);
            if (!next) return failed("second resource remains reachable");
            const auto nextName=workspace.table()->index(1,0).data(Qt::UserRole+1).toString();
            QTest::mouseMove(window,radar->mapToScene(next->bounds.center()).toPoint());
            if (!waitFor([&] { const auto* text=item(window,"radarInfoText"); return text && text->property("text").toString().contains(nextName); })) return failed("hover remains live after reading info");
            if (!saveShellFrame(window,requestedScenario) || !saveShellFrame(window,requestedScenario+"-details",tip)) return failed("info frame capture");
            auto* closeControl=item(window,"closeRadarInfo");
            if (!closeControl || !podlord::test::visibleBounds(window,closeControl).contains(closeControl->mapRectToScene(closeControl->boundingRect())))
                return failed("details close action must remain visible after scrolling");
            const auto selectedBeforeClose=radar->property("currentIndex");
            const auto cameraBeforeClose=radar->property("viewPose");
            QSignalSpy closeActivated(closeControl,SIGNAL(clicked()));
            const bool closeClicked=click(window,closeControl) && waitFor([&] { return !closeActivated.isEmpty(); },2000);
            const bool detailsClosed=waitFor([&] { return !tip->isVisible(); });
            if (!closeClicked || !detailsClosed
                || radar->property("currentIndex")!=selectedBeforeClose || radar->property("viewPose")!=cameraBeforeClose
                || radar->mapRectToScene(radar->boundingRect())!=initialViewport) {
                qWarning() << "Radar details close:" << "clicked" << closeClicked << "closed" << detailsClosed
                    << "selection" << selectedBeforeClose << radar->property("currentIndex")
                    << "camera" << cameraBeforeClose << radar->property("viewPose")
                    << "canvas" << initialViewport << radar->mapRectToScene(radar->boundingRect())
                    << "focus" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{});
                saveShellFrame(window,requestedScenario+"-close-failure");
                return failed("closing details preserves selection and drawing area");
            }
            radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Home);
            if (!waitFor([&] { return tip->isVisible(); })) return failed("closed details can reopen through keyboard navigation");
            const auto selectedBeforeEscape=radar->property("currentIndex");
            QTest::keyClick(window,Qt::Key_Escape);
            if (!waitFor([&] { return !tip->isVisible(); }) || radar->property("currentIndex")!=selectedBeforeEscape)
                return failed("Escape closes details without clearing selection");
            QTest::keyClick(window,Qt::Key_Home);
            if (!waitFor([&] { return tip->isVisible(); }) || radar->property("currentIndex")!=selectedBeforeEscape
                || !click(window,item(window,"closeRadarInfo")) || !waitFor([&] { return !tip->isVisible(); }))
                return failed("explicit focus reopens details after dismissal without changing resource identity");
            auto* control=item(window,"radarZoom"); QSignalSpy activated(control,SIGNAL(clicked()));
            if (!click(window,control) || !waitFor([&] { return !activated.isEmpty(); })) return failed("info leaves zoom operable");
            if (!typeRadarQuery("\"no-matching-resource\"")
                || !waitFor([&] { return workspace.resourceCount()==0 && !tip->property("hasDetails").toBool(); })) return failed("filter removes stale external details");
            return server.requests.size()==calls;
        }
        if (scenario.startsWith("radar_accessibility_")) {
            const auto exposedResources = [&] {
                QStringList names;
                QList<QAccessibleInterface*> pending{QAccessible::queryAccessibleInterface(window)};
                for (qsizetype index = 0; index < pending.size() && index < 10000; ++index) {
                    auto* entry = pending[index];
                    if (!entry || !entry->isValid() || entry->state().invisible) continue;
                    const auto name = entry->text(QAccessible::Name);
                    if (entry->role() == QAccessible::Button && (name.startsWith("Pod ") || name.startsWith("Widget "))) names.append(name);
                    for (int child = 0; child < entry->childCount(); ++child) pending.append(entry->child(child));
                }
                return names;
            };
            if (exposedResources().size() > 1) return failed("unbounded accessibility resources");
            auto* overview = QAccessible::queryAccessibleInterface(radar);
            if (!overview || overview->role() != QAccessible::Grouping
                || overview->text(QAccessible::Name) != "Resource radar, 1001 resources") return failed("accessible overview");
            if (scenario.endsWith("focus")) {
                radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
                const auto first = workspace.table()->data(workspace.table()->index(0,0), Qt::UserRole+1).toString();
                if (!waitFor([&] { const auto names = exposedResources(); return names.size() == 1 && names.first().contains(first); })) return failed("accessible first resource");
                QTest::keyClick(window, Qt::Key_End);
                const auto last = workspace.table()->data(workspace.table()->index(expectedRows-1,0), Qt::UserRole+1).toString();
                if (!waitFor([&] { const auto names = exposedResources(); return names.size() == 1 && names.first().contains(last); })) return failed("accessible last resource");
            }
            return workspace.resourceCount() == expectedRows && server.requests.size() == calls;
        }
        if (scenario == "radar_compact_controls") {
            for (const auto* name : {"radarZoomOut", "radarZoom"}) {
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
            if ((scenario=="radar_water_motion" || scenario=="radar_water_reopen")
                && (!workspace.saveRadarWater(true,100) || !waitFor([&] { return !workspace.busy() && water->property("speedPercent").toInt()==100; }))) return false;
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
            if (scenario.startsWith("radar_water_background")) {
                if (scenario.endsWith("_light") && (!workspace.saveAppearance("Imperial Ledger", "light")
                    || !waitFor([&] { return !workspace.busy(); }))) return false;
                if (!click(window,item(window,"settingsWorkspaceButton"))
                    || !click(window,item(window,"settingsAppearanceSection"))
                    || !stopWater()
                    || !click(window,item(window,"resourcesWorkspaceButton")) || !still()) return false;
                const auto image=capture(QRectF(8,8,100,80));
                int waterPixels=0;
                for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
                    if (image.pixelColor(x,y)==QColor("#061621")) ++waterPixels;
                if (waterPixels<=image.width()*image.height()/2)
                    std::fprintf(stderr,"Reference water background missing: %d of %d pixels.\n",waterPixels,image.width()*image.height());
                return !image.isNull() && waterPixels>image.width()*image.height()/2 && server.requests.size()==calls;
            }
            if (scenario=="radar_water_rules_disabled") {
                if (!workspace.alerts()->reload() || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
                for (const auto& value:workspace.alerts()->rules()) if (value.toMap()["enabled"].toBool()) return false;
                return workspace.alerts()->rules().size()==3 && server.requests.size()==calls;
            }
            if (scenario=="radar_water_interaction") {
                QImage paused;
                bool sampledPause=false;
                for (int attempt=0;attempt<4 && !sampledPause;++attempt) {
                    radar->forceActiveFocus();
                    QElapsedTimer interaction; interaction.start();
                    QTest::keyClick(window,Qt::Key_Right); QTest::qWait(15);
                    const auto first=capture(QRectF(8,8,100,80));
                    QTest::qWait(35);
                    const auto second=capture(QRectF(8,8,100,80));
                    if (interaction.elapsed()>=190) continue;
                    if (first!=second) { std::fprintf(stderr,"Radar water moved during interaction pause.\n"); return false; }
                    paused=second;
                    sampledPause=true;
                }
                if (!sampledPause) { std::fprintf(stderr,"Could not sample the 200 ms radar water pause under load.\n"); return false; }
                const bool resumed=waitFor([&] { return paused!=capture(QRectF(8,8,100,80)); },1000);
                if (!resumed) std::fprintf(stderr,"Radar water did not resume within one second: playing=%d speed=%d visible=%d\n",
                    water->property("playing").toBool(),water->property("speedPercent").toInt(),water->isVisible());
                return resumed && server.requests.size()==calls;
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
                for (int index=0;index<expectedRows;++index) if (workspace.table()->index(index,0).data(Qt::UserRole+1)==name) target=radarTarget(window,index);
                if (!target || !displayedWithin(radar,target)) return false;
                const auto center=target->mapToItem(radar,QPointF(target->width()/2,target->height()/2));
                if (scenario=="radar_water_event_tile") {
                    const auto tileWidth=target->width();
                    const QRectF around(center-QPointF(18,18),QSizeF(36,36));
                    const auto before=capture(around);
                    if (!type(window,item(window,"resourceFilter"),"custom-widget") || !waitFor([&] { return radar->property("count").toInt()==1; })) return false;
                    QTest::qWait(100);
                    const auto widget=radarPoint(radar,0);
                    if (!widget || workspace.table()->index(0,0).data(Qt::UserRole+1)!="custom-widget"
                        || widget->path!=workspace.table()->index(0,0).data(Qt::UserRole)) return false;
                    const QRectF eventArea(center-QPointF(tileWidth/2,tileWidth/2),QSizeF(tileWidth,tileWidth));
                    const auto widgetArea=widget->bounds;
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
                if (!image.isNull() && image.pixelColor(0,0)!=expected) std::fprintf(stderr,"Terrain pixel: actual=%s expected=%s focused=%d effect=%s\n",qPrintable(image.pixelColor(0,0).name()),qPrintable(expected.name()),target->hasActiveFocus(),QJsonDocument(QJsonObject::fromVariantMap(target->property("alertEffect").toMap())).toJson(QJsonDocument::Compact).constData());
                return !image.isNull() && image.pixelColor(0,0)==expected && server.requests.size()==calls;
            }
            if (scenario=="radar_water_hidden") return click(window,item(window,"toggleSidebar")) && waitFor([&] { return !radar->isVisible(); }) && still() && server.requests.size()==calls;
            if (scenario=="radar_water_minimized") { window->setVisibility(QWindow::Minimized); return still() && server.requests.size()==calls; }
            if (scenario=="radar_water_reduced") return workspace.alerts()->setPreferences(true,true) && waitFor([&] { return !workspace.alerts()->busy(); }) && still() && server.requests.size()==calls;
            if (scenario=="radar_water_legacy_disabled" || scenario=="radar_water_zero" || scenario=="radar_water_settings") {
                if (scenario=="radar_water_legacy_disabled"
                    && (!workspace.saveRadarWater(false,45) || !waitFor([&] { return !workspace.busy(); }))) return false;
                if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"settingsAppearanceSection"))) return false;
                if (scenario=="radar_water_legacy_disabled") {
                    auto* speed=item(window,"inlineRadarWaterSpeed"); if (!speed || !podlord::test::scrollIntoView(window,speed)) return false;
                    if (speed->property("value").toInt()!=0 || item(window,"inlineRadarWaterEnabled")) return false;
                    speed->forceActiveFocus(Qt::TabFocusReason);
                    QTest::keyClick(window,Qt::Key_Right);
                    return waitFor([&] { return !workspace.busy() && workspace.radarWaterEnabled() && workspace.radarWaterSpeedPercent()==5; })
                        && server.requests.size()==calls;
                }
                if (!stopWater()) return false;
                if (!waitFor([&] { return !workspace.busy(); }) || !click(window,item(window,"resourcesWorkspaceButton")) || !still()) return false;
                if (scenario!="radar_water_settings") return server.requests.size()==calls;
                if (!workspace.reload() || !waitFor([&] { return !workspace.busy(); })) return false;
                if (!click(window,item(window,"settingsWorkspaceButton")) || !click(window,item(window,"settingsAppearanceSection"))) return false;
                auto* speed=item(window,"inlineRadarWaterSpeed");
                if (!speed || !podlord::test::scrollIntoView(window,speed) || speed->property("value").toInt()!=0 || item(window,"inlineRadarWaterEnabled")) return false;
                speed->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Right);
                if (!waitFor([&] { return !workspace.busy() && workspace.radarWaterSpeedPercent()==5 && workspace.radarWaterEnabled(); })
                    || !workspace.reload() || !waitFor([&] { return !workspace.busy(); })) return false;
                return workspace.radarWaterSpeedPercent()==5 && workspace.radarWaterEnabled() && server.requests.size()==calls;
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
                if (!click(window,item(window,"toggleSidebar"))) return failed("water close toolbar");
                if (!still()) return failed("water hidden clock");
                if (!click(window,item(window,"toggleSidebar"))) return failed("water reopen toolbar");
                frames.clear(); QTest::qWait(650);
                if (frames.size()<2 || server.requests.size()!=calls) {
                    std::fprintf(stderr,"Reopened water: frames=%lld playing=%d visible=%d calls=%d initial=%d\n",
                        static_cast<long long>(frames.size()),water->property("playing").toBool(),water->isVisible(),int(server.requests.size()),int(calls));
                    return failed("water restored clock");
                }
                return true;
            }
            return true;
        }
        if (scenario.startsWith("radar_island_")) {
            const auto first=radarPoint(radar,0);
            if (!first || !radarPointVisible(radar,0)) return false;
            const auto initial=first->bounds.center();
            const auto initialViewport = radar->size();
            const auto initialWidth=first->bounds.width();
            if (scenario == "radar_island_layout") {
                const auto second=radarPoint(radar,1);
                return second && radarPointVisible(radar,1) && !first->bounds.intersects(second->bounds)
                    && server.requests.size() == calls;
            }
            radar->forceActiveFocus();
            if (scenario == "radar_island_selection") {
                QTest::keyClick(window,Qt::Key_Home);
                if (!typeRadarQuery("bravo") || !waitFor([&] { return radar->property("count").toInt()==1; })) return failed("selection filter");
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Return); QTest::qWait(50);
                if (!workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) {
                    qWarning() << "Filtered selection" << workspace.inspectorPath() << "calls" << server.requests.size() << calls;
                    return failed("selection hidden identity inert");
                }
                QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return);
                if (!waitFor([&] { return workspace.inspectorName()=="bravo" && workspace.canEditYaml(); })) {
                    qWarning() << "Selected Radar target" << workspace.inspectorName() << workspace.canEditYaml() << "focus" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{});
                    return failed("selection explicit replacement");
                }
                return true;
            }
            if (scenario == "radar_island_refresh") {
                if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && server.requests.size()>calls; })) return false;
                return waitFor([&] {
                    const auto tile=radarPoint(radar,0);
                    return tile && QLineF(initial,tile->bounds.center()).length()<1;
                });
            }
            if (scenario == "radar_island_sort") {
                auto* drawer=window->findChild<QObject*>("sidebarDrawer");
                if (drawer && drawer->property("visible").toBool()) {
                    QTest::keyClick(window,Qt::Key_Escape);
                    if (!waitFor([&] { return !drawer->property("visible").toBool(); })) return failed("close phone radar before table input");
                }
                if (!click(window,item(window,"resourcesWorkspaceButton"))) return false;
                auto* header=item(window,"header_0");
                auto* viewport=item(window,"resourceHeaderView");
                if (!header || !viewport) return false;
                // A frozen-column layout can expose only part of a header on a
                // phone. Tap that real visible part, not its offscreen center.
                const auto area=header->mapRectToScene(QRectF(QPointF{},header->size()))
                    .intersected(viewport->mapRectToScene(QRectF(QPointF{},viewport->size())))
                    .intersected(QRectF(0,0,window->width(),window->height()));
                if (area.isEmpty()) return false;
                if (!saveShellFrame(window,requestedScenario+"-before-sort")) return false;
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,area.center().toPoint());
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,area.center().toPoint());
                if (!podlord::test::focusResourceRadar(window,mapRadar)) return false;
                const bool retained=waitFor([&] { const auto tile=radarPoint(radar,2); const auto expected=workspace.table()->index(2,0); return tile && expected.data(Qt::UserRole+1)=="alpha" && tile->path==expected.data(Qt::UserRole) && QLineF(initial,tile->bounds.center()).length()<1; });
                if (!retained) {
                    saveShellFrame(window,requestedScenario+"-sort-failure");
                    std::fprintf(stderr,"Sorted Radar: last name=%s, viewport=%gx%g, calls=%lld/%lld\n",
                        qPrintable(workspace.table()->index(2,0).data(Qt::UserRole+1).toString()),radar->width(),radar->height(),static_cast<long long>(server.requests.size()),static_cast<long long>(calls));
                    return failed("sort retains identity and position");
                }
                return server.requests.size()==calls;
            }
            if (scenario == "radar_island_session") {
                const auto session=workspace.currentSession(); QTest::keyClick(window,Qt::Key_Right); QTest::keyClick(window,Qt::Key_Down);
                const auto savedPose=radar->property("viewPose").toMap();
                if (savedPose["x"].toDouble()!=-24 || savedPose["y"].toDouble()!=-24 || savedPose["zoom"].toDouble()!=1) return false;

                if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return workspace.currentSession()!=session && !workspace.busy() && !workspace.loading(); },10000)) return false;
                if (!click(window,item(window,"resourcesWorkspaceButton"))) return false;
                const auto view=workspace.radarView();
                if (!podlord::RadarIsland::validPose(view) || view==savedPose) return false;
                if (!click(window,item(window,"activateSession_"+session))) return false;
                if (!waitFor([&] { return workspace.currentSession()==session && workspace.radarView()==savedPose && radar->property("viewPose").toMap()==savedPose; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_0);
                return waitFor([&] { const auto tile=radarPoint(radar,0); return tile && QLineF(initial,tile->bounds.center()).length()<1; });
            }
            if (scenario == "radar_island_pan") {
                QTest::keyClick(window, Qt::Key_Right); QTest::keyClick(window, Qt::Key_Down);
                const auto expected = initial - QPointF(24,24);
                const bool remainsVisible = QRectF(expected - QPointF(initialWidth/2,initialWidth/2), QSizeF(initialWidth,initialWidth)).intersects(QRectF(QPointF{},radar->size()));
                if (!waitFor([&] {
                    const auto tile=radarPoint(radar,0);
                    const auto pose = radar->property("viewPose").toMap();
                    return pose["x"].toDouble() == -24 && pose["y"].toDouble() == -24
                        && (tile ? QLineF(expected,tile->bounds.center()).length()<1 : !remainsVisible);
                })) {
                    const auto pose = radar->property("viewPose").toMap();
                    const auto tile=radarPoint(radar,0);
                    const auto actual=tile ? tile->bounds.center() : QPointF{};
                    std::fprintf(stderr, "Radar key pan: focus=%d; pose=%g,%g; expected=%g,%g; actual=%g,%g; calls=%lld/%lld\n", radar->hasActiveFocus(), pose["x"].toDouble(), pose["y"].toDouble(), initial.x()-24, initial.y()-24, actual.x(), actual.y(), static_cast<long long>(server.requests.size()), static_cast<long long>(calls));
                    return false;
                }
                QTest::keyClick(window, Qt::Key_0);
                if (!waitFor([&] { const auto tile=radarPoint(radar,0); return tile && QLineF(initial,tile->bounds.center()).length()<1; })) return false;
                if (!typeRadarQuery("",Qt::Key_Left)) return false;
                const auto restored=radarPoint(radar,0);
                return restored && QLineF(initial,restored->bounds.center()).length()<1 && server.requests.size()==calls;
            }
            if (scenario == "radar_island_filter") {
                const auto second=radarPoint(radar,1);
                if (!second || !radarPointVisible(radar,1)) return failed("filter source visible");
                const auto greyPosition=second->bounds.center();
                QPointF greyPoint;
                if (!typeRadarQuery("alpha") || !waitFor([&] { return radar->property("count").toInt()==1; })) {
                    auto* input=item(window,"resourceFilter");
                    qWarning() << "Radar filter input" << input->isVisible() << input->isEnabled() << input->property("text")
                        << "focus" << (window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{}) << "window" << window->size();
                    return failed("filter input");
                }
                const auto viewportShift=[&] { return QPointF((radar->width()-initialViewport.width())/2,(radar->height()-initialViewport.height())/2); };
                if (!waitFor([&] { const auto tile=radarPoint(radar,0); return tile && QLineF(initial+viewportShift(),tile->bounds.center()).length()<=1; })) {
                    qWarning() << "Filtered Radar position" << initial << "viewport" << radar->size();
                    return failed("filter stable position");
                }
                QColor terrain;
                if (!waitFor([&] { greyPoint=radar->mapToScene(greyPosition+viewportShift()); const auto image=window->grabWindow(); const auto scale=double(image.width())/window->width(); terrain=image.pixelColor(qRound(greyPoint.x()*scale),qRound(greyPoint.y()*scale)); return terrain==QColor("#50575B"); })) {
                    qWarning() << "Filtered Radar terrain" << terrain << "point" << greyPoint;
                    return failed("filter muted terrain");
                }
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,greyPoint.toPoint());
                if (!workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) return failed("filter muted tile inert");
                return typeRadarQuery("") && waitFor([&] { return radar->property("count").toInt()==expectedRows; }) && server.requests.size()==calls;
            }
            if (scenario == "radar_island_drag") {
                const auto start=radar->mapToScene(initial).toPoint();
                QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
                for (int step=1;step<=6;++step) { QTest::mouseMove(window,start+QPoint(step*10,step*5)); QTest::qWait(10); }
                QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(60,30));
                if (!waitFor([&] { const auto tile=radarPoint(radar,0); return tile && tile->bounds.left()>initial.x()+20; }) || !workspace.inspectorPath().isEmpty() || server.requests.size()!=calls) return false;
                const auto tile=radarPoint(radar,0);
                if (!tile) return false;
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,radar->mapToScene(tile->bounds.center()).toPoint());
                return waitFor([&] { return workspace.inspectorName()=="alpha" && workspace.canEditYaml(); });
            }
            const auto at=radar->mapToScene(initial);
            const bool trackpad=scenario=="radar_island_trackpad_zoom";
            const bool zoomOut=scenario=="radar_island_zoom_out";
            const bool horizontal=scenario=="radar_island_scroll_horizontal";
            QWheelEvent wheel(at,window->mapToGlobal(at.toPoint()),trackpad ? QPoint(18,120) : horizontal ? QPoint(18,0) : QPoint{},trackpad || horizontal ? QPoint{} : QPoint(0,zoomOut ? -120 : 120),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QCoreApplication::sendEvent(window,&wheel);
            return waitFor([&] {
                const auto tile=radarPoint(radar,0);
                const bool widthMatches=tile && (horizontal ? qAbs(tile->bounds.width()-initialWidth)<.01 : zoomOut ? tile->bounds.width()<initialWidth/1.1 : tile->bounds.width()>initialWidth*1.1);
                return widthMatches && QLineF(initial,tile->bounds.center()).length()<1;
            }) && server.requests.size()==calls;
        }
        if (scenario.startsWith("radar_pooled_")) {
            const bool moving = scenario != "radar_pooled_none" && scenario != "radar_pooled_outline";
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            const auto remainsIdle = [&] {
                QElapsedTimer quiet; quiet.start();
                QObject observer;
                QObject::connect(window, &QQuickWindow::frameSwapped, &observer, [&] { quiet.restart(); });
                if (!waitFor([&] { return quiet.elapsed() >= 250; })) return false;
                frames.clear(); QTest::qWait(250);
                if (frames.size() > 1)
                    std::fprintf(stderr, "Idle radar caused %lld frames after quiescence.\n", static_cast<long long>(frames.size()));
                return frames.size() <= 1;
            };
            if (moving && !waitFor([&] { return frames.size() >= 4; })) return false;
            if (!moving && !remainsIdle()) return false;
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(radar, radarTarget(window,1000)); })) return false;
            const int panSteps=int(std::ceil(radar->width()/24))+2;
            for (int step=0;step<panSteps;++step) QTest::keyClick(window, Qt::Key_Right);
            if (!waitFor([&] { return !radarPointVisible(radar,0); })) return false;
            if (!remainsIdle()) return false;
            QTest::keyClick(window, Qt::Key_Home);
            if (!waitFor([&] { return displayedWithin(radar, radarTarget(window,0)); })) return false;
            QTest::qWait(100); frames.clear();
            if (moving && !waitFor([&] { return frames.size() >= 4; })) return false;
            if (!moving && !remainsIdle()) return false;
            return server.requests.size() == calls;
        }
        if (scenario == "radar_initial_population") {
            const auto firstRow=radarPoint(radar,0);
            return firstRow && radarPointVisible(radar,0) && workspace.table()->index(0,0).data(Qt::UserRole+1)=="radar-0000"
                && firstRow->path==workspace.table()->index(0,0).data(Qt::UserRole)
                && server.requests.size() == calls;
        }
        if (scenario == "radar_reopen_position") {
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(radar, radarTarget(window,1000)); })) return false;
            if (!click(window, item(window, "toggleSidebar")) || !waitFor([&] { return !radar->isVisible(); })) return false;
            if (!podlord::test::focusResourceRadar(window,mapRadar)) return false;
            return waitFor([&] { return displayedWithin(radar, radarTarget(window,1000)); }) && server.requests.size() == calls;
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
            if (!waitFor([&] { return !alerts->busy(); })) return failed("load catalog");
            for (const auto& value : alerts->rules()) {
                auto rule = value.toMap(); rule["enabled"] = false;
                if (!alerts->saveRule(rule) || !waitFor([&] { return !alerts->busy(); })) return failed("disable defaults");
            }
            if (!alerts->setPreferences(true, false) || !waitFor([&] { return !alerts->busy(); })) return failed("set preferences");
            QVariantMap rule{{"id", ""}, {"name", "Rendered radar action"}, {"description", "Local visual action"},
                {"enabled", true}, {"builtIn", false}, {"draft", false},
                {"groups", QVariantList{QVariant(QVariantList{QVariantMap{{"field", "name"}, {"expression", "\"alpha\""}}})}},
                {"color", "#0088ff"}, {"colorMode", "no-match"}, {"colorSeconds", 5},
                {"animation", "none"}, {"animationMode", "no-match"}, {"animationSeconds", 5},
                {"zoom", 0}, {"sound", "none"}, {"soundMinimumMatches", 1}};
            if (!alerts->saveRule(rule) || !waitFor([&] { return !alerts->busy(); })) return failed("save rule");
            if (!workspace.filter("\"alpha\"") || !waitFor([&] { return workspace.resourceCount() == 1; })) return failed("select target");
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
            if (!waitFor([&] { return radarTarget(window,0) != nullptr; })) return failed("render target");
            auto* tile = radarTarget(window,0);
            const auto path = workspace.table()->data(workspace.table()->index(0, 0), Qt::UserRole).toString();
            if (!waitFor([&] { return alerts->effect(path).value("color").toString() == "#0088ff"; })) return failed("apply color");
            const auto renderedTile = [&] {
                const auto frame = window->grabWindow();
                const auto position = tile->mapToScene({-2, -2});
                const double ratio = static_cast<double>(frame.width()) / window->width();
                // The outline extends beyond the tile; include it rather than only the selection border.
                const auto image = frame.copy(QRectF(position.x() * ratio, position.y() * ratio,
                    (tile->width() + 4) * ratio, (tile->height() + 4) * ratio).toAlignedRect());
                if (image.isNull()) {
                    std::fprintf(stderr, "Radar capture unavailable: window=%dx%d frame=%dx%d position=(%g,%g) tile=%gx%g viewport=%d\n",
                        window->width(), window->height(), frame.width(), frame.height(), position.x(), position.y(),
                        tile->width(), tile->height(), tile->property("inViewport").toBool());
                    const auto evidence = qEnvironmentVariable("PODLORD_RADAR_ACTION_EVIDENCE");
                    if (!evidence.isEmpty() && !frame.isNull()) frame.save(evidence + "/" + scenario + "-"
                        + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + "-capture-failure.png", "PNG");
                }
                return image;
            };
            QTest::qWait(80);
            const auto baseline = renderedTile();
            if (baseline.isNull()) return false;
            const QString animation = scenario.endsWith("pulse") ? "pulse" : scenario.endsWith("sweep") ? "sweep"
                : scenario.endsWith("outline") ? "outline" : "blink";
            rule = alerts->rules().last().toMap(); rule["animation"] = animation;
            if (!alerts->saveRule(rule) || !waitFor([&] { return !alerts->busy() && alerts->effect(path).value("animation").toString() == animation; })) return failed("apply animation");
            QTest::qWait(80);
            const auto first = renderedTile();
            bool changed = false;
            for (int sample = 0; sample < 8; ++sample) {
                QTest::qWait(80);
                const auto frame = renderedTile();
                if (frame.isNull()) return false;
                changed = changed || frame != first;
            }
            const bool rendered = animation == "outline" ? first != baseline && !changed : changed;
            if (!rendered) {
                std::fprintf(stderr, "Radar action did not render: animation=%s visible=%d viewport=%d tile=%gx%g position=(%g,%g) frame=%dx%d effect=%s\n",
                    qPrintable(animation), tile->isVisible(), tile->property("inViewport").toBool(),
                    tile->width(), tile->height(), tile->mapToScene({0, 0}).x(), tile->mapToScene({0, 0}).y(),
                    first.width(), first.height(), QJsonDocument(QJsonObject::fromVariantMap(alerts->effect(path))).toJson(QJsonDocument::Compact).constData());
                const auto evidence = qEnvironmentVariable("PODLORD_RADAR_ACTION_EVIDENCE");
                if (!evidence.isEmpty()) {
                    baseline.save(evidence + "/" + scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + "-baseline.png", "PNG");
                    first.save(evidence + "/" + scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + "-observed.png", "PNG");
                    window->grabWindow().save(evidence + "/" + scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + "-window.png", "PNG");
                }
                return false;
            }
            if (animation == "outline") return server.requests.size() == calls;
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
            auto* navigation = item(window, "radarZoomOut");
            QSignalSpy presented(window,&QQuickWindow::frameSwapped); window->update();
            if (!presented.wait(2000)) return false;
            const auto position = navigation->mapToScene({0, 0});
            if (!workspace.refresh()) return false;
            QTest::qWait(40);
            const bool stableDuringSync = navigation->mapToScene({0, 0}) == position;
            if (!waitFor([&] { return !workspace.loading(); })) return false;
            QTest::qWait(40);
            const auto after = navigation->mapToScene({0, 0});
            if (!stableDuringSync || after != position) std::fprintf(stderr,"Radar refresh moved: before=(%.1f,%.1f) after=(%.1f,%.1f) stableDuring=%d\n",position.x(),position.y(),after.x(),after.y(),stableDuringSync);
            return stableDuringSync && after == position;
        }
        if (scenario == "radar_glyph" || scenario == "radar_glyph_reuse" || scenario == "radar_theme") {
            if (!workspace.alerts()->setPreferences(true,true) || !waitFor([&] { return !workspace.alerts()->busy(); })) return false;
            QList<QVariantMap> cameraChanges;
            radar->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Home);
            for (int step = 0; step < 3; ++step) {
                auto* control=item(window,"radarZoom");
                QSignalSpy activated(control,SIGNAL(clicked()));
                if (!click(window,control) || !waitFor([&] { return !activated.isEmpty(); })) {
                    qWarning() << "Unactivated glyph zoom" << requestedScenario << "step" << step << "pose" << radar->property("viewPose");
                    return failed("glyph zoom control");
                }
                cameraChanges.append(radar->property("viewPose").toMap());
            }
            const auto renderedGlyph=[&](const QString& kind) {
                auto* tile=radarTarget(window,0);
                const auto expectedResource=workspace.table()->index(0,0);
                if (!tile || !displayedWithin(radar,tile) || tile->width()<9 || expectedResource.data(Qt::UserRole+2)!=kind
                    || tile->property("resourcePath")!=expectedResource.data(Qt::UserRole)) return false;
                window->contentItem()->forceActiveFocus();
                const auto frame=window->grabWindow();
                if (frame.isNull()) return false;
                const double ratio=double(frame.width())/window->width();
                auto* tiles=radar->property("tiles").value<QAbstractItemModel*>();
                if (!tiles) return false;
                int terrainRole=-1;
                const auto roles=tiles->roleNames();
                for (auto role=roles.cbegin();role!=roles.cend();++role) if (role.value()=="terrainColor") terrainRole=role.key();
                QColor terrain;
                for (int row=0;row<tiles->rowCount();++row) if (tiles->index(row,0).data(Qt::UserRole)==tile->property("resourcePath")) terrain=tiles->index(row,0).data(terrainRole).value<QColor>();
                if (!terrain.isValid()) return false;
                QImage expected(frame.size(),QImage::Format_ARGB32_Premultiplied); expected.setDevicePixelRatio(ratio); expected.fill(Qt::transparent);
                QPainter painter(&expected);
                const auto origin=tile->mapToScene(QPointF{});
                const QRectF box(origin,tile->size());
                painter.fillRect(box,terrain);
                podlord::KindGlyph::draw(&painter,box,kind,workspace.property("appearanceColors").toMap().value("inset").value<QColor>(),terrain);
                painter.end();
                const auto area=box.adjusted(2,2,-2,-2);
                const auto pixels=QRectF(area.topLeft()*ratio,area.size()*ratio).toAlignedRect().intersected(frame.rect());
                if (pixels.isEmpty()) return false;
                qint64 difference=0;
                for (int y=pixels.top();y<=pixels.bottom();++y) for (int x=pixels.left();x<=pixels.right();++x) {
                    const auto a=frame.pixelColor(x,y),b=expected.pixelColor(x,y);
                    difference+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());
                }
                const auto mean=double(difference)/(pixels.width()*pixels.height()*3);
                if (mean>12) std::fprintf(stderr,"Rendered %s glyph differs from shared painter: mean RGB error=%g\n",qPrintable(kind),mean);
                return mean<=12;
            };
            if (!waitFor([&] { return renderedGlyph("Pod"); })) {
                auto* tile=radarTarget(window,0);
                qWarning() << "Glyph target" << requestedScenario << "tile" << tile << "width" << (tile ? tile->width() : 0)
                    << "kind" << workspace.table()->index(0,0).data(Qt::UserRole+2) << "viewport" << radar->size() << "pose" << radar->property("viewPose") << "zoom actions" << cameraChanges;
                return failed("shared glyph pixels");
            }
            if (scenario == "radar_glyph_reuse") {
                if (!typeRadarQuery("custom-widget")
                    || !waitFor([&] { return radar->property("count").toInt() == 1; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
                if (!waitFor([&] { return renderedGlyph("Widget"); })) return false;
                if (!typeRadarQuery("alpha")
                    || !waitFor([&] { return radar->property("count").toInt() == 1; })) return false;
                radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
                if (!waitFor([&] { return renderedGlyph("Pod"); })) return false;
            }
            if (scenario=="radar_theme") {
                for (const auto& theme : workspace.themeNames()) for (const auto& variant : {QString("dark"), QString("light")}) {
                    if (!workspace.saveAppearance(theme,variant) || !waitFor([&] { return !workspace.busy(); })
                        || !waitFor([&] { return renderedGlyph("Pod"); })) return false;
                }
            }
            return server.requests.size() == calls;
        }
        if (scenario == "radar_keyboard") {
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
            // A platform input method may consume key-down before Qt receives it.
            QTest::keyRelease(window, Qt::Key_Return);
            return waitFor([&] { const auto* title=item(window,"inspectorResourceName");
                return title && title->isVisible() && workspace.inspectorName() == "alpha" && workspace.canEditYaml(); });
        }
        if (scenario == "radar_zoom") {
            auto* zoom = item(window, "radarZoom"); if (!zoom) return false;
            const auto initial = radar->property("cellWidth").toDouble();
            if (!click(window, zoom)) return false;
            return waitFor([&] { return radar->property("cellWidth").toDouble() != initial; }) && server.requests.size() == calls;
        }
        if (scenario == "radar_sort") {
            if (!click(window, item(window, "resourcesWorkspaceButton")) || !click(window, item(window, "header_0")) || !click(window, item(window, "header_0"))
                || !podlord::test::focusResourceRadar(window,mapRadar)) return false;
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Return);
            return waitFor([&] { return workspace.inspectorName() == "custom-widget"; });
        }
        if (scenario == "radar_session") {
            const auto first = workspace.currentSession();
            if (!type(window, item(window, "resourceFilter"), "bravo")) return false;

            if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return workspace.currentSession() != first && !workspace.busy() && !workspace.loading(); }, 10000)) return false;
            if (!click(window, item(window, "resourcesWorkspaceButton")) || radar->property("count").toInt() != 3) return false;
            if (!click(window, item(window, "activateSession_" + first))) return false;
            return waitFor([&] { return workspace.currentSession() == first && radar->isVisible() && radar->property("count").toInt() == 1; }) && workspace.filterText() == "bravo";
        }
        if (scenario == "radar_many" || (scenario == "radar_navigation_repeat" || scenario == "radar_navigation_water")) {
            radar->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
            if (!waitFor([&] { return displayedWithin(radar, radarTarget(window,1000)); })) return false;
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
            const char* stage = "session counts";
            bool completed = false;
            const auto diagnostics = qScopeGuard([&] {
                if (!completed) qWarning() << "Related Event alias stage:" << stage << "scenario" << scenario
                    << "events" << workspace.totalEventCount() << "rows" << workspace.eventTable()->rowCount()
                    << "time" << text(window, "eventCell_0_0") << "inspector" << workspace.inspectorName()
                    << "layout error" << workspace.tableLayoutError();
            });
            const int expectedEvents = scenario == "inspector_related_alias_distinct" || scenario == "inspector_related_alias_reuse" ? 2 : 1;
            if (workspace.totalEventCount() != expectedEvents || workspace.totalResourceCount() != expectedRows) return false;
            stage = "radar";
            if (!podlord::test::focusResourceRadar(window,mapRadar) || !waitFor([&] { return item(window, "resourceRadar")->property("count").toInt() == expectedRows; })) return false;
            stage = "event columns";
            if (!click(window, item(window, "eventsWorkspaceButton")) || !waitFor([&] { return item(window, "eventTable")->property("rows").toInt() == expectedEvents; })
                || !showEventColumn(window, workspace, "eventCount")) return false;
            stage = "event search";
            if (!type(window, item(window, "eventFilter"), "\"Modern event message\"") || !waitFor([&] { return workspace.eventTable()->rowCount() == 1; })) return false;
            stage = "event count";
            if (!revealTableCell(window, "eventTable", 0, 5) || !waitFor([&] { return text(window, "eventCell_0_5") == "7"; })) {
                auto* view = item(window, "eventTable");
                std::fprintf(stderr, "Modern Event count was not presented: %s; model=%s; filter=%s; cell=%d; width=%.0f; contentX=%.0f; contentWidth=%.0f; columns=%d\n",
                    qPrintable(text(window, "eventCell_0_5")), qPrintable(workspace.eventTable()->data(workspace.eventTable()->index(0, 5), Qt::DisplayRole).toString()), qPrintable(text(window, "eventFilter")),
                    item(window, "eventCell_0_5") != nullptr, view->width(), view->property("contentX").toDouble(), view->property("contentWidth").toDouble(), view->property("columns").toInt());
                return false;
            }
            if (scenario == "inspector_related_alias_reuse") {
                const auto requests = server.requests.size();
                for (int pass = 0; pass < 10; ++pass) {
                    if (!type(window, item(window, "eventFilter"), "") || !waitFor([&] { return item(window, "eventTable")->property("rows").toInt() == 2; })) return false;
                    if (!type(window, item(window, "eventFilter"), "\"Modern event message\"")
                        || !revealTableCell(window, "eventTable", 0, 5)
                        || !waitFor([&] { return item(window, "eventTable")->property("rows").toInt() == 1 && text(window, "eventCell_0_5") == "7"; })) return false;
                }
                if (server.requests.size() != requests) return false;
            }
            stage = "legacy timestamp";
            if (scenario == "inspector_related_alias_legacy_time"
                && (!showEventColumn(window, workspace, "eventTime") || !revealTableCell(window, "eventTable", 0, 0)
                    || !waitFor([&] { auto* cell = item(window, "eventCell_0_0");
                        return cell && !podlord::test::visibleBounds(window, cell).isEmpty()
                            && text(window, "eventCell_0_0") == QLocale::system().toString(
                                QDateTime::fromString("2026-10-03T10:03:00Z",Qt::ISODate).toLocalTime(),QLocale::ShortFormat); }))) return false;
            stage = "event inspector";
            if (!revealTableCell(window, "eventTable", 0, 2) || !click(window, item(window, "eventCell_0_2")) || !waitFor([&] { return workspace.inspectorName() == "alpha"; })) return false;
            stage = "related events";
            completed = click(window, item(window, "inspectorEventsButton"))
                && waitFor([&] { return workspace.inspectorEvents().size() == expectedEvents; });
            return completed;
        }
        if (scenario == "inspector_related_workspace" || scenario.startsWith("inspector_related_event_")) {
            const auto calls = server.requests.size();
            if (!click(window, item(window, "eventsWorkspaceButton")) || !waitFor([&] { return item(window, "eventTable")->isVisible() && item(window, "eventTable")->property("rows").toInt() == 2; })) return false;
            if (scenario == "inspector_related_workspace"
                && (!waitFor([&] { return text(window, "eventCell_0_7") == "related-event"; })
                    || text(window, "eventCell_0_8") != "Pod/alpha")) return false;
            if (scenario == "inspector_related_event_sort") {
                if (!showEventColumn(window, workspace, "eventCount") || !revealTableCell(window, "eventTable", 0, 5)
                    || !waitFor([&] { return item(window, "eventHeader_5") && item(window, "eventHeader_5")->width() > 0; })) return false;
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
        if (!type(window, item(window, "resourceFilter"), "alpha")) {
            std::fprintf(stderr, "Related inspector filter input unavailable: page=%s busy=%d\n", qPrintable(workspace.workspacePage()), workspace.busy());
            return false;
        }
        const int matchingRows = scenario.startsWith("inspector_related_table_events_") ? 4 : 3;
        if (!waitFor([&] { return workspace.table()->rowCount() == matchingRows; })) {
            std::fprintf(stderr, "Related inspector filter mismatch: rows=%d filter=%s error=%s\n",
                workspace.table()->rowCount(), qPrintable(workspace.filterText()), qPrintable(workspace.filterError()));
            return false;
        }
        int alphaRow = -1;
        for (int row = 0; row < workspace.table()->rowCount(); ++row)
            if (workspace.table()->data(workspace.table()->index(row, 0), Qt::UserRole + 1).toString() == "alpha") alphaRow = row;
        const auto alphaCell = QString("cell_%1_0").arg(alphaRow).toUtf8();
        QSignalSpy relatedChanges(&workspace, &podlord::Workspace::inspectorRelatedChanged);
        if (alphaRow < 0 || !click(window, item(window, alphaCell.constData()))) {
            std::fprintf(stderr, "Related inspector Pod row unavailable: row=%d page=%s\n",
                alphaRow, qPrintable(workspace.workspacePage()));
            return false;
        }
        if (!waitFor([&] { return workspace.canEditYaml(); })) {
            std::fprintf(stderr, "Related inspector not fresh: path=%s; summary=%s; detail=%s; requests=%s; cell-path=%s\n", qPrintable(workspace.inspectorPath()), qPrintable(workspace.inspected()), qPrintable(workspace.inspectorStatus()), qPrintable(server.requests.join('\n')), qPrintable(item(window, alphaCell.constData())->property("resourcePath").toString())); return false;
        }
        const auto calls = server.requests.size();
        if (scenario == "inspector_related_filter" || scenario == "inspector_related_filter_keyboard") {
            auto* action = item(window, "overviewFilter_kind");
            if (!action || !action->isVisible() || !action->isEnabled()) return false;
            if (scenario.endsWith("keyboard")) {
                action->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
            } else if (!click(window, action)) return false;
            const bool passed = waitFor([&] { return workspace.resourceFieldFilters().value("kind").toString() == "\"Pod\""; })
                && workspace.filterText() == "alpha" && workspace.table()->rowCount() == 2
                && workspace.table()->data(workspace.table()->index(0,1)).toString() == "Pod"
                && workspace.table()->data(workspace.table()->index(1,1)).toString() == "Pod"
                && workspace.inspectorName() == "alpha" && server.requests.size() == calls;
            if (!passed) std::fprintf(stderr,"Metadata filter: fields=%s global=%s rows=%d inspector=%s calls=%lld/%lld\n",
                QJsonDocument::fromVariant(workspace.resourceFieldFilters()).toJson().constData(),qPrintable(workspace.filterText()),
                workspace.table()->rowCount(),qPrintable(workspace.inspectorName()),static_cast<long long>(server.requests.size()),static_cast<long long>(calls));
            return passed;
        }
        if (scenario.startsWith("inspector_related_table_")) {
            const bool event = scenario.contains("_events_");
            const auto prefix = event ? QString("inspectorEvent") : QString("inspectorLink");
            const auto name = [&](const QString& suffix) { return (prefix + suffix).toUtf8(); };
            if (!click(window, item(window, event ? "inspectorEventsButton" : "inspectorLinksButton"))) return false;
            auto* model = workspace.property(event ? "inspectorEventTable" : "inspectorLinkTable").value<QAbstractItemModel*>();
            if (!model || model->rowCount() != 2 || model->columnCount() != 5) return false;
            if (scenario.contains("_color_")) return eventColor("inspectorEventCell_0_1") && server.requests.size() == calls;
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
        if (scenario.startsWith("inspector_related_chrome")) {
            if (scenario.endsWith("narrow")) window->setWidth(360);
            const auto compact = [&] {
                const auto* back = item(window, "inspectorBack");
                const auto* forward = item(window, "inspectorForward");
                const auto* close = item(window, "closeInspector");
                const auto* title = item(window, "inspectorResourceName");
                const auto* overview = item(window, "overviewButton");
                const auto* field = item(window, "overviewFieldHeader");
                if (!scenario.endsWith("narrow")) {
                    const auto* links=item(window,"inspectorLinksButton");
                    const auto* logs=item(window,"logsButton");
                    const auto* terminal=item(window,"terminalButton");
                    if (!links || !logs || !terminal
                        || qAbs(logs->mapToScene({0,0}).x()-links->mapToScene({links->width(),0}).x())>1
                        || qAbs(terminal->mapToScene({0,0}).x()-logs->mapToScene({logs->width(),0}).x())>1) return false;
                }
                return back && forward && close && title && overview && field
                    && !back->property("showText").toBool() && !forward->property("showText").toBool()
                    && close->property("glyph") == "Close"
                    && title->property("text") == "Pod/alpha"
                    && qAbs(back->mapToScene({0,0}).y() - close->mapToScene({0,0}).y()) < 1
                    && back->mapToScene({0,0}).x() < forward->mapToScene({0,0}).x()
                    && title->mapToScene({0,0}).x() < close->mapToScene({0,0}).x()
                    && overview->height() >= (scenario.endsWith("narrow") ? 44 : 32)
                    && field->isVisible() && text(window, "overview_name") == "alpha";
            };
            const auto tabFailure = [&] {
                std::fprintf(stderr, "Inspector tab journey failed: page=%s path=%s calls=%lld/%lld\n",
                    qPrintable(workspace.inspectorPage()), qPrintable(workspace.inspectorPath()),
                    static_cast<long long>(server.requests.size()), static_cast<long long>(calls));
                for (const auto* name : {"yamlButton", "inspectorEventsButton", "inspectorLinksButton", "inspectorEvents", "inspectorLinks", "inspectorYaml"}) {
                    auto* target = item(window, name);
                    if (target) std::fprintf(stderr, "  %s visible=%d enabled=%d x=%.1f y=%.1f w=%.1f h=%.1f\n",
                        name, target->isVisible(), target->isEnabled(), target->mapToScene(QPointF()).x(),
                        target->mapToScene(QPointF()).y(), target->width(), target->height());
                }
                auto* overview = item(window,"overviewScroll");
                auto* nameField = item(window,"overview_name");
                std::fprintf(stderr,"  overview_name exists=%d visible=%d text=\"%s\"\n",nameField!=nullptr,nameField && nameField->isVisible(),
                    qPrintable(nameField ? nameField->property("text").toString() : QString("<missing>")));
                if (overview) {
                    const auto scene = overview->mapRectToScene(QRectF(QPointF{},overview->size()));
                    const auto y = overview->property("contentY").toDouble(), origin = overview->property("originY").toDouble();
                    auto* fields = overview->property("model").value<QAbstractItemModel*>();
                    std::fprintf(stderr,"  overviewScroll visible=%d contentY=%.3f originY=%.3f normalizedY=%.3f size=%gx%g scene=(%g,%g,%g,%g) contentHeight=%g count=%d modelRows=%d moving=%d\n",
                        overview->isVisible(),y,origin,y-origin,overview->width(),overview->height(),scene.x(),scene.y(),scene.width(),scene.height(),
                        overview->property("contentHeight").toDouble(),overview->property("count").toInt(),fields ? fields->rowCount() : -1,overview->property("moving").toBool());
                    QQmlExpression top(qmlContext(overview),overview,"indexAt(1, contentY + 1)");
                    const int topRow = top.evaluate().toInt();
                    std::fprintf(stderr,"  overview firstVisibleRow=%d lookupError=%s\n",topRow,qPrintable(top.hasError() ? top.error().toString() : QString{}));
                    for (int row = 0; fields && row < fields->rowCount(); ++row) {
                        const auto field = fields->data(fields->index(row,0),Qt::UserRole).toMap();
                        if (row != topRow && field.value("id").toString() != "name") continue;
                        QQmlExpression loaded(qmlContext(overview),overview,QString("(function() { const row = itemAtIndex(%1); return row ? 'id=' + row.modelData.id + ' value=' + row.modelData.value + ' y=' + row.y + ' height=' + row.height : '<not instantiated>'; })()").arg(row));
                        const auto loadedState = loaded.evaluate().toString();
                        std::fprintf(stderr,"  overview row=%d modelId=%s modelValue=\"%s\" delegate=%s lookupError=%s\n",row,
                            qPrintable(field.value("id").toString()),qPrintable(field.value("value").toString()),qPrintable(loadedState),
                            qPrintable(loaded.hasError() ? loaded.error().toString() : QString{}));
                    }
                }
                for (auto* ancestor = nameField ? nameField : overview; ancestor; ancestor = ancestor->parentItem()) {
                    if (!ancestor->clip() && !ancestor->property("contentY").isValid()) continue;
                    const auto scene = ancestor->mapRectToScene(QRectF(QPointF{},ancestor->size()));
                    std::fprintf(stderr,"  ancestor class=%s name=%s clip=%d scene=(%g,%g,%g,%g) contentY=%s originY=%s contentHeight=%s moving=%s\n",
                        ancestor->metaObject()->className(),qPrintable(ancestor->objectName()),ancestor->clip(),scene.x(),scene.y(),scene.width(),scene.height(),
                        qPrintable(ancestor->property("contentY").toString()),qPrintable(ancestor->property("originY").toString()),
                        qPrintable(ancestor->property("contentHeight").toString()),qPrintable(ancestor->property("moving").toString()));
                }
                auto* inspector = item(window,"workspaceInspector");
                if (inspector && !saveShellFrame(window,scenario+"-tab-failure",inspector)) std::fputs("Inspector failure component capture failed.\n",stderr);
                return false;
            };
            if (!waitFor(compact) || server.requests.size() != calls
                || !saveShellFrame(window, scenario, item(window, "workspaceInspector"))
                || !click(window, item(window, "yamlButton"))) return tabFailure();
            if (!waitFor([&] {
                    auto* editor = item(window, "inspectorYaml");
                    return workspace.inspectorPage() == "yaml" && editor && editor->isVisible()
                        && !editor->property("text").toString().isEmpty();
                })) return tabFailure();
            auto* editor = item(window, "inspectorYaml");
            if (!QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 8))) return tabFailure();
            const auto selection = editor->property("selectedText").toString();
            if (selection.isEmpty()
                || !click(window, item(window, "inspectorEventsButton"))
                || !waitFor([&] {
                    auto* events = item(window, "inspectorEvents");
                    return workspace.inspectorPage() == "events" && events && events->isVisible()
                        && events->width() > 0 && events->height() > 0;
                })
                || !click(window, item(window, "inspectorLinksButton"))
                || !waitFor([&] {
                    auto* links = item(window, "inspectorLinks");
                    return workspace.inspectorPage() == "links" && links && links->isVisible()
                        && links->width() > 0 && links->height() > 0;
                })
                || !click(window, item(window, "yamlButton"))
                || !waitFor([&] {
                    auto* current = item(window, "inspectorYaml");
                    return current && current->isVisible()
                        && current->property("selectedText").toString() == selection;
                })
                || !click(window, item(window, "overviewButton"))
                || !waitFor(compact)
                || !saveShellFrame(window, scenario + "-returned-overview", item(window, "workspaceInspector"))
                || server.requests.size() != calls
                || !click(window, item(window, "closeInspector"))
                || !workspace.inspectorPath().isEmpty()
                || !waitFor([&] {
                    auto* cell = item(window, alphaCell.constData());
                    return cell && cell->isVisible() && cell->width() > 0 && cell->height() > 0;
                })
                || !click(window, item(window, alphaCell.constData()))
                || !waitFor([&] { return workspace.canEditYaml(); })
                || !click(window, item(window, "yamlButton"))) return tabFailure();
            const bool reopened = waitFor([&] {
                auto* current = item(window, "inspectorYaml");
                return current && current->isVisible()
                    && !current->property("text").toString().isEmpty()
                    && current->property("selectedText").toString().isEmpty();
            });
            if (!reopened) std::fprintf(stderr, "Inspector tab lifecycle failed: page=%s status=%s\n",
                qPrintable(workspace.inspectorPage()), qPrintable(workspace.inspectorStatus()));
            return reopened && click(window, item(window, "closeInspector"))
                && workspace.inspectorPath().isEmpty();
        }
        if (scenario == "inspector_related_overview") {
            if (text(window, "overview_name") != "alpha" || text(window, "overview_kind") != "Pod" || text(window, "overview_namespace") != "default") return false;
            if (!waitFor([&] { return text(window,"overview_uid") == "uid-alpha"; }) || relatedChanges.count() != 0) return false;
            if (!click(window, item(window, "inspectorEventsButton"))
                || !waitFor([&] { return text(window, "inspectorEventCell_0_2") == "Scheduled"; })) return false;
            return relatedChanges.count() == 1 && server.requests.size() == calls;
        }
        if (scenario == "inspector_related_events" || scenario == "inspector_related_wrong_uid") {
            if (!click(window, item(window, "inspectorEventsButton"))) return false;
            if (scenario == "inspector_related_wrong_uid") return item(window, "emptyInspectorEvents")->isVisible()
                && text(window, "emptyInspectorEvents") == "No related events." && server.requests.size() == calls;
            if (!waitFor([&] { return text(window, "inspectorEventCell_0_2") == "Scheduled"; })) return false;
            const auto observed = QLocale::system().toString(
                QDateTime::fromString("2026-10-03T10:00:00Z",Qt::ISODate).toLocalTime(),QLocale::ShortFormat);
            if (!waitFor([&] { return text(window,"inspectorEventCell_0_0") == observed; })) return false;
            item(window,"inspectorEventCell_0_0")->forceActiveFocus();
            QTest::keySequence(window,QKeySequence::Copy);
            if (QGuiApplication::clipboard()->text() != observed) return false;
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
        if (!click(window,item(window,"closeCurrentSession"))
            || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
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
        // TCP receipt timing includes the external server's scheduling delay.
        for (int i = 1; i < dispatched.size(); ++i) if (dispatched[i] - dispatched[i - 1] < 500) return false;
        return dispatched.size() >= 7 && server.starts.size() >= 7;
    }
    if (scenario == "cache_expiry") {
        reference = reference.addSecs(86401);
        if (!workspace.refresh()) return false;
        return waitFor([&] { return workspace.table()->rowCount() == 0 && text(window, "syncProblemMessage").contains("HTTP 500"); });
    }
    if (scenario == "sync" || scenario == "periodic_sync" || scenario == "inactive_sync" || scenario == "adaptive_sync") {
        reference = reference.addSecs(26);
        if (!click(window,item(window,"closeCurrentSession"))
            || !waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })) return false;
        // A public central-owner tick with the external reference clock, not a private cache edit.
        podlord::ResourceClient client(nullptr, [&] { return reference; });
        const auto sources = podlord::KubeconfigStore(profile).list();
        if (!std::holds_alternative<podlord::SourceCatalog>(sources)) return false;
        const auto contexts = std::get<podlord::SourceCatalog>(sources).sources.first().contexts;
        const auto visibleContext = std::find_if(contexts.cbegin(), contexts.cend(), [](const auto& context) { return context.name == "local"; });
        if (visibleContext == contexts.cend()) return false;
        const auto connection = podlord::KubeconfigStore(profile).connection(visibleContext->id);
        if (!std::holds_alternative<podlord::ClusterConnection>(connection)) return false;
        const auto firstRequest = server.requests.size();
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
        if (scenario == "adaptive_sync") {
            const auto pathsSince = [&](qsizetype start) {
                QStringList paths;
                for (const auto& request : server.requests.mid(start)) paths.append(QUrl(request).path());
                return paths;
            };
            const QString pods = "/api/v1/pods", events = "/api/v1/events", widgets = "/apis/example.test/v1/widgets";
            const auto startup = pathsSince(firstRequest);
            if (!startup.contains(pods) || !startup.contains("/apis/example.test/v1") || startup.indexOf(pods) >= startup.indexOf("/apis/example.test/v1")) return false;

            const auto runSync = [&](int seconds) {
                reference = reference.addSecs(seconds);
                const auto start = server.requests.size();
                if (!client.synchronize() || !waitFor([&] { return !client.loading("sync"); })) return QStringList{};
                return pathsSince(start);
            };
            const auto establishStability = runSync(26);
            if (!establishStability.contains(widgets) || !establishStability.contains(pods)) return false;
            const auto liveOnly = runSync(26);
            if (!liveOnly.contains(pods) || !liveOnly.contains(events) || liveOnly.contains(widgets) || liveOnly.contains("/api")) return false;
            const auto stableDue = runSync(95);
            if (!stableDue.contains(widgets) || stableDue.contains("/api")) return false;

            reference = reference.addSecs(26);
            const auto reopening = server.requests.size();
            if (!client.close("sync") || !client.open("sync", std::get<podlord::ClusterConnection>(connection), {})
                || !waitFor([&] { return !client.loading("sync"); })) return false;
            const auto reopened = pathsSince(reopening);
            return reopened.contains(pods) && !reopened.contains(widgets) && !reopened.contains("/api")
                && client.rows("sync").size() == 3;
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
        if (!workspace.refresh()) return false;
        return waitFor([&] { return text(window, "syncProblemMessage").contains("HTTP 500"); }) && item(window, "resourceTable")->property("rows").toInt() == 3;
    }
    if (scenario.startsWith("inspector_")) {
        QString otherSession;
        if (scenario.startsWith("inspector_edit_session_") || scenario.startsWith("inspector_edit_map_history_")) {
            const auto first = workspace.currentSession();

            if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return workspace.tabs().size() == 2 && workspace.currentSession() != first && !workspace.loading() && !workspace.busy(); }, 10000)) return false;
            otherSession = workspace.currentSession();
            if (!click(window, item(window, "activateSession_" + first)) || !waitFor([&] { return workspace.currentSession() == first && !workspace.busy() && !workspace.loading(); })) return false;
        }
        const bool secret = scenario.startsWith("inspector_secret_") || scenario == "inspector_edit_secret" || scenario == "inspector_auto_secret";
        if (!type(window, item(window, "resourceFilter"), secret ? "Secret" : scenario == "inspector_custom_secret" ? "custom-secret" : "bravo") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); })) {
            std::fputs("Inspector resource filter did not expose its expected row.\n",stderr); return false;
        }
        if (!click(window, item(window, "cell_0_0")) || !click(window, item(window, "yamlButton"))) {
            std::fputs("Inspector resource row or YAML tab was not reachable.\n",stderr); return false;
        }
        auto* yamlView = item(window, "inspectorYaml");
        if (!yamlView || !yamlView->isVisible() || !yamlView->property("readOnly").toBool() || !text(window, "inspectorReadStatus").contains("Refreshing")) {
            std::fprintf(stderr,"Inspector cached YAML: visible=%d readOnly=%d status=%s\n",yamlView && yamlView->isVisible(),yamlView && yamlView->property("readOnly").toBool(),qPrintable(text(window,"inspectorReadStatus")));
            return false;
        }
        const auto expectedName = secret ? "alpha" : scenario == "inspector_custom_secret" ? "custom-secret" : "bravo";
        if (!text(window, "inspectorResourceName").endsWith("/" + QString(expectedName))) {
            std::fputs("Inspector selected a different resource.\n",stderr); return false;
        }
        if (scenario.startsWith("inspector_edit_")) {
            auto* edit = item(window, "editYaml");
            if (!edit || edit->isEnabled()) return false;
            if (scenario == "inspector_edit_late") {
                if (!waitFor([&] { return server.detailReads == 1; }) || !type(window, item(window, "resourceFilter"), "charlie") || !waitFor([&] { return workspace.table()->rowCount() == 1; }) || !click(window, item(window, "cell_0_0"))) return false;
                if (!waitFor([&] { return server.detailReads == 2; }) || edit->isEnabled() || !yamlView->property("readOnly").toBool() || text(window, "inspectorResourceName") != "ConfigMap/charlie") return false;
                return waitFor([&] { return edit->isEnabled() && !workspace.loading(); }) && click(window, edit) && !yamlView->property("readOnly").toBool() && text(window, "inspectorYaml").contains("name: \"charlie\"");
            }
        }
        if (scenario == "inspector_close_late") {
            if (!waitFor([&] { return server.detailReads == 1; }) || !click(window, item(window, "closeInspector"))) return false;
            if (!waitFor([&] { return !workspace.loading(); })) return false;
            auto* closedYaml = item(window, "inspectorYaml");
            return workspace.inspectorPath().isEmpty() && (!closedYaml || !closedYaml->isVisible())
                && text(window, "inspectorYaml").isEmpty();
        }
        if (!waitFor([&] { return !text(window, "inspectorYaml").isEmpty() && !workspace.loading(); })) return false;
        if (text(window, "inspectorReadStatus").contains("Refreshing")) return false;
        const auto oldYaml = text(window, "inspectorYaml");
        if (scenario == "inspector_auto_uid") {
            server.inspectorResourceRecreated = true;
            return workspace.refresh() && waitFor([&] { return server.detailReads == 2 && !workspace.loading(); })
                && workspace.yamlText().contains("uid-bravo-recreated") && workspace.yamlText().contains("ordinary-value-v2");
        }
        if (scenario == "inspector_expiry" || scenario.startsWith("inspector_auto_")) {
            const auto reads = server.detailReads;
            const auto callsBeforeRenewal = server.requests.size();
            QString renewalStage = "policy";
            bool renewalComplete = false;
            const auto renewalDiagnostic = qScopeGuard([&] {
                if (scenario == "inspector_auto_queued" && !renewalComplete)
                    qWarning() << "Queued renewal:" << renewalStage << "reads" << server.detailReads << "requests" << server.requests.size()
                        << "limit" << workspace.requestLimit() << "loading" << workspace.loading() << "busy" << workspace.busy();
            });
            const auto path = workspace.inspectorPath();
            const auto draft = oldYaml + "\n# retained local draft\n";
            if (scenario == "inspector_auto_secret") {
                if (!workspace.setInspectorPage("values") || !waitFor([&] { return item(window, "reveal_data/alpha"); })
                    || !click(window, item(window, "reveal_data/alpha"))
                    || !waitFor([&] { return accessibleName(window, "alpha, Value, alpha-private-value"); })) return false;
            }
            if (scenario == "inspector_auto_hidden" && !workspace.setWorkspacePage("settings")) return false;
            if (scenario == "inspector_auto_inactive" && !workspace.setWindowFocused(false)) return false;
            if (scenario == "inspector_auto_draft" && (!workspace.beginYamlEdit() || !workspace.setYamlDraft(draft))) return false;
            if (scenario == "inspector_auto_closed" && !workspace.closeInspector()) return false;
            if (scenario == "inspector_auto_queued"
                && (!workspace.saveReadSettings(60, workspace.inactiveSyncMinutes(), "5", workspace.collectionCacheHours())
                    || !waitFor([&] { return !workspace.busy() && workspace.requestLimit() == 60; }))) return false;
            reference = reference.addSecs(269);
            renewalStage = "before renewal";
            if (!workspace.setWindowFocused(scenario != "inspector_auto_inactive")) return false;
            QTest::qWait(50);
            if (server.detailReads != reads) return false;
            if (scenario == "inspector_auto_queued") {
                renewalStage = "establish request spacing";
                if (!workspace.refresh() || !waitFor([&] { return server.requests.size() > callsBeforeRenewal; })) return false;
            }
            reference = reference.addSecs(2);
            renewalStage = "renewal due";
            if (!workspace.setWindowFocused(scenario != "inspector_auto_inactive")) return false;
            if (scenario == "inspector_auto_queued") {
                QTest::qWait(50);
                renewalStage = "withdraw queued renewal";
                if (server.detailReads != reads || !workspace.setWindowFocused(true) || !workspace.setWindowFocused(false)) return false;
                renewalStage = "release request spacing";
                if (!workspace.saveReadSettings(0, workspace.inactiveSyncMinutes(), "5", workspace.collectionCacheHours())
                    || !waitFor([&] { return !workspace.busy() && !workspace.loading(); }) || server.detailReads != reads) return false;
                renewalStage = "reactivate inspector";
                if (!workspace.setWindowFocused(true)) return false;
            }
            const bool quiet = scenario == "inspector_auto_hidden" || scenario == "inspector_auto_inactive"
                || scenario == "inspector_auto_draft" || scenario == "inspector_auto_closed";
            if (quiet) {
                if (!workspace.refresh() || !waitFor([&] { return !workspace.loading(); }) || server.detailReads != reads) return false;
                if (scenario == "inspector_auto_draft") return workspace.yamlEditing() && workspace.yamlText() == draft;
                if (scenario == "inspector_auto_closed") return workspace.inspectorPath().isEmpty();
                if (scenario == "inspector_auto_hidden" && !workspace.setWorkspacePage("resources")) return false;
                if (scenario == "inspector_auto_inactive" && !workspace.setWindowFocused(true)) return false;
            }
            renewalStage = "await renewed detail";
            if (!waitFor([&] { return server.detailReads == reads + 1; })) return false;
            if (scenario == "inspector_auto_late") {
                if (!workspace.closeInspector() || !waitFor([&] { return !workspace.loading(); })) return false;
                return workspace.inspectorPath().isEmpty() && server.delayedReadDelivered && !server.prematureDisconnect;
            }
            if (scenario == "inspector_auto_draft_late") {
                if (!workspace.beginYamlEdit() || !workspace.setYamlDraft(draft)
                    || !waitFor([&] { return !workspace.loading(); })) return false;
                return workspace.yamlEditing() && workspace.yamlText() == draft
                    && server.detailReads == reads + 1 && server.delayedReadDelivered && !server.prematureDisconnect;
            }
            if (!waitFor([&] { return !workspace.loading(); }) || workspace.inspectorPath() != path) return false;
            if (scenario == "inspector_auto_auth" || scenario == "inspector_auto_failure") {
                if (workspace.yamlText() != oldYaml || !workspace.inspectorStatus().contains(scenario.endsWith("auth") ? "Authentication" : "HTTP 500", Qt::CaseInsensitive)) return false;
                if (scenario.endsWith("auth")) {
                    if (!workspace.authenticationRequired()) return false;
                    reference = reference.addSecs(600);
                } else {
                    reference = reference.addSecs(31);
                    if (!workspace.setWindowFocused(true) || !workspace.yamlText().isEmpty()) return false;
                }
            } else if (scenario == "inspector_auto_secret") {
                if (!accessibleName(window, "alpha, Value, alpha-private-value") || !accessibleName(window, "alpha, Hide")
                    || !accessibleName(window, "beta, Value, [hidden]")
                    || workspace.yamlText().contains("alpha-private-value") || workspace.yamlText().contains("beta-renewed-private-value")
                    || workspace.yamlText().contains("YWxwaGEtcHJpdmF0ZS12YWx1ZQ==")) return false;
            } else if (!workspace.yamlText().contains("ordinary-value-v2")) return false;
            for (int repeat = 0; repeat < 3; ++repeat) if (!workspace.setWindowFocused(true)) return false;
            QTest::qWait(100);
            renewalComplete = server.detailReads == reads + 1;
            return renewalComplete;
        }
        if (scenario.startsWith("inspector_yaml_expiry_")) {
            if (!click(window, item(window, "inspectorEventsButton"))) return false;
            reference = reference.addSecs(301);
            if (!workspace.refresh()
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
            if (scenario == "inspector_edit_fresh" && item(window,"yamlDraftStatus")->isVisible()) return false;
            if (scenario == "inspector_edit_cached" || scenario == "inspector_edit_failure" || scenario == "inspector_edit_malformed" || scenario == "inspector_edit_auth" || scenario == "inspector_edit_same_version") {
                if (scenario == "inspector_edit_cached") {
                    if (!click(window, item(window, "closeInspector")) || !click(window, item(window, "cell_0_0"))
                        || !click(window, item(window, "yamlButton"))
                        || !waitFor([&] {
                            auto* current = item(window, "inspectorYaml");
                            return current && current->isVisible();
                        })) return false;
                    yamlView = item(window, "inspectorYaml");
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
            if (scenario == "inspector_edit_fresh") return text(window, "inspectorYaml") == oldYaml
                && !item(window,"yamlDraftStatus")->isVisible() && noWrites();
            if (scenario == "inspector_edit_clean") return click(window, item(window, "closeInspector")) && workspace.inspectorPath().isEmpty() && !item(window, "discardStay")->isVisible() && noWrites();
            if (scenario == "inspector_edit_clean_tab") {
                if (!click(window,item(window,"closeCurrentSession"))) return false;
                return waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); })
                    && workspace.inspectorPath().isEmpty()
                    && (!item(window, "inspectorYaml") || !item(window, "inspectorYaml")->isVisible())
                    && !item(window, "discardStay")->isVisible() && noWrites();
            }
            yamlView->forceActiveFocus(); QTest::keySequence(window, QKeySequence::MoveToEndOfDocument); QTest::keyClick(window, Qt::Key_Return);
            for (const auto ch : QString("# operator draft")) QTest::keyClick(window, ch.toLatin1());
            if (!text(window, "inspectorYaml").contains("# operator draft")) return false;
            if (scenario == "inspector_edit_empty") { QTest::keySequence(window, QKeySequence::SelectAll); QTest::keyClick(window, Qt::Key_Backspace); }
            const auto draft = text(window, "inspectorYaml");
            if (scenario == "inspector_edit_expiry_reopen") {
                reference = reference.addSecs(301);
                if (!workspace.refresh()
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
                if (scenario == "inspector_edit_expiry") { reference = reference.addSecs(301); if (!workspace.refresh()) return false; }
                else if (!click(window, item(window, "refreshInspector"))) return false;
                if (!waitFor([&] { return !workspace.loading(); }) || text(window, "inspectorYaml") != draft || yamlView->property("readOnly").toBool()) return false;
                const auto status = text(window, "yamlDraftStatus");
                return noWrites() && (scenario == "inspector_edit_refresh" ? status.contains("version changed") : scenario == "inspector_edit_same_version" ? !status.contains("version changed") : status.contains("unavailable"));
            }
            if (scenario == "inspector_edit_close_keyboard" || scenario == "inspector_edit_close_keypad" || scenario == "inspector_edit_close_space") {
                window->requestActivate();
                if (!waitFor([&] { return window->isActive(); })) { std::fputs("Discard prompt window did not activate before interaction.\n", stderr); return false; }
            }
            const auto originalSession = workspace.currentSession();
            const auto originalTabCount = workspace.tabs().size();
            QString requestedContext;
            if (scenario.startsWith("inspector_edit_resource_")) {
                if (!type(window, item(window, "resourceFilter"), "charlie") || !waitFor([&] { return workspace.table()->rowCount() == 1; }) || !click(window, item(window, "cell_0_0"))) return false;
            } else if (scenario.startsWith("inspector_edit_window_")) { if (window->close()) return false; }
            else if (scenario.startsWith("inspector_edit_tab_")) { if (!click(window,item(window,"closeCurrentSession"))) return false; }
            else if (scenario.startsWith("inspector_edit_session_")) { if (!click(window, item(window, "activateSession_" + otherSession))) return false; }
            else if (scenario.startsWith("inspector_edit_open_context_")) {
                const bool duplicate = scenario.startsWith("inspector_edit_open_context_tab_");
                const auto originalContext = workspace.sessionConfiguration(originalSession).value("contextId").toString();
                requestedContext = duplicate ? originalContext : workspace.contexts().last().toMap().value("id").toString();
                if (requestedContext.isEmpty() || (!duplicate && requestedContext == originalContext)
                    || workspace.openContext(requestedContext, false, duplicate)) return false;
            } else if (!click(window, item(window, "closeInspector"))) return false;
            auto* discardDialog = window->findChild<QObject*>("discardYamlDialog");
            QSignalSpy discarded(discardDialog, SIGNAL(accepted()));
            QSignalSpy stayed(discardDialog, SIGNAL(rejected()));
            QSignalSpy closed(discardDialog, SIGNAL(closed()));
            QString keyFocusBefore, keyFocusPressed;
            bool keyDown = false;
            if (!waitFor([&] { auto* dialog = window->findChild<QObject*>("discardYamlDialog");
                    return dialog && dialog->property("opened").toBool() && item(window, "discardStay") && item(window, "discardStay")->isVisible(); })
                || text(window, "inspectorYaml") != draft || workspace.currentSession() != originalSession || workspace.inspectorPath().isEmpty() || !window->isVisible()) return false;
            const bool accept = scenario.endsWith("_accept") || scenario == "inspector_edit_resource_bound";
            if (scenario == "inspector_edit_resource_bound") workspace.filter("bravo");
            if (scenario == "inspector_edit_close_escape") QTest::keyClick(window, Qt::Key_Escape);
            else if (scenario == "inspector_edit_close_keyboard" || scenario == "inspector_edit_close_keypad" || scenario == "inspector_edit_close_space") {
                QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
                window->requestActivate();
                window->update();
                if (!waitFor([&] { return !rendered.isEmpty(); })) {
                    std::fputs("Discard keyboard surface did not produce a frame.\n", stderr);
                    return false;
                }
                if (!waitFor([&] { return window->isActive() && QGuiApplication::focusObject() == item(window, "discardStay")
                        && item(window, "discardStay")->hasActiveFocus() && item(window, "discardStay")->property("visualFocus").toBool(); })) {
                    std::fprintf(stderr, "Discard keyboard focus: window=%d active=%d visual=%d current=%s\n", window->isActive(), item(window,"discardStay")->hasActiveFocus(), item(window,"discardStay")->property("visualFocus").toBool(), window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none");
                    return false;
                }
                QTest::keyClick(window, Qt::Key_Tab);
                if (!waitFor([&] { return QGuiApplication::focusObject() == item(window, "discardAccept")
                        && item(window, "discardAccept")->hasActiveFocus() && item(window, "discardAccept")->property("visualFocus").toBool(); })) {
                    std::fputs("Discard prompt Tab did not focus Discard changes.\n", stderr);
                    return false;
                }
                QTest::keyClick(window, Qt::Key_Backtab);
                if (!waitFor([&] { return QGuiApplication::focusObject() == item(window, "discardStay")
                        && item(window, "discardStay")->hasActiveFocus() && item(window, "discardStay")->property("visualFocus").toBool(); })) {
                    std::fputs("Discard prompt Backtab did not restore Stay focus.\n", stderr);
                    return false;
                }
                const auto key = scenario.endsWith("_keypad") ? Qt::Key_Enter : scenario.endsWith("_space") ? Qt::Key_Space : Qt::Key_Return;
                keyFocusBefore = window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{};
                QTest::keyPress(window, key);
                keyFocusPressed = window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString{};
                keyDown = item(window, "discardStay")->property("down").toBool();
                QTest::keyRelease(window, key);
            }
            else if (!click(window, item(window, accept ? "discardAccept" : "discardStay"))) return false;
            if (!waitFor([&] { return !item(window, "discardStay")->isVisible(); })) {
                auto* dialog = window->findChild<QObject*>("discardYamlDialog");
                std::fprintf(stderr, "Discard activation did not close: scenario=%s opened=%d focus=%s down=%d\n", qPrintable(scenario),
                    dialog && dialog->property("opened").toBool(), window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none",
                    item(window, "discardStay")->property("down").toBool());
                qWarning() << "Discard key focus:" << keyFocusBefore << keyFocusPressed << "pressed" << keyDown
                    << "accepted/rejected/closed" << discarded.size() << stayed.size() << closed.size();
                return false;
            }
            if (!accept) {
                const bool retained = text(window, "inspectorYaml") == draft && !yamlView->property("readOnly").toBool() && workspace.currentSession() == originalSession && !workspace.inspectorPath().isEmpty() && window->isVisible() && noWrites();
                if (!retained) std::fprintf(stderr, "Stay: draft=%d readOnly=%d session=%d inspector=%s visible=%d writes=%d\n", text(window,"inspectorYaml")==draft, yamlView->property("readOnly").toBool(), workspace.currentSession()==originalSession, qPrintable(workspace.inspectorPath()), window->isVisible(), !noWrites());
                return retained
                    && (!scenario.startsWith("inspector_edit_open_context_") || workspace.tabs().size() == originalTabCount);
            }
            if (scenario.startsWith("inspector_edit_resource_")) return waitFor([&] { return text(window, "inspectorResourceName") == "ConfigMap/charlie" && !workspace.loading(); }) && !text(window, "inspectorYaml").contains("# operator draft") && yamlView->property("readOnly").toBool() && noWrites();
            if (scenario == "inspector_edit_window_accept") return !window->isVisible() && noWrites();
            if (scenario == "inspector_edit_tab_accept") return waitFor([&] { return workspace.currentSession().isEmpty() && !workspace.busy(); }) && noWrites();
            if (scenario == "inspector_edit_session_accept") return waitFor([&] { return workspace.currentSession() != originalSession && !workspace.busy(); }) && noWrites();
            if (scenario == "inspector_edit_open_context_accept" || scenario == "inspector_edit_open_context_tab_accept") return waitFor([&] {
                const auto active = workspace.currentSession();
                return !active.isEmpty() && active != originalSession && workspace.tabs().size() == originalTabCount + 1
                    && !workspace.busy() && workspace.sessionConfiguration(active).value("contextId").toString() == requestedContext;
            }) && noWrites();
            return workspace.inspectorPath().isEmpty() && text(window, "inspectorYaml").isEmpty() && noWrites();
        }
        if (scenario == "inspector_list_wrong_version") {
            if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && text(window, "syncProblemMessage").contains("invalid resource"); })) return false;
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
        if (scenario == "inspector_custom_secret") return oldYaml.contains("enabled: true") && oldYaml.contains("count: 42") && oldYaml.contains("ratio: 0.125")
            && oldYaml.contains("- ~") && oldYaml.contains("- \"null\"") && !oldYaml.contains("[hidden]") && !item(window, "valuesButton")->isVisible();
        if (scenario == "inspector_yaml") {
            const auto requests = server.requests.size();
            if (!click(window, item(window, "copyYaml"))) return false;
            return QGuiApplication::clipboard()->text() == oldYaml && oldYaml.contains("numericText: \"123\"")
                && oldYaml.contains("booleanText: \"true\"") && oldYaml.contains("nullText: \"null\"")
                && oldYaml.contains("immutable: false") && server.requests.size() == requests;
        }
        if (scenario.startsWith("inspector_yaml_presentation")) {
            auto* gutter = item(window, "yamlLineNumbers");
            const auto calls = server.requests.size();
            if (!gutter || !gutter->isVisible() || gutter->width() < 20) { std::fprintf(stderr,"YAML gutter missing or too narrow\n"); return false; }
            if (scenario.endsWith("draft") || scenario.endsWith("contrast")) {
                const bool contrast = scenario.endsWith("contrast");
                const QString draft = QString("# local draft\n\"name\": \"user:value#literal\"\ncount: 42\nflag: false\nmessage: |\n  name: scalar # not a comment\nnext: 'quoted # scalar'\n")
                    + (contrast ? "items:\n  - value\n" : "");
                if (!click(window,item(window,"editYaml"))) { std::fprintf(stderr,"YAML edit action unreachable\n"); return false; }
                QGuiApplication::clipboard()->setText(draft);
                yamlView->forceActiveFocus(); QTest::keySequence(window,QKeySequence::SelectAll); QTest::keySequence(window,QKeySequence::Paste);
                if (!waitFor([&] { return workspace.yamlText() == draft; })) { std::fprintf(stderr,"YAML paste did not reach draft\n"); return false; }
                const auto renderedToken = [&](const QString& token,const QColor& expected) {
                    QRectF start,finish;
                    const int position = draft.indexOf(token);
                    if (position < 0 || !QMetaObject::invokeMethod(yamlView,"positionToRectangle",Qt::DirectConnection,Q_RETURN_ARG(QRectF,start),Q_ARG(int,position))
                        || !QMetaObject::invokeMethod(yamlView,"positionToRectangle",Qt::DirectConnection,Q_RETURN_ARG(QRectF,finish),Q_ARG(int,position+token.size()))) return false;
                    auto* scroll = item(window,"yamlScroll");
                    auto* viewport = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
                    if (!viewport || viewport->height() < start.height()) return false;
                    viewport->setProperty("contentY",std::clamp(start.y(),0.0,qMax(0.0,viewport->property("contentHeight").toDouble()-viewport->height())));
                    QSignalSpy frame(window,&QQuickWindow::frameSwapped);
                    window->update();
                    if (!frame.wait(1000)) return false;
                    const auto image = window->grabWindow();
                    if (image.isNull()) return false;
                    const auto dpr = image.devicePixelRatio();
                    const auto origin = yamlView->mapToScene(start.topLeft());
                    const auto clip = viewport->mapRectToScene(viewport->boundingRect());
                    const auto region = QRect(qRound(origin.x()*dpr),qRound(origin.y()*dpr),qRound(qMax(1.0,finish.x()-start.x())*dpr),qRound(start.height()*dpr))
                        .intersected(QRect(qRound(clip.x()*dpr),qRound(clip.y()*dpr),qRound(clip.width()*dpr),qRound(clip.height()*dpr))).intersected(image.rect());
                    int pixels = 0;
                    for (int y=region.top(); y<=region.bottom(); ++y) for (int x=region.left(); x<=region.right(); ++x) {
                        const auto color = image.pixelColor(x,y);
                        if (qAbs(color.red()-expected.red())<10 && qAbs(color.green()-expected.green())<10 && qAbs(color.blue()-expected.blue())<10) ++pixels;
                    }
                    if (pixels <= 5) std::fprintf(stderr,"YAML token not rendered: %s, viewport %.0fx%.0f\n",qPrintable(token),viewport->width(),viewport->height());
                    return pixels > 5;
                };
                if (!waitFor([&] { return renderedToken("# local draft",QColor("#6f7f8f")) && renderedToken("\"name\"",QColor("#6ec6ff"))
                    && renderedToken("user:value#literal",QColor("#7ad8ff")) && renderedToken("42",QColor("#7ddc8d"))
                    && renderedToken("false",QColor("#f0c44f")) && renderedToken("name: scalar # not a comment",QColor("#d6c39a"))
                    && renderedToken("quoted # scalar",QColor("#d6c39a")); })) { std::fprintf(stderr,"YAML syntax pixels not visible\n"); return false; }
                if (contrast) {
                    const auto* quickDocument = yamlView->property("textDocument").value<QQuickTextDocument*>();
                    if (!quickDocument || !quickDocument->textDocument()) { std::fprintf(stderr,"YAML public text document missing\n"); return false; }
                    const QStringList tokens{"# local draft", "\"name\"", "user:value#literal", "42", "false", "name: scalar # not a comment", "quoted # scalar", "- value"};
                    const auto tokenColor = [&](const QString& token) {
                        const int position = draft.indexOf(token);
                        const auto block = quickDocument->textDocument()->findBlock(position);
                        if (position < 0 || !block.isValid() || !block.layout()) return QColor{};
                        for (const auto& range : block.layout()->formats())
                            if (position-block.position() >= range.start && position-block.position() < range.start+range.length) return range.format.foreground().color();
                        return QColor{};
                    };
                    const auto luminance = [](const QColor& color) {
                        const auto linear = [](double channel) { return channel <= .04045 ? channel/12.92 : std::pow((channel+.055)/1.055,2.4); };
                        return .2126*linear(color.redF()) + .7152*linear(color.greenF()) + .0722*linear(color.blueF());
                    };
                    QMap<QString,QColor> dark;
                    for (const auto& token : tokens) { dark.insert(token,tokenColor(token)); if (!dark[token].isValid()) { std::fprintf(stderr,"YAML token format missing: %s\n",qPrintable(token)); return false; } }
                    const auto capture = [&](const QString& theme, const QString& variant) {
                        const auto directory = qEnvironmentVariable("PODLORD_THEME_EVIDENCE_DIR");
                        return directory.isEmpty() || (QDir().mkpath(directory) && window->grabWindow().save(directory+"/yaml-"+theme+"-"+variant+"-"+qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")+".png","PNG"));
                    };
                    for (const auto& theme : workspace.themeNames()) {
                        if (!workspace.saveAppearance(theme,"light") || !waitFor([&] { return !workspace.busy() && workspace.themeName()==theme && workspace.themeVariant()=="light"; })) return false;
                        if (!waitFor([&] {
                            const double background = luminance(workspace.appearanceColors().value("inset").value<QColor>());
                            for (const auto& token : tokens) {
                                const auto color = tokenColor(token);
                                if (!color.isValid()) return false;
                                const double foreground = luminance(color);
                                if ((std::max(foreground,background)+.05)/(std::min(foreground,background)+.05) < 4.5) return false;
                            }
                            return true;
                        })) { std::fprintf(stderr,"Light YAML contrast failed: %s\n",qPrintable(theme)); return false; }
                        if (text(window,"inspectorYaml") != draft || !capture(theme,"light")) return false;
                        if (!workspace.saveAppearance(theme,"dark") || !waitFor([&] {
                            if (workspace.busy() || workspace.themeVariant()!="dark") return false;
                            for (const auto& token : tokens) if (tokenColor(token) != dark[token]) return false;
                            return true;
                        }) || text(window,"inspectorYaml") != draft || !capture(theme,"dark")) return false;
                    }
                }
                yamlView->forceActiveFocus(); QTest::keySequence(window,QKeySequence::SelectAll); QTest::keySequence(window,QKeySequence::Copy);
                return QGuiApplication::clipboard()->text() == draft && !yamlView->property("readOnly").toBool()
                    && yamlView->property("textFormat").toInt() == 0 && server.requests.size() == calls;
            }
            if (scenario.endsWith("scroll")) {
                QSignalSpy rendered(window,&QQuickWindow::afterAnimating);
                window->update();
                if (rendered.isEmpty() && !rendered.wait(2000)) return false;
                const auto local = yamlView->mapToScene(QPointF(40, 100));
                QWheelEvent wheel(local, window->mapToGlobal(local.toPoint()), {}, QPoint(0, -1200), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &wheel);
                if (!waitFor([&] { return gutter->property("firstVisibleLine").toInt() > 1; })) {
                    const auto* scroll = item(window,"yamlScroll");
                    const auto* content = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
                    std::fprintf(stderr,"YAML gutter: line=%d y=%.1f editorY=%.1f viewport=%.1f gutter=%.1f content=%.1f\n",gutter->property("firstVisibleLine").toInt(),
                        content ? content->property("contentY").toDouble() : -1,yamlView->y(),content ? content->height() : -1,gutter->height(),
                        content ? content->property("contentHeight").toDouble() : -1);
                    return false;
                }
            }
            const auto hasSyntax = [&] {
                const auto image = window->grabWindow();
                if (image.isNull()) return false;
                const auto dpr = image.devicePixelRatio();
                const auto origin = yamlView->mapToScene({0, 0});
                const auto region = QRect(qRound(origin.x()*dpr), qRound(qMax(origin.y(), gutter->mapToScene({0,0}).y())*dpr),
                    qRound(qMin(500.0, yamlView->width())*dpr), qRound(qMin(100.0, gutter->height())*dpr)).intersected(image.rect());
                int keyPixels = 0;
                for (int y=region.top(); y<=region.bottom(); ++y)
                    for (int x=region.left(); x<=region.right(); ++x) {
                        const auto color = image.pixelColor(x,y);
                        if (qAbs(color.red()-110)<10 && qAbs(color.green()-198)<10 && qAbs(color.blue()-255)<10) ++keyPixels;
                    }
                return keyPixels > 10;
            };
            return waitFor(hasSyntax) && click(window, item(window, "copyYaml"))
                && QGuiApplication::clipboard()->text() == oldYaml && text(window, "inspectorYaml") == oldYaml
                && yamlView->property("readOnly").toBool() && yamlView->property("textFormat").toInt() == 0
                && server.requests.size() == calls;
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
                QSignalSpy rendered(window, &QQuickWindow::afterAnimating);
                window->update();
                if (rendered.isEmpty() && !rendered.wait(2000)) return false;
                if (!waitFor([&] { return scroll->width() > 0 && scroll->height() > 0
                    && content->property("contentHeight").toDouble() > content->height(); })) return false;
                const auto local = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
                QTest::mouseMove(window, local.toPoint());
                QWheelEvent wheel(local, window->mapToGlobal(local.toPoint()), {}, QPoint(0, -1200), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QElapsedTimer wheelClock; wheelClock.start();
                wheel.setTimestamp(static_cast<ulong>(wheelClock.msecsSinceReference() + wheelClock.elapsed()));
                QCoreApplication::sendEvent(window, &wheel);
                if (!waitFor([&] { return content->property("contentY").toDouble() > 0 && !content->property("moving").toBool(); })) {
                    std::fprintf(stderr, "YAML wheel did not settle: y=%.1f content=%.1f viewport=%.1f moving=%d pointer=(%.1f,%.1f) accepted=%d focus=%s\n", content->property("contentY").toDouble(), content->property("contentHeight").toDouble(), content->height(), content->property("moving").toBool(), local.x(), local.y(), wheel.isAccepted(), window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none");
                    const auto capture = qEnvironmentVariable("PODLORD_INSPECTOR_FAILURE_FRAME");
                    if (!capture.isEmpty()) window->grabWindow().save(capture);
                    return false;
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
            const auto failure = scenario == "inspector_failure" ? "HTTP 500" : scenario == "inspector_malformed" ? "invalid response" : scenario == "inspector_forbidden" ? "Authorization denied"
                : scenario == "inspector_auth" ? "Authentication failed" : scenario == "inspector_redirect" ? "Redirect refused" : "limiting requests";
            if (text(window, "inspectorYaml") != oldYaml || !status.contains(failure) || !status.contains("Updated:") || status.contains("Cached") || !noLeak()) return false;
            const auto requests = server.requests.size(); QTest::qWait(100);
            return server.requests.size() == requests && !server.requests.contains("/credential-leak");
        }
        if (!click(window, item(window, "valuesButton"))) {
            std::fputs("Inspector values tab was not reachable.\n",stderr); return false;
        }
        if (scenario.startsWith("inspector_secret_table_")) {
            const auto before = server.requests.size();
            const auto action = scenario.mid(QString("inspector_secret_table_").size());
            auto* model = workspace.property("valuesTable").value<QAbstractItemModel*>();
            if (!model || model->columnCount() != 5 || model->rowCount() != 4) return false;
            if (!waitFor([&] { return item(window, "valueHeader_0") && item(window, "value_data/alpha"); })) return false;
            if (const auto frame = qEnvironmentVariable("PODLORD_VALUES_FRAME"); !frame.isEmpty()) window->grabWindow().save(frame);
            if (action == "accessibility") {
                const auto revealAction=[&](const QString& name) {
                    auto* cell=item(window,"reveal_data/alpha");
                    auto* accessible=cell ? QAccessible::queryAccessibleInterface(cell) : nullptr;
                    return accessible && accessible->text(QAccessible::Name)=="alpha, "+name ? accessible->actionInterface() : nullptr;
                };
                const auto matches=[&] {
                    auto* cell=item(window,"value_data/alpha");
                    auto* accessible=cell ? QAccessible::queryAccessibleInterface(cell) : nullptr;
                    const auto identity=model->index(0,0).data(Qt::UserRole+1).toString();
                    const auto column=cell ? cell->property("column").toInt() : -1;
                    const auto label=model->headerData(column,Qt::Horizontal).toString();
                    const auto value=cell ? cell->property("text").toString() : QString{};
                    const auto expected=(identity.isEmpty() ? model->index(0,0).data(Qt::UserRole).toString() : identity)+", "+label+", "+value;
                    return accessible && accessible->text(QAccessible::Name)==expected;
                };
                if (!waitFor(matches) || text(window,"value_data/alpha")!="[hidden]" || !revealAction("Reveal")) return false;
                item(window,"reveal_data/alpha")->forceActiveFocus();
                revealAction("Reveal")->doAction(QAccessibleActionInterface::pressAction());
                if (!waitFor(matches) || text(window,"value_data/alpha")=="[hidden]" || !revealAction("Hide")
                    || !window->activeFocusItem() || window->activeFocusItem()->objectName()!="reveal_data/alpha") return false;
                revealAction("Hide")->doAction(QAccessibleActionInterface::pressAction());
                return waitFor(matches) && revealAction("Reveal")
                    && text(window,"value_data/alpha")=="[hidden]" && server.requests.size()==before;
            }
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
        if (!noLeak() || !(scenario == "inspector_secret_copy" ? revealTableCell(window,"valueTable",0,3)
            : waitFor([&] { return item(window, "reveal_data/alpha"); }))) {
            auto* values = item(window,"resourceValues");
            std::fprintf(stderr,"Secret values preparation: masked=%d reveal=%d viewport=%gx%g window=%dx%d\n",noLeak(),bool(item(window,"reveal_data/alpha")),
                values ? values->width() : 0,values ? values->height() : 0,window->width(),window->height());
            const auto capture=qEnvironmentVariable("PODLORD_INSPECTOR_FAILURE_FRAME");
            if (!capture.isEmpty()) window->grabWindow().save(capture);
            return false;
        }
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
            const auto* button = item(window, "copy_data/alpha");
            const auto bounds = button ? button->mapRectToScene(button->boundingRect()) : QRectF{};
            const bool copiedByClick = click(window, item(window, "copy_data/alpha"));
            const bool copied = copiedByClick && waitFor([&] { return QGuiApplication::clipboard()->text() == "alpha-private-value"; });
            const bool masked = revealTableCell(window,"valueTable",0,2) && noLeak() && text(window, "value_data/alpha") == "[hidden]";
            const bool quiet = server.requests.size() == requests;
            if (!copiedByClick || !copied || !masked || !quiet)
                std::fprintf(stderr, "Secret copy: clicked=%d copied=%d masked=%d quiet=%d button=(%g,%g %gx%g) window=%dx%d\n",
                    copiedByClick, copied, masked, quiet, bounds.x(), bounds.y(), bounds.width(), bounds.height(), window->width(), window->height());
            if (!copiedByClick || !copied || !masked || !quiet) return false;
            const auto capture=qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
            return capture.isEmpty() || window->grabWindow().save(capture);
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
            const auto context = workspace.sessionConfiguration(session).value("contextId").toString();
            const auto path = workspace.inspectorPath();
            if (context.isEmpty() || path.isEmpty() || server.requests.size() != requests
                || !click(window,item(window,"closeCurrentSession"))
                || !waitFor([&] { return workspace.currentSession().isEmpty() && workspace.tabs().isEmpty()
                    && !workspace.busy() && workspace.inspectorPath().isEmpty(); }) || !noLeak()) return false;
            if (!click(window,item(window,"mapWorkspaceButton"))) return false;
            const auto card="contextMapCard_"+context;
            if (!waitFor([&] { return item(window,card) && item(window,card)->isVisible(); }) || !click(window,item(window,card))) return false;
            if (!waitFor([&] {
                const auto active = workspace.currentSession();
                return !active.isEmpty() && active != session && workspace.tabs().size() == 1 && !workspace.busy() && !workspace.loading()
                    && workspace.sessionConfiguration(active).value("contextId").toString() == context
                    && workspace.inspectorPath().isEmpty() && workspace.yamlText().isEmpty() && workspace.valuesTable()->rowCount() == 0;
            }) || !noLeak() || !click(window,item(window,"resourcesWorkspaceButton"))
                || !type(window,item(window,"resourceFilter"),"Secret")
                || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window,"cell_0_0"); })
                || !click(window,item(window,"cell_0_0")) || !click(window,item(window,"valuesButton"))) return false;
            const bool restored=waitFor([&] { return workspace.inspectorPath() == path && !workspace.loading()
                && text(window, "value_data/alpha") == "[hidden]" && text(window, "value_data/beta") == "[hidden]"; });
            if (!restored) std::fprintf(stderr,"Secret tab reopen: fresh=%d overview=%d inspector=%s page=%s values=%d masked=%d\n",workspace.currentSession()!=session,workspace.mapOverview(),qPrintable(workspace.inspectorPath()),qPrintable(workspace.property("inspectorPage").toString()),workspace.valuesTable()->rowCount(),text(window,"value_data/alpha")=="[hidden]");
            return restored && noLeak();
        }
        if (scenario == "inspector_secret_session") {
            const auto first = workspace.currentSession();

            if (!workspace.openContext(workspace.contexts().last().toMap()["id"].toString()) || !waitFor([&] { return workspace.currentSession() != first && !workspace.busy() && !workspace.loading() && workspace.table()->rowCount() == 3; }, 10000)) return false;
            if (!click(window,item(window,"resourcesWorkspaceButton"))) return false;
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
        if (!workspace.refresh()) return false;
        return waitFor([&] { return !workspace.inspected().contains("fetchedAt"); }) && !workspace.inspected().isEmpty();
    }
    if (scenario == "slow_close") {
        if (!waitFor([&] { return server.requests.join('\n').contains("/api/v1/namespaces/default/pods/alpha"); })) return false;
        if (!click(window,item(window,"closeCurrentSession"))
            || !waitFor([&] { return workspace.currentSession().isEmpty(); })) return false;
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
    auto clipboard = std::make_unique<QMimeData>();
    if (const auto* current = QGuiApplication::clipboard()->mimeData())
        for (const auto& format : current->formats()) clipboard->setData(format, current->data(format));
    const auto restoreClipboard = qScopeGuard([&] { QGuiApplication::clipboard()->setMimeData(clipboard.release()); });
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    if (!podlord::test::muteAlerts(workspace.alerts())) return false;
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
    if (!waitFor([&] { return !workspace.busy(); }) || !workspace.quickImportFile(source)
        || !waitFor([&] { return !workspace.busy() && workspace.contexts().size()==1; })
        || !workspace.openContext(workspace.contexts().first().toMap()["id"].toString())
        || !waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })
        || !workspace.setWorkspacePage("resources")) return failed("public source import/open");
    if (!waitFor([&] { return item(window, "resourceFilter")->isEnabled(); })) return failed("filter readiness");
    if (!click(window,item(window,"resourcesWorkspaceButton"))) return failed("resource tab");
    if (!type(window, item(window, "resourceFilter"), "podlord-native-e2e")) return failed("filter action");
    if (!waitFor([&] { return item(window, "resourceTable")->property("rows").toInt() == 1 && item(window, "cell_0_0"); }, 60000)) {
        std::fprintf(stderr, "TLS backend: %s; built library: %s; runtime library: %s\n",
            qPrintable(QSslSocket::activeBackend()), qPrintable(QSslSocket::sslLibraryBuildVersionString()),
            qPrintable(QSslSocket::sslLibraryVersionString()));
        std::fprintf(stderr, "Real cluster load failed: %s %s\n", qPrintable(text(window, "errorMessage")), qPrintable(text(window, "syncProblemMessage"))); return false;
    }
    if (!click(window, item(window, "cell_0_0"))) return failed("open ConfigMap row");
    const bool systemWindow = QGuiApplication::platformName() == "ios" || QGuiApplication::platformName() == "android";
    if (!systemWindow) window->resize(640, 320);
    if (!waitFor([&] {
        auto* content = item(window, "workspaceContent");
        auto* inspector = item(window, "workspaceInspector");
        auto* footer = item(window, "workspaceFooter");
        return content && inspector && footer && (systemWindow || !content->isVisible()) && inspector->isVisible()
            && inspector->height() > 100
            && inspector->mapToScene(QPointF(0, inspector->height())).y()
                <= (footer->isVisible() ? footer->mapToScene(QPointF()).y() : window->height()) + 1;
    })) return failed(systemWindow ? "system-sized inspector layout" : "compact landscape inspector layout");
    std::printf("Real inspector viewport: platform=%s width=%d height=%d desktop-resize=%d\n",
        qPrintable(QGuiApplication::platformName()), window->width(), window->height(), !systemWindow);
    if (!systemWindow) window->resize(1440, 920);
    if (!waitFor([&] { return workspace.inspected().contains("fetchedAt"); })
        || !workspace.inspected().contains("ConfigMap") || !workspace.inspected().contains("podlord-native-e2e")) return failed("fresh ConfigMap detail");
    if (!click(window, item(window, "yamlButton")) || !waitFor([&] { return text(window, "inspectorYaml").contains("native-api"); })
        || !text(window, "inspectorYaml").contains("apiVersion:")
        || text(window, "inspectorYaml").contains("\"apiVersion\"")
        || !item(window, "inspectorYaml")->property("readOnly").toBool()) return failed("ConfigMap YAML");
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
    if (!item(window, "workspaceContent")->isVisible()
        && (!click(window, item(window, "closeInspector")) || !waitFor([&] { return item(window, "workspaceContent")->isVisible(); }))) return failed("close full-pane inspector before Secret filter");
    if (!type(window, item(window, "resourceFilter"), "podlord-native-secret-e2e") || !waitFor([&] { return workspace.table()->rowCount() == 1 && item(window, "cell_0_0"); }, 60000)) return failed("filter Secret");
    if (!click(window, item(window, "cell_0_0")) || !click(window, item(window, "valuesButton"))
        || !waitFor([&] { return text(window, "value_data/alpha") == "[hidden]" && text(window, "value_data/beta") == "[hidden]"; })) return failed("Secret values");
    const auto encoded = QString::fromLatin1(QByteArray("podlord-alpha-real-secret").toBase64());
    if (text(window, "inspectorYaml").contains(encoded) || text(window, "inspectorYaml").contains("podlord-alpha-real-secret")) return failed("Secret YAML masking");
    std::unique_ptr<QPointingDevice> valueTouch;
    const auto clickValueAction = [&](const QString& name) {
        auto* table = item(window, "valueTable");
        if (!table) return false;
        for (int attempt = 0; attempt < 12; ++attempt) {
            if (auto* action = item(window, name); action && action->isEnabled()) return click(window, action);
            if (!valueTouch) valueTouch.reset(QTest::createTouchDevice());
            const auto start = table->mapToScene({table->width()*0.85, std::min(32.0, table->height()/2)}).toPoint();
            const auto end = table->mapToScene({table->width()*0.15, std::min(32.0, table->height()/2)}).toPoint();
            QTest::touchEvent(window, valueTouch.get()).press(0, start, window).commit();
            for (int step = 1; step <= 5; ++step) {
                QTest::touchEvent(window, valueTouch.get()).move(0, start + (end-start)*step/5, window).commit();
                QTest::qWait(20);
            }
            QTest::touchEvent(window, valueTouch.get()).release(0, end, window).commit();
            QTest::qWait(100);
        }
        std::fprintf(stderr, "Value action unavailable: %s; scroll=%g width=%g content=%g columns=%s\n", qPrintable(name),
            table->property("contentX").toDouble(), table->width(), table->property("contentWidth").toDouble(),
            qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(workspace.property("valuesColumns")).toJson(QJsonDocument::Compact))));
        return false;
    };
    if (!clickValueAction("reveal_data/alpha") || !waitFor([&] { return text(window, "value_data/alpha") == "podlord-alpha-real-secret"; })) return failed("Secret reveal");
    if (text(window, "value_data/beta") != "[hidden]" || !clickValueAction("copy_data/alpha") || QGuiApplication::clipboard()->text() != "podlord-alpha-real-secret") return failed("Secret copy");
    const auto screenshot = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        if (!clickValueAction("reveal_data/alpha") || !waitFor([&] { return text(window, "value_data/alpha") == "[hidden]"; }) || !click(window, item(window, "yamlButton"))) return false;
        QTest::qWait(50);
        if (!window->grabWindow().save(screenshot, "PNG")) return false;
    }
    return true;
}
bool systemHttps(const QString& address, bool expected, bool cancel = false) {
    const QUrl url(address);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    podlord::PlatformNetworkAccessManager network;
    QNetworkRequest request(url);
    const auto caFile = qEnvironmentVariable("PODLORD_TEST_HTTPS_CA_FILE");
    if (!caFile.isEmpty()) {
        const auto authorities = QSslCertificate::fromPath(caFile);
        if (authorities.isEmpty()) return false;
        auto tls = request.sslConfiguration(); tls.setCaCertificates(authorities); request.setSslConfiguration(tls);
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QList<qint64> heartbeats;
    QTimer heartbeat; heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { heartbeats.append(QDateTime::currentMSecsSinceEpoch()); });
    heartbeat.start();
    QElapsedTimer elapsed; elapsed.start();
    auto* reply = network.get(request);
    int finishes = 0;
    QObject::connect(reply, &QNetworkReply::finished, reply, [&] { ++finishes; });
    reply->setReadBufferSize(65536);
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply] { reply->readAll(); });
    if (cancel) QTimer::singleShot(1000, reply, &QNetworkReply::abort);
    if (!waitFor([&] { return reply->isFinished(); }, 20000)) return false;
    const auto completedMs = elapsed.elapsed();
    if (cancel) QTest::qWait(2500);
    const auto validationStart = reply->rawHeader("X-Podlord-CA-Start").toLongLong();
    const auto validationEnd = reply->rawHeader("X-Podlord-CA-End").toLongLong();
    const auto duringValidation = std::count_if(heartbeats.cbegin(), heartbeats.cend(), [&](qint64 time) { return time >= validationStart && time <= validationEnd; });
    const bool actual = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
    std::printf("System HTTPS proof: expected=%d actual=%d backend=%s system-cas=%lld error=%d\n", expected, actual,
        qPrintable(QSslSocket::activeBackend()), static_cast<long long>(QSslConfiguration::systemCaCertificates().size()), int(reply->error()));
    std::fflush(stdout);
    std::printf("System HTTPS responsiveness: elapsed_ms=%lld validation_ticks=%lld finishes=%d\n",
        static_cast<long long>(completedMs), static_cast<long long>(duringValidation), finishes);
    if (cancel) return reply->error() == QNetworkReply::OperationCanceledError && finishes == 1 && completedMs < 3000;
    return (expected ? actual : !actual && reply->error() == QNetworkReply::SslHandshakeFailedError)
        && (validationStart == 0 || duringValidation >= 2);
}
bool systemWebsocket(const QString& address, bool expected, bool cancel = false) {
    const QUrl url(address);
    if (!url.isValid() || url.scheme() != "wss" || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    QWebSocket socket;
    auto tls = socket.sslConfiguration();
    const auto caFile = qEnvironmentVariable("PODLORD_TEST_HTTPS_CA_FILE");
    if (!caFile.isEmpty()) {
        const auto authorities = QSslCertificate::fromPath(caFile);
        if (authorities.isEmpty()) return false;
        tls.setCaCertificates(authorities);
    }
    socket.setSslConfiguration(tls);
    podlord::configurePlatformTrust(socket);
    QList<qint64> heartbeats;
    QTimer heartbeat; heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] { heartbeats.append(QDateTime::currentMSecsSinceEpoch()); });
    heartbeat.start();
    bool received = false;
    QJsonObject message;
    QObject::connect(&socket, &QWebSocket::textMessageReceived, &socket, [&](const QString& value) {
        message = QJsonDocument::fromJson(value.toUtf8()).object(); received = true;
    });
    QElapsedTimer elapsed; elapsed.start();
    socket.open(url);
    if (cancel) QTimer::singleShot(1000, &socket, &QWebSocket::abort);
    if (!waitFor([&] { return received || socket.state() == QAbstractSocket::UnconnectedState; }, 20000)) return false;
    const auto completedMs = elapsed.elapsed();
    const auto error = socket.error();
    if (cancel) QTest::qWait(2500);
    const auto start = message.value("start").toInteger();
    const auto end = message.value("end").toInteger();
    const auto ticks = std::count_if(heartbeats.cbegin(), heartbeats.cend(), [&](qint64 time) { return time >= start && time <= end; });
    const bool actual = received && message.value("text").toString() == "Podlord isolated WebSocket";
    std::printf("System WebSocket proof: expected=%d actual=%d backend=%s error=%d elapsed_ms=%lld validation_ticks=%lld\n",
        expected, actual, qPrintable(QSslSocket::activeBackend()), int(error), static_cast<long long>(completedMs), static_cast<long long>(ticks));
    std::fflush(stdout);
    socket.abort();
    if (cancel) return !received && completedMs >= 900 && completedMs < 3000;
    return (expected ? actual : !actual && error == QAbstractSocket::SslHandshakeFailedError) && (start == 0 || ticks >= 2);
}
enum class ClusterJourney { Search, Themes, Map, Input };
bool realSearchCluster(const QString& source, ClusterJourney journey = ClusterJourney::Search) {
    const bool themeMatrix=journey==ClusterJourney::Themes;
    QTemporaryDir temporary; if (!temporary.isValid()) return false;
    podlord::Workspace workspace(temporary.filePath("profile"));
    if (!podlord::test::muteAlerts(workspace.alerts())) return false;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    const auto failure = [&](const char* stage) {
        std::fprintf(stderr, "Real cluster UI stage failed: %s; window=%dx%d exposed=%d; %s; %s\n", stage,
            window ? window->width() : 0, window ? window->height() : 0, window && window->isExposed(),
            qPrintable(workspace.error()), qPrintable(workspace.status()));
        const auto frame = qEnvironmentVariable("PODLORD_UI_FAILURE_FRAME");
        if (window && !frame.isEmpty()) window->grabWindow().save(frame, "PNG");
        return false;
    };
    if (!window || !waitFor([&] { return window->isExposed() && !workspace.busy(); })
        ) return failure("import readiness");
#ifdef Q_OS_IOS
    if (!podlord::test::orientIosTestWindow(window)) return failure("system orientation");
#endif
    if (!workspace.saveReadSettings(1200,0,"5",workspace.collectionCacheHours()) || !waitFor([&] { return !workspace.busy() && workspace.requestLimit()==1200; })) return failure("read policy");
    if (!workspace.quickImportFile(source) || !waitFor([&] { return !workspace.busy() && workspace.contexts().size()==1; })) return failure("context discovery");
    if (!click(window,item(window,"mapWorkspaceButton"))) return failure("open imported context overview");
    std::printf("Real cluster TLS: supported=%d backend=%s available=%s loaded=%s built=%s\n", QSslSocket::supportsSsl(),
        qPrintable(QSslSocket::activeBackend()), qPrintable(QSslSocket::availableBackends().join(',')),
        qPrintable(QSslSocket::sslLibraryVersionString()), qPrintable(QSslSocket::sslLibraryBuildVersionString()));
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("PODLORD_TLS_DIAGNOSTICS")) {
        const auto resolved = podlord::KubeconfigStore(temporary.filePath("profile")).connection(workspace.contexts().first().toMap().value("id").toString());
        if (const auto* connection = std::get_if<podlord::ClusterConnection>(&resolved)) {
            QSslSocket socket;
            socket.setSslConfiguration(connection->tls);
            socket.setPeerVerifyName(connection->peerName);
            socket.connectToHostEncrypted(connection->server.host(), quint16(connection->server.port(443)));
            waitFor([&] { return socket.isEncrypted() || socket.state() == QAbstractSocket::UnconnectedState; }, 10000);
            QStringList errors;
            for (const auto& error : socket.sslHandshakeErrors()) errors.append(QString::number(error.error()));
            std::printf("Real fixture TLS probe: encrypted=%d socket-error=%d ssl-errors=%s reason=%s\n", socket.isEncrypted(), int(socket.error()),
                qPrintable(errors.join(',')), qPrintable(socket.errorString().left(200)));
            std::fflush(stdout);
            socket.disconnectFromHost();
        } else std::fprintf(stderr, "Real fixture TLS probe could not resolve its imported connection.\n");
    }
    const auto contextCard="contextMapCard_"+workspace.contexts().first().toMap()["id"].toString();
    if (!waitFor([&] { auto* card=item(window,contextCard); return card && card->isVisible(); })) return failure("imported context card");
    QSignalSpy contextOpened(item(window,contextCard),SIGNAL(clicked()));
    if (!click(window,item(window,contextCard)) || !waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); },10000)) {
        std::fprintf(stderr,"Context open: clicks=%lld busy=%d active=%s count=%lld chosen=%s\n",static_cast<long long>(contextOpened.size()),workspace.busy(),qPrintable(workspace.currentSession()),static_cast<long long>(workspace.contexts().size()),qPrintable(workspace.contexts().isEmpty() ? QString{} : workspace.contexts().first().toMap()["id"].toString()));
        return failure("explicit context selection");
    }
    if (!waitFor([&] {
        return !workspace.loading() && (workspace.totalResourceCount()>=1051 || !workspace.error().isEmpty());
    },120000) || workspace.totalResourceCount()<1051) {
        std::fprintf(stderr,"Real search load: total=%d; %s; %s\n",workspace.totalResourceCount(),qPrintable(workspace.error()),qPrintable(workspace.status())); return failure("initial resource reads");
    }
    if (journey!=ClusterJourney::Map && !click(window,item(window,"resourcesWorkspaceButton"))) return failure("resource tab");
    if (journey==ClusterJourney::Input) {
        const auto directory=qEnvironmentVariable("PODLORD_INPUT_EVIDENCE_DIR");
        auto* input=QGuiApplication::inputMethod();
        if (directory.isEmpty() || !QDir().mkpath(directory) || !input) return failure("input output");
        const auto aboveKeyboard=[&](const char* name,int minimumHeight) {
            auto* target=item(window,name); const auto keyboard=input->keyboardRectangle();
            if (!input->isVisible() || keyboard.height()<=0 || !target || !target->isVisible() || target->height()<minimumHeight) return false;
            const auto top=target->mapToScene(QPointF{}).y();
            return top>=window->safeAreaMargins().top()
                && target->mapToScene({0,target->height()}).y()<=keyboard.top();
        };
        const auto capture=[&](const char* name) { return window->grabWindow().save(directory+"/"+name+".png"); };
        if (!click(window,item(window,"workspaceSearchButton")) || !type(window,item(window,"resourceFilter"),"visual-multi-container")) return failure("input search");
        input->show();
        if (!waitFor([&] { return aboveKeyboard("resourceFilter",32); },10000) || !capture("search-keyboard")) return failure("search keyboard geometry");
        input->hide();
        if (!waitFor([&] { return !input->isVisible(); })) return failure("search keyboard closure");
        auto* filterToggle=item(window,window->width()<600 ? "toggleSidebar" : "toggleLandscapeFilters");
        if (!filterToggle || !click(window,filterToggle)) return failure("preset filters disclosure");
        if (window->width()<600) {
            auto* drawer=window->findChild<QObject*>("sidebarDrawer");
            if (!drawer || !waitFor([&] { return drawer->property("opened").toBool(); })) return failure("preset drawer readiness");
        }
        if (!click(window,item(window,"filterPreset"))
            || !waitFor([&] { auto* field=item(window,"filterPresetName"); return field && field->isVisible(); })) return failure("saved filters disclosure");
        if (!type(window,item(window,"filterPresetName"),"iOS-preset")) return failure("preset name entry");
        input->show();
        if (!waitFor([&] { return aboveKeyboard("filterPresetName",16); },10000)
            || !capture("preset-save-keyboard")) return failure("preset keyboard geometry");
        input->hide();
        if (!waitFor([&] { return !input->isVisible(); }) || !click(window,item(window,"saveFilterPreset"))
            || !waitFor([&] { return !workspace.filterPresetsBusy() && workspace.filterPresets().contains("iOS-preset"); })) return failure("preset save");
        if (!click(window,item(window,"resetResourceFilters")) || !waitFor([&] { return workspace.filterText().isEmpty(); })) return failure("preset reset");
        if (!item(window,"filterPreset")->property("checked").toBool() && !click(window,item(window,"filterPreset"))) return failure("preset reset and disclosure");
        const auto choice="filterPresetOption_"+QString::number(workspace.filterPresets().indexOf("iOS-preset"));
        if (!waitFor([&] { auto* option=item(window,choice); return option && option->isVisible(); })
            || !click(window,item(window,choice)) || !waitFor([&] { return workspace.filterText()=="visual-multi-container"; })) return failure("preset restores search");
        if (!click(window,item(window,"filterPreset")) || !click(window,item(window,"renameFilterPreset"))
            || !type(window,item(window,"renameFilterPresetName"),"iOS-renamed")) return failure("preset rename entry");
        input->show();
        if (!waitFor([&] { return aboveKeyboard("renameFilterPresetName",16); },10000)
            || !capture("preset-rename-keyboard")) return failure("preset rename keyboard geometry");
        input->hide();
        if (!waitFor([&] { return !input->isVisible(); })) return failure("preset rename keyboard closure");
        auto* rename=window->findChild<QObject*>("renameFilterPresetDialog");
        if (!rename) return failure("preset rename dialog");
        QQmlExpression confirmation(qmlContext(rename),rename,"standardButton(Dialog.Ok)");
        auto* confirm=confirmation.evaluate().value<QQuickItem*>();
        if (confirmation.hasError() || !confirm || !click(window,confirm)
            || !waitFor([&] { return !workspace.filterPresetsBusy() && workspace.filterPresets().contains("iOS-renamed")
                && !workspace.filterPresets().contains("iOS-preset"); })) return failure("preset rename confirmation");
        if (!click(window,item(window,"deleteFilterPreset"))
            || !waitFor([&] { return !workspace.filterPresetsBusy() && workspace.filterPresets()==QStringList{"default"}; })
            || workspace.filterText()!="visual-multi-container" || !capture("preset-deleted-search-retained")) return failure("preset delete retains search");
        auto* drawer=window->findChild<QObject*>("sidebarDrawer");
        if (drawer && drawer->property("visible").toBool()) {
            QTest::keyClick(window,Qt::Key_Escape);
            if (!waitFor([&] { return !drawer->property("visible").toBool(); })) return failure("preset drawer closure");
        } else if (filterToggle->property("checked").toBool() && !click(window,filterToggle)) return failure("preset filters closure");
        if (!waitFor([&] { return !input->isVisible(); })
            || !workspace.inspectPath("/api/v1/namespaces/visual-a/pods/visual-multi-container")
            || !waitFor([&] { return workspace.canStartTerminal(); },30000)) return failure("input terminal readiness");
        if (window->width()<600) {
            if (!click(window,item(window,"overviewButton"))) return failure("portrait inspector tabs");
            const int y=qRound(item(window,"overviewButton")->mapToScene({0,22}).y());
            const QPoint start(window->width()-30,y),end(30,y);
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
            for (int step=1;step<=8;++step) QTest::mouseMove(window,start+(end-start)*step/8,20);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        }
        if (!click(window,item(window,"terminalButton")) || !click(window,item(window,"terminalConnect"))) return failure("input terminal start");
        if (!waitFor([&] { return workspace.containerTerminal() && workspace.containerTerminal()->connected(); },30000)) return failure("input terminal connection");
        const auto* connected=workspace.containerTerminal();
        input->show();
        if (!waitFor([&] { return aboveKeyboard("terminalStatus",16) && aboveKeyboard("terminalSurface",28); },10000)
            || !capture("terminal-keyboard")) return failure("terminal keyboard geometry");
        auto* surface=item(window,"terminalSurface");
        QInputMethodEvent command; command.setCommitString("printf 'PODLORD_IOS_IME_OK\\n'"); QCoreApplication::sendEvent(surface,&command);
        QTest::keyClick(window,Qt::Key_Return);
        if (!waitFor([&] { return surface->property("visibleText").toString().split('\n').contains("PODLORD_IOS_IME_OK"); },15000)) return failure("input terminal echo");
        input->hide();
        if (!waitFor([&] { auto* actions=item(window,"terminalActions"); return !input->isVisible()
            && workspace.containerTerminal()==connected && connected->connected() && actions && actions->isVisible(); })
            || !capture("terminal-restored")) return failure("input controls restored");
        if (!click(window,surface)) return failure("terminal exit focus");
        QInputMethodEvent exit; exit.setCommitString("exit"); QCoreApplication::sendEvent(surface,&exit); QTest::keyClick(window,Qt::Key_Return);
        return waitFor([&] { return !connected->active(); },15000) || failure("terminal shell exit");
    }
    if (journey==ClusterJourney::Map) {
        const auto directory=qEnvironmentVariable("PODLORD_MAP_EVIDENCE_DIR");
        if (directory.isEmpty() || !QDir().mkpath(directory) || !workspace.setWorkspacePage("map")) return failure("map output");
        const auto session=workspace.currentSession();
        const auto count=workspace.totalResourceCount();
        const auto theme=workspace.themeName();
        const auto context=workspace.contexts().first().toMap()["id"].toString();
        if (!waitFor([&] { return !workspace.mapPreviewUrl(context).isEmpty(); })) return failure("accepted map preview");
        for (const auto& variant : {QString("dark"),QString("light")}) {
            if (!workspace.saveAppearance(theme,variant) || !waitFor([&] { return !workspace.busy(); })) return failure("map theme");
            if (auto* map = item(window,"resourceRadar")) { map->forceActiveFocus(); QTest::keyClick(window,Qt::Key_0); } else return failure("map reset");
            for (int zoom=0;zoom<4;++zoom) if (!click(window,item(window,"radarZoomOut"))) return failure("map overview zoom");
            for (const auto& view : {QString("overview"),QString("details")}) {
                if (view=="details") for (int zoom=0;zoom<7;++zoom) if (!click(window,item(window,"radarZoom"))) return failure("map detail zoom");
                QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
                if (!waitFor([&] { return !frames.isEmpty(); })
                    || !window->grabWindow().save(directory+"/map-"+variant+"-"+view+".png")) return failure("map framebuffer");
            }
            if (!click(window,item(window,"mapOverview"))
                || !waitFor([&] { auto* image=item(window,"contextMapPreview_"+context); return workspace.mapOverview() && image && image->property("status").toInt()==1; })) return failure("context preview overview");
            QSignalSpy frames(window,&QQuickWindow::frameSwapped); window->update();
            if (!waitFor([&] { return !frames.isEmpty(); }) || !window->grabWindow().save(directory+"/contexts-"+variant+".png")) return failure("context overview framebuffer");
            if (!click(window,item(window,"contextMapCard_"+context)) || !waitFor([&] { return !workspace.busy() && !workspace.mapOverview() && workspace.currentSession()==session; })) return failure("reused context open");
        }
        return workspace.currentSession()==session && workspace.totalResourceCount()==count && workspace.filterText().isEmpty();
    }
    const QMap<QString,QPair<QString,int>> expectedKinds{
        {"ConfigMap",{"visual-config-",512}}, {"Secret",{"visual-secret-",256}},
        {"Deployment",{"visual-worker-",64}}, {"StatefulSet",{"visual-state-",32}},
        {"DaemonSet",{"visual-daemon-",32}}, {"CronJob",{"visual-scheduled-",32}},
        {"PersistentVolumeClaim",{"visual-pending-storage-",32}}, {"Service",{"visual-service-",64}},
        {"ServiceAccount",{"visual-account-",16}}, {"Job",{"visual-suspended-job",8}},
        {"Namespace",{"visual-",8}}, {"RadarProbe",{"visual-radar-probe",8}},
        {"Pod",{"visual-multi-container",1}}};
    QMap<QString,int> discovered;
    for (int row=0;row<workspace.table()->rowCount();++row) {
        const auto index=workspace.table()->index(row,0);
        const auto kind=index.data(Qt::UserRole+2).toString();
        const auto found=expectedKinds.constFind(kind);
        if (found!=expectedKinds.cend() && index.data(Qt::UserRole+1).toString().startsWith(found->first))
            ++discovered[kind];
    }
    for (auto it=expectedKinds.cbegin();it!=expectedKinds.cend();++it) {
        if (discovered.value(it.key())!=it->second) {
            std::fprintf(stderr,"Real discovery %s: count=%d expected=%d\n",qPrintable(it.key()),discovered.value(it.key()),it->second);
            return false;
        }
    }
    if (themeMatrix) {
        const auto directory = qEnvironmentVariable("PODLORD_THEME_EVIDENCE_DIR");
        if (directory.isEmpty() || !QDir{}.mkpath(directory) || workspace.themeNames().size() != 19) return false;
        QJsonArray captures;
        QTcpServer portProbe;
        if (!portProbe.listen(QHostAddress::LocalHost, 0)) return failure("theme loopback port");
        const auto loopbackPort = QString::number(portProbe.serverPort());
        portProbe.close();
        if (!workspace.inspectPath("/api/v1/namespaces/visual-a/pods/visual-multi-container")
            || !waitFor([&] { return workspace.canPortForward(); })
            || !workspace.preparePortForward() || !workspace.startPreparedPortForward(loopbackPort, "8080")
            || !waitFor([&] { return !workspace.portForwards().isEmpty(); }) || !workspace.closeInspector())
            return failure("theme owned connection");
        const auto choose = [&](const QString& name, int index) {
            if (!podlord::test::clickVisible(window, name)) return false;
            auto* control = item(window, name);
            auto* popup = control ? control->property("popup").value<QObject*>() : nullptr;
            if (!popup || !waitFor([&] { return popup->property("opened").toBool(); })) return false;
            QTest::keyClick(window, Qt::Key_Home);
            for (int row = 0; row < index; ++row) QTest::keyClick(window, Qt::Key_Down);
#ifdef Q_OS_IOS
            QQuickItem* option = nullptr;
            if (!waitFor([&] {
                QQmlExpression expression(qmlContext(control), control, QString("popup.contentItem.itemAtIndex(%1)").arg(index));
                option = expression.evaluate().value<QQuickItem*>();
                return !expression.hasError() && option && !podlord::test::visibleBounds(window, option).isEmpty();
            })) return false;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, podlord::test::visibleBounds(window, option).center().toPoint());
#else
            QTest::keyClick(window, Qt::Key_Return);
#endif
            return waitFor([&] { return !popup->property("visible").toBool() && !workspace.busy(); });
        };
        const QStringList sections{"alerts", "appearance", "diagnostics", "sources", "sync", "about"};
        const auto section = [&](const QString& name) { return podlord::test::selectSettingsSection(window, name); };
        const auto dismissSidebar = [&] { return podlord::test::closeSidebarDrawer(window); };
        const auto capture = [&](const QString& view, int themeIndex, const QString& variant) {
            QSignalSpy frame(window, &QQuickWindow::frameSwapped);
            window->requestUpdate();
            if (!waitFor([&] { return !frame.isEmpty(); })) return false;
            const auto image = window->grabWindow();
            const auto file = QString("%1-%2-%3.png").arg(themeIndex, 2, 10, QLatin1Char('0')).arg(variant, view);
            if (image.isNull() || !image.save(QDir(directory).filePath(file), "PNG")) return false;
            captures.append(QJsonObject{{"theme", workspace.themeName()}, {"variant", variant}, {"view", view},
                {"file", file}, {"width", image.width()}, {"height", image.height()},
                {"resources", workspace.totalResourceCount()}, {"workspacePage",workspace.workspacePage()},
                {"bodyPixels",window->property("font").value<QFont>().pixelSize()}});
            return true;
        };
        const auto names = workspace.themeNames();
        for (int themeIndex = 0; themeIndex < names.size(); ++themeIndex) {
            for (const auto& variant : QStringList{"dark", "light"}) {
                if (!podlord::test::clickVisible(window, "settingsWorkspaceButton")
                    || !waitFor([&] { return workspace.workspacePage() == "settings"; })) return failure("theme settings navigation");
                if (!section("appearance")) return failure("theme appearance selection");
                if (!choose("inlineAppearanceTheme", themeIndex) || !choose("inlineAppearanceVariant", variant == "dark" ? 0 : 1)
                    || !waitFor([&] { return workspace.themeName() == names[themeIndex] && workspace.themeVariant() == variant; })) return failure("theme selection");
                for (const auto& name : sections)
                    if (!section(name) || !capture("settings-" + name, themeIndex, variant)) return failure("theme settings capture");
                if (!workspace.setWorkspacePage("map") || workspace.mapOverview()
                    || !capture("map-context",themeIndex,variant)) return failure("theme context map capture");
                if (!workspace.showMapOverview() || !waitFor([&] {
                    if (!workspace.mapOverview() || workspace.contexts().isEmpty()) return false;
                    const auto context=workspace.contexts().first().toMap().value("id").toString();
                    const auto* preview=item(window,"contextMapPreview_"+context);
                    return preview && preview->property("status").toInt()==1;
                }) || !capture("map-overview",themeIndex,variant)) return failure("theme persisted overview capture");
                if (!workspace.activate(workspace.currentSession()) || !waitFor([&] { return !workspace.busy() && !workspace.mapOverview(); })) return failure("theme current context navigation");
                for (const auto& view : QList<QPair<QString, QString>>{{"resources", "resourcesWorkspaceButton"},
                    {"radar", ""}, {"events", "eventsWorkspaceButton"}, {"ports", "portForwardTasksButton"}}) {
                    if (view.first=="radar" ? !podlord::test::focusResourceRadar(window) : !click(window,item(window,view.second))) return failure("theme workspace navigation");
                    if (view.first != "radar" && !waitFor([&] { return workspace.workspacePage() == view.first; })) return failure("theme workspace selection");
                    if (!capture(view.first, themeIndex, variant) || !dismissSidebar()) return failure(qPrintable("theme workspace capture "+view.first));
                }
                if (!click(window, item(window, "resourcesWorkspaceButton"))) return failure("theme resource navigation");
                if (!type(window, item(window, "resourceFilter"), "\"local-path-config\"")) return failure("theme resource search input");
                if (!waitFor([&] {
                    auto* cell = item(window, "cell_0_0");
                    return workspace.resourceCount() == 1 && cell && cell->isVisible() && cell->width() > 0 && cell->height() > 0;
                })) return failure("theme filtered row readiness");
                if (!click(window, item(window, "cell_0_0"))) return failure("theme inspector open");
                if (!waitFor([&] { auto* button = item(window, "overviewButton"); return button && button->isVisible(); })) return failure("theme inspector readiness");
                for (const auto& view : QList<QPair<QString, QString>>{{"overview", "overviewButton"}, {"yaml", "yamlButton"},
                    {"events", "inspectorEventsButton"}, {"links", "inspectorLinksButton"}, {"values", "valuesButton"}}) {
                    if (!click(window, item(window, view.second))) return false;
                    if (view.first == "yaml" && !waitFor([&] { return workspace.canEditYaml()
                        && item(window,"editYaml")->isEnabled() && !item(window,"yamlDraftStatus")->isVisible(); })) return false;
                    if (!capture("inspector-" + view.first, themeIndex, variant)) return false;
                }
                if (!click(window, item(window, "closeInspector"))) return failure("theme inspector close");
                if (!workspace.filterField("kind","\"Pod\"") || !type(window,item(window,"resourceFilter"),"\"visual-multi-container\"") || !waitFor([&] {
                    const auto* cell=item(window,"cell_0_0"); return workspace.resourceCount()==1 && cell && cell->isVisible();
                }) || !click(window,item(window,"cell_0_0")) || !waitFor([&] { return workspace.canEditYaml(); })) return failure("theme terminal Pod readiness");
                if (!workspace.setInspectorPage("terminal") || !waitFor([&] {
                    const auto* connect=item(window,"terminalConnect"); return connect && connect->isVisible() && connect->isEnabled();
                }) || !capture("inspector-terminal",themeIndex,variant)) return failure("theme terminal ready form capture");
                if (!workspace.setInspectorPage("overview") || !waitFor([&] {
                    const auto* terminal=item(window,"terminalConnect");
                    const auto* close=item(window,"closeInspector");
                    return (!terminal || !terminal->isVisible()) && close && close->isVisible() && close->isEnabled();
                }) || !click(window,item(window,"closeInspector"))) return failure("theme terminal inspector close");
                auto* sidebarHost = item(window, "workspaceSidebarHost");
                auto* drawer = window->findChild<QObject*>("sidebarDrawer");
                const bool revealSidebar = !(sidebarHost && sidebarHost->isVisible())
                    && !(drawer && drawer->property("visible").toBool());
                if (revealSidebar && (!click(window, item(window, "toggleSidebar")) || !waitFor([&] {
                    return (sidebarHost && sidebarHost->isVisible()) || (drawer && drawer->property("opened").toBool());
                }))) return failure("theme filter sidebar open");
                auto* filterToggle = item(window, "toggleLandscapeFilters");
                const bool expandFilters = filterToggle && filterToggle->isVisible() && !filterToggle->property("checked").toBool();
                if (expandFilters && (!click(window, filterToggle)
                    || !waitFor([&] { return item(window, "sidebarFilterScroll")->isVisible(); }))) return failure("theme filter panel open");
                if (!click(window, item(window, "resetResourceFilters"))) return failure("theme filter reset");
                if (expandFilters && (!click(window, item(window, "toggleLandscapeFilters"))
                    || !waitFor([&] { return !item(window, "sidebarFilterScroll")->isVisible(); }))) return failure("theme filter panel close");
                if (revealSidebar && (sidebarHost && sidebarHost->isVisible()
                    ? !click(window, item(window, "toggleSidebar")) || !waitFor([&] { return !sidebarHost->isVisible(); })
                    : !dismissSidebar())) return failure("theme filter sidebar close");
                if (!waitFor([&] { return workspace.resourceCount() == workspace.totalResourceCount(); })) return failure("theme filter reset results");
                std::printf("Theme views captured: %s / %s\n", qPrintable(names[themeIndex]), qPrintable(variant));
                std::fflush(stdout);
            }
        }
        QFile manifest(QDir(directory).filePath("matrix.json"));
        const auto bytes = QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()},
            {"style", qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE")}, {"captures", captures}}).toJson();
        if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size()) return false;
        std::printf("Real Kubernetes theme matrix: %lld populated view captures.\n", static_cast<long long>(captures.size()));
        return true;
    }
    const QList<QPair<QString,int>> queries{{"~visual-config-",512},{"~visual-secret-",256},
        {"\"visual-radar-probe\"",8},
        {"/^visual-config-000[1-3]$/",24},{"\"visual-config-0001\" \"visual-secret-0001\"",16},
        {"/[/",0},{"not-a-resource-in-this-owned-cluster",0}};
    for (const auto& query:queries) {
        if (!type(window,item(window,"resourceFilter"),query.first)
            || !waitFor([&] { return workspace.resourceCount()==query.second; })) {
            std::fprintf(stderr,"Real search %s: count=%d expected=%d; %s\n",qPrintable(query.first),workspace.resourceCount(),query.second,qPrintable(workspace.filterError())); return false;
        }
        if ((query.first=="/[/") != item(window,"filterErrorMessage")->isVisible()) return false;
        if (!podlord::test::focusResourceRadar(window) || !waitFor([&] { return item(window,"resourceRadar")->property("count").toInt()==query.second; })) return false;
        if (!click(window,item(window,"resourcesWorkspaceButton"))) return false;
    }
    if (!click(window,item(window,"resetResourceFilters")) || !waitFor([&] { return workspace.resourceCount()==workspace.totalResourceCount() && workspace.resourceCount()>=1051; })) return false;
    if (!type(window,item(window,"resourceFilter"),"/^visual-config-000[1-3]$/") || !podlord::test::focusResourceRadar(window)) return false;
    if (!waitFor([&] { return item(window,"resourceRadar")->property("count").toInt()==24; })) return false;
    auto* filteredRadar=item(window,"resourceRadar");
    const auto firstResource=workspace.table()->index(0,0);
    const auto expectedFocus=firstResource.data(Qt::UserRole+2).toString()+" "+firstResource.data(Qt::UserRole+1).toString();
    filteredRadar->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Home);
    if (!waitFor([&] {
        auto* details=item(window,"radarInfoText");
        return details && details->isVisible() && details->property("text").toString()==expectedFocus;
    })) {
        const auto first=workspace.table()->index(0,0);
        qWarning()<<"Real filtered Radar focus:"<<filteredRadar->hasActiveFocus()<<filteredRadar->property("currentIndex")
            <<first.data(Qt::UserRole)<<first.data(Qt::UserRole+1)<<first.data(Qt::UserRole+2);
        return failure("filtered ConfigMap keyboard focus");
    }
    const auto screenshot=qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
    if (!screenshot.isEmpty()) { QTest::qWait(50); if (!window->grabWindow().save(screenshot,"PNG")) return false; }
    std::printf("Real Kubernetes discovery/search: ConfigMaps, Secrets, workloads, storage, services, accounts, namespaces, error Pods and custom RadarProbes; radar/table agree; regex, alternatives, invalid/empty/reset verified.\n");
    return true;
}
bool applicationBoundary(const QString& scenario, const QString& binary) {
#if QT_CONFIG(process)
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
#else
    Q_UNUSED(scenario);
    Q_UNUSED(binary);
    std::fprintf(stderr, "Application subprocess tests require Qt process support.\n");
    return false;
#endif
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2 && argc != 3) { std::fprintf(stderr,"Expected a UI scenario and optional input, received %d arguments.\n",argc); return 2; }
    const QString scenario = QString::fromLocal8Bit(argv[1]);
    const auto run = [&] {
        const bool passed = argc == 3 ? (scenario.startsWith("app.") ? applicationBoundary(scenario, QString::fromLocal8Bit(argv[2]))
            : scenario == "system_https_trusted" || scenario == "system_https_untrusted" || scenario == "system_https_canceled" ? systemHttps(QString::fromLocal8Bit(argv[2]), scenario == "system_https_trusted", scenario == "system_https_canceled")
            : scenario == "system_wss_trusted" || scenario == "system_wss_untrusted" || scenario == "system_wss_canceled" ? systemWebsocket(QString::fromLocal8Bit(argv[2]), scenario == "system_wss_trusted", scenario == "system_wss_canceled")
            : scenario == "real_search" || scenario == "real_themes" || scenario == "real_map" || scenario == "real_input" ? realSearchCluster(QString::fromLocal8Bit(argv[2]),
                scenario=="real_themes" ? ClusterJourney::Themes : scenario=="real_map" ? ClusterJourney::Map : scenario=="real_input" ? ClusterJourney::Input : ClusterJourney::Search)
            : scenario == "real" && realCluster(QString::fromLocal8Bit(argv[2]))) : execute(scenario);
        if (!passed) std::fprintf(stderr, "UI scenario failed: %s\n", argv[1]);
        else std::fprintf(stdout, "UI scenario passed: %s\n", argv[1]);
        return passed ? 0 : 1;
    };
#ifdef Q_OS_IOS
    QTimer::singleShot(0, &app, [&] { app.exit(run()); });
    return app.exec();
#else
    return run();
#endif
}
