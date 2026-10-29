#include "resource_client.h"
#include "yaml_apply.h"
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cstdio>
#include <stdexcept>

namespace {
const QString path = "/api/v1/namespaces/default/configmaps/checked";
const QJsonObject baseline{{"apiVersion", "v1"}, {"kind", "ConfigMap"},
    {"metadata", QJsonObject{{"name", "checked"}, {"namespace", "default"},
        {"uid", "original-uid"}, {"resourceVersion", "1"}}},
    {"data", QJsonObject{{"setting", "before"}}}};

bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return value;
}

// Only the external Kubernetes HTTP boundary is replaced.
struct Kubernetes final {
    QTcpServer server;
    QJsonObject document = baseline;
    QPointer<QTcpSocket> held;
    int writes = 0;
    bool hold = false, rejectAuthentication = false, allowDelete = true;

    Kubernetes() {
        require(server.listen(QHostAddress::LocalHost), "Cannot bind local Kubernetes boundary");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, &server, [this, socket, bytes = QByteArray{}]() mutable {
                    bytes += socket->readAll();
                    const auto end = bytes.indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const auto headers = bytes.left(end).split('\n');
                    qint64 length = 0;
                    for (const auto& header : headers)
                        if (header.toLower().startsWith("content-length:")) length = header.mid(15).trimmed().toLongLong();
                    if (bytes.size() - end - 4 < length) return;
                    const auto request = headers.first().trimmed().split(' ');
                    require(request.size() == 3, "Malformed request at external boundary");
                    const auto method = request[0];
                    const auto route = QUrl(QString::fromLatin1(request[1])).path();
                    const auto body = bytes.mid(end + 4, length);
                    bytes.clear();
                    if (rejectAuthentication) { reply(socket, 401, {{"apiVersion", "v1"}, {"kind", "Status"}, {"code", 401}}); return; }
                    if (method == "PATCH") {
                        ++writes;
                        const auto patch = QJsonDocument::fromJson(body).array();
                        const QJsonArray expected{
                            QJsonObject{{"op", "test"}, {"path", "/metadata/uid"}, {"value", "original-uid"}},
                            QJsonObject{{"op", "test"}, {"path", "/metadata/resourceVersion"}, {"value", "1"}},
                            QJsonObject{{"op", "replace"}, {"path", "/data/setting"}, {"value", "after"}}};
                        if (route != path || patch != expected || document["metadata"].toObject()["resourceVersion"] != "1") {
                            reply(socket, 422, {{"apiVersion", "v1"}, {"kind", "Status"}, {"code", 422}}); return;
                        }
                        document["data"] = QJsonObject{{"setting", "after"}};
                        auto metadata = document["metadata"].toObject(); metadata["resourceVersion"] = "2"; document["metadata"] = metadata;
                        if (hold) held = socket;
                        else reply(socket, 200, document);
                        return;
                    }
                    if (method != "GET") { ++writes; reply(socket, 405, {}); return; }
                    if (route == "/api") reply(socket, 200, {{"versions", QJsonArray{"v1"}}});
                    else if (route == "/apis") reply(socket, 200, {{"groups", QJsonArray{}}});
                    else if (route == "/api/v1") reply(socket, 200, {{"groupVersion", "v1"}, {"resources", QJsonArray{
                        QJsonObject{{"name", "configmaps"}, {"kind", "ConfigMap"}, {"namespaced", true},
                            {"verbs", allowDelete ? QJsonArray{"list", "get", "patch", "delete"} : QJsonArray{"list", "get", "patch"}}}}}});
                    else if (route == "/api/v1/configmaps") reply(socket, 200,
                        {{"apiVersion", "v1"}, {"kind", "ConfigMapList"}, {"metadata", QJsonObject{{"resourceVersion", "1"}}}, {"items", QJsonArray{document}}});
                    else if (route == path) reply(socket, 200, document);
                    else reply(socket, 404, {{"apiVersion", "v1"}, {"kind", "Status"}, {"code", 404}, {"reason", "NotFound"}});
                });
            }
        });
    }
    bool reply(QTcpSocket* socket, int status, const QJsonObject& value) {
        const auto body = QJsonDocument(value).toJson(QJsonDocument::Compact);
        return socket->write("HTTP/1.1 " + QByteArray::number(status)
            + " Result\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
            + "\r\nConnection: close\r\n\r\n" + body) >= 0 && (socket->disconnectFromHost(), true);
    }
    bool release() { return held && reply(held, 200, document); }
    podlord::ClusterConnection connection() const {
        podlord::ClusterConnection result;
        result.contextId = "test-context"; result.credentialId = "test-credential";
        result.server = QUrl("http://127.0.0.1:" + QString::number(server.serverPort()));
        return result;
    }
};

