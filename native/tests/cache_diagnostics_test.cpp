#include "workspace.h"
#include "resource_client.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <limits>

namespace {
QVariantMap cacheMetric(const podlord::Workspace& workspace) {
    for (const auto& value : workspace.settingsDiagnostics().value("metrics").toList())
        if (value.toMap().value("id") == "cachePayload") return value.toMap();
    return {};
}
bool waitFor(const std::function<bool()>& ready) { return QTest::qWaitFor(ready, 15000); }
bool run(const QString& scenario) {
    QTemporaryDir temporary;
    QTcpServer api;
    if (!temporary.isValid() || !api.listen(QHostAddress::LocalHost)) return false;
    auto reference = QDateTime::currentDateTimeUtc();
    const auto now = reference.toString(Qt::ISODateWithMs);
    const auto object = [&](const QString& kind, const QString& name) {
        return QJsonObject{{"apiVersion", "v1"}, {"kind", kind},
            {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", kind + '-' + name}, {"resourceVersion", "1"}, {"creationTimestamp", now}}}};
    };
    auto pod = object("Pod", "cached-pod");
    pod["spec"] = QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "main"}, {"image", "local-image"}}}}};
    pod["status"] = QJsonObject{{"phase", "Running"}};
    auto config = object("ConfigMap", "cached-config");
    config["data"] = QJsonObject{{"message", scenario == "unicode" ? QString::fromUtf8("Gr\xc3\xbc\xc3\x9f" "e") : "local configuration"}};
    auto secret = object("Secret", "cached-secret");
    const auto secretValue = QByteArray("private-value-not-for-diagnostics").toBase64();
    secret["data"] = QJsonObject{{"token", QString::fromLatin1(secretValue)}};
    const auto podPath = QStringLiteral("/api/v1/namespaces/default/pods/cached-pod");
    const auto configPath = QStringLiteral("/api/v1/namespaces/default/configmaps/cached-config");
    const auto secretPath = QStringLiteral("/api/v1/namespaces/default/secrets/cached-secret");
    bool changed = false;
    int requests = 0;
    QPointer<QTcpSocket> heldList;
    QByteArray heldResponse;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->peek(65536);
                if (!request.contains("\r\n\r\n")) return;
                socket->readAll(); ++requests;
                const auto path = QUrl(QString::fromUtf8(request.split('\n').first().split(' ').value(1))).path();
                QJsonObject response;
                int status = 200;
                QByteArray raw;
                if (path == "/api") response = {{"versions", QJsonArray{"v1"}}};
                else if (path == "/apis") response = {{"groups", QJsonArray{}}};
                else if (path == "/api/v1") {
                    QJsonArray resources;
                    for (const auto& pair : QList<QPair<QString, QString>>{{"pods", "Pod"}, {"configmaps", "ConfigMap"}, {"secrets", "Secret"}})
                        resources.append(QJsonObject{{"name", pair.first}, {"kind", pair.second}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}});
                    response = {{"resources", resources}};
                } else if (path == "/api/v1/pods" || path == "/api/v1/configmaps" || path == "/api/v1/secrets") {
                    const auto kind = path.endsWith("pods") ? "Pod" : path.endsWith("configmaps") ? "ConfigMap" : "Secret";
                    QJsonArray items{kind == QString("Pod") ? pod : kind == QString("ConfigMap") ? config : secret};
                    if (changed && kind == QString("ConfigMap")) items.append(object("ConfigMap", "another-config"));
                    response = {{"apiVersion", "v1"}, {"kind", QString(kind) + "List"},
                        {"metadata", QJsonObject{{"resourceVersion", changed ? "2" : "1"}}}, {"items", items}};
                } else if (path == podPath || path == configPath || path == secretPath) {
                    if (scenario == "rejected") { status = 500; response = {{"kind", "Status"}, {"message", "external read failed"}}; }
                    else response = path == podPath ? pod : path == configPath ? config : secret;
                } else if (path == podPath + "/log") raw = now.toUtf8() + " hello from main\n";
                else { status = 404; response = {{"kind", "Status"}, {"message", "not found"}}; }
                const auto bytes = raw.isEmpty() ? QJsonDocument(response).toJson(QJsonDocument::Compact) : raw;
                const auto reply = "HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n" + bytes;
                if (scenario == "collection_ttl_inflight" && path == "/api/v1/pods") {
                    heldList = socket; heldResponse = reply; return;
                }
                socket->write(reply);
                socket->disconnectFromHost();
            });
        }
    });
    if (scenario.startsWith("collection_ttl_")) {
        podlord::ReadSettings settings;
        if (scenario == "collection_ttl_short" || scenario == "collection_ttl_closed") settings.collectionCacheHours = 1;
        else if (scenario == "collection_ttl_long" || scenario == "collection_ttl_detail" || scenario == "collection_ttl_auth") settings.collectionCacheHours = 48;
        else if (scenario == "collection_ttl_maximum") settings.collectionCacheHours = std::numeric_limits<int>::max();
        else if (scenario != "collection_ttl_default" && scenario != "collection_ttl_shorten" && scenario != "collection_ttl_shorten_closed"
            && scenario != "collection_ttl_lengthen" && scenario != "collection_ttl_invalid" && scenario != "collection_ttl_inflight") return false;
        const auto fetchedAt = reference;
        podlord::ClusterConnection connection;
        connection.server = QUrl(QString("http://127.0.0.1:%1").arg(api.serverPort())); connection.credentialId = "cache-ttl";
        if (scenario == "collection_ttl_auth") connection.expiresAt = reference.addSecs(600);
        podlord::ResourceClient client(nullptr, [&] { return reference; });
        if (!client.configure(settings) || !client.open("cached", connection, {})) return false;
        if (scenario == "collection_ttl_inflight") {
            if (!waitFor([&] { return !heldList.isNull(); })) return false;
            settings.collectionCacheHours = 1;
            if (!client.configure(settings) || !heldList || heldList->write(heldResponse) != heldResponse.size()) return false;
            heldList->disconnectFromHost(); heldList.clear(); heldResponse.clear();
        }
        if (!waitFor([&] { return client.initialSyncComplete("cached") && !client.loading("cached") && client.rows("cached").size() == 3; })) return false;
        const auto populated = client.cacheDiagnostics();
        if (populated.value("collections").toLongLong() != 3 || populated.value("resources").toLongLong() != 3 || populated.value("payloadBytes").toLongLong() <= 0) return false;
        if (scenario == "collection_ttl_detail"
            && (!client.inspect("cached", podPath) || !waitFor([&] { return !client.loading("cached") && !client.document("cached", podPath).isEmpty(); }))) return false;
        if (scenario == "collection_ttl_closed" || scenario == "collection_ttl_shorten_closed") {
            if (!client.close("cached")) return false;
        } else if (!client.showSession({})) return false;
        const auto calls = requests;
        if (scenario == "collection_ttl_invalid") {
            auto invalid = settings; invalid.collectionCacheHours = 0;
            if (client.configure(invalid)) return false;
            invalid.collectionCacheHours = -1;
            if (client.configure(invalid) || client.cacheDiagnostics() != populated) return false;
        }
        if (scenario == "collection_ttl_shorten" || scenario == "collection_ttl_shorten_closed") {
            reference = fetchedAt.addSecs(2 * 60 * 60);
            if (!client.synchronize() || client.rows("cached").size() != 3) return false;
            settings.collectionCacheHours = 1;
            if (!client.configure(settings)) return false;
        } else {
            if (scenario == "collection_ttl_lengthen") {
                reference = fetchedAt.addSecs(23 * 60 * 60);
                if (!client.synchronize() || client.rows("cached").size() != 3) return false;
                settings.collectionCacheHours = 48;
                if (!client.configure(settings)) return false;
            }
            if (scenario == "collection_ttl_detail") {
                reference = fetchedAt.addSecs(300);
                if (!client.synchronize() || client.document("cached", podPath).isEmpty()) return false;
                reference = reference.addMSecs(1);
                if (!client.synchronize() || !client.document("cached", podPath).isEmpty()
                    || client.cacheDiagnostics().value("details").toLongLong() != 0 || client.rows("cached").size() != 3) return false;
            }
            if (scenario == "collection_ttl_auth") {
                reference = fetchedAt.addSecs(600);
                if (!client.synchronize() || !client.authenticationRequired("cached") || client.credentialRevision("cached") || client.rows("cached").size() != 3) return false;
            }
            if (settings.collectionCacheHours > 24) {
                reference = fetchedAt.addSecs(24 * 60 * 60).addMSecs(1);
                if (!client.synchronize() || client.rows("cached").size() != 3) return false;
            }
            reference = fetchedAt.addSecs(static_cast<qint64>(settings.collectionCacheHours) * 60 * 60);
            if (!client.synchronize() || client.rows("cached").size() != 3) return false;
            const auto retained = client.cacheDiagnostics();
            const auto audit = client.requestAudit("cached");
            reference = reference.addMSecs(1);
            if (client.cacheDiagnostics() != retained || client.requestAudit("cached") != audit || requests != calls) return false;
            if (!client.synchronize()) return false;
        }
        const auto expired = client.cacheDiagnostics();
        return client.rows("cached").isEmpty() && !client.containsResource("cached", podPath)
            && expired.value("collections").toLongLong() == 0 && expired.value("resources").toLongLong() == 0
            && expired.value("payloadBytes").toLongLong() == 0 && requests == calls
            && client.synchronize() && client.cacheDiagnostics() == expired && requests == calls;
    }
    podlord::Workspace workspace(temporary.filePath("profile"));
    if (!waitFor([&] { return !workspace.busy(); })) return false;
    const auto empty = cacheMetric(workspace);
    if (empty.isEmpty() || empty.value("value").toLongLong() != 0) {
        std::fprintf(stderr, "Empty cache payload metric is missing or not zero.\n"); return false;
    }
    if (scenario == "empty") {
        workspace.refreshSettingsDiagnostics();
        auto* model = workspace.property("diagnosticTable").value<QAbstractItemModel*>();
        if (!model) return false;
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->index(row, 0).data(Qt::UserRole).toString() == "cachePayload")
                return model->index(row, 1).data().toString() == "0" && requests == 0;
        return false;
    }
    if (scenario == "missing") {
        workspace.importFile(temporary.filePath("missing"));
        return waitFor([&] { return !workspace.busy(); }) && workspace.contexts().isEmpty()
            && !workspace.sourceImportError().isEmpty() && cacheMetric(workspace).value("value").toLongLong() == 0 && requests == 0;
    }
    const auto kube = QByteArray("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:")
        + QByteArray::number(api.serverPort())
        + "\nusers:\n- name: anonymous\n  user: {}\ncontexts:\n- name: one\n  context: {cluster: local, user: anonymous}\n- name: two\n  context: {cluster: local, user: anonymous}\ncurrent-context: one\n";
    QFile file(temporary.filePath("kubeconfig"));
    if (!file.open(QIODevice::WriteOnly) || file.write(kube) != kube.size()) return false;
    file.close();
    if (!workspace.importFile(file.fileName()) || !waitFor([&] { return !workspace.busy() && workspace.contexts().size() == 2; })) return false;
    if (!workspace.openContext(workspace.contexts().first().toMap().value("id").toString())
        || !waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.totalResourceCount() == 3; })) return false;
    const auto before = cacheMetric(workspace);
    const auto bytes = before.value("value").toLongLong();
    if (bytes <= 0 || !before.value("description").toString().contains("3 collections, 3 resources, 0 details")) return false;
    const auto calls = requests;
    if (scenario == "populated") {
        workspace.refreshSettingsDiagnostics();
        auto* model = workspace.property("diagnosticTable").value<QAbstractItemModel*>();
        for (int row = 0; model && row < model->rowCount(); ++row)
            if (model->index(row, 0).data(Qt::UserRole).toString() == "cachePayload")
                return model->index(row, 1).data().toString() == QString::number(bytes) && requests == calls;
        return false;
    }
    if (scenario == "repeat") {
        for (int i = 0; i < 20; ++i) if (cacheMetric(workspace) != before) return false;
        return requests == calls;
    }
    if (scenario == "filtered") return workspace.filter("no-such-resource") && workspace.resourceCount() == 0
        && cacheMetric(workspace) == before && requests == calls;
    if (scenario == "close") return workspace.close(workspace.currentSession()) && waitFor([&] { return !workspace.busy(); })
        && cacheMetric(workspace) == before && requests == calls;
    if (scenario == "refresh") {
        changed = true;
        return workspace.refresh() && waitFor([&] { return !workspace.loading() && workspace.totalResourceCount() == 4; })
            && cacheMetric(workspace).value("value").toLongLong() > bytes
            && cacheMetric(workspace).value("description").toString().contains("3 collections, 4 resources");
    }
    if (scenario == "multiple") {
        const auto first = workspace.currentSession();
        return workspace.openContext(workspace.contexts().last().toMap().value("id").toString())
            && waitFor([&] { return !workspace.busy() && !workspace.loading() && workspace.currentSession() != first && workspace.totalResourceCount() == 3; })
            && cacheMetric(workspace).value("value").toLongLong() > bytes
            && cacheMetric(workspace).value("description").toString().contains("6 collections, 6 resources");
    }
    const auto path = scenario == "secret" ? secretPath : scenario == "unicode" ? configPath : podPath;
    if (!workspace.inspectPath(path) || !waitFor([&] { return !workspace.loading(); })) return false;
    const auto inspected = cacheMetric(workspace);
    if (scenario == "rejected") return inspected == before && workspace.inspectorStatus().contains("500");
    if (!waitFor([&] { return workspace.canEditYaml(); })) return false;
    const auto document = scenario == "secret" ? secret : scenario == "unicode" ? config : pod;
    if (inspected.value("value").toLongLong() - bytes != QJsonDocument(document).toJson(QJsonDocument::Compact).size()
        || !inspected.value("description").toString().contains("1 details")) return false;
    if (scenario == "secret") {
        const auto diagnostics = QJsonDocument::fromVariant(workspace.settingsDiagnostics()).toJson();
        return !diagnostics.contains(secretValue) && !diagnostics.contains("private-value-not-for-diagnostics");
    }
    if (scenario == "logs") return workspace.setInspectorPage("logs")
        && waitFor([&] { return workspace.logRows()->rowCount() == 1 && !workspace.loading(); })
        && cacheMetric(workspace).value("value").toLongLong() > inspected.value("value").toLongLong()
        && cacheMetric(workspace).value("description").toString().contains("1 log histories, 1 log lines");
    return scenario == "detail" || scenario == "unicode";
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const bool passed = argc == 2 && run(QString::fromLocal8Bit(argv[1]));
    if (!passed) std::fprintf(stderr, "Cache diagnostics scenario failed: %s\n", argc == 2 ? argv[1] : "missing");
    return passed ? 0 : 1;
}
