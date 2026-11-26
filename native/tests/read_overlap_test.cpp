#include "resource_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>

namespace {
bool waitFor(const std::function<bool()>& ready, int timeoutMs = 16000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeoutMs) QTest::qWait(10);
    return ready();
}
// Kubernetes is the only simulated boundary; cache and scheduler are the real client.
class Kubernetes final : public QTcpServer {
public:
    int active = 0, maximum = 0, requests = 0, podLists = 0, version = 1;
    QMap<QString, int> pathRequests;
    int allDelay = 0, listDelay = 0, detailDelay = 0, listStatus = 200;
    bool paginatePods = false;
    QByteArray expectedAuthorization;
    std::optional<QJsonObject> discovery;
    QJsonObject podStatus{{"phase", "Running"}};
    QString podNamespace = "default";
    QMap<QString, int> logs;
    QMap<QString, QJsonObject> responseBodies;
    QMap<QString, QByteArray> responseBytes;
    QMap<QString, int> responseStatuses;
    QByteArray retryAfter = "2";
    bool authorizationMismatch = false;
    Kubernetes() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection(); auto input = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, input] {
                    input->append(socket->readAll()); if (!input->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++requests; ++active; maximum = std::max(maximum, active);
                    if (!expectedAuthorization.isEmpty()) {
                        QByteArray authorization;
                        for (const auto& line : input->split('\n'))
                            if (line.toLower().startsWith("authorization:")) authorization = line.mid(14).trimmed();
                        authorizationMismatch = authorizationMismatch || authorization != expectedAuthorization;
                    }
                    const auto url = QUrl::fromEncoded(input->split(' ')[1]);
                    const auto path = url.path();
                    ++pathRequests[path];
                    const QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"}, {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", podNamespace}, {"uid", "uid-alpha"}, {"resourceVersion", QString::number(version)}}},
                        {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "alpha"}, {"image", "private.registry.example/team/app:v1"}, {"imagePullPolicy", "Always"}}, QJsonObject{{"name", "beta"}}}}}}, {"status", podStatus}};
                    QJsonObject body; int delay = allDelay, status = 200;
                    if (path == "/api") body = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") body = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") {
                        QJsonArray resources{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}};
                        for (const auto& name : {"configmaps", "secrets", "services", "endpoints", "serviceaccounts", "persistentvolumeclaims"})
                            resources.append(QJsonObject{{"name", name}, {"kind", QString(name)}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}});
                        body = discovery.value_or(QJsonObject{{"resources", resources}});
                    } else if (path == "/api/v1/pods" || path == "/api/v1/namespaces/default/pods"
                        || path == "/api/v1/namespaces/team/pods") {
                        ++podLists; delay = std::max(delay, listDelay); status = listStatus;
                        auto pagePod = pod;
                        const bool nextPage = paginatePods && QUrlQuery(url).queryItemValue("continue") == "next";
                        if (nextPage) {
                            auto metadata = pagePod["metadata"].toObject();
                            metadata["name"] = "beta"; metadata["uid"] = "uid-beta";
                            pagePod["metadata"] = metadata;
                        }
                        body = {{"metadata", paginatePods && !nextPage ? QJsonObject{{"continue", "next"}} : QJsonObject{}},
                                {"items", QJsonArray{pagePod}}};
                    }
                    else if (path == "/api/v1/namespaces/default/pods/alpha") { delay = std::max(delay, detailDelay); body = pod; }
                    else body = {{"metadata", QJsonObject{}}, {"items", QJsonArray{}}};
                    if (responseBodies.contains(path)) body = responseBodies.value(path);
                    status = responseStatuses.value(path, status);
                    auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
                    if (path.endsWith("/log")) {
                        const auto container = QUrlQuery(url).queryItemValue("container");
                        bytes = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + " " + container.toUtf8()
                            + " entry " + QByteArray::number(++logs[container]) + '\n';
                    }
                    if (responseBytes.contains(path)) bytes = responseBytes.value(path);
                    QTimer::singleShot(delay, socket, [this, socket, bytes, status] {
                        --active;
                        socket->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nContent-Type: application/json\r\nRetry-After: " + retryAfter + "\r\nConnection: close\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
                        socket->disconnectFromHost();
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};
bool providerScenario(const QString& scenario) {
    Kubernetes server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    const podlord::KubeconfigStore store(directory.filePath("profile"));
    QByteArray user = "    auth-provider:\n      name: oidc\n      config:\n";
    QByteArray expected = "private-access-token";
    if (scenario == "provider_id") { user += "        id-token: private-id-token\n"; expected = "private-id-token"; }
    else if (scenario == "provider_missing") user = "    auth-provider:\n      name: oidc\n";
    else if (scenario == "provider_empty") user += "        access-token: ''\n        id-token: ''\n";
    else if (scenario == "provider_bad_config") user = "    auth-provider:\n      name: oidc\n      config: []\n";
    else if (scenario == "provider_bad_token") user += "        access-token: 'private token'\n";
    else if (scenario == "provider_bad_token_type") user += "        access-token: []\n";
    else if (scenario == "provider_empty_access") { user += "        access-token: ''\n        id-token: private-id-token\n"; expected = "private-id-token"; }
    else if (scenario == "provider_null_access") { user += "        access-token: null\n        id-token: private-id-token\n"; expected = "private-id-token"; }
    else user += "        access-token: private-access-token\n        id-token: private-id-token\n";
    if (scenario == "provider_token_precedence") { user += "    token: private-static-token\n"; expected = "private-static-token"; }
    if (scenario == "provider_file_precedence") {
        QFile token(directory.filePath("token"));
        if (!token.open(QIODevice::WriteOnly) || token.write("private-file-token\n") != 19) return false;
        token.close(); user += "    tokenFile: token\n"; expected = "private-file-token";
    }
    if (scenario == "provider_mixed_exec") user += "    exec:\n      command: must-not-run\n      apiVersion: client.authentication.k8s.io/v1\n      interactiveMode: Never\n";
    if (scenario == "provider_impersonation_uid") user += "    as-uid: other-user\n";
    const auto yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:" + QByteArray::number(server.serverPort())
        + "\nusers:\n- name: local\n  user:\n" + user + "contexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n";
    const auto imported = store.importText(directory.filePath("config"), QString::fromUtf8(yaml));
    if (!std::holds_alternative<podlord::SourceSnapshot>(imported)) return false;
    const auto context = std::get<podlord::SourceSnapshot>(imported).contexts.first().id;
    const auto resolved = store.connection(context);
    if (scenario == "provider_missing" || scenario == "provider_empty" || scenario == "provider_impersonation_uid") {
        const auto* failure = std::get_if<podlord::Failure>(&resolved);
        return failure && failure->code == podlord::StoreError::UnsupportedVersion && server.requests == 0
            && (scenario != "provider_impersonation_uid" || failure->message.contains("impersonation"));
    }
    if (scenario == "provider_bad_config" || scenario == "provider_bad_token" || scenario == "provider_bad_token_type" || scenario == "provider_mixed_exec") {
        const auto* failure = std::get_if<podlord::Failure>(&resolved);
        return failure && failure->code == podlord::StoreError::InvalidData && !failure->message.contains("private") && server.requests == 0;
    }
    if (!std::holds_alternative<podlord::ClusterConnection>(resolved)) return false;
    auto connection = std::get<podlord::ClusterConnection>(resolved);
    if (connection.authorization != "Bearer " + expected || connection.exec || !connection.credentialReady || server.requests) return false;
    if (QJsonDocument(podlord::sourceJson(std::get<podlord::SourceSnapshot>(imported))).toJson().contains("private-")) return false;
    const auto repeated = store.connection(context);
    if (!std::holds_alternative<podlord::ClusterConnection>(repeated) || std::get<podlord::ClusterConnection>(repeated).authorization != connection.authorization) return false;
    if (scenario == "provider_rotate") {
        QByteArray changed = yaml; changed.replace("private-access-token", "private-new-token");
        const auto fresh = store.importText(directory.filePath("config"), QString::fromUtf8(changed));
        if (!std::holds_alternative<podlord::SourceSnapshot>(fresh)) return false;
        const auto newContext = std::get<podlord::SourceSnapshot>(fresh).contexts.first().id;
        if (newContext == context) return false;
        const auto oldConnection = store.connection(context), newConnection = store.connection(newContext);
        if (!std::holds_alternative<podlord::ClusterConnection>(oldConnection) || !std::holds_alternative<podlord::ClusterConnection>(newConnection)) return false;
        if (std::get<podlord::ClusterConnection>(oldConnection).authorization != "Bearer private-new-token") return false;
        connection = std::get<podlord::ClusterConnection>(newConnection); expected = "private-new-token";
    }
    server.expectedAuthorization = "Bearer " + expected;
    if (scenario == "provider_401") server.listStatus = 401;
    podlord::ResourceClient client;
    podlord::ReadSettings settings; settings.requestHardLimitPerMinute = 0;
    if (!client.configure(settings) || !client.open("session", connection, {})) return false;
    if (!waitFor([&] { return !client.loading("session") && !server.active; }) || !server.requests || server.authorizationMismatch) return false;
    if (scenario != "provider_401") return client.rows("session").size() == 1;
    if (!client.authenticationRequired("session") || client.refresh("session")) return false;
    const auto before = server.requests;
    QTest::qWait(2000);
    return server.requests == before && client.authenticationRequired("session");
}
bool viewScenario(const QString& scenario) {
    Kubernetes first, second;
    if (!first.listen(QHostAddress::LocalHost, 0) || !second.listen(QHostAddress::LocalHost, 0)) return false;
    auto now = QDateTime::currentDateTimeUtc();
    podlord::ResourceClient client(nullptr, [&] { return scenario == "view_focus" ? now : QDateTime::currentDateTimeUtc(); });
    const auto connection = [&](const Kubernetes& server, const QString& credential) {
        podlord::ClusterConnection result;
        result.server = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())); result.credentialId = credential;
        if (scenario == "view_shared_limit" || scenario == "view_shared_backoff") result.contextId = "same-context";
        return result;
    };
    podlord::ReadSettings settings;
    if (scenario == "view_limit" || scenario == "view_shared_limit") settings.requestHardLimitPerMinute = 60;
    if (scenario == "view_auth") second.listStatus = 401;
    if (scenario == "view_switch_initial") {
        first.allDelay = 150;
        second.allDelay = 150;
        if (!client.configure(settings) || !client.open("first", connection(first, "first"), {})
            || !waitFor([&] { return first.requests > 0; }) || !client.showSession("second")
            || !client.open("second", connection(second, "second"), {})) return false;
        if (!waitFor([&] { return !client.loading("second") && client.rows("second").size() == 1; }, 20000)) return false;
        return waitFor([&] { return !client.loading("first") && client.initialSyncComplete("first")
            && client.rows("first").size() == 1; }, 20000);
    }
    if (scenario == "view_backoff_isolation") {
        first.responseStatuses.insert("/api", 429);
        first.retryAfter = "5";
        if (!client.configure(settings) || !client.open("first", connection(first, "first"), {})
            || !client.showSession("second", "secondary")
            || !client.open("second", connection(second, "second"), {})) return false;
        return waitFor([&] { return second.requests > 0; }, 2500)
            && waitFor([&] { return !client.loading("second") && client.rows("second").size() == 1; }, 16000);
    }
    if (scenario == "view_shared_backoff") {
        first.responseStatuses.insert("/api", 429);
        first.retryAfter = "2";
        if (!client.configure(settings) || !client.open("first", connection(first, "first"), {})
            || !client.showSession("second", "secondary")
            || !client.open("second", connection(second, "second"), {})
            || !waitFor([&] { return first.pathRequests.value("/api") > 0; }, 1500)) return false;
        QTest::qWait(1000);
        if (second.requests != 0) return false;
        return waitFor([&] { return second.requests > 0; }, 4000);
    }
    if (scenario == "view_hidden_failure") {
        first.responseStatuses.insert("/api", 500);
        if (!client.configure(settings) || !client.open("first", connection(first, "first"), {})
            || !client.showSession("second") || !client.open("second", connection(second, "second"), {})) return false;
        if (!waitFor([&] { return !client.loading("first") && !client.loading("second") && !first.active; }, 16000)) return false;
        const auto requests = first.requests;
        QTest::qWait(1800);
        return requests > 0 && first.requests == requests;
    }
    QList<qint64> starts;
    QMap<QString, QList<qint64>> startsBySession;
    QObject::connect(&client, &podlord::ResourceClient::requestStarted, &client, [&](const QString& id, const QString&, qint64 at) {
        starts.append(at);
        startsBySession[id].append(at);
    });
    if (!client.configure(settings) || !client.open("first", connection(first, "first"), {})
        || !client.showSession("second", "secondary") || !client.open("second", connection(second, "second"), {})) return false;
    if (!waitFor([&] { return !client.loading("first") && !client.loading("second") && !first.active && !second.active; },
            scenario == "view_limit" || scenario == "view_shared_limit" ? 30000 : 16000)) return false;
    if (client.rows("first").size() != 1) return false;
    const auto path = QString("/api/v1/namespaces/default/pods/alpha");
    if (scenario == "view_auth") {
        const auto before = second.requests;
        if (!client.authenticationRequired("second") || client.authenticationRequired("first") || client.refresh("second")) return false;
        if (!client.inspect("first", path) || !waitFor([&] { return !client.document("first", path).isEmpty(); })) return false;
        QTest::qWait(500);
        return second.requests == before && client.authenticationRequired("second");
    }
    if (client.rows("second").size() != 1) return false;
    if (scenario == "view_forward_move" || scenario == "view_forward_close") {
        const auto startForward = [&](const QString& id) -> podlord::Result<QString> {
            QTcpServer reservation;
            if (!reservation.listen(QHostAddress::LocalHost, 0)) return podlord::Failure{podlord::StoreError::WriteFailed, "Cannot reserve a local test port."};
            const int port = reservation.serverPort(); reservation.close();
            return client.startPortForward(id, path, port, 8080);
        };
        if (!std::holds_alternative<QString>(startForward("first")) || !std::holds_alternative<QString>(startForward("second"))) return false;
        const auto firstForwards = client.portForwards("first"), secondForwards = client.portForwards("second");
        if (firstForwards.size() != 1 || secondForwards.size() != 1 || !client.moveSession({}, "detached")) return false;
        if (client.portForwards("first") != firstForwards || client.portForwards("second") != secondForwards) return false;
        QTcpServer firstProbe, secondProbe;
        const auto firstPort = firstForwards.first().toMap().value("localPort").toUInt();
        const auto secondPort = secondForwards.first().toMap().value("localPort").toUInt();
        if (scenario == "view_forward_move")
            return !firstProbe.listen(QHostAddress::LocalHost, firstPort) && !secondProbe.listen(QHostAddress::LocalHost, secondPort);
        return client.close("first") && client.portForwards("first").isEmpty() && client.portForwards("second") == secondForwards
            && firstProbe.listen(QHostAddress::LocalHost, firstPort) && !secondProbe.listen(QHostAddress::LocalHost, secondPort);
    }
    if (scenario == "view_focus") {
        if (!client.setFocused(true) || !client.userActivity() || !client.setFocused(false, "secondary")) return false;
        const auto beforeFirst = first.requests, beforeSecond = second.requests;
        now = now.addSecs(30);
        if (!client.synchronize() || !waitFor([&] { return first.requests > beforeFirst && !client.loading("first"); })) return false;
        return second.requests == beforeSecond && !client.loading("second");
    }
    if (scenario == "view_conflict" && client.showSession("first", "secondary")) return false;
    if (scenario == "view_move_occupied" && client.moveSession({}, "secondary")) return false;
    if (scenario == "view_move_missing" && client.moveSession("missing", "secondary")) return false;
    if (scenario == "view_inspect" || scenario == "view_conflict" || scenario == "view_move_occupied" || scenario == "view_move_missing") {
        const auto beforeFirst = first.requests, beforeSecond = second.requests;
        return client.inspect("first", path) && client.inspect("second", path)
            && waitFor([&] { return !client.document("first", path).isEmpty() && !client.document("second", path).isEmpty(); })
            && first.requests == beforeFirst + 1 && second.requests == beforeSecond + 1;
    }
    if (!client.showLogs("first", path) || !client.showLogs("second", path, "secondary")) return false;
    if (scenario == "view_pause" && !client.pauseLogs(true)) return false;
    if (scenario == "view_hide" && !client.hideLogs()) return false;
    if (scenario == "view_close" && !client.close("first")) return false;
    if (!waitFor([&] { return client.logEntries("second", path).size() >= 2 && !second.active; })) return false;
    if (scenario == "view_pause" || scenario == "view_hide" || scenario == "view_close") {
        if (!waitFor([&] { return !first.active; })) return false;
        const auto firstRequests = first.requests, secondLogs = second.logs.value("alpha");
        if (!client.selectLogContainer("alpha", "secondary") || !waitFor([&] { return second.logs.value("alpha") > secondLogs; })) return false;
        return first.requests == firstRequests && second.logs.value("beta") > 0 && client.rows("first").size() == 1;
    }
    if (scenario == "view_move") {
        if (!waitFor([&] { return client.logEntries("first", path).size() >= 2 && !first.active; })) return false;
        const auto rows = client.rows("first");
        const auto entries = client.logEntries("first", path).size();
        const auto before = first.requests;
        if (!client.moveSession({}, "detached") || first.requests != before || client.rows("first") != rows
            || client.logEntries("first", path).size() != entries || client.pauseLogs(true) || !client.pauseLogs(true, "detached")) return false;
        return client.moveSession("detached", "detached") && client.inspect("first", path)
            && waitFor([&] { return !client.document("first", path).isEmpty(); });
    }
    if (scenario == "view_logs" || scenario == "view_limit" || scenario == "view_shared_limit") {
        if (!waitFor([&] { return client.logEntries("first", path).size() >= 2 && !first.active; })) return false;
        for (qsizetype i = 1; i < starts.size(); ++i)
            if (starts[i] - starts[i - 1] < 400) return false;
        if (scenario == "view_limit") {
            bool interleaved = false;
            for (qsizetype i = 1; i < starts.size(); ++i) interleaved = interleaved || starts[i] - starts[i - 1] < 1000;
            if (!interleaved) return false;
            for (const auto& times : startsBySession)
                for (qsizetype i = 1; i < times.size(); ++i)
                    if (times[i] - times[i - 1] < 1000) return false;
        }
        if (scenario == "view_shared_limit") {
            for (qsizetype i = 1; i < starts.size(); ++i)
                if (starts[i] - starts[i - 1] < 1000) return false;
            if (client.requestCountLastMinute("first") != client.requestCountLastMinute("second")) return false;
        }
        return first.logs.value("alpha") > 0 && first.logs.value("beta") > 0 && second.logs.value("alpha") > 0 && second.logs.value("beta") > 0;
    }
    return false;
}
bool run(const QString& scenario) {
    if (scenario.startsWith("view_")) return viewScenario(scenario);
    if (scenario.startsWith("provider_")) return providerScenario(scenario);
    Kubernetes server; if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    auto reference = QDateTime::currentDateTimeUtc();
    podlord::ResourceClient client(nullptr, [&] { return scenario == "progress_auto" || scenario == "recovery_passive" || scenario == "recovery_bootstrap" || scenario == "adaptive_sync"
        || scenario.startsWith("list_namespaced_fallback") ? reference : QDateTime::currentDateTimeUtc(); });
    podlord::ClusterConnection connection;
    connection.server = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())); connection.credentialId = "local";
    if (scenario == "list_namespaced_fallback_preferred") connection.preferredNamespace = "team";
    podlord::ReadSettings settings;
    settings.requestHardLimitPerMinute = scenario == "limit" ? 60 : 0;
    if (scenario == "discovery_invalid_body") server.discovery = QJsonObject{{"resources", "not-an-array"}};
    if (scenario == "discovery_invalid_entry") server.discovery = QJsonObject{{"resources", QJsonArray{
        QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}},
        QJsonObject{{"name", "invalid"}, {"kind", ""}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}}}}};
    if (scenario == "core_invalid_body") server.responseBodies.insert("/api", QJsonObject{{"versions", "v1"}});
    if (scenario == "core_invalid_version") server.responseBodies.insert("/api", QJsonObject{{"versions", QJsonArray{"bad/version"}}});
    if (scenario == "groups_invalid_body") server.responseBodies.insert("/apis", QJsonObject{{"groups", "bad"}});
    if (scenario == "groups_invalid_version") server.responseBodies.insert("/apis", QJsonObject{{"groups", QJsonArray{
        QJsonObject{{"preferredVersion", QJsonObject{{"groupVersion", "v1"}}}}}}});
    if (scenario == "list_invalid_body") server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{}}, {"items", "bad"}});
    if (scenario == "list_invalid_entry") server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{
        QJsonObject{{"metadata", QJsonObject{{"name", ".."}, {"namespace", "default"}}}}}}});
    if (scenario == "list_invalid_version" || scenario == "list_invalid_version_type" || scenario == "list_invalid_kind"
        || scenario == "list_invalid_kind_type" || scenario == "list_missing_namespace" || scenario == "list_control_name") {
        QJsonObject metadata{{"name", "alpha"}, {"namespace", "default"}};
        QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"}};
        if (scenario == "list_invalid_version") pod["apiVersion"] = "v2";
        if (scenario == "list_invalid_version_type") pod["apiVersion"] = 7;
        if (scenario == "list_invalid_kind") pod["kind"] = "Secret";
        if (scenario == "list_invalid_kind_type") pod["kind"] = 7;
        if (scenario == "list_missing_namespace") metadata.remove("namespace");
        if (scenario == "list_control_name") metadata["name"] = "alpha\nerror";
        pod["metadata"] = metadata;
        server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}});
    }
    if (scenario == "list_invalid_continuation") server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{{"continue", 7}}}, {"items", QJsonArray{}}});
    if (scenario == "list_page_success") server.paginatePods = true;
    if (scenario == "related_refresh") {
        server.discovery = QJsonObject{{"resources", QJsonArray{
            QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
            QJsonObject{{"name", "nodes"}, {"kind", "Node"}, {"namespaced", false}, {"verbs", QJsonArray{"get", "list"}}},
            QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
        const QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"},
            {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", "default"}, {"uid", "uid-alpha"}, {"resourceVersion", "1"}}},
            {"spec", QJsonObject{{"nodeName", "worker-a"}, {"containers", QJsonArray{QJsonObject{{"name", "app"}}}}}},
            {"status", QJsonObject{{"phase", "Running"}, {"containerStatuses", QJsonArray{QJsonObject{{"name", "app"}, {"ready", true}, {"restartCount", 0}}}}}}};
        const QJsonObject node{{"apiVersion", "v1"}, {"kind", "Node"},
            {"metadata", QJsonObject{{"name", "worker-a"}, {"uid", "uid-node"}, {"resourceVersion", "1"}}},
            {"status", QJsonObject{{"conditions", QJsonArray{QJsonObject{{"type", "Ready"}, {"status", "True"}}}}}}};
        server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}});
        server.responseBodies.insert("/api/v1/nodes", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{node}}});
        server.responseBodies.insert("/api/v1/events", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{}}});
        auto failedPod = pod;
        auto metadata = failedPod["metadata"].toObject(); metadata["resourceVersion"] = "2"; failedPod["metadata"] = metadata;
        failedPod["status"] = QJsonObject{{"phase", "Running"}, {"containerStatuses", QJsonArray{QJsonObject{
            {"name", "app"}, {"ready", false}, {"restartCount", 1},
            {"state", QJsonObject{{"waiting", QJsonObject{{"reason", "CrashLoopBackOff"}}}}}}}}};
        server.responseBodies.insert("/api/v1/namespaces/default/pods/alpha", failedPod);
    }
    if (scenario == "list_malformed_json") server.responseBytes.insert("/api/v1/pods", "{");
    if (scenario == "list_forbidden" || scenario.startsWith("list_namespaced_fallback"))
        server.responseStatuses.insert("/api/v1/pods", 403);
    if (scenario == "list_namespaced_fallback_preferred") server.podNamespace = "team";
    if (scenario == "list_forbidden" || scenario == "list_namespaced_fallback_denied") server.responseStatuses.insert("/api/v1/namespaces/default/pods", 403);
    if (scenario == "list_redirect") server.responseStatuses.insert("/api/v1/pods", 302);
    if (scenario == "recovery_bootstrap") server.responseStatuses.insert("/api/v1/pods", 503);
    if (scenario == "list_rate_limited_invalid_retry_after") {
        server.responseStatuses.insert("/api/v1/pods", 429);
        server.retryAfter = "invalid";
    }
    if (scenario == "namespace_scoped_mismatch" || scenario == "namespace_all_valid") server.podNamespace = "other";
    if (scenario == "pod_problem_projection") {
        server.discovery = QJsonObject{{"resources", QJsonArray{
            QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
            QJsonObject{{"name", "secrets"}, {"kind", "Secret"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}},
            QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
        const QJsonObject registrySecret{{"apiVersion", "v1"}, {"kind", "Secret"},
            {"metadata", QJsonObject{{"name", "registry"}, {"namespace", "default"}, {"uid", "secret-uid"}}},
            {"type", "kubernetes.io/dockerconfigjson"}, {"data", QJsonObject{{".dockerconfigjson", "e30="}}}};
        server.responseBodies.insert("/api/v1/secrets", QJsonObject{{"metadata", QJsonObject{}},
            {"items", QJsonArray{registrySecret}}});
        const QJsonObject pullEvent{{"apiVersion", "v1"}, {"kind", "Event"},
            {"metadata", QJsonObject{{"name", "pull-warning"}, {"namespace", "default"}, {"uid", "event-uid"}}},
            {"type", "Warning"}, {"reason", "Failed"}, {"action", "Pulling"},
            {"involvedObject", QJsonObject{{"uid", "uid-alpha"}, {"fieldPath", "spec.containers{alpha}"}}}};
        server.responseBodies.insert("/api/v1/events", QJsonObject{{"metadata", QJsonObject{}},
            {"items", QJsonArray{pullEvent}}});
        server.podStatus = QJsonObject{{"phase", "Running"}, {"qosClass", "Burstable"},
        {"conditions", QJsonArray{QJsonObject{{"type", "ContainersReady"}, {"status", "False"},
            {"reason", "ContainersNotReady"}, {"lastTransitionTime", "2026-11-19T11:54:00Z"}}}},
        {"containerStatuses", QJsonArray{
            QJsonObject{{"name", "alpha"}, {"ready", false}, {"restartCount", 3}, {"state", QJsonObject{{"waiting", QJsonObject{
                {"reason", "CrashLoopBackOff"}, {"message", "Back-off restarting failed container"}}}}}},
            QJsonObject{{"name", "beta"}, {"ready", true}, {"restartCount", 0}, {"state", QJsonObject{{"running", QJsonObject{}}}}}}}};
    }
    if (scenario == "large_list_nonblocking" || scenario == "rebind_stale_projection") {
        QJsonArray items;
        for (int index = 0; index < 20000; ++index) items.append(QJsonObject{
            {"apiVersion", "v1"}, {"kind", "Pod"},
            {"metadata", QJsonObject{{"name", QString("pod-%1").arg(index)}, {"namespace", "default"},
                                      {"uid", QString("uid-%1").arg(index)}, {"resourceVersion", "1"}}},
            {"status", QJsonObject{{"phase", "Running"}}}});
        server.responseBytes.insert("/api/v1/pods", QJsonDocument(QJsonObject{
            {"metadata", QJsonObject{}}, {"items", items}}).toJson(QJsonDocument::Compact));
    }
    if (!client.configure(settings)) return false;
    if (scenario == "rebind_stale_projection") {
        Kubernetes replacement;
        if (!replacement.listen(QHostAddress::LocalHost, 0)) return false;
        replacement.podNamespace = "other";
        auto renewed = connection;
        renewed.server = QUrl(QString("http://127.0.0.1:%1").arg(replacement.serverPort()));
        renewed.credentialId = "replacement";
        bool rebound = false, projectionPending = false;
        QObject::connect(&client, &podlord::ResourceClient::changed, &client, [&](const QString& id) {
            if (rebound || id != "session" || !client.rows(id).isEmpty()) return;
            for (const auto& value : client.requestAudit(id)) {
                const auto request = value.toMap();
                if (request.value("path") != "/api/v1/pods" || request.value("status") != "Success") continue;
                projectionPending = client.loading(id);
                rebound = true;
                QTimer::singleShot(0, &client, [&] { client.open("session", renewed, {}); });
                break;
            }
        });
        if (!client.open("session", connection, {}) || !waitFor([&] {
            return rebound && projectionPending && !client.loading("session") && client.rows("session").size() == 1
                && client.rows("session").first().toObject()["path"] == "/api/v1/namespaces/other/pods/alpha";
        }, 30000)) return false;
        QTest::qWait(500);
        return client.rows("session").size() == 1
            && client.rows("session").first().toObject()["path"] == "/api/v1/namespaces/other/pods/alpha";
    }
    if (scenario == "large_list_nonblocking") {
        QElapsedTimer elapsed; elapsed.start();
        qint64 lastTick = elapsed.elapsed(), longestGap = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &client, [&] {
            const auto now = elapsed.elapsed();
            longestGap = std::max(longestGap, now - lastTick);
            lastTick = now;
        });
        heartbeat.start();
        if (!client.open("session", connection, {})) return false;
        const bool published = waitFor([&] { return client.rows("session").size() == 20000; }, 30000);
        heartbeat.stop();
        if (!published || longestGap > 80) {
            std::fprintf(stderr, "Large list: published=%d, longest UI event gap=%lld ms\n", published, static_cast<long long>(longestGap));
            return false;
        }
        return true;
    }
    QList<qint64> starts;
    QObject::connect(&client, &podlord::ResourceClient::requestStarted, &client, [&](const QString&, const QString&, qint64 at) { starts.append(at); });
    if (scenario == "parallel" || scenario == "limit" || scenario == "coalesce") server.allDelay = 2000;
    if (!client.open("session", connection, scenario.startsWith("namespace_scoped_") ? QStringList{"default"} : QStringList{})) return false;
    const QMap<QString, QString> invalidReadMessages{
        {"core_invalid_body", "Invalid core discovery response"},
        {"core_invalid_version", "Invalid core API version"},
        {"groups_invalid_body", "Invalid group discovery response"},
        {"groups_invalid_version", "Invalid preferred API version"},
        {"list_invalid_body", "Invalid resource list response"},
        {"list_invalid_entry", "invalid resource"},
        {"list_invalid_version", "invalid resource"},
        {"list_invalid_version_type", "invalid resource"},
        {"list_invalid_kind", "invalid resource"},
        {"list_invalid_kind_type", "invalid resource"},
        {"list_missing_namespace", "invalid resource"},
        {"list_control_name", "invalid resource"},
        {"list_invalid_continuation", "Invalid list continuation"},
        {"list_malformed_json", "invalid response"},
        {"list_forbidden", "Authorization denied"},
        {"list_redirect", "Redirect refused"},
        {"list_rate_limited_invalid_retry_after", "limiting requests"}
    };
    if (invalidReadMessages.contains(scenario))
        return waitFor([&] { return !client.loading("session") && !server.active; })
            && client.rows("session").size() == (scenario.startsWith("groups_") ? 1 : 0)
            && client.failureStatus("session").contains(invalidReadMessages.value(scenario));
    if (scenario == "recovery_bootstrap") {
        if (!waitFor([&] { return !client.loading("session") && !server.active
            && client.failureStatus("session").contains("/api/v1/pods"); })) return false;
        const auto before = server.pathRequests.value("/api/v1/pods");
        server.responseStatuses.remove("/api/v1/pods");
        reference = reference.addSecs(61);
        return client.setFocused(true) && waitFor([&] { return !client.loading("session") && !server.active
            && client.rows("session").size() == 1 && client.failureStatus("session").isEmpty(); })
            && server.pathRequests.value("/api/v1/pods") == before + 1;
    }
    if (scenario.startsWith("list_namespaced_fallback")) {
        if (!waitFor([&] { return !client.loading("session") && !server.active; })) return false;
        if (scenario == "list_namespaced_fallback_denied") return client.rows("session").isEmpty() && server.podLists == 2;
        if (scenario == "list_namespaced_fallback_preferred") return client.rows("session").size() == 1
            && client.resource("session", "/api/v1/namespaces/team/pods/alpha")["name"] == "alpha"
            && server.pathRequests.value("/api/v1/namespaces/team/pods") == 1;
        if (client.rows("session").size() != 1 || server.podLists != 2
            || client.resource("session", "/api/v1/namespaces/default/pods/alpha")["name"] != "alpha") return false;
        if (!client.refresh("session") || !waitFor([&] { return !client.loading("session") && !server.active; })) return false;
        if (server.podLists != 3 || server.pathRequests.value("/api/v1/pods") != 1 || !client.failureStatus("session").isEmpty()) return false;
        if (scenario == "list_namespaced_fallback_ttl" || scenario == "list_namespaced_fallback_recover") {
            reference = reference.addSecs(16 * 60);
            if (scenario == "list_namespaced_fallback_recover") server.responseStatuses.remove("/api/v1/pods");
            if (!client.refresh("session") || !waitFor([&] { return !client.loading("session") && !server.active; })) return false;
            if (scenario == "list_namespaced_fallback_recover") return server.podLists == 4
                && server.pathRequests.value("/api/v1/pods") == 2 && client.rows("session").size() == 1;
            return server.podLists == 5 && server.pathRequests.value("/api/v1/pods") == 2;
        }
        return true;
    }
    if (scenario == "list_page_success")
        return waitFor([&] { return !client.loading("session") && !server.active; })
            && server.podLists == 2 && client.rows("session").size() == 2
            && client.resource("session", "/api/v1/namespaces/default/pods/alpha")["name"] == "alpha"
            && client.resource("session", "/api/v1/namespaces/default/pods/beta")["name"] == "beta";
    if (scenario.startsWith("namespace_scoped_")) {
        if (!waitFor([&] { return !client.loading("session") && !server.active; })) return false;
        if (scenario == "namespace_scoped_valid")
            return client.rows("session").size() == 1 && client.rows("session").first().toObject()["namespace"] == "default";
        if (scenario == "namespace_scoped_retain") {
            const auto previous = client.rows("session");
            if (previous.size() != 1) return false;
            server.podNamespace = "other";
            if (!client.refresh("session") || !waitFor([&] { return !client.loading("session") && !server.active; })) return false;
            return client.rows("session") == previous && client.failureStatus("session").contains("invalid resource");
        }
        return client.rows("session").isEmpty() && client.failureStatus("session").contains("invalid resource");
    }
    if (scenario == "namespace_all_valid")
        return waitFor([&] { return !client.loading("session") && !server.active; })
            && client.rows("session").size() == 1
            && client.rows("session").first().toObject()["path"] == "/api/v1/namespaces/other/pods/alpha";
    if (scenario == "coalesce") for (int repeat = 0; repeat < 10; ++repeat) if (!client.refresh("session")) return false;
    if (scenario == "discovery_invalid_body")
        return waitFor([&] { return !client.loading("session") && !server.active; })
            && client.rows("session").isEmpty() && client.failureStatus("session").contains("Invalid discovery response");
    if (!waitFor([&] { return !client.loading("session")
        && client.rows("session").size() == (scenario == "pod_problem_projection" ? 3 : scenario == "related_refresh" ? 2 : 1); })) return false;
    if (scenario == "list_repeated_continuation") {
        const auto previous = client.rows("session");
        server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{{"continue", "repeat"}}}, {"items", QJsonArray{}}});
        if (!client.refresh("session") || !waitFor([&] { return !client.loading("session") && !server.active; })) return false;
        return server.podLists == 3 && client.rows("session") == previous
            && client.failureStatus("session").contains("repeated a resource page");
    }
    if (scenario == "discovery_invalid_entry")
        return client.failureStatus("session").contains("Invalid resource discovery entry") && server.podLists == 1;
    if (scenario == "pod_problem_projection") {
        QJsonObject row, secret, event;
        for (const auto& value : client.rows("session")) {
            if (value.toObject()["kind"] == "Pod") row = value.toObject();
            if (value.toObject()["kind"] == "Secret") secret = value.toObject();
            if (value.toObject()["kind"] == "Event") event = value.toObject();
        }
        return row["status"] == "CrashLoopBackOff"
            && row["issue"] == "CrashLoopBackOff, Ready 1/2, Container restarts"
            && row["problems"] == true && row["restarts"] == 3 && row["ready"] == "1/2"
            && row["qosClass"] == "Burstable"
            && row["image"] == "private.registry.example/team/app:v1"
            && row["conditions"].toArray().first().toObject()["reason"] == "ContainersNotReady"
            && row["containerStates"].toArray().first().toObject()["message"] == "Back-off restarting failed container"
            && row["containerStates"].toArray().first().toObject()["image"] == "private.registry.example/team/app:v1"
            && row["containerStates"].toArray().first().toObject()["imagePullPolicy"] == "Always"
            && secret["secretType"] == "kubernetes.io/dockerconfigjson"
            && secret["registryAuthKeyPresent"] == true
            && event["eventAction"] == "Pulling" && event["eventTargetUid"] == "uid-alpha"
            && event["eventTargetFieldPath"] == "spec.containers{alpha}";
    }
    if (scenario == "request_audit") {
        const auto path = QString("/api/v1/namespaces/default/pods/alpha");
        server.detailDelay = 1200;
        const auto before = server.requests;
        if (!client.inspect("session", path) || !waitFor([&] { return server.requests > before; })) return false;
        bool running = false;
        for (const auto& value : client.requestAudit("session")) {
            const auto record = value.toMap();
            if (record.value("path") == path && record.value("status") == "Running"
                && record.value("method") == "GET" && record.value("priority") == "Foreground"
                && QDateTime::fromString(record.value("time").toString(), Qt::ISODateWithMs).isValid()) running = true;
        }
        if (!running || !client.requestAudit("other").isEmpty()
            || !waitFor([&] { return !client.document("session", path).isEmpty(); })) return false;
        bool completed = false;
        for (const auto& value : client.requestAudit("session")) {
            const auto record = value.toMap();
            if (record.value("path") == path && record.value("status") == "Success"
                && record.value("outcome") == "HTTP 200" && !record.contains("authorization")) completed = true;
        }
        if (!completed) return false;
        server.responseStatuses.insert(path, 503);
        if (!client.inspect("session", path) || !waitFor([&] { return client.failureStatus("session").contains(path); })) return false;
        const auto audit = client.requestAudit("session");
        if (audit.isEmpty()) return false;
        const auto latest = audit.first().toMap();
        return latest.value("path") == path && latest.value("status") == "Failed"
            && latest.value("outcome") == "HTTP 503"
            && QDateTime::fromString(latest.value("time").toString(), Qt::ISODateWithMs).isValid();
    }
    if (scenario == "recovery_passive") {
        const auto failed = [&](const char* stage) {
            std::fprintf(stderr, "Recovery stage %s: failure=%s core=%d detail=%d active=%d loading=%d\n",
                stage, qPrintable(client.failureStatus("session")), server.pathRequests.value("/api"),
                server.pathRequests.value("/api/v1/namespaces/default/pods/alpha"), server.active, client.loading("session"));
            return false;
        };
        if (!waitFor([&] { return !client.loading("session") && !server.active && !client.rows("session").isEmpty(); })) return failed("initial");
        QSignalSpy failures(&client, &podlord::ResourceClient::transientFailureStarted);
        server.responseStatuses.insert("/api", 503);
        if (!client.refresh("session") || !waitFor([&] { return client.failureStatus("session").contains("/api:")
            && !client.loading("session") && !server.active; })) return failed("core failure");
        if (failures.size() != 1 || failures.first().at(1) != "/api") return failed("failure signal");
        server.responseStatuses.remove("/api");
        const auto beforeCore = server.pathRequests.value("/api");
        reference = reference.addSecs(61);
        if (!client.setFocused(true) || !waitFor([&] { return !client.failureStatus("session").contains("/api:"); })) return failed("core recovery");
        if (server.pathRequests.value("/api") != beforeCore + 1) return failed("core probe count");
        const QString path = "/api/v1/namespaces/default/pods/alpha";
        server.responseStatuses.insert(path, 503);
        if (!client.inspect("session", path) || !waitFor([&] { return client.failureStatus("session").contains(path)
            && !client.loading("session") && !server.active; })) return failed("detail failure");
        server.responseStatuses.remove(path);
        const auto beforeDetail = server.pathRequests.value(path);
        reference = reference.addSecs(61);
        if (!client.setFocused(true) || !waitFor([&] { return !client.failureStatus("session").contains(path); })) return failed("detail recovery");
        if (server.pathRequests.value(path) != beforeDetail + 1 || failures.size() != 2) return failed("detail recovery count");
        server.responseStatuses.insert(path, 503);
        if (!client.inspect("session", path) || !waitFor([&] { return client.failureStatus("session").contains(path)
            && !client.loading("session") && !server.active; })) return failed("removed detail failure");
        server.responseBodies.insert("/api/v1/pods", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{}}});
        return client.refresh("session") && waitFor([&] { return !client.loading("session") && !server.active
            && client.rows("session").isEmpty() && client.failureStatus("session").isEmpty(); });
    }
    if (scenario == "adaptive_sync") {
        if (!waitFor([&] { return !client.loading("session") && !server.active && server.pathRequests.value("/api/v1/pods") == 1; })) return false;
        const auto podLists = [&] { return server.pathRequests.value("/api/v1/pods"); };
        reference = reference.addSecs(30);
        if (!client.setFocused(true) || !waitFor([&] { return podLists() == 2 && !client.loading("session"); })) return false;
        reference = reference.addSecs(30);
        if (!client.setFocused(true)) return false;
        QTest::qWait(100);
        if (podLists() != 2) return false;
        reference = reference.addSecs(25);
        if (!client.setFocused(true) || !waitFor([&] { return podLists() == 3 && !client.loading("session"); })) return false;
        reference = reference.addSecs(60);
        if (!client.setFocused(true)) return false;
        QTest::qWait(100);
        if (podLists() != 3) return false;
        reference = reference.addSecs(45);
        return client.setFocused(true) && waitFor([&] { return podLists() == 4 && !client.loading("session"); });
    }
    if (scenario == "related_refresh") {
        if (!waitFor([&] { return !client.loading("session") && !server.active && client.rows("session").size() == 2; })) {
            std::fprintf(stderr, "Related initial: nodes=%d pods=%d rows=%lld failure=%s\n",
                server.pathRequests.value("/api/v1/nodes"), server.pathRequests.value("/api/v1/pods"),
                static_cast<long long>(client.rows("session").size()), qPrintable(client.failureStatus("session")));
            return false;
        }
        const auto nodeLists = server.pathRequests.value("/api/v1/nodes");
        const auto eventLists = server.pathRequests.value("/api/v1/events");
        const auto coreReads = server.pathRequests.value("/api");
        const QJsonObject warning{{"apiVersion", "v1"}, {"kind", "Event"},
            {"metadata", QJsonObject{{"name", "restart-warning"}, {"namespace", "default"}, {"uid", "event-1"},
                {"creationTimestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}},
            {"type", "Warning"}, {"reason", "BackOff"}, {"involvedObject", QJsonObject{{"uid", "uid-alpha"}}}};
        server.responseBodies.insert("/api/v1/events", QJsonObject{{"metadata", QJsonObject{}}, {"items", QJsonArray{warning}}});
        const QString path = "/api/v1/namespaces/default/pods/alpha";
        const bool accepted = client.inspect("session", path)
            && waitFor([&] {
                const auto rows = client.rows("session");
                const bool podProblem = std::any_of(rows.cbegin(), rows.cend(), [&](const QJsonValue& value) {
                    const auto row = value.toObject(); return row["path"] == path && row["problemSeverity"].toInt() > 0;
                });
                return podProblem && server.pathRequests.value("/api/v1/nodes") == nodeLists + 1
                    && server.pathRequests.value("/api/v1/events") == eventLists + 1 && !client.loading("session");
            }) && server.pathRequests.value("/api") == coreReads;
        QTest::qWait(700);
        const auto rows = client.rows("session");
        const bool warningVisible = std::any_of(rows.cbegin(), rows.cend(), [](const QJsonValue& value) {
            const auto row = value.toObject();
            return row["kind"] == "Event" && row["problemSeverity"].toInt() > 0;
        });
        const bool stable = server.pathRequests.value("/api/v1/events") == eventLists + 1
            && rows.size() == 3 && warningVisible;
        if (!accepted) std::fprintf(stderr, "Related refresh: nodes=%d/%d events=%d/%d core=%d/%d failure=%s\n",
            server.pathRequests.value("/api/v1/nodes"), nodeLists,
            server.pathRequests.value("/api/v1/events"), eventLists,
            server.pathRequests.value("/api"), coreReads, qPrintable(client.failureStatus("session")));
        if (!stable) std::fprintf(stderr, "Related refresh: Events reads=%d rows=%lld warning=%d.\n",
            server.pathRequests.value("/api/v1/events") - eventLists,
            static_cast<long long>(rows.size()), warningVisible);
        return accepted && stable;
    }
    if (scenario == "snapshot_fields") {
        const auto snapshot = client.rows("session");
        const auto row = snapshot.first().toObject();
        for (const auto& field : {"kubernetesEvent", "changedAt", "createdAt", "restarts", "eventTime", "metricAt", "cpu", "memory", "storage"}) {
            if (row.contains(field)) {
                std::fprintf(stderr, "Absent resource field was manufactured: %s\n", field);
                return false;
            }
        }
        return client.refresh("session") && waitFor([&] { return !client.loading("session"); })
            && client.rows("session") == snapshot;
    }
    if (scenario.startsWith("progress_")) {
        if (!client.initialSyncComplete("session") || client.loadingProgress("session") != 1) return false;
        bool retained = true;
        QObject observer;
        QObject::connect(&client, &podlord::ResourceClient::changed, &observer, [&](const QString& id) {
            if (id == "session") retained = retained && client.loadingProgress(id) == 1;
        });
        const auto before = server.requests;
        server.version = 2;
        if (scenario == "progress_auto") {
            reference = reference.addSecs(300);
            if (!client.synchronize()) return false;
        } else if (scenario == "progress_reopen") {
            if (!client.close("session") || !client.open("session", connection, {})) return false;
        } else {
            if (scenario == "progress_auth") server.listStatus = 401;
            if (!client.refresh("session")) return false;
        }
        if (!retained || client.loadingProgress("session") != 1) return false;
        if (scenario == "progress_reopen") return !client.loading("session") && server.requests == before;
        if (!waitFor([&] { return !client.loading("session"); }) || !retained || server.requests <= before) return false;
        return scenario == "progress_auth" ? client.authenticationRequired("session")
            : client.resource("session", "/api/v1/namespaces/default/pods/alpha")["resourceVersion"] == "2";
    }
    for (qsizetype i = 1; i < starts.size(); ++i) if (starts[i] - starts[i-1] < (scenario == "limit" ? 1000 : 400)) return false;
    if (scenario == "parallel" || scenario == "limit" || scenario == "coalesce")
        return server.maximum >= 2 && server.maximum <= 4 && (scenario != "parallel" || server.maximum == 4)
            && server.requests == 10 && client.loadingProgress("session") == 1;
    const auto path = QString("/api/v1/namespaces/default/pods/alpha");
    if (scenario.startsWith("detail_invalid_")) {
        QJsonObject detail{{"apiVersion", "v1"}, {"kind", "Pod"},
            {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", "default"}, {"uid", "uid-alpha"}, {"resourceVersion", "1"}}}};
        if (scenario == "detail_invalid_version") detail["apiVersion"] = "v2";
        if (scenario == "detail_invalid_kind") detail["kind"] = "Secret";
        if (scenario == "detail_invalid_name") detail["metadata"] = QJsonObject{{"name", "other"}, {"namespace", "default"}};
        if (scenario == "detail_invalid_namespace") detail["metadata"] = QJsonObject{{"name", "alpha"}, {"namespace", "other"}};
        server.responseBodies.insert(path, detail);
        return client.inspect("session", path)
            && waitFor([&] { return client.detailStatus("session", path).contains("Invalid detail response"); })
            && client.document("session", path).isEmpty() && client.rows("session").size() == 1;
    }
    if (scenario == "late_list") {
        server.listDelay = 1800; const auto lists = server.podLists;
        if (!client.refresh("session") || !waitFor([&] { return server.podLists > lists; })) return false;
        server.version = 2;
        if (!client.inspect("session", path) || !waitFor([&] { return client.document("session", path)["metadata"].toObject()["resourceVersion"] == "2"; })) return false;
        return waitFor([&] { return !client.loading("session"); }) && client.resource("session", path)["resourceVersion"] == "2";
    }
    if (scenario == "late_detail") {
        server.detailDelay = 3000;
        const auto requests = server.requests;
        if (!client.inspect("session", path) || !waitFor([&] { return server.requests > requests; })) return false;
        server.version = 2;
        if (!client.refresh("session") || !waitFor([&] { return !client.loading("session"); })) return false;
        return client.resource("session", path)["resourceVersion"] == "2" && client.document("session", path).isEmpty()
            && client.detailStatus("session", path).contains("newer");
    }
    if (scenario == "auth" || scenario == "backoff") {
        server.listStatus = scenario == "auth" ? 401 : 429; const auto before = starts.size();
        if (!client.refresh("session") || !waitFor([&] { return !client.loading("session") && !server.active; })) return false;
        const auto requests = server.requests; QTest::qWait(100);
        if (requests != server.requests || client.rows("session").size() != 1) return false;
        if (scenario == "auth") return client.authenticationRequired("session") && !client.refresh("session");
        bool held = false;
        for (qsizetype i = before + 1; i < starts.size(); ++i) held = held || starts[i] - starts[i-1] >= 2000;
        return held && client.status("session").contains("limiting requests");
    }
    return false;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); if (argc != 2) return 2;
    const bool passed = run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Read overlap scenario failed: %s\n", argv[1]);
    return passed ? 0 : 1;
}