bool run(const QString& scenario) {
    const QStringList cases{"unknown_session", "empty_token", "empty_patch", "short_patch", "wrong_uid",
        "wrong_name", "wrong_kind", "wrong_api_version", "wrong_namespace", "wrong_uid_guard", "wrong_version_guard",
        "reversed_guards", "oversize_patch", "closed", "inactive", "disabled", "authentication",
        "stale_revision", "empty_detail_authority", "expired_detail_authority", "backward_detail_authority", "credential_expired",
        "queued_duplicate", "running_duplicate", "cancel_queued", "cancel_running", "cancel_wrong_token",
        "cancel_empty_queue", "cancel_read_queue", "readback_rejected", "readback_uncertain",
        "readback_unknown_session", "readback_empty_token", "readback_closed", "readback_inactive"};
    const QStringList deleteCases{"delete_unknown_session", "delete_empty_session", "delete_empty_token",
        "delete_missing_resource", "delete_empty_path", "delete_unsupported", "delete_missing_uid",
        "delete_wrong_uid", "delete_wrong_kind", "delete_wrong_api_version", "delete_wrong_name",
        "delete_wrong_namespace", "delete_closed", "delete_inactive", "delete_authentication",
        "delete_readback_unknown_session", "delete_readback_empty_token", "delete_readback_missing_uid",
        "delete_readback_closed", "delete_readback_inactive", "delete_cancel_empty_queue"};
    require(cases.contains(scenario) || deleteCases.contains(scenario), "Unknown mutation admission scenario");
    Kubernetes kubernetes;
    kubernetes.allowDelete = scenario != "delete_unsupported";
    auto now = QDateTime::currentDateTimeUtc();
    bool controlledClock = false;
    podlord::ResourceClient client(nullptr, [&] { return controlledClock ? now : QDateTime::currentDateTimeUtc(); });
    auto connection = kubernetes.connection();
    if (scenario == "credential_expired") connection.expiresAt = now.addSecs(600);
    require(client.open("one", connection, {}) && client.showSession("one"), "Cannot open real request client");
    require(QTest::qWaitFor([&] { return client.initialSyncComplete("one") && client.containsResource("one", path); }, 10000), "Discovery did not publish target");
    if (scenario == "empty_detail_authority") {
        const auto row = client.resource("one", path);
        const auto authority = client.credentialRevision("one");
        require(authority.has_value(), "Initial credentials are not ready");
        kubernetes.document = {};
        require(client.inspect("one", path), "Cannot request malformed external detail");
        require(QTest::qWaitFor([&] { return client.detailStatus("one", path).contains("Invalid detail response."); }, 10000),
            "Empty HTTP document was not explicitly rejected");
        require(!client.detailRevision("one", path).has_value() && client.document("one", path).isEmpty(),
            "Empty HTTP document granted edit authority");
        require(client.credentialRevision("one") == authority && client.resource("one", path) == row,
            "Malformed detail revoked credentials or replaced the collection row");
        require(kubernetes.writes == 0, "Malformed detail caused a write");
        return true;
    }
    require(client.inspect("one", path) && QTest::qWaitFor([&] { return client.document("one", path) == baseline; }, 10000), "Cannot acquire fresh target through real GET");
    if (deleteCases.contains(scenario)) {
        auto target = client.resource("one", path);
        require(target["deletable"].toBool() == kubernetes.allowDelete, "Discovery did not preserve delete support");
        QString session = "one", targetPath = path, token = "confirmed-delete";
        if (scenario.endsWith("unknown_session")) session = "missing";
        else if (scenario.endsWith("empty_session")) session.clear();
        else if (scenario.endsWith("empty_token")) token.clear();
        else if (scenario == "delete_missing_resource") targetPath += "-missing";
        else if (scenario == "delete_empty_path") targetPath.clear();
        else if (scenario.endsWith("missing_uid")) target.remove("uid");
        else if (scenario == "delete_wrong_uid") target["uid"] = "replacement-uid";
        else if (scenario == "delete_wrong_kind") target["kind"] = "Secret";
        else if (scenario == "delete_wrong_api_version") target["apiVersion"] = "example.invalid/v1";
        else if (scenario == "delete_wrong_name") target["name"] = "other";
        else if (scenario == "delete_wrong_namespace") target["namespace"] = "elsewhere";
        else if (scenario.endsWith("closed")) require(client.close("one"), "Cannot close deletion session");
        else if (scenario.endsWith("inactive"))
            require(client.showSession("two") && client.open("two", kubernetes.connection(), {}), "Cannot hide deletion session");
        else if (scenario == "delete_authentication") {
            kubernetes.rejectAuthentication = true;
            require(client.refresh("one") && QTest::qWaitFor([&] { return client.authenticationRequired("one"); }, 10000), "Cannot observe authentication failure");
        }
        const bool admitted = scenario == "delete_cancel_empty_queue" ? client.cancelDeletion(token)
            : scenario.startsWith("delete_readback_") ? client.readBackDeletion(session, targetPath, token, target)
            : client.deleteResource(session, targetPath, token, target);
        require(!admitted && kubernetes.writes == 0 && kubernetes.document == baseline,
            "Invalid deletion was admitted or changed the external resource");
        return true;
    }
    auto desired = baseline;
    desired["data"] = QJsonObject{{"setting", "after"}};
    const auto preparation = podlord::prepareYaml(podlord::resourceYaml(desired), baseline, 3 * 1048576);
    require(std::holds_alternative<podlord::PreparedYaml>(preparation), "Cannot prepare real guarded patch");
    auto patch = std::get<podlord::PreparedYaml>(preparation).patch;
    auto original = baseline;
    const auto revision = client.detailRevision("one", path);
    require(revision.has_value(), "Fresh detail did not grant current-connection authority");
    if (scenario == "expired_detail_authority" || scenario == "backward_detail_authority") {
        const auto cached = client.detail("one", path);
        require(!cached.isEmpty(), "Fresh detail was not published");
        now = QDateTime::currentDateTimeUtc().addSecs(scenario == "expired_detail_authority" ? 301 : -1);
        controlledClock = true;
        require(!client.detailRevision("one", path).has_value(), "Stale detail granted new edit authority");
        require(client.credentialRevision("one") == revision, "Detail expiry revoked usable credentials");
        require(client.detail("one", path) == cached && client.document("one", path) == baseline,
            "Read-only cached detail disappeared when edit authority expired");
        require(kubernetes.writes == 0 && kubernetes.document == baseline, "Freshness check changed the external resource");
        return true;
    }
    if (scenario == "credential_expired") {
        now = now.addSecs(601);
        controlledClock = true;
        require(!client.credentialRevision("one").has_value(), "Expired credentials granted mutation authority");
    }
    const auto authority = *revision + (scenario == "stale_revision" ? 1 : 0);
    QString session = "one", token = "confirmed-edit";
    QSignalSpy outcomes(&client, &podlord::ResourceClient::yamlApplyFinished);
    require(outcomes.isValid(), "Mutation outcome signal is unavailable");

    if (scenario.endsWith("unknown_session")) session = "missing";
    else if (scenario.endsWith("empty_token")) token.clear();
    else if (scenario == "empty_patch") patch = {};
    else if (scenario == "short_patch") patch.removeLast();
    else if (scenario == "wrong_kind") original["kind"] = "Secret";
    else if (scenario == "wrong_api_version") original["apiVersion"] = "example.invalid/v1";
    else if (QStringList{"wrong_uid", "wrong_name", "wrong_namespace"}.contains(scenario)) {
        auto metadata = original["metadata"].toObject(); metadata[scenario.mid(6)] = "wrong"; original["metadata"] = metadata;
    } else if (scenario == "wrong_uid_guard" || scenario == "wrong_version_guard") {
        const int index = scenario == "wrong_uid_guard" ? 0 : 1;
        auto guard = patch[index].toObject(); guard["value"] = "wrong"; patch[index] = guard;
    } else if (scenario == "reversed_guards") { const QJsonValue first = patch[0]; patch[0] = patch[1]; patch[1] = first; }
    else if (scenario == "oversize_patch") patch.append(QJsonObject{{"op", "add"}, {"path", "/data/large"}, {"value", QString(3 * 1048576, 'x')}});
    else if (scenario.endsWith("closed")) require(client.close("one"), "Cannot close target session");
    else if (scenario.endsWith("inactive")) {
        require(client.showSession("two") && client.open("two", kubernetes.connection(), {}), "Cannot select another session");
    } else if (scenario == "disabled") require(client.enableRequests(false), "Cannot disable requests");
    else if (scenario == "authentication") {
        kubernetes.rejectAuthentication = true;
        require(client.refresh("one") && QTest::qWaitFor([&] { return client.authenticationRequired("one"); }, 10000), "Cannot observe real auth failure");
    }

    if (scenario == "cancel_empty_queue" || scenario == "cancel_read_queue") {
        if (scenario == "cancel_read_queue") require(client.refresh("one"), "Cannot queue collection reads");
        require(!client.cancelYamlApply(token), "Cancellation claimed a nonexistent write");
    } else if (scenario.startsWith("readback_")) {
        const bool valid = scenario == "readback_rejected" || scenario == "readback_uncertain";
        require(client.readBackYaml(session, path, token, original, desired,
            scenario == "readback_rejected" ? "rejected" : "uncertain") == valid, "Read-back admission did not respect session/token boundary");
        if (valid) {
            require(QTest::qWaitFor([&] { return outcomes.count() == 1; }, 10000), "Observational read-back did not finish");
            require(outcomes.first()[3] == (scenario == "readback_rejected" ? "rejected" : "uncertain"), "Unchanged target was misreported as applied");
            require(outcomes.first()[5].toJsonObject() == baseline, "Read-back did not retain the actual original object");
        }
    } else if (scenario.startsWith("queued_") || scenario.startsWith("running_")
               || scenario == "cancel_queued" || scenario == "cancel_running" || scenario == "cancel_wrong_token") {
        kubernetes.hold = true;
        require(client.applyYaml(session, path, token, original, desired, patch, authority), "Valid confirmed patch was not admitted");
        const bool sent = scenario == "running_duplicate" || scenario == "cancel_running";
        if (sent) require(QTest::qWaitFor([&] { return kubernetes.writes == 1; }, 10000), "Confirmed patch was not dispatched");
        if (scenario == "queued_duplicate" || scenario == "running_duplicate")
            require(!client.applyYaml(session, path, "second-edit", original, desired, patch, authority), "Duplicate target write was admitted");
        else if (scenario == "cancel_wrong_token") require(!client.cancelYamlApply("different-edit"), "Unrelated token cancelled target write");
        else require(client.cancelYamlApply(token) == !sent, "Cancellation did not distinguish queued from dispatched writes");
        if (scenario != "cancel_queued") {
            require(QTest::qWaitFor([&] { return kubernetes.held != nullptr; }, 10000) && kubernetes.release(), "Cannot finish dispatched response");
            require(QTest::qWaitFor([&] { return outcomes.count() == 1; }, 10000) && outcomes.first()[3] == "applied", "Dispatched write/read-back did not finish once");
        }
    } else require(!client.applyYaml(session, path, token, original, desired, patch, authority), "Invalid or unavailable write was admitted");

    QTest::qWait(1500);
    const bool wrote = QStringList{"queued_duplicate", "running_duplicate", "cancel_running", "cancel_wrong_token"}.contains(scenario);
    require(kubernetes.writes == (wrote ? 1 : 0), "Rejected/cancelled/observational action sent a write, or a dispatched write was retried");
    require(kubernetes.document == (wrote ? outcomes.first()[5].toJsonObject() : baseline), "Action changed or lost the external resource unexpectedly");
    return true;
}
}

int main(int argc, char* argv[]) {
    const QCoreApplication application(argc, argv);
    try { return require(argc == 2, "Expected one explicit scenario") && run(QString::fromLocal8Bit(argv[1])) ? 0 : 1; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
