#include "resource_client.h"
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool accepted, const char* message) {
    if (!accepted) throw std::runtime_error(message);
}

QJsonObject pod(const QString& name, const QString& phase, const QString& created, int restarts = 0) {
    QJsonObject status{{"phase", phase}};
    if (phase == "Running") status["containerStatuses"] = QJsonArray{QJsonObject{{"restartCount", restarts}, {"ready", true}}};
    return {{"apiVersion", "v1"}, {"kind", "Pod"},
        {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", name},
            {"resourceVersion", "1"}, {"creationTimestamp", created}}},
        {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "main"}}}}}}, {"status", status}};
}

QJsonObject event(const QString& name, const QString& at) {
    return {{"apiVersion", "v1"}, {"kind", "Event"},
        {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", name}, {"resourceVersion", "1"}}},
        {"type", "Warning"}, {"reason", "BackOff"}, {"eventTime", at},
        {"involvedObject", QJsonObject{{"kind", "Pod"}, {"name", "running-3"}, {"uid", "running-3"}}}};
}

QJsonObject collection(const QJsonArray& items) {
    return {{"metadata", QJsonObject{{"resourceVersion", "1"}}}, {"items", items}};
}

struct Kubernetes final {
    QTcpServer server;
    Kubernetes() {
        require(server.listen(QHostAddress::LocalHost), "Cannot bind local Kubernetes boundary");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, &server, [socket, bytes = QByteArray{}]() mutable {
                    bytes += socket->readAll();
                    if (!bytes.contains("\r\n\r\n")) return;
                    const auto request = bytes.left(bytes.indexOf("\r\n")).split(' ');
                    const auto route = request.size() >= 2 ? QUrl(QString::fromLatin1(request[1])).path() : QString{};
                    QJsonObject body;
                    int status = 200;
                    if (route == "/api") body = {{"versions", QJsonArray{"v1"}}};
                    else if (route == "/apis") body = {{"groups", QJsonArray{QJsonObject{
                        {"preferredVersion", QJsonObject{{"groupVersion", "batch/v1"}}}}}}};
                    else if (route == "/api/v1") body = {{"resources", QJsonArray{
                        QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}},
                        QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}}}}};
                    else if (route == "/api/v1/pods") body = collection(QJsonArray{
                        pod("running-0", "Running", "2026-10-15T11:40:00Z"),
                        pod("running-1", "Running", "2026-10-15T11:40:00Z"),
                        pod("running-2", "Running", "2026-10-15T11:40:00Z"),
                        pod("running-3", "Running", "2026-10-15T11:40:00Z", 8),
                        pod("pending-new", "Pending", "2026-10-15T11:59:00Z"),
                        pod("pending-old", "Pending", "2026-10-15T11:40:00Z")});
                    else if (route == "/api/v1/events") body = collection(QJsonArray{
                        event("warning-new", "2026-10-15T11:59:00Z"),
                        event("warning-old", "2026-10-15T11:00:00Z")});
                    else if (route == "/apis/batch/v1") body = {{"resources", QJsonArray{QJsonObject{
                        {"name", "cronjobs"}, {"kind", "CronJob"}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}}}}};
                    else if (route == "/apis/batch/v1/cronjobs") body = collection(QJsonArray{QJsonObject{
                            {"apiVersion", "batch/v1"}, {"kind", "CronJob"},
                            {"metadata", QJsonObject{{"name", "nightly"}, {"namespace", "default"},
                                {"uid", "cron-uid"}, {"resourceVersion", "1"}}},
                            {"spec", QJsonObject{{"suspend", true}}}}});
                    else { status = 404; body = {{"kind", "Status"}, {"reason", "NotFound"}, {"code", 404}}; }
                    const auto content = QJsonDocument(body).toJson(QJsonDocument::Compact);
                    socket->write((status == 200 ? QByteArray("HTTP/1.1 200 OK\r\n") : QByteArray("HTTP/1.1 404 Not Found\r\n"))
                        + "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(content.size())
                        + "\r\nConnection: close\r\n\r\n" + content);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    podlord::ClusterConnection connection() const {
        podlord::ClusterConnection result;
        result.contextId = "projection"; result.credentialId = "projection";
        result.server = QUrl("http://127.0.0.1:" + QString::number(server.serverPort()));
        return result;
    }
};

QJsonObject row(const QJsonArray& rows, const QString& name) {
    for (const auto& value : rows) if (value.toObject()["name"] == name) return value.toObject();
    throw std::runtime_error("Expected Kubernetes resource was not published");
}

void suspendedCronJob(const QJsonArray& rows) {
    const auto value = row(rows, "nightly");
    require(value["kind"] == "CronJob" && value["status"] == "Suspended", "Suspended CronJob status differs");
    require(!value["problems"].toBool() && value["problemSeverity"].toInt() == 0 && !value["activity"].toBool(),
        "Suspended CronJob health differs");
}

void restartOutlier(const QJsonArray& rows) {
    const auto ordinary = row(rows, "running-0");
    const auto outlier = row(rows, "running-3");
    require(!ordinary["problems"].toBool() && ordinary["activity"].toBool(), "Ordinary running Pod health differs");
    require(outlier["problems"].toBool() && outlier["issue"].toString().contains("Restart outlier")
        && outlier["problemSeverity"].toInt() == 1, "Restart outlier was not marked as a warning");
}

void pendingGrace(const QJsonArray& rows) {
    const auto value = row(rows, "pending-new");
    require(!value["problems"].toBool() && value["activity"].toBool(), "New Pending Pod lost its startup grace period");
}

void expiredPendingGrace(const QJsonArray& rows) {
    const auto value = row(rows, "pending-old");
    require(value["problems"].toBool() && value["problemSeverity"].toInt() == 1
        && value["issue"].toString().contains("Pending"), "Old Pending Pod was not marked as a warning");
}

void recentWarning(const QJsonArray& rows) {
    const auto value = row(rows, "warning-new");
    require(value["problems"].toBool() && value["activity"].toBool() && value["problemSeverity"].toInt() == 1,
        "Recent Warning event was not active and yellow");
}

void expiredWarning(const QJsonArray& rows) {
    const auto value = row(rows, "warning-old");
    require(!value["problems"].toBool() && !value["activity"].toBool(), "Expired Warning event stayed active");
}
}

int main(int argc, char* argv[]) {
    const QCoreApplication application(argc, argv);
    try {
        require(argc == 2, "Expected one explicit health scenario");
        const auto scenario = QString::fromLocal8Bit(argv[1]);
        Kubernetes kubernetes;
        const auto now = QDateTime::fromString("2026-10-15T12:00:00Z", Qt::ISODate);
        podlord::ResourceClient client(nullptr, [now] { return now; });
        require(client.open("projection", kubernetes.connection(), {}), "Cannot open Kubernetes request client");
        require(QTest::qWaitFor([&] { return client.initialSyncComplete("projection") && client.rows("projection").size() == 9; }, 10000),
            "Kubernetes health scenarios did not reach the public cache");
        const auto rows = client.rows("projection");
        if (scenario == "cronjob_suspended") suspendedCronJob(rows);
        else if (scenario == "restart_outlier") restartOutlier(rows);
        else if (scenario == "pending_grace") pendingGrace(rows);
        else if (scenario == "pending_grace_expired") expiredPendingGrace(rows);
        else if (scenario == "warning_recent") recentWarning(rows);
        else if (scenario == "warning_expired") expiredWarning(rows);
        else throw std::invalid_argument("Unknown health scenario");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
