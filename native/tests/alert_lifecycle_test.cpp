#include "alerts.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <cstdio>

namespace {
// Only Kubernetes is replaced; discovery, HTTP, the session cache, rules and evaluation are real.
class Kubernetes final : public QTcpServer {
public:
    int requests = 0;
    int version = 1;
    int restarts = 0;
    int extraPods = 0;
    int peerRestarts = 0;
    QString name = "worker";
    QString phase = "Running";
    QDateTime created;
    explicit Kubernetes(QDateTime now) : created(now.addSecs(-60)) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto input = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, input] {
                    input->append(socket->readAll());
                    if (!input->contains("\r\n\r\n")) return;
                    ++requests;
                    const auto path = QUrl(QString::fromUtf8(input->split(' ').value(1))).path();
                    QJsonObject result;
                    if (path == "/api") result = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") result = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") result = {{"groupVersion", "v1"}, {"resources", QJsonArray{
                        QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else if (path == "/api/v1/pods" || path == "/api/v1/namespaces/default/pods") result = {{"apiVersion", "v1"}, {"kind", "PodList"}, {"metadata", QJsonObject{{"resourceVersion", QString::number(version)}}}, {"items", QJsonArray{
                        QJsonObject{{"apiVersion", "v1"}, {"kind", "Pod"},
                            {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", "worker-uid"},
                                {"resourceVersion", QString::number(version)}, {"creationTimestamp", created.toString(Qt::ISODateWithMs)}}},
                            {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "worker"}, {"image", "local:test"}}}}}},
                            {"status", QJsonObject{{"phase", phase}, {"containerStatuses", QJsonArray{
                                QJsonObject{{"name", "worker"}, {"ready", false}, {"restartCount", restarts}, {"state", QJsonObject{{"running", QJsonObject{}}}}}}}}}}}}};
                    else result = {{"kind", "Status"}, {"status", "Failure"}, {"reason", "NotFound"}, {"code", 404}};
                    if (extraPods && (path == "/api/v1/pods" || path == "/api/v1/namespaces/default/pods")) {
                        auto items = result["items"].toArray();
                        for (int index = 0; index < extraPods; ++index) {
                            auto peer = items.first().toObject();
                            auto metadata = peer["metadata"].toObject();
                            metadata["name"] = QString("peer-%1").arg(index);
                            metadata["uid"] = QString("peer-uid-%1").arg(index);
                            peer["metadata"] = metadata;
                            auto status = peer["status"].toObject();
                            auto container = status["containerStatuses"].toArray().first().toObject();
                            container["restartCount"] = peerRestarts;
                            status["containerStatuses"] = QJsonArray{container};
                            peer["status"] = status;
                            items.append(peer);
                        }
                        result["items"] = items;
                    }
                    const auto body = QJsonDocument(result).toJson(QJsonDocument::Compact);
                    const auto status = result["code"] == 404 ? QByteArray("404 Not Found") : QByteArray("200 OK");
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
};
template<class Predicate> bool waitFor(Predicate predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 10000) QTest::qWait(5);
    return predicate();
}
bool run(const QString& scenario) {
    using namespace podlord;
    struct CriterionCase final { QString field, expression; int count; };
    const QMap<QString, CriterionCase> criteria{
        {"criterion_text_prefix", {"name", "~wor", 1}},
        {"criterion_text_suffix", {"name", "ker~", 1}},
        {"criterion_text_alternative", {"name", "absent worker", 1}},
        {"criterion_text_miss", {"name", "absent", 0}},
        {"criterion_regex_miss", {"name", "/^absent$/", 0}},
        {"criterion_image", {"image", "local:test", 1}},
        {"criterion_problem_true", {"problems", "true", 1}},
        {"criterion_problem_false", {"problems", "false", 0}},
        {"criterion_numeric_gt_miss", {"restarts", ">0", 0}},
        {"criterion_numeric_gte", {"restarts", ">=0", 1}},
        {"criterion_numeric_lte", {"restarts", "<=0", 1}},
        {"criterion_numeric_exact", {"restarts", "=0", 1}},
        {"criterion_multi_exact", {"restarts", "=0 =4", 2}},
        {"criterion_age_minutes", {"age", ">=1m", 1}},
        {"criterion_age_missing", {"age", ">0", 0}},
        {"criterion_p95", {"restarts", "p95", 1}},
        {"criterion_outlier", {"restarts", "outlier", 1}}
    };
    QTemporaryDir profile;
    if (!profile.isValid()) return false;
    auto now = QDateTime::currentDateTimeUtc();
    Kubernetes server(now);
    server.phase = scenario == "duration_survives_unmatch" ? "Running" : "Failed";
    if (scenario == "criterion_p95" || scenario == "criterion_multi_exact") { server.extraPods = 1; server.peerRestarts = 4; }
    if (scenario == "criterion_outlier") { server.restarts = 4; server.extraPods = 5; }
    if (scenario == "criterion_age_missing") server.created = {};
    if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    ResourceClient client(nullptr, [&] { return now; });
    Alerts alerts(profile.path(), &client, nullptr, [&] { return now; });
    QSignalSpy focuses(&alerts, &Alerts::focusRequested);
    if (!waitFor([&] { return !alerts.busy(); }) || !alerts.error().isEmpty()) return false;
    const auto builtins = alerts.rules();
    for (int index = 1; index < builtins.size(); ++index) {
        auto disabled = builtins[index].toMap(); disabled["enabled"] = false;
        if (!alerts.saveRule(disabled) || !waitFor([&] { return !alerts.busy(); }) || !alerts.error().isEmpty()) return false;
    }
    auto rule = alerts.rules().first().toMap();
    rule["id"] = "view-rule"; rule["name"] = "View rule"; rule["builtIn"] = false;
    rule["sound"] = "none"; rule["zoom"] = scenario.endsWith("animation") ? 125 : 0; rule["color"] = "#123456"; rule["animation"] = "pulse";
    const bool view = scenario.startsWith("view_");
    rule["groups"] = QJsonArray{QJsonValue(QJsonArray{QJsonObject{{"field", view ? "newInView" : "name"},
        {"expression", view ? (scenario == "view_false" ? "false" : "true") : "worker"}}})}.toVariantList();
    if (scenario == "duration_survives_unmatch") rule["groups"] = QJsonArray{QJsonValue(QJsonArray{
        QJsonObject{{"field", "status"}, {"expression", "Running"}}})}.toVariantList();
    if (scenario == "criterion_search_regex") rule["groups"] = QJsonArray{QJsonValue(QJsonArray{
        QJsonObject{{"field", "search"}, {"expression", "/worker/"}}})}.toVariantList();
    if (scenario == "criterion_namespace_exact") rule["groups"] = QJsonArray{QJsonValue(QJsonArray{
        QJsonObject{{"field", "namespace"}, {"expression", "\"default\""}}})}.toVariantList();
    if (scenario == "criterion_or_fallback") rule["groups"] = QJsonArray{
        QJsonValue(QJsonArray{QJsonObject{{"field", "status"}, {"expression", "\"Succeeded\""}}}),
        QJsonValue(QJsonArray{QJsonObject{{"field", "name"}, {"expression", "/worker/"}}})}.toVariantList();
    if (scenario == "criterion_restart_limit") rule["groups"] = QJsonArray{QJsonValue(QJsonArray{
        QJsonObject{{"field", "restarts"}, {"expression", "<1"}}})}.toVariantList();
    if (scenario == "recent_change_baseline") rule["groups"] = QJsonArray{QJsonValue(QJsonArray{
        QJsonObject{{"field", "recentlyChanged"}, {"expression", "true"}}})}.toVariantList();
    if (const auto it = criteria.constFind(scenario); it != criteria.cend())
        rule["groups"] = QJsonArray{QJsonValue(QJsonArray{
            QJsonObject{{"field", it->field}, {"expression", it->expression}}})}.toVariantList();
    rule["colorMode"] = view ? "new-in-view" : scenario.startsWith("once_") ? "once" : "duration";
    rule["animationMode"] = rule["colorMode"];
    rule["colorSeconds"] = 2; rule["animationSeconds"] = 2;
    if (!alerts.saveRule(rule) || !waitFor([&] { return !alerts.busy(); }) || !alerts.error().isEmpty()) return false;
    ClusterConnection connection;
    connection.contextId = "local-context";
    connection.server = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
    if (!client.open("session", connection, {"default"}) || !alerts.showSession("session")) return false;
    const QString path = "/api/v1/namespaces/default/pods/worker";
    if (!waitFor([&] { return client.initialSyncComplete("session") && !alerts.matches().isEmpty(); })) {
        std::fprintf(stderr, "Initial cache: requests=%d rows=%lld status=%s alert-error=%s\n", server.requests,
            static_cast<long long>(client.rows("session").size()), qPrintable(client.status("session")), qPrintable(alerts.error()));
        return false;
    }
    if (!alerts.setVisibleResources("session", {path})) return false;
    QTest::qWait(30);
    const auto countForRule = [&] {
        for (const auto& entry : alerts.matches()) if (entry.toMap()["id"] == "view-rule") return entry.toMap()["count"].toInt();
        return 0;
    };
    const auto effect = [&] { return alerts.effect(path); };
    const int requests = server.requests;
    if (const auto it = criteria.constFind(scenario); it != criteria.cend())
        return waitFor([&] { return countForRule() == it->count; }) && server.requests == requests;
    if (scenario == "recent_change_baseline") {
        if (countForRule() != 0 || effect()["color"] == "#123456") return false;
        ++server.version; ++server.restarts;
        now = now.addMSecs(1000);
        if (!client.refresh("session") || !waitFor([&] { return !client.syncLoading("session")
                && client.rows("session").first().toObject()["restarts"].toInt() == 1; })) return false;
        return waitFor([&] { return countForRule() == 1 && effect()["color"] == "#123456"; });
    }
    if (scenario == "criterion_search_regex" || scenario == "criterion_namespace_exact" || scenario == "criterion_or_fallback")
        return countForRule() == 1 && server.requests == requests;
    if (scenario == "criterion_restart_limit") {
        if (countForRule() != 1) return false;
        ++server.version; ++server.restarts;
        if (!client.refresh("session") || !waitFor([&] { return !client.syncLoading("session") && client.rows("session").first().toObject()["restarts"].toInt() == 1; })) return false;
        return waitFor([&] { return countForRule() == 0; });
    }
    if (scenario == "view_invalid") return !alerts.setVisibleResources("missing", {path}) && server.requests == requests;
    if (scenario == "session_close") return alerts.closeSession("session") && !alerts.setVisibleResources("session", {path}) && effect().isEmpty();
    if (view) {
        if (scenario == "view_false") return countForRule() == 0 && effect()["color"] != "#123456" && server.requests == requests;
        if (countForRule() != 1) return false;
        if (!alerts.setVisibleResources("session", {}) || !waitFor([&] { return effect()["color"] != "#123456"; })) return false;
        if (scenario == "view_hidden" || scenario == "view_global") return countForRule() == 1 && server.requests == requests;
        if (!alerts.setVisibleResources("session", {path}) || !waitFor([&] { return effect()["color"] == "#123456" && effect()["animation"] == "pulse"; })) return false;
        if (scenario == "view_entry") return countForRule() == 1 && server.requests == requests;
        if (scenario == "view_repeat") {
            now = now.addMSecs(1500);
            if (!alerts.setVisibleResources("session", {path})) return false;
        }
        now = now.addSecs(3);
        QTest::qWait(2100);
        if (!waitFor([&] { return effect()["color"] != "#123456" && effect()["animation"] != "pulse"; })) return false;
        if (scenario == "view_reentry") {
            if (!alerts.setVisibleResources("session", {}) || !alerts.setVisibleResources("session", {path})) return false;
            return waitFor([&] { return effect()["color"] == "#123456"; }) && server.requests == requests;
        }
        return (scenario == "view_expiry" || scenario == "view_repeat") && countForRule() == 1 && server.requests == requests;
    }
    if (!waitFor([&] { return effect()["color"] == "#123456"; })) return false;
    ++server.version; ++server.restarts;
    now = now.addMSecs(1000);
    if (!client.refresh("session") || !waitFor([&] { return !client.syncLoading("session") && client.rows("session").first().toObject()["restarts"].toInt() == 1; })) return false;
    if (!waitFor([&] { return effect()["animation"] == "pulse"; })) return false;
    if (scenario == "transfer_hold") {
        Alerts target(profile.path(), &client, nullptr, [&] { return now; });
        QSignalSpy targetFocuses(&target, &Alerts::focusRequested);
        if (!waitFor([&] { return !target.busy(); }) || !target.error().isEmpty()
            || !target.showSession("session") || !alerts.transferSession("session", target)) return false;
        if (!waitFor([&] { return target.effect(path)["color"] == "#123456"
                && target.effect(path)["animation"] == "pulse"; })
            || !alerts.effect(path).isEmpty() || !targetFocuses.isEmpty()) return false;
        now = now.addSecs(3); QTest::qWait(2100);
        return waitFor([&] { return target.effect(path)["color"] != "#123456"
            && target.effect(path)["animation"] != "pulse"; }) && targetFocuses.isEmpty();
    }
    if (scenario == "duration_survives_unmatch") {
        server.phase = "Succeeded"; ++server.version;
        if (!client.refresh("session") || !waitFor([&] { return !client.syncLoading("session"); })) return false;
        if (!waitFor([&] { return countForRule() == 0 && effect()["color"] == "#123456"; })) return false;
        now = now.addMSecs(1100); QTest::qWait(2100);
        return waitFor([&] { return effect()["color"] != "#123456"; });
    }
    now = now.addMSecs(scenario.startsWith("once_") ? 400 : 1100);
    QTest::qWait(2100);
    const auto action = scenario.endsWith("animation") ? QString("animation") : QString("color");
    if (action == "animation") {
        // Animation starts after the silent initial baseline; another status change must not extend it.
        focuses.clear();
        ++server.version; ++server.restarts;
        if (!client.refresh("session") || !waitFor([&] { return !client.syncLoading("session") && client.rows("session").first().toObject()["restarts"].toInt() == 2; })) return false;
        if (!waitFor([&] { return !focuses.isEmpty() && focuses.last().value(2).toInt() == 125; })) return false;
        now = now.addSecs(1); QTest::qWait(2100);
    }
    return waitFor([&] { return !effect().contains(action) || effect()[action] != (action == "color" ? "#123456" : "pulse"); });
}
}
int main(int argc, char** argv) {
    const QGuiApplication application(argc, argv);
    if (argc == 2 && run(QString::fromLocal8Bit(argv[1]))) return 0;
    std::fprintf(stderr, "Alert lifecycle behavior failed: %s\n", argc == 2 ? argv[1] : "missing scenario");
    return 1;
}
