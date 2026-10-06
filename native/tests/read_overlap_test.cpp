#include "resource_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cstdio>

namespace {
bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 16000) QTest::qWait(10);
    return ready();
}
// Kubernetes is the only simulated boundary; cache and scheduler are the real client.
class Kubernetes final : public QTcpServer {
public:
    int active = 0, maximum = 0, requests = 0, podLists = 0, version = 1;
    int allDelay = 0, listDelay = 0, detailDelay = 0, listStatus = 200;
    QByteArray expectedAuthorization;
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
                    const auto path = QUrl::fromEncoded(input->split(' ')[1]).path();
                    const QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"}, {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", "default"}, {"uid", "uid-alpha"}, {"resourceVersion", QString::number(version)}}}, {"status", QJsonObject{{"phase", "Running"}}}};
                    QJsonObject body; int delay = allDelay, status = 200;
                    if (path == "/api") body = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") body = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") {
                        QJsonArray resources{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}};
                        for (const auto& name : {"configmaps", "secrets", "services", "endpoints", "serviceaccounts", "persistentvolumeclaims"})
                            resources.append(QJsonObject{{"name", name}, {"kind", QString(name)}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}});
                        body = {{"resources", resources}};
                    } else if (path == "/api/v1/pods") { ++podLists; delay = std::max(delay, listDelay); status = listStatus; body = {{"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}}; }
                    else if (path == "/api/v1/namespaces/default/pods/alpha") { delay = std::max(delay, detailDelay); body = pod; }
                    else body = {{"metadata", QJsonObject{}}, {"items", QJsonArray{}}};
                    const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
                    QTimer::singleShot(delay, socket, [this, socket, bytes, status] {
                        --active;
                        socket->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nContent-Type: application/json\r\nRetry-After: 2\r\nConnection: close\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
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
        if (std::get<podlord::ClusterConnection>(oldConnection).authorization != "Bearer private-access-token") return false;
        connection = std::get<podlord::ClusterConnection>(newConnection); expected = "private-new-token";
    }
    server.expectedAuthorization = "Bearer " + expected;
    if (scenario == "provider_401") server.listStatus = 401;
    podlord::ResourceClient client;
    podlord::ReadSettings settings; settings.inactiveSyncMinutes = 0; settings.requestHardLimitPerMinute = 0;
    if (!client.configure(settings) || !client.open("session", connection, {})) return false;
    if (!waitFor([&] { return !client.loading("session") && !server.active; }) || !server.requests || server.authorizationMismatch) return false;
    if (scenario != "provider_401") return client.rows("session").size() == 1;
    if (!client.authenticationRequired("session") || client.refresh("session")) return false;
    const auto before = server.requests;
    QTest::qWait(2000);
    return server.requests == before && client.authenticationRequired("session");
}
bool run(const QString& scenario) {
    if (scenario.startsWith("provider_")) return providerScenario(scenario);
    Kubernetes server; if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    podlord::ResourceClient client; podlord::ClusterConnection connection;
    connection.server = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())); connection.credentialId = "local";
    podlord::ReadSettings settings; settings.inactiveSyncMinutes = 0;
    settings.requestHardLimitPerMinute = scenario == "limit" ? 60 : 0;
    if (!client.configure(settings)) return false;
    QList<qint64> starts;
    QObject::connect(&client, &podlord::ResourceClient::requestStarted, &client, [&](const QString&, const QString&, qint64 at) { starts.append(at); });
    if (scenario == "parallel" || scenario == "limit" || scenario == "coalesce") server.allDelay = 2000;
    if (!client.open("session", connection, {})) return false;
    if (scenario == "coalesce") for (int repeat = 0; repeat < 10; ++repeat) if (!client.refresh("session")) return false;
    if (!waitFor([&] { return !client.loading("session") && client.rows("session").size() == 1; })) return false;
    for (qsizetype i = 1; i < starts.size(); ++i) if (starts[i] - starts[i-1] < (scenario == "limit" ? 1000 : 400)) return false;
    if (scenario == "parallel" || scenario == "limit" || scenario == "coalesce")
        return server.maximum >= 2 && server.maximum <= 4 && (scenario != "parallel" || server.maximum == 4)
            && server.requests == 10 && client.loadingProgress("session") == 1;
    const auto path = QString("/api/v1/namespaces/default/pods/alpha");
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
        return held && client.status("session").contains("Rate limited");
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
