#include "workspace.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

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
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
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
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
        }
    });
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
