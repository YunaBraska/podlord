#include "workspace.h"
#include "ui_input.h"
#include <QClipboard>
#include <QDesktopServices>
#include "kubeconfig_store.h"
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QUrlQuery>
#include <QSslSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QPointer>
#include <QtTest/QTest>
#include <cstdio>
#include <functional>
#include <memory>
#include <algorithm>

class BrowserBoundary final : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
    BrowserBoundary() {
        QDesktopServices::setUrlHandler("http", this, "open");
        QDesktopServices::setUrlHandler("https", this, "open");
    }
    ~BrowserBoundary() override {
        QDesktopServices::unsetUrlHandler("http"); QDesktopServices::unsetUrlHandler("https");
    }
public slots:
    bool open(const QUrl& url) { urls.append(url); return true; }
};

namespace {
bool waitFor(const std::function<bool()>& predicate, int limit = 15000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QTest::qWait(10); }
    return predicate();
}
bool run(const QString& scenario, const QString& configPath) {
    const bool real = scenario.startsWith("real_");
    const bool service = scenario.contains("service") || scenario == "selector" || scenario == "no_pod" || scenario == "udp"
        || scenario == "auth" || scenario == "forbidden" || scenario == "rate" || scenario == "redirect" || scenario == "malformed" || scenario == "recreated";
    QTemporaryDir profile;
    QTcpServer api;
    QWebSocketServer streaming("External Kubernetes boundary", QWebSocketServer::NonSecureMode);
    streaming.setSupportedSubprotocols({scenario == "protocol" ? "unsupported" : "v4.channel.k8s.io"});
    if (!profile.isValid() || (!real && !api.listen(QHostAddress::LocalHost))) return false;
    const QString ns = real ? "visual-a" : "default";
    const QString podName = real ? "podlord-forward-echo" : "echo";
    const QString serviceName = real ? "podlord-forward-echo" : "echo-service";
    const QString podPath = "/api/v1/namespaces/" + ns + "/pods/" + podName;
    const QString servicePath = "/api/v1/namespaces/" + ns + "/services/" + serviceName;
    const QString selected = service ? servicePath : podPath;
    QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"}, {"metadata", QJsonObject{{"name", podName}, {"namespace", ns}, {"uid", "pod-uid"}, {"resourceVersion", "1"}, {"labels", QJsonObject{{"app", "echo"}}}}},
        {"status", QJsonObject{{"phase", scenario == "terminal" ? "Succeeded" : "Running"}}},
        {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "echo"}, {"image", "external-test"}, {"ports", QJsonArray{QJsonObject{{"name", "http"}, {"containerPort", 8080}}}}}}}}}};
    QJsonObject svc{{"apiVersion", "v1"}, {"kind", "Service"}, {"metadata", QJsonObject{{"name", serviceName}, {"namespace", ns}, {"uid", "service-uid"}, {"resourceVersion", "1"}}},
        {"spec", QJsonObject{{"selector", scenario == "selector" ? QJsonObject{} : QJsonObject{{"app", "echo"}}}, {"ports", QJsonArray{QJsonObject{{"port", 80}, {"targetPort", scenario == "service_numeric" ? QJsonValue(8080) : QJsonValue("http")}, {"protocol", scenario == "udp" ? "UDP" : "TCP"}}}}}}};
    int requests = 0, upgrades = 0;
    int targetLists = 0;
    bool changed = false, safe = true;
    QPointer<QTcpSocket> heldUpgrade;
    QStringList dispatchOrder;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto bytes = socket->peek(65536);
                if (!bytes.contains("\r\n\r\n")) return;
                if (bytes.toLower().contains("upgrade: websocket")) {
                    ++upgrades;
                    dispatchOrder.append("upgrade");
                    safe = safe && bytes.toLower().contains("authorization: bearer local-forward-token");
                    if (!bytes.toLower().contains("accept: */*\r\n")) {
                        socket->readAll();
                        socket->write("HTTP/1.1 406 Not Acceptable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost(); return;
                    }
                    if (scenario == "queued_close") { socket->readAll(); return; }
                    if (scenario == "priority" && upgrades == 1) { heldUpgrade = socket; socket->readAll(); return; }
                    QObject::disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);
                    streaming.handleConnection(socket); return;
                }
                socket->readAll(); ++requests;
                const QUrl url(QString::fromUtf8(bytes.split('\n').first().split(' ').value(1)));
                const auto path = url.path();
                QJsonObject result; int http = 200;
                if (path == "/api") result = {{"versions", QJsonArray{"v1"}}};
                else if (path == "/apis") result = {{"groups", QJsonArray{}}};
                else if (path == "/api/v1") result = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}, QJsonObject{{"name", "services"}, {"kind", "Service"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                else if (path == "/api/v1/pods") result = {{"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}};
                else if (path == "/api/v1/services") result = {{"metadata", QJsonObject{}}, {"items", QJsonArray{svc}}};
                else if (path == "/api/v1/namespaces/default/pods") {
                    ++targetLists;
                    safe = safe && QUrlQuery(url).queryItemValue("labelSelector") == "app=echo";
                    auto item = pod;
                    if (scenario == "service_list_items") { item.remove("kind"); item.remove("apiVersion"); }
                    if (scenario == "service_wrong_item") item["kind"] = "Secret";
                    result = {{"apiVersion", "v1"}, {"kind", scenario == "service_wrong_list" ? "SecretList" : "PodList"},
                        {"metadata", QJsonObject{}}, {"items", scenario == "no_pod" ? QJsonArray{} : QJsonArray{item}}};
                    auto metadata = item["metadata"].toObject();
                    if (scenario == "service_reject_pod_namespace") metadata["namespace"] = "other";
                    if (scenario == "service_reject_pod_name") metadata["name"] = "../other";
                    if (scenario == "service_reject_pod_uid") metadata.remove("uid");
                    if (scenario == "service_reject_pod_deleted") metadata["deletionTimestamp"] = "2026-10-06T09:00:00Z";
                    if (scenario == "service_reject_pod_labels") metadata["labels"] = QJsonObject{{"app", "other"}};
                    item["metadata"] = metadata;
                    if (scenario == "service_reject_pod_phase") item["status"] = QJsonObject{{"phase", "Pending"}};
                    if (scenario == "service_reject_pod_version") item["apiVersion"] = "other/v1";
                    if (scenario == "service_reject_target_udp") {
                        auto spec = item["spec"].toObject(); auto containers = spec["containers"].toArray();
                        auto container = containers.first().toObject(); auto ports = container["ports"].toArray();
                        auto port = ports.first().toObject(); port["protocol"] = "UDP"; ports[0] = port;
                        container["ports"] = ports; containers[0] = container; spec["containers"] = containers; item["spec"] = spec;
                    }
                    if (scenario != "no_pod") result["items"] = QJsonArray{item};
                    if (scenario == "service_reject_list_metadata") result["metadata"] = "invalid";
                    if (scenario == "service_reject_list_items") result["items"] = QJsonObject{};
                    if (scenario == "service_reject_continue_type" || scenario == "service_reject_continue_repeat"
                        || (scenario == "service_pagination" && targetLists == 1)) {
                        result["items"] = QJsonArray{};
                        result["metadata"] = QJsonObject{{"continue", scenario == "service_reject_continue_type" ? QJsonValue(7) : QJsonValue("page-two")}};
                    }
                    if (scenario == "service_pagination" && targetLists == 2)
                        safe = safe && QUrlQuery(url).queryItemValue("continue") == "page-two";
                } else if (path == podPath || path == servicePath) {
                    if (changed && path == servicePath) dispatchOrder.append("detail");
                    result = path == podPath ? pod : svc;
                    if (changed && path == servicePath) {
                        auto metadata = result["metadata"].toObject();
                        if (scenario == "service_reject_deleted") metadata["deletionTimestamp"] = "2026-10-06T09:00:00Z";
                        if (scenario == "service_reject_uid") metadata.remove("uid");
                        if (scenario == "service_reject_namespace") metadata["namespace"] = "other";
                        result["metadata"] = metadata;
                        auto spec = result["spec"].toObject();
                        if (scenario == "service_reject_selector_key") spec["selector"] = QJsonObject{{"bad key", "echo"}};
                        if (scenario == "service_reject_selector_value") spec["selector"] = QJsonObject{{"app", "bad=value"}};
                        if (scenario == "service_reject_selector_type") spec["selector"] = QJsonObject{{"app", 7}};
                        auto ports = spec["ports"].toArray(); auto port = ports.first().toObject();
                        const QMap<QString, QJsonValue> targets{{"service_reject_target_zero", 0}, {"service_reject_target_high", 65536},
                            {"service_reject_target_fraction", 80.5}, {"service_reject_target_boolean", true},
                            {"service_reject_target_null", QJsonValue::Null}, {"service_reject_target_name", "missing-http"}};
                        if (targets.contains(scenario)) port["targetPort"] = targets.value(scenario);
                        if (scenario == "service_port_default") port.remove("targetPort");
                        ports[0] = port; spec["ports"] = scenario == "service_reject_no_ports" ? QJsonArray{} : ports;
                        result["spec"] = spec;
                    }
                    if (scenario == "pod_get_forbidden" && path == podPath) { http = 403; result = {{"kind", "Status"}, {"code", 403}}; }
                    if (changed && scenario == "recreated") { auto metadata = result["metadata"].toObject(); metadata["uid"] = "replacement-uid"; result["metadata"] = metadata; }
                    if (changed && (scenario == "auth" || scenario == "forbidden" || scenario == "rate" || scenario == "redirect" || scenario == "malformed")) {
                        http = scenario == "auth" ? 401 : scenario == "forbidden" ? 403 : scenario == "rate" ? 429 : scenario == "redirect" ? 302 : 200;
                        result = scenario == "malformed" ? QJsonObject{{"kind", "NotAPod"}} : QJsonObject{{"kind", "Status"}, {"code", http}};
                    }
                } else { http = 404; result = {{"kind", "Status"}, {"code", 404}}; }
                const auto body = QJsonDocument(result).toJson(QJsonDocument::Compact);
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(http) + " Test\r\nContent-Type: application/json\r\nRetry-After: 1\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QObject::connect(&streaming, &QWebSocketServer::newConnection, &streaming, [&] {
        while (auto* socket = streaming.nextPendingConnection()) {
            const int port = QUrlQuery(socket->requestUrl()).queryItemValue("ports").toInt();
            safe = safe && port == (scenario == "service_port_default" ? 80 : 8080) && socket->requestUrl().path() == podPath + "/portforward";
            const int headerPort = scenario == "wrong_port" ? 8081 : port;
            QByteArray number; number.append(char(headerPort & 255)); number.append(char(headerPort >> 8));
            if (scenario == "empty_frame") socket->sendBinaryMessage({});
            else if (scenario == "short_header") socket->sendBinaryMessage(QByteArray(1, char(0)) + number.first(1));
            else {
                if (scenario == "headers_reversed") socket->sendBinaryMessage(QByteArray(1, char(1)) + number);
                socket->sendBinaryMessage(QByteArray(1, char(0)) + number + (scenario == "header_payload" ? QByteArray("boundary-prefix") : QByteArray{}));
                if (scenario != "headers_reversed") socket->sendBinaryMessage(QByteArray(1, char(1)) + number);
                if (scenario == "empty_payload") socket->sendBinaryMessage(QByteArray(1, char(0)));
            }
            if (scenario == "channel") socket->sendBinaryMessage(QByteArray(1, char(2)) + "bad");
            if (scenario == "text") socket->sendTextMessage("not-binary");
            if (scenario == "remote_error") socket->sendBinaryMessage(QByteArray(1, char(1)) + "boundary-private-value");
            if (scenario == "oversized") socket->sendBinaryMessage(QByteArray(1, char(0)) + QByteArray(1024 * 1024 + 1, 's'));
            const auto aggregate = std::make_shared<QByteArray>();
            QObject::connect(socket, &QWebSocket::binaryMessageReceived, socket, [&, socket, aggregate](const QByteArray& message) {
                safe = safe && !message.isEmpty() && message[0] == char(0);
                if (scenario == "large_frame") {
                    aggregate->append(message.sliced(1));
                    if (aggregate->size() == 256 * 1024) { socket->sendBinaryMessage(QByteArray(1, char(0)) + *aggregate); aggregate->clear(); }
                } else socket->sendBinaryMessage(message);
            });
            QObject::connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto path = profile.path() + "/source.yaml";
    if (!real) {
        QFile config(path); if (!config.open(QIODevice::WriteOnly)) return false;
        config.write(QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: local-forward-token\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n- name: independent\n  context:\n    cluster: local\n    user: local\ncurrent-context: local\n").arg(api.serverPort()).toUtf8());
    }
    const auto imported = podlord::KubeconfigStore(profile.path()).importFile(real ? configPath : path);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported);
    if (!source || source->contexts.isEmpty()) return false;
    if (real) std::fprintf(stderr,"TLS runtime: backend=%s available=%s supports=%d build=%s loaded=%s\n",
        qPrintable(QSslSocket::activeBackend()),qPrintable(QSslSocket::availableBackends().join(',')),QSslSocket::supportsSsl(),
        qPrintable(QSslSocket::sslLibraryBuildVersionString()),qPrintable(QSslSocket::sslLibraryVersionString()));
    podlord::Workspace workspace(profile.path());
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return !workspace.busy(); }) || !workspace.openContext(source->contexts.first().id)) {
        std::fprintf(stderr,"Forward setup could not open its context: %s\n",qPrintable(workspace.error())); return false;
    }
    if (!waitFor([&] { return !workspace.busy() && workspace.table()->rowCount() >= 2 && !workspace.loading(); }, real ? 120000 : 15000)) {
        std::fprintf(stderr,"Forward setup resource cache did not settle: rows=%d, %s / %s\n",workspace.table()->rowCount(),qPrintable(workspace.error()),qPrintable(workspace.status())); return false;
    }
    if (!workspace.inspectPath(selected) || !waitFor([&] { return (scenario == "pod_get_forbidden" || workspace.canEditYaml()) && !workspace.loading(); }, real ? 60000 : 15000)) {
        std::fprintf(stderr,"Forward setup did not obtain its fresh target: %s / %s\n",qPrintable(workspace.error()),qPrintable(workspace.inspectorStatus())); return false;
    }
    const auto named = [&](const char* name) -> QQuickItem* {
        const auto parts = QString::fromLatin1(name).split('_');
        if ((parts.size() == 3 && parts[0] == "portCell") || (parts.size() == 2 && parts[0] == "portHeader")) {
            auto* view = podlord::test::visibleItem(window->contentItem(), parts.size() == 3 ? "portTable" : "portHeaderView");
            if (!view) return nullptr;
            QQmlExpression lookup(qmlContext(view), view, parts.size() == 3
                ? QString("itemAtIndex(model.index(%1, %2))").arg(parts[1], parts[2])
                : QString("(function() { for (let column = 0; column < columns; ++column) { const header = itemAtCell(Qt.point(column, 0)); if (header && header.objectName === '%1') return header; } return null; })()").arg(QString::fromLatin1(name)));
            auto* result = lookup.evaluate().value<QQuickItem*>();
            if (lookup.hasError()) std::fprintf(stderr, "Ports delegate lookup: %s\n", qPrintable(lookup.error().toString()));
            return result;
        }
        if (auto* visible = podlord::test::visibleItem(window->contentItem(), QString::fromLatin1(name))) return visible;
        QList<QQuickItem*> pending{window->contentItem()};
        while (!pending.isEmpty()) { auto* item = pending.takeLast(); if (item->objectName() == QLatin1String(name)) return item; pending.append(item->childItems()); }
        return nullptr;
    };
    const auto click = [&](const char* name) {
        QSignalSpy frame(window, &QQuickWindow::frameSwapped); window->update();
        if (!frame.wait(1000)) return false;
        auto* item = named(name); if (!item || !item->isVisible() || !item->isEnabled()) return false;
        QList<QQuickItem*> parents; for (auto* next = item; next; next = next->parentItem()) parents.prepend(next);
        for (auto* next : parents) next->ensurePolished();
        QCoreApplication::processEvents();
        if (!podlord::test::scrollIntoView(window,item)) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        QCoreApplication::processEvents(); return true;
    };
    if (!named("preparePortForward")) { std::fputs("Missing native Inspector port-forward action.\n", stderr); return false; }
    if (scenario == "terminal") return !named("preparePortForward")->isEnabled() && upgrades == 0;
    QTcpServer reserve;
    if (!reserve.listen(QHostAddress::LocalHost)) return false;
    const auto localPort = reserve.serverPort();
    if (scenario != "busy") reserve.close();
    if (!click("preparePortForward")) { std::fputs("Forward prepare control could not be clicked.\n",stderr); return false; }
    auto* local = named("portForwardLocal"); auto* remote = named("portForwardRemote");
    if (!local || !remote) { std::fputs("Forward port controls are missing.\n",stderr); return false; }
    QString localText = QString::number(localPort);
    if (scenario == "invalid") localText = "65536";
    if (scenario == "invalid_empty") localText.clear();
    if (scenario == "invalid_zero") localText = "0";
    if (scenario == "invalid_negative") localText = "-1";
    if (scenario == "invalid_fraction") localText = "8080.5";
    if (scenario == "invalid_text") localText = "port";
    local->setProperty("text", localText);
    remote->setProperty("text", scenario == "invalid_remote" ? "0" : scenario == "service_reject_undeclared_port" ? "81" : service ? "80" : "8080");
    changed = true; const auto before = requests;
    if (scenario == "cancel") {
        QTest::keyClick(window, Qt::Key_Escape);
        return workspace.portForwardTarget().isEmpty() && workspace.portForwards().isEmpty() && requests == before && upgrades == 0;
    }
    if (scenario == "selection_changed") {
        if (!workspace.inspectPath(servicePath) || !waitFor([&] { return !workspace.loading(); })) return false;
        const auto afterSelection = requests;
        if (!click("startPortForward")) return false;
        return workspace.portForwards().isEmpty() && requests == afterSelection && !workspace.error().isEmpty();
    }
    if (!click("startPortForward")) { std::fputs("Forward start control could not be clicked.\n",stderr); return false; }
    if (scenario.startsWith("invalid") || scenario == "busy") return requests == before && upgrades == 0 && !workspace.error().isEmpty();
    if (scenario == "selector" || scenario == "no_pod" || scenario == "udp" || scenario == "auth" || scenario == "forbidden" || scenario == "rate" || scenario == "redirect" || scenario == "malformed" || scenario == "recreated"
        || scenario == "service_wrong_list" || scenario == "service_wrong_item" || scenario.startsWith("service_reject_")) {
        if (!waitFor([&] { return !workspace.loading() && !workspace.error().isEmpty(); })) return false;
        QTcpServer reusable;
        return workspace.portForwards().isEmpty() && reusable.listen(QHostAddress::LocalHost, localPort) && upgrades == 0 && safe;
    }
    if (!waitFor([&] { return workspace.property("portForwards").toList().size() == 1 && !workspace.loading(); }, real ? 60000 : 15000)) {
        std::fprintf(stderr,"Forward transport did not become active: %s / %s\n",qPrintable(workspace.error()),qPrintable(workspace.portForwardError())); return false;
    }
    if (scenario == "switch") {
        if (!workspace.openContext(source->contexts.last().id) || !waitFor([&] { return !workspace.busy(); }) || !workspace.property("portForwards").toList().isEmpty()) return false;
    }
    const auto failed = [&](const char* stage) {
        std::fprintf(stderr,"Forward stage failed: %s; scenario=%s busy=%d loading=%d forwards=%lld error=%s transport=%s\n",
            stage,qPrintable(scenario),workspace.busy(),workspace.loading(),static_cast<long long>(workspace.portForwards().size()),
            qPrintable(workspace.error()),qPrintable(workspace.portForwardError()));
        const auto evidence=qEnvironmentVariable("PODLORD_FORWARD_FRAME");
        if (!evidence.isEmpty()) window->grabWindow().save(evidence+".failure.png","PNG");
        return false;
    };
    QTcpSocket client; client.connectToHost(QHostAddress::LocalHost, localPort);
    if (!waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; })) return failed("local connection");
    const QByteArray message = real ? QByteArray("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        : scenario == "bulk" ? QByteArray(2 * 1024 * 1024, 'b')
        : scenario == "large_frame" ? QByteArray(256 * 1024, 'l') : QByteArray::fromHex("00017f80ff010203706f646c6f7264");
    client.write(message);
    QByteArray received;
    if (scenario == "priority") {
        if (!waitFor([&] { return heldUpgrade && upgrades == 1; })) return false;
        QTcpSocket next; next.connectToHost(QHostAddress::LocalHost, localPort);
        if (!waitFor([&] { return next.state() == QAbstractSocket::ConnectedState; })) return false;
        next.write(message); QTest::qWait(50);
        if (!workspace.inspectPath(servicePath)) return false;
        heldUpgrade->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        heldUpgrade->disconnectFromHost();
        QByteArray data;
        if (!waitFor([&] { data += next.readAll(); return data == message && workspace.canEditYaml() && !workspace.loading(); })) return false;
        const bool inspectorFirst = dispatchOrder == QStringList{"upgrade", "detail", "upgrade"};
        if (!inspectorFirst) std::fprintf(stderr, "Inspector/forward dispatch order: %s\n", qPrintable(dispatchOrder.join(',')));
        const bool closed = workspace.close(workspace.currentSession()) && waitFor([&] { return !workspace.busy(); });
        QTcpServer reusable;
        return inspectorFirst && closed && safe && reusable.listen(QHostAddress::LocalHost, localPort);
    }
    if (scenario == "queued_close") {
        if (!waitFor([&] { return upgrades == 1; })) return false;
        QTcpSocket unsent; unsent.connectToHost(QHostAddress::LocalHost, localPort);
        if (!waitFor([&] { return unsent.state() == QAbstractSocket::ConnectedState; })) return false;
        QTest::qWait(50); unsent.abort(); QTest::qWait(50);
        if (!workspace.close(workspace.currentSession()) || !waitFor([&] { return !workspace.busy(); })) return false;
        QTcpServer reusable;
        return upgrades == 1 && workspace.portForwards().isEmpty() && reusable.listen(QHostAddress::LocalHost, localPort);
    }
    if (scenario == "protocol" || scenario == "wrong_port" || scenario == "channel" || scenario == "text" || scenario == "remote_error" || scenario == "oversized" || scenario == "empty_frame" || scenario == "short_header") {
        if (!waitFor([&] { return client.state() == QAbstractSocket::UnconnectedState; })) {
            std::fprintf(stderr, "Rejected stream still open: state=%d bytes=%lld error=%s\n", static_cast<int>(client.state()), static_cast<long long>(client.bytesAvailable()), qPrintable(workspace.error())); return false;
        }
        const auto data = client.readAll();
        const bool passed = (data.isEmpty() || (scenario == "oversized" && message.startsWith(data))) && !workspace.error().contains("boundary-private-value")
            && (scenario != "oversized" || !workspace.error().isEmpty()) && safe;
        if (!passed) std::fprintf(stderr, "Rejected stream result: bytes=%lld error=%s safe=%d\n", static_cast<long long>(data.size()), qPrintable(workspace.error()), safe);
        return passed;
    }
    const auto expected = scenario == "header_payload" ? QByteArray("boundary-prefix") + message : message;
    if (!waitFor([&] { received += client.readAll(); return real ? received.contains("podlord-native-forward") : received == expected; }, real ? 60000 : 15000)) {
        std::fprintf(stderr, "Forward data failure: bytes=%lld upgrades=%d error=%s\n", static_cast<long long>(received.size()), upgrades, qPrintable(workspace.error())); return false;
    }
    if (scenario == "service_pagination" && targetLists != 2) return false;
    if (scenario == "switch") return safe && received == message;
    if (scenario == "concurrent") {
        QTcpSocket other; other.connectToHost(QHostAddress::LocalHost, localPort);
        if (!waitFor([&] { return other.state() == QAbstractSocket::ConnectedState; })) return false;
        const QByteArray second = "independent-second-connection"; other.write(second);
        QByteArray bytes;
        if (!waitFor([&] { bytes += other.readAll(); return bytes == second; }) || upgrades != 2) return false;
    }
    if (scenario == "close_other") {
        const auto first = workspace.currentSession();
        if (!workspace.openContext(source->contexts.last().id) || !waitFor([&] { return !workspace.busy() && workspace.currentSession() != first && workspace.table()->rowCount() == 2 && !workspace.loading(); })
            || !workspace.inspectPath(selected) || !waitFor([&] { return !workspace.loading(); }) || !click("preparePortForward")) return false;
        QTcpServer temporary; if (!temporary.listen(QHostAddress::LocalHost)) return false;
        const int secondPort = temporary.serverPort(); temporary.close();
        named("portForwardLocal")->setProperty("text", QString::number(secondPort)); named("portForwardRemote")->setProperty("text", "8080");
        if (!click("startPortForward") || !waitFor([&] { return workspace.portForwards().size() == 1 && !workspace.loading(); })) return false;
        QTcpSocket other; other.connectToHost(QHostAddress::LocalHost, secondPort);
        if (!waitFor([&] { return other.state() == QAbstractSocket::ConnectedState; })) return false;
        other.write(message); QByteArray bytes;
        if (!waitFor([&] { bytes += other.readAll(); return bytes == message; })) return false;
        if (!workspace.close(workspace.currentSession()) || !waitFor([&] { return !workspace.busy(); })) return false;
        QTcpServer reusable; if (!reusable.listen(QHostAddress::LocalHost, secondPort)) return false;
        const QByteArray continuation = "first-session-survives"; client.write(continuation); QByteArray ongoing;
        if (!waitFor([&] { ongoing += client.readAll(); return ongoing == continuation; })) return false;
        if (!workspace.activate(first) || !waitFor([&] { return !workspace.busy(); })) return false;
        const bool passed = workspace.currentSession() == first && workspace.portForwards().size() == 1 && safe;
        if (!passed) std::fprintf(stderr, "Forward session isolation: current=%s expected=%s forwards=%lld safe=%d\n", qPrintable(workspace.currentSession()), qPrintable(first), static_cast<long long>(workspace.portForwards().size()), safe);
        return passed;
    }
    if (scenario.startsWith("ports_")) {
        if (scenario == "ports_table_sort" || scenario == "ports_table_find" || scenario == "ports_table_view_restart") {
            if (!workspace.inspectPath(servicePath) || !waitFor([&] { return !workspace.loading(); }) || !click("preparePortForward")) return false;
            QTcpServer reserveSecond; if (!reserveSecond.listen(QHostAddress::LocalHost)) return false;
            const auto secondPort = reserveSecond.serverPort(); reserveSecond.close();
            named("portForwardLocal")->setProperty("text", QString::number(secondPort)); named("portForwardRemote")->setProperty("text", "80");
            if (!click("startPortForward") || !waitFor([&] { return workspace.portForwards().size() == 2 && !workspace.loading(); })) return false;
        }
        auto calls=requests;
        const auto openedStreams = upgrades;
        const auto token=workspace.portForwards().first().toMap()["id"].toString();
        const auto endpoint=workspace.portForwards().first().toMap()["endpoint"].toString();
        if (!click("portForwardTasksButton") || !waitFor([&] { return named("portCell_0_0") && named("portCell_0_0")->isVisible(); })) {
            std::fputs("Ports table did not render its first endpoint.\n", stderr); return false;
        }
        if (scenario.startsWith("ports_table_")) {
            auto* table = workspace.property("portTable").value<QAbstractItemModel*>();
            if (!table || table->rowCount() < 1 || table->columnCount() != 7) return false;
            const auto actualToken = table->data(table->index(0, 0), Qt::UserRole).toString();
            if (scenario == "ports_table_view_restart") {
                if (!click("workspaceSearchButton")) return false;
                if (!click("portHeader_4")) return false;
                auto* search = named("portFilter"); if (!search) return false;
                search->forceActiveFocus(); for (const auto ch : QByteArray("echo-service")) QTest::keyClick(window, ch);
                bool reloaded = false;
                if (!waitFor([&] { return table->rowCount() == 1; }) || !waitFor([&] { return reloaded || (reloaded = workspace.reloadSavedViews()); })
                    || !waitFor([&] { return !workspace.busy(); })) return false;
                podlord::Workspace restored(profile.path());
                const bool passed = waitFor([&] { return !restored.busy() && restored.currentSession() == workspace.currentSession(); })
                    && restored.portFilterText() == "echo-service" && restored.portSortColumnIndex() == 4 && restored.portForwards().isEmpty();
                if (!passed) std::fprintf(stderr, "Ports restart: filter=%s column=%d session=%s/%s error=%s failed=%d original=%s/%d\n",
                    qPrintable(restored.portFilterText()), restored.portSortColumnIndex(), qPrintable(restored.currentSession()),
                    qPrintable(workspace.currentSession()), qPrintable(restored.error()), restored.viewStateFailed(),
                    qPrintable(workspace.portFilterText()), workspace.portSortColumnIndex());
                return passed;
            }
            if (scenario == "ports_table_find") {
                const auto fail = [&](const char* stage) {
                    std::fprintf(stderr, "Ports Find %s: count=%s rows=%d requests=%d/%d upgrades=%d inspector=%s\n", stage,
                        qPrintable(named("portFindCount") ? named("portFindCount")->property("text").toString() : QString("missing")),
                        table->rowCount(), requests, calls, upgrades, qPrintable(workspace.inspectorPath()));
                    return false;
                };
                if (!workspace.closeInspector() || !click("portFindButton")) return fail("open");
                auto* search = named("portFindInput");
                if (!search) return fail("input");
                search->forceActiveFocus();
                for (const auto ch : QByteArray("echo")) QTest::keyClick(window, ch);
                if (!waitFor([&] { return named("portFindCount") && named("portFindCount")->property("text").toString() == "1/2"; })) return fail("matches");
                if (!click("portFindNext") || named("portFindCount")->property("text").toString() != "2/2") return fail("next");
                if (!click("portFindNext") || named("portFindCount")->property("text").toString() != "1/2") return fail("wrap");
                if (!click("portFindPrevious") || named("portFindCount")->property("text").toString() != "2/2") return fail("previous");
                return (table->rowCount() == 2 && workspace.portFilterText().isEmpty()
                    && workspace.inspectorPath().isEmpty() && requests == calls && upgrades == openedStreams) || fail("cache-only");
            }
            const auto frame = qEnvironmentVariable("PODLORD_PORTS_TABLE_FRAME");
            if (!frame.isEmpty() && (!podlord::test::scrollIntoView(window, named("portCell_0_0")) || !window->grabWindow().save(frame))) return false;
            const auto menu = [&](const char* action) {
                auto* cell = named("portCell_0_0"); if (!cell || !podlord::test::scrollIntoView(window, cell)) return false;
                QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, cell->mapToScene({cell->width()/2,cell->height()/2}).toPoint());
                return waitFor([&] { return named(action) && named(action)->isVisible(); }) && click(action);
            };
            if (scenario == "ports_table_invalid") {
                QGuiApplication::clipboard()->setText("retained");
                return !workspace.copyPortValue("not-an-active-token", 0) && !workspace.copyPortValue(actualToken, -1)
                    && !workspace.copyPortValue(actualToken, 7) && QGuiApplication::clipboard()->text() == "retained" && requests == calls;
            }
            if (scenario == "ports_table_context_open") {
                BrowserBoundary browser;
                return menu("portContextOpenHttp") && browser.urls == QList<QUrl>{QUrl(QString("http://127.0.0.1:%1/").arg(localPort))} && requests == calls;
            }
            if (scenario == "ports_table_context_stop") {
                if (!menu("portContextStop") || !waitFor([&] { return workspace.portForwards().isEmpty() && table->rowCount() == 0; })) return false;
                QTcpServer reusable; return reusable.listen(QHostAddress::LocalHost, localPort) && requests == calls;
            }
            if (scenario == "ports_table_context_inspect") return workspace.closeInspector() && menu("portMenuInspector")
                && waitFor([&] { return workspace.inspectorPath() == selected; }) && requests == calls;
            if (scenario == "ports_table_keyboard_copy") {
                auto* cell = named("portCell_0_0"); if (!cell || !podlord::test::scrollIntoView(window, cell)) return false;
                cell->forceActiveFocus(); QTest::keySequence(window, QKeySequence::Copy);
                return QGuiApplication::clipboard()->text() == endpoint && requests == calls;
            }
            if (scenario == "ports_table_hover") {
                auto layout = workspace.property("portColumns").toList(); auto first = layout.first().toMap(); first["width"] = 40; layout[0] = first;
                if (!workspace.saveTableLayout("port", layout) || !waitFor([&] { return !workspace.tableLayoutSaving(); }) || !workspace.tableLayoutError().isEmpty()) return false;
                auto* cell = named("portCell_0_0"); if (!cell || !podlord::test::scrollIntoView(window, cell)) return false;
                QTest::mouseMove(window, cell->mapToScene({cell->width()/2,cell->height()/2}).toPoint());
                return waitFor([&] { const auto* tip = window->findChild<QObject*>("portValueTooltip"); return tip && tip->property("visible").toBool() && tip->property("text").toString() == endpoint; }) && requests == calls;
            }
            if (scenario == "ports_table_filter_restore") {
                if (!click("workspaceSearchButton")) return false;
                const auto firstSession = workspace.currentSession(); auto* search = named("portFilter"); if (!search) return false;
                search->forceActiveFocus(); for (const auto ch : QByteArray("no-such-forward")) QTest::keyClick(window,ch);
                if (!waitFor([&] { return table->rowCount() == 0; }) || !workspace.openContext(source->contexts.last().id)
                    || !waitFor([&] { return !workspace.busy() && !workspace.loading(); }) || !workspace.portFilterText().isEmpty() || !workspace.portForwards().isEmpty()) return false;
                return workspace.activate(firstSession) && waitFor([&] { return !workspace.busy() && !workspace.loading(); })
                    && workspace.portFilterText() == "no-such-forward" && table->rowCount() == 0 && workspace.portForwards().size() == 1;
            }
            if (scenario == "ports_table_sort") {
                if (!click("portHeader_4") || table->data(table->index(0, 4)).toString() != "80") return false;
                if (!click("portHeader_4") || table->data(table->index(0, 4)).toString() != "8080") return false;
                return click("portHeader_4") && workspace.property("portSortColumnIndex").toInt() == -1 && requests == calls;
            }
            if (scenario == "ports_table_copy") {
                auto* cell = named("portCell_0_0"); if (!cell || !podlord::test::scrollIntoView(window, cell)) return false;
                QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, cell->mapToScene({cell->width()/2, cell->height()/2}).toPoint());
                return waitFor([&] { return named("portMenuCopy") && named("portMenuCopy")->isVisible(); }) && click("portMenuCopy") && QGuiApplication::clipboard()->text() == endpoint && requests == calls;
            }
            if (scenario == "ports_table_keyboard") {
                if (!workspace.closeInspector()) return false;
                auto* cell = named("portCell_0_0"); if (!cell || !podlord::test::scrollIntoView(window, cell)) return false;
                cell->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return);
                return waitFor([&] { return workspace.inspectorPath() == selected; }) && requests == calls;
            }
            if (scenario == "ports_table_color") return table->data(table->index(0, 2), Qt::ForegroundRole).value<QColor>().alpha() > 0
                && table->data(table->index(0, 3), Qt::ForegroundRole).value<QColor>().alpha() > 0 && requests == calls;
            if (scenario == "ports_table_defaults") return waitFor([&] {
                const auto* header = named("portHeader_6"); const auto* viewport = named("portTable");
                return header && header->isVisible() && viewport && header->mapToScene({header->width(),0}).x() <= viewport->mapToScene({viewport->width(),0}).x();
            }, 1000) && requests == calls;
            if (scenario == "ports_table_narrow") {
                window->setWidth(360);
                if (!waitFor([&] { auto* grid = named("portTable"); return grid && grid->width() > 0 && grid->mapToScene({grid->width(),0}).x() <= window->width(); })) return false;
                return click("portColumnsButton") && waitFor([&] { return named("portColumnVisible_endpoint") && named("portColumnVisible_endpoint")->isVisible(); }) && requests == calls;
            }
            const auto beforeResource = workspace.resourceColumns();
            if (!click("portColumnsButton") || !waitFor([&] { return named("portColumnVisible_kind") && named("portColumnVisible_kind")->isVisible(); })) return false;
            QLockFile lock(profile.filePath("table-layouts.json.lock"));
            if (scenario == "ports_table_busy" && !lock.tryLock(0)) return false;
            if (scenario == "ports_table_conflict") {
                podlord::Workspace other(profile.path());
                if (!waitFor([&] { return !other.busy() && !other.loading(); })) return false;
                auto layout = other.defaultTableColumns("port"); auto first = layout.first().toMap(); first["width"] = 333; layout[0] = first;
                if (!other.saveTableLayout("port", layout) || !waitFor([&] { return !other.tableLayoutSaving(); }) || !other.tableLayoutError().isEmpty()) return false;
                calls = requests;
            }
            if (scenario == "ports_table_hide" && !click("portColumnVisible_kind")) return false;
            if (scenario == "ports_table_pin" && !click("portColumnPinned_namespace")) return false;
            if (scenario == "ports_table_order" && !click("portColumnEarlier_namespace")) return false;
            if (scenario == "ports_table_width" || scenario == "ports_table_restore" || scenario == "ports_table_busy" || scenario == "ports_table_conflict") {
                auto* input = named("portColumnWidth_endpoint"); if (!input || !podlord::test::scrollIntoView(window, input)) return false;
                input->forceActiveFocus(); QTest::keySequence(window,QKeySequence::SelectAll);
                for (const auto ch : QByteArray("222")) QTest::keyClick(window,ch);
                QTest::keyClick(window,Qt::Key_Tab);
            }
            if (!click("portSaveColumns") || !waitFor([&] { return !workspace.tableLayoutSaving(); })) return false;
            if (scenario == "ports_table_busy" || scenario == "ports_table_conflict") {
                const bool passed = !workspace.tableLayoutError().isEmpty() && named("portColumnWidth_endpoint")->property("text").toString() == "222"
                    && workspace.resourceColumns() == beforeResource && requests == calls;
                if (!passed) std::fprintf(stderr,"Rejected Ports layout: error=%s draft=%s resourceRetained=%d requests=%d/%d\n",qPrintable(workspace.tableLayoutError()),qPrintable(named("portColumnWidth_endpoint")->property("text").toString()),workspace.resourceColumns()==beforeResource,requests,calls);
                return passed;
            }
            if (!workspace.tableLayoutError().isEmpty()) return false;
            const auto columns = workspace.property("portColumns").toList();
            const auto column = [&](const QString& id) { for (const auto& value : columns) if (value.toMap()["id"] == id) return value.toMap(); return QVariantMap{}; };
            if (scenario == "ports_table_hide") return !column("kind")["visible"].toBool() && workspace.resourceColumns() == beforeResource && requests == calls;
            if (scenario == "ports_table_pin") return column("namespace")["pinned"].toBool() && waitFor([&] { return named("pinnedCell_0_3") && named("pinnedCell_0_3")->isVisible(); }) && requests == calls;
            if (scenario == "ports_table_order") return columns[2].toMap()["id"] == "namespace" && requests == calls;
            if (scenario == "ports_table_width") return column("endpoint")["width"].toInt() == 222 && requests == calls;
            if (scenario == "ports_table_restore") {
                podlord::Workspace restored(profile.path());
                return waitFor([&] { return !restored.busy(); }) && restored.property("portColumns").toList() == columns
                    && restored.portForwards().isEmpty() && workspace.resourceColumns() == beforeResource;
            }
            std::fprintf(stderr,"Unsupported table scenario %s for %s\n",qPrintable(scenario),qPrintable(actualToken)); return false;
        }
        if (!click("portCell_0_0")) return false;
        if (scenario.startsWith("ports_open_")) {
            BrowserBoundary browser;
            const auto open = [&](const QString& id, bool secure) {
                bool accepted = false;
                return QMetaObject::invokeMethod(&workspace, "openPortForwardEndpoint", Qt::DirectConnection, Q_RETURN_ARG(bool, accepted), Q_ARG(QString, id), Q_ARG(bool, secure)) && accepted;
            };
            if (scenario == "ports_open_invalid") return !open("https://untrusted.invalid", false) && browser.urls.isEmpty() && requests == calls;
            if (scenario == "ports_open_other_session") {
                if (!workspace.openContext(source->contexts.last().id) || !waitFor([&] { return !workspace.busy() && !workspace.loading(); })) return false;
                const auto nextCalls = requests;
                return !open(token, false) && browser.urls.isEmpty() && requests == nextCalls;
            }
            if (scenario == "ports_open_stopped") return workspace.stopPortForward(token) && !open(token, false) && browser.urls.isEmpty() && requests == calls;
            const bool secure = scenario == "ports_open_https";
            if (!click("openPortForwardTask") || !click(secure ? "openPortForwardHttps" : "openPortForwardHttp")) return false;
            if (scenario == "ports_open_repeat" && (!click("openPortForwardTask") || !click("openPortForwardHttp"))) return false;
            const auto expected = QUrl(QString(secure ? "https://127.0.0.1:%1/" : "http://127.0.0.1:%1/").arg(localPort));
            const int count = scenario == "ports_open_repeat" ? 2 : 1;
            return browser.urls.size() == count && std::all_of(browser.urls.cbegin(), browser.urls.cend(), [&](const auto& url) { return url == expected; })
                && workspace.portForwards().size() == 1 && requests == calls;
        }
        if (scenario=="ports_copy") return click("copyPortForwardTask") && QGuiApplication::clipboard()->text()==endpoint && requests==calls;
        if (scenario=="ports_invalid_copy") {
            QGuiApplication::clipboard()->setText("retained");
            return !workspace.copyPortForwardEndpoint("not-an-active-token") && QGuiApplication::clipboard()->text()=="retained" && requests==calls;
        }
        if (scenario=="ports_search") {
            if (!click("workspaceSearchButton")) return false;
            auto* search=named("portFilter"); search->forceActiveFocus();
            for (const auto ch : QStringLiteral("no-such-forward")) QTest::keyClick(window,ch.toLatin1());
            if (!waitFor([&] { return workspace.portTable()->rowCount()==0; })) return false;
            QTest::keyClick(window,Qt::Key_Escape);
            if (search->isVisible() || workspace.portFilterText() != "no-such-forward"
                || !click("workspaceSearchButton") || !waitFor([&] { return search->hasActiveFocus(); })) return false;
            QTest::keySequence(window,QKeySequence::SelectAll); QTest::keyClick(window,Qt::Key_Backspace);
            return waitFor([&] { return workspace.portTable()->rowCount()==1; }) && workspace.portForwards().size()==1 && requests==calls;
        }
        if (scenario=="ports_inspect") return workspace.closeInspector() && click("portCell_0_0") && waitFor([&] { return workspace.inspectorPath()==selected; });
        if (scenario=="ports_stop") {
            if (!click("stopPortForwardTask") || !waitFor([&] { return workspace.portForwards().isEmpty() && workspace.portTable()->rowCount() == 0; })) return false;
            QTcpServer reusable; return reusable.listen(QHostAddress::LocalHost,localPort);
        }
        return false;
    }
    const auto frame = qEnvironmentVariable("PODLORD_FORWARD_FRAME");
    if (!frame.isEmpty()) { if (!click("portForwardTasksButton")) return failed("open Ports for evidence"); QTest::qWait(120); if (!window->grabWindow().save(frame)) return failed("capture Ports"); QTest::keyClick(window, Qt::Key_Escape); }
    client.abort();
    if (scenario == "real_context_removal" || scenario == "real_context_draft" || scenario == "context_removal") {
        if (scenario == "real_context_draft") {
            if (!workspace.beginYamlEdit() || !workspace.setYamlDraft(workspace.yamlText() + "\n# Unapplied user change\n")
                || workspace.requestSourceRemoval(source->contexts.first().id) || workspace.sourceImportError().isEmpty()
                || !workspace.sourceRemoval().isEmpty() || workspace.portForwards().isEmpty()) return false;
            if (workspace.leaveYamlEdit() || !workspace.discardPending() || !workspace.confirmDiscard(true)) return false;
        }
        if (!click("settingsWorkspaceButton") || !podlord::test::selectSettingsSection(window, "sources")) {
            std::fprintf(stderr, "Forward owner removal could not open Sources.\n"); return false;
        }
        const auto removal = "removeSource_" + source->contexts.first().id;
        if (!waitFor([&] { return podlord::test::visibleItem(window->contentItem(), removal); })
            || !podlord::test::clickVisible(window, removal)) {
            std::fprintf(stderr, "Forward owner removal control is unavailable: %s\n", qPrintable(removal)); return false;
        }
        const auto confirmationFrame = qEnvironmentVariable("PODLORD_SOURCE_REMOVAL_FRAME");
        if (!confirmationFrame.isEmpty()) {
            QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
            if (!frames.wait(1000) || !window->grabWindow().save(confirmationFrame)) return false;
        }
        if (!click("confirmSourceRemoval") || !waitFor([&] { return !workspace.busy(); })) return false;
        const auto saved = podlord::SessionStore(profile.path()).list();
        if (!std::holds_alternative<podlord::SessionCatalog>(saved)
            || std::any_of(std::get<podlord::SessionCatalog>(saved).sessions.cbegin(), std::get<podlord::SessionCatalog>(saved).sessions.cend(),
                [&](const auto& session) { return session.config.contextId == source->contexts.first().id; })
            || !std::holds_alternative<podlord::Failure>(podlord::KubeconfigStore(profile.path()).connection(source->contexts.first().id))) return false;
    } else if (scenario == "close") {
        if (!workspace.close(workspace.currentSession()) || !waitFor([&] { return !workspace.busy(); })) return false;
    } else {
        if (!click("preparePortForward") || named("startPortForward")->isVisible()) return failed("reopen existing forward controls");
        if (scenario == "repeat" && (workspace.startPreparedPortForward(QString::number(localPort), "8080") || workspace.portForwards().size() != 1)) return false;
        if (!click("stopPortForward")) return failed("stop existing forward");
    }
    QTcpServer reusable;
    if (!waitFor([&] { return workspace.property("portForwards").toList().isEmpty(); }) || !reusable.listen(QHostAddress::LocalHost, localPort) || !safe) return failed("release local listener");
    if (scenario == "reuse") {
        reusable.close();
        if (!click("preparePortForward")) return false;
        named("portForwardLocal")->setProperty("text", QString::number(localPort)); named("portForwardRemote")->setProperty("text", "8080");
        if (!click("startPortForward") || !waitFor([&] { return workspace.portForwards().size() == 1 && !workspace.loading(); })) return false;
        QTcpSocket again; again.connectToHost(QHostAddress::LocalHost, localPort);
        if (!waitFor([&] { return again.state() == QAbstractSocket::ConnectedState; })) return false;
        again.write(message); QByteArray bytes;
        if (!waitFor([&] { bytes += again.readAll(); return bytes == message; }) || !click("preparePortForward") || !click("stopPortForward")) return false;
        return reusable.listen(QHostAddress::LocalHost, localPort) && upgrades == 2 && safe;
    }
    return true;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); Q_INIT_RESOURCE(workspace_ui);
    const auto args = app.arguments();
    if (!run(args.value(1), args.value(2))) { std::fprintf(stderr, "Native port-forward public UI scenario failed: %s\n", qPrintable(args.value(1))); return 1; }
    std::printf("Native port-forward public UI scenario passed: %s\n", qPrintable(args.value(1))); return 0;
}

#include "port_forward_ui_test.moc"
