#include "incident_ai.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct OllamaBoundary final : QTcpServer {
    int chats = 0;
    QByteArray submitted;
    bool failure = false, malformed = false, hold = false;
    QTcpSocket* held = nullptr;
    OllamaBoundary() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                bytes->append(socket->readAll());
                const auto boundary = bytes->indexOf("\r\n\r\n");
                if (boundary < 0) return;
                const auto header = bytes->left(boundary).toLower();
                const auto lengthStart = header.indexOf("content-length:");
                const auto length = lengthStart < 0 ? 0 : header.mid(lengthStart + 15).split('\r').first().trimmed().toInt();
                if (bytes->size() < boundary + 4 + length) return;
                const bool tags = bytes->startsWith("GET /api/tags ");
                const bool show = bytes->startsWith("POST /api/show ");
                const bool chat = bytes->startsWith("POST /api/chat ");
                if (!tags && !show && !chat) { socket->disconnectFromHost(); return; }
                QJsonObject response;
                if (tags) response = {{"models", QJsonArray{QJsonObject{{"name", "local-test:9b"}, {"size", 7500000000.0}}}}};
                else if (show) response = {{"capabilities", QJsonArray{"completion", "thinking"}},
                    {"thinking", QJsonObject{{"values", QJsonArray{"low", "medium", "high"}}, {"default", "high"}}}};
                else {
                    ++chats; submitted = bytes->mid(boundary + 4, length);
                    if (hold) { held = socket; return; }
                    response = {{"message", QJsonObject{{"content", malformed ? "not json" : "{\"summary\":\"Der Image-Abruf scheitert.\",\"action\":\"Prüfe Registry und Image-Referenz.\"}"}}}};
                }
                const auto payload = failure && chat ? QByteArray("failure") : QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 " + QByteArray(failure && chat ? "503 Failure" : "200 OK")
                    + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(payload.size())
                    + "\r\nConnection: close\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
        });
    }
};
QJsonArray rows(QString podName = "pod-one", QString path = "/api/v1/namespaces/team/pods/pod-one") {
    const auto replicaUid = podName == "pod-one" ? QString("replica-uid") : QString("replica-uid-two");
    const QJsonObject owner{{"uid", replicaUid}};
    const QJsonObject deployment{{"uid", "deploy-uid"}, {"kind", "Deployment"}, {"name", "web"}, {"namespace", "team"}};
    const QJsonObject replica{{"uid", replicaUid}, {"kind", "ReplicaSet"}, {"name", "web-abc123"},
        {"namespace", "team"}, {"owners", QJsonArray{QJsonObject{{"uid", "deploy-uid"}}}}};
    const QJsonObject pod{{"uid", podName}, {"kind", "Pod"}, {"name", podName}, {"namespace", "team"},
        {"path", path}, {"owners", QJsonArray{owner}}, {"owner", "ReplicaSet/web-abc123"}, {"issue", "ImagePullBackOff, Ready 0/1"},
        {"status", "ImagePullBackOff"}, {"problemSeverity", 2}, {"message", "private-status-marker"},
        {"eventMessage", "private-event-marker"}, {"secret", "private-secret-marker"}};
    const QJsonObject event{{"uid", "event-uid"}, {"kind", "Event"}, {"kubernetesEvent", true},
        {"eventReason", "BackOff"}, {"eventMessage", "private-event-marker"}, {"problemSeverity", 2}};
    return {deployment, replica, pod, event};
}
void scenario(const QString& name) {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create temporary profile");
    if (name == "live") {
        podlord::IncidentAi ai(directory.filePath("profile"));
        require(QTest::qWaitFor([&] { return !ai.models().isEmpty() && ai.reasoningOptions().size() > 1; }, 10000), "Local Ollama model unavailable");
        require(ai.setEnabled("local", true), "Cannot enable local analysis");
        ai.observe("local", rows(), "de");
        const auto path = rows().at(2).toObject()["path"].toString();
        require(QTest::qWaitFor([&] { return !ai.result("local", path).value("summary").toString().isEmpty() || ai.status("local").contains("failed"); }, 180000), "Local model did not finish");
        const auto answer = ai.result("local", path);
        require(!answer.value("summary").toString().isEmpty(), "Local model returned no valid diagnosis");
        std::fprintf(stdout, "%s\n%s\n", qPrintable(answer.value("summary").toString()), qPrintable(answer.value("action").toString()));
        return;
    }
    OllamaBoundary server;
    require(server.listen(QHostAddress::LocalHost, 0), "Cannot start local Ollama boundary");
    const QUrl endpoint(QString("http://127.0.0.1:%1").arg(server.serverPort()));
    const auto profile = directory.filePath("profile");
    server.hold = name == "cancel" || name == "close" || name == "resolve";
    server.failure = name == "failure";
    server.malformed = name == "malformed";
    if (name == "invalid") {
        require(QDir().mkpath(profile), "Cannot prepare profile");
        QFile invalid(QDir(profile).filePath("incident-ai.json"));
        require(invalid.open(QIODevice::WriteOnly) && invalid.write("invalid") == 7, "Cannot prepare invalid settings");
        invalid.close();
        podlord::IncidentAi ai(profile, endpoint);
        require(!ai.setEnabled("context", true) && !ai.enabled("context"), "Invalid settings enabled analysis");
        return;
    }
    if (name == "events") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Event test model unavailable");
        require(ai.setEnabled("context", true), "Cannot enable Event analysis");
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject(); pod["issue"] = ""; pod["status"] = "Running"; pod["problemSeverity"] = 0; snapshot.replace(2, pod);
        auto event = snapshot.at(3).toObject(); event["eventReason"] = "FailedMount"; event["eventType"] = "Warning";
        event["eventTargetUid"] = "pod-one"; snapshot.replace(3, event);
        ai.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000), "Warning Event reason was not analyzed");
        require(server.submitted.contains("FailedMount") && !server.submitted.contains("private-event-marker"), "Event sent free message or lost its reason");
        require(QTest::qWaitFor([&] { return ai.result("context", pod["path"].toString()).value("code") == "FailedMount"; }, 2000), "Event result missing from target resource");
        event["eventCount"] = 100; snapshot.replace(3, event);
        ai.observe("context", snapshot, "de");
        require(server.chats == 1, "Event retry count caused a new analysis");
        return;
    }
    if (name == "missing_owner") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Fallback model unavailable");
        require(ai.setEnabled("context", true), "Cannot enable fallback analysis");
        auto first = rows(); first.removeAt(0); first.removeAt(0);
        ai.observe("context", first, "de");
        require(QTest::qWaitFor([&] { return !ai.result("context", first.at(0).toObject()["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
            "First Pod was not analyzed");
        auto second = rows("pod-two", "/api/v1/namespaces/team/pods/pod-two"); second.removeAt(0); second.removeAt(0);
        ai.observe("context", second, "de");
        require(server.chats == 1 && !ai.result("context", second.at(0).toObject()["path"].toString()).value("summary").toString().isEmpty(),
            "Missing owner rows caused per-Pod analysis during scaling");
        return;
    }
    if (name == "conflict") {
        podlord::IncidentAi first(profile, endpoint);
        require(QTest::qWaitFor([&] { return first.reasoningOptions().contains("medium"); }, 2000), "First model unavailable");
        podlord::IncidentAi stale(profile, endpoint);
        require(QTest::qWaitFor([&] { return stale.reasoningOptions().contains("medium"); }, 2000), "Second model unavailable");
        require(first.setEnabled("context", true), "First writer failed");
        require(!stale.setEnabled("other", true) && !stale.enabled("other"), "Stale writer overwrote context consent");
        return;
    }
    {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b") && ai.reasoningOptions().contains("medium"); }, 2000), "Local model capabilities were not discovered");
        ai.observe("context", rows(), "de");
        QTest::qWait(50);
        require(server.chats == 0, "Analysis ran before consent");
        require(ai.setEnabled("context", true), "Cannot enable this context");
        ai.observe("context", rows(), "de");
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000), "Problem did not reach local model");
        require(!server.submitted.contains("private-") && !server.submitted.contains("pod-one")
            && server.submitted.contains("ImagePullBackOff") && server.submitted.contains("de"), "AI payload exposed untrusted data or omitted evidence");
        require(QJsonDocument::fromJson(server.submitted).object()["think"] == "medium", "Automatic reasoning did not choose the advertised medium level");
        if (name == "cancel") {
            require(ai.setEnabled("context", false) && ai.result("context", rows().at(2).toObject()["path"].toString()).isEmpty(), "Opt-out left analysis visible");
            QTest::qWait(50);
            require(!ai.enabled("context") && ai.result("context", rows().at(2).toObject()["path"].toString()).isEmpty(), "In-flight response outlived consent");
            return;
        }
        if (name == "close") {
            ai.retainContexts({});
            QTest::qWait(50);
            require(ai.enabled("context") && ai.result("context", rows().at(2).toObject()["path"].toString()).isEmpty(), "Closed context retained active work or lost opt-in");
            return;
        }
        if (name == "resolve") {
            auto healthy = rows();
            auto pod = healthy.at(2).toObject(); pod["issue"] = ""; pod["status"] = "Running"; pod["problemSeverity"] = 0;
            healthy.replace(2, pod);
            ai.observe("context", healthy, "de");
            QTest::qWait(50);
            require(ai.result("context", pod["path"].toString()).isEmpty() && ai.status("context").isEmpty(),
                "Resolved problem retained or failed an obsolete request");
            return;
        }
        if (name == "failure" || name == "malformed") {
            require(QTest::qWaitFor([&] { return ai.status("context").contains("failed"); }, 2000), "Failed analysis never settled");
            ai.observe("context", rows(), "de");
            require(server.chats == 1, "Failed analysis retried without cooldown");
            return;
        }
        require(QTest::qWaitFor([&] { return !ai.result("context", rows().at(2).toObject()["path"].toString()).value("summary").toString().isEmpty(); }, 2000), "AI result not shown");
        if (name == "rate") {
            auto changed = rows();
            auto pod = changed.at(2).toObject(); pod["issue"] = "CrashLoopBackOff"; pod["status"] = "CrashLoopBackOff";
            changed.replace(2, pod);
            ai.observe("context", changed, "de");
            QTest::qWait(150);
            require(server.chats == 1 && ai.result("context", pod["path"].toString()).value("pending").toBool(),
                "A second problem bypassed request spacing");
            return;
        }
        if (name == "reasoning") {
            require(ai.setReasoning("low") && ai.reasoning() == "low"
                && ai.result("context", rows().at(2).toObject()["path"].toString()).value("pending").toBool(),
                "Changing reasoning reused the old analysis");
        } else {
            const auto recreated = rows("pod-two", "/api/v1/namespaces/team/pods/pod-two");
            ai.observe("context", recreated, "de");
            require(!ai.result("context", recreated.at(2).toObject()["path"].toString()).value("summary").toString().isEmpty()
                && server.chats == 1, "Pod replacement missed the cached workload analysis");
            require(ai.result("context", recreated.at(2).toObject()["path"].toString()).value("command").toString().contains("pod-two"), "Read-only command targets wrong Pod");
        }
    }
    if (name == "persist") {
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.models().contains("local-test:9b") && reopened.reasoningOptions().contains("medium"); }, 2000), "Reopened model catalog unavailable");
        require(reopened.enabled("context"), "Consent was not restored");
        const auto recreated = rows("pod-two", "/api/v1/namespaces/team/pods/pod-two");
        reopened.observe("context", recreated, "de");
        require(!reopened.result("context", recreated.at(2).toObject()["path"].toString()).value("summary").toString().isEmpty()
            && server.chats == 1, "Restart repeated a cached analysis");
    }
    if (name == "reasoning") {
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("low"); }, 2000), "Reasoning options unavailable after restart");
        require(reopened.reasoning() == "low", "Chosen reasoning was not restored");
        reopened.observe("context", rows(), "de");
        require(QTest::qWaitFor([&] { return server.chats == 2; }, 2000), "New reasoning did not request a distinct analysis");
        require(QJsonDocument::fromJson(server.submitted).object()["think"] == "low", "Chosen reasoning was not sent to Ollama");
    }
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    try { scenario(QString::fromLocal8Bit(argv[1])); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
