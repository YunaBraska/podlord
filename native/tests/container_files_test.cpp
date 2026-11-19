#include "container_files.h"
#include "resource_client.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <QWebSocketServer>
#include <cstdio>

using namespace podlord;
namespace {
bool waitFor(const std::function<bool()>& ready) { return QTest::qWaitFor(ready, 10000); }
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "Files failed at %d: %s\n", __LINE__, #condition); return false; } } while (false)
bool run(const QString& scenario) {
    QTemporaryDir containerRoot;
    REQUIRE(containerRoot.isValid());
    if (scenario == "local_shell") {
        REQUIRE(QDir().mkpath(containerRoot.filePath("sub")));
        QFile file(containerRoot.filePath("hello+world.txt"));
        REQUIRE(file.open(QIODevice::WriteOnly) && file.write("hello from pod\n") == 15);
    }
    QTcpServer api;
    QWebSocketServer streams("Kubernetes files boundary", QWebSocketServer::NonSecureMode);
    streams.setSupportedSubprotocols({"v5.channel.k8s.io"});
    REQUIRE(api.listen(QHostAddress::LocalHost));
    const QString path = "/api/v1/namespaces/default/pods/test-pod";
    const QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"},
        {"metadata", QJsonObject{{"name", "test-pod"}, {"namespace", "default"}, {"uid", "original"}}},
        {"status", QJsonObject{{"phase", "Running"}}},
        {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "main"}}}}}}};
    QJsonArray resources{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"list", "get"}}}};
    if (scenario == "priority") for (int i = 0; i < 40; ++i)
        resources.append(QJsonObject{{"name", QString("extras%1").arg(i)}, {"kind", QString("Extra%1").arg(i)},
            {"namespaced", true}, {"verbs", QJsonArray{"list", "get"}}});
    int targetReads = 0, upgrades = 0;
    QUrl lastUpgrade;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto data = socket->peek(65536);
                if (!data.contains("\r\n\r\n")) return;
                const QUrl url(QString::fromUtf8(data.split('\n').first().split(' ').value(1)));
                if (data.toLower().contains("upgrade: websocket")) {
                    ++upgrades; lastUpgrade = url;
                    QObject::disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);
                    streams.handleConnection(socket); return;
                }
                socket->readAll();
                QJsonObject body;
                int status = 200;
                if (url.path() == "/api") body = {{"versions", QJsonArray{"v1"}}};
                else if (url.path() == "/apis") body = {{"groups", QJsonArray{}}};
                else if (url.path() == "/api/v1") body = {{"resources", resources}};
                else if (url.path() == "/api/v1/pods" || url.path() == "/api/v1/namespaces/default/pods")
                    body = {{"apiVersion", "v1"}, {"kind", "PodList"}, {"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}};
                else if (scenario == "priority" && url.path().startsWith("/api/v1/namespaces/default/extras"))
                    body = {{"apiVersion", "v1"}, {"kind", "Extra" + url.path().section('/', -1).mid(6) + "List"},
                        {"metadata", QJsonObject{}}, {"items", QJsonArray{}}};
                else if (url.path() == path) {
                    ++targetReads; body = pod;
                    if (scenario == "recreated") { auto meta = body["metadata"].toObject(); meta["uid"] = "replacement"; body["metadata"] = meta; }
                    if (scenario == "target_forbidden") status = 403;
                } else status = 404;
                const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QObject::connect(&streams, &QWebSocketServer::newConnection, &streams, [&] {
        auto* peer = streams.nextPendingConnection();
        const auto commands = QUrlQuery(lastUpgrade).allQueryItemValues("command", QUrl::FullyDecoded);
        const bool listing = commands.last() == "/";
        if (scenario == "exec_failure") {
            peer->sendBinaryMessage(QByteArray(1, char(3)) + "{\"status\":\"Failure\",\"reason\":\"NonZeroExitCode\"}");
            peer->sendBinaryMessage(QByteArray(1, char(255)) + QByteArray(1, char(1)));
            peer->sendBinaryMessage(QByteArray(1, char(255)) + QByteArray(1, char(3)));
            QObject::connect(peer, &QWebSocket::disconnected, peer, &QObject::deleteLater);
            return;
        }
        QByteArray output;
        if (scenario == "local_shell") {
            if (listing) output = QByteArray("d\0fixture\0", 10);
            else {
                const auto target = commands.last();
                if (commands.size() != 5 || (target != "/fixture" && !target.startsWith("/fixture/"))) {
                    peer->close(); return;
                }
                QProcess shell;
                shell.start("/bin/sh", {"-c", commands.at(2), "sh", containerRoot.path() + target.mid(8)});
                if (!shell.waitForFinished(3000) || shell.exitCode() != 0) { peer->close(); return; }
                output = shell.readAllStandardOutput();
            }
        } else if (listing) {
            output.append("d", 1); output.append('\0'); output.append("sub", 3); output.append('\0');
            output.append("f", 1); output.append('\0'); output.append("hello.txt", 9); output.append('\0');
        } else output = "hello from pod\n";
        peer->sendBinaryMessage(QByteArray(1, char(1)) + output);
        if (scenario == "status_disconnect") {
            peer->sendBinaryMessage(QByteArray(1, char(3)) + "{\"status\":\"Success\"}");
            peer->close();
        } else {
            peer->sendBinaryMessage(QByteArray(1, char(255)) + QByteArray(1, char(1)));
            peer->sendBinaryMessage(QByteArray(1, char(255)) + QByteArray(1, char(3)));
        }
        QObject::connect(peer, &QWebSocket::disconnected, peer, &QObject::deleteLater);
    });
    ClusterConnection connection;
    connection.server = QUrl(QString("http://127.0.0.1:%1").arg(api.serverPort()));
    connection.authorization = "Bearer local-file-token";
    ResourceClient client;
    REQUIRE(client.open("files", connection, {"default"}));
    REQUIRE(waitFor([&] { return client.containsResource("files", path); }));
    QList<QPair<QString, qint64>> fileStarts;
    QObject::connect(&client, &ResourceClient::requestStarted, &client,
        [&](const QString& id, const QString& requestPath, qint64 at) {
            if (id == "files" && requestPath.startsWith(path)) fileStarts.append({requestPath, at});
        });
    ContainerFiles files(client, "files", path, "main");
    if (scenario == "priority") {
        bool queued = false;
        for (const auto& request : client.requestAudit("files")) {
            const auto row = request.toMap();
            if (row.value("status") == "Queued" && row.value("path").toString().contains("/extras")) queued = true;
        }
        REQUIRE(queued);
    }
    REQUIRE(!files.download("../bad"));
    REQUIRE(!files.download("not-listed"));
    REQUIRE(files.browse());
    REQUIRE(waitFor([&] { return !files.busy(); }));
    REQUIRE(targetReads == 1);
    if (scenario == "priority") {
        REQUIRE(files.status().isEmpty() && upgrades == 1);
        return true;
    }
    if (scenario == "local_shell") {
        REQUIRE(files.entries().size() == 1 && files.entries().first().toMap().value("name") == "fixture");
        REQUIRE(files.browse("fixture") && waitFor([&] { return !files.busy(); }));
        REQUIRE(files.entries().size() == 2 && files.directory() == "/fixture");
        REQUIRE(files.entries().first().toMap().value("name") == "sub");
        REQUIRE(files.download("hello+world.txt") && waitFor([&] { return !files.busy(); }));
        REQUIRE(lastUpgrade.toString(QUrl::FullyEncoded).contains("hello%2Bworld.txt"));
        QTemporaryDir destination; REQUIRE(destination.isValid());
        const auto local = destination.filePath("copied.txt");
        REQUIRE(files.save(QUrl::fromLocalFile(local)));
        QFile copied(local);
        REQUIRE(copied.open(QIODevice::ReadOnly) && copied.readAll() == "hello from pod\n");
        REQUIRE(upgrades == 3 && targetReads == 3);
        return true;
    }
    if (scenario != "recreated" && scenario != "target_forbidden")
        REQUIRE(fileStarts.size() == 2 && fileStarts[0].first == path && fileStarts[1].first == path + "/exec"
            && fileStarts[1].second - fileStarts[0].second >= 400);
    if (scenario == "recreated" || scenario == "target_forbidden") {
        REQUIRE(upgrades == 0 && files.status() == (scenario == "recreated" ? "files.targetChanged" : "files.execFailed"));
        return true;
    }
    if (scenario == "exec_failure") {
        REQUIRE(upgrades == 1 && files.status() == "files.execFailed" && files.entries().isEmpty());
        return true;
    }
    REQUIRE(upgrades == 1 && files.status().isEmpty() && files.entries().size() == 2);
    REQUIRE(files.entries().first().toMap().value("name") == "sub");
    REQUIRE(files.download("hello.txt"));
    REQUIRE(waitFor([&] { return !files.busy(); }));
    REQUIRE(targetReads == 2 && upgrades == 2 && files.downloadName() == "hello.txt");
    const auto commands = QUrlQuery(lastUpgrade).allQueryItemValues("command", QUrl::FullyDecoded);
    REQUIRE(commands.size() == 5 && commands.last() == "/hello.txt" && commands.first() == "/bin/sh");
    QTemporaryDir destination; REQUIRE(destination.isValid());
    const auto local = destination.filePath("copied.txt");
    REQUIRE(files.save(QUrl::fromLocalFile(local)));
    QFile copied(local); REQUIRE(copied.open(QIODevice::ReadOnly) && copied.readAll() == "hello from pod\n");
    REQUIRE(files.refresh());
    REQUIRE(waitFor([&] { return !files.busy(); }));
    REQUIRE(upgrades == 3 && files.entries().size() == 2);
    REQUIRE(client.close("files"));
    REQUIRE(!files.refresh() && files.status() == "files.targetChanged" && upgrades == 3);
    return true;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return run(argc > 1 ? QString::fromUtf8(argv[1]) : QString{}) ? 0 : 1;
}
