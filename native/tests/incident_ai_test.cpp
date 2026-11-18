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
        {"status", "ImagePullBackOff"}, {"problemSeverity", 2}, {"message", "API_KEY=sk_test_0123456789_private-status-marker"},
        {"eventMessage", "private-event-marker"}, {"secret", "c2VjcmV0LXZhbHVlLXRlc3Q="},
        {"data", QJsonObject{{"registry-token", "c2VjcmV0LXZhbHVlLXRlc3Q="}}}};
    const QJsonObject event{{"uid", "event-uid"}, {"kind", "Event"}, {"kubernetesEvent", true},
        {"eventReason", "BackOff"}, {"eventMessage", "private-event-marker c2VjcmV0LXZhbHVlLXRlc3Q="}, {"problemSeverity", 2}};
    return {deployment, replica, pod, event};
}
void scenario(const QString& name) {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create temporary profile");
    if (name == "live") {
        podlord::IncidentAi ai(directory.filePath("profile"));
        require(QTest::qWaitFor([&] { return !ai.models().isEmpty(); }, 10000), "Local Ollama model unavailable");
        require(ai.setModel(ai.models().first()), "Cannot select local Ollama model");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().size() > 1; }, 10000), "Local model reasoning unavailable");
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
    if (name == "migration") {
        require(QDir().mkpath(profile), "Cannot prepare legacy profile");
        QFile legacy(QDir(profile).filePath("incident-ai.json"));
        const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"enabledContexts", QJsonArray{"context"}},
            {"model", ""}, {"reasoning", "auto"}, {"records", QJsonArray{}}}).toJson();
        require(legacy.open(QIODevice::WriteOnly) && legacy.write(data) == data.size(), "Cannot prepare legacy consent");
        legacy.close();
        podlord::IncidentAi ai(profile, endpoint);
        require(ai.enabled("context"), "Legacy Ollama consent was not retained");
        require(ai.setProvider("chatgpt") && !ai.enabled("context"), "Local consent authorized ChatGPT analysis");
        require(ai.setProvider("claude") && !ai.enabled("context"), "Local consent authorized Claude analysis");
        require(ai.setProvider("openrouter") && !ai.enabled("context"), "Local consent authorized OpenRouter analysis");
        require(ai.setProvider("ollama") && ai.enabled("context"), "Provider switch lost local consent");
        QFile stored(QDir(profile).filePath("incident-ai.json"));
        require(stored.open(QIODevice::ReadOnly), "Cannot read migrated settings");
        const auto root = QJsonDocument::fromJson(stored.readAll()).object();
        require(root.value("version") == 3 && root.value("enabledByProvider").toObject().value("ollama").toArray().contains("context")
            && root.value("enabledByProvider").toObject().value("chatgpt").toArray().isEmpty(), "Consent migration is invalid");
        return;
    }
    if (name == "model_choice") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000), "Model catalog unavailable");
        require(ai.model().isEmpty(), "A local model was selected without a user decision");
        require(ai.setEnabled("context", true), "Cannot enable model-choice context");
        ai.observe("context", rows(), "de");
        QTest::qWait(50);
        require(server.chats == 0, "Analysis ran before the user chose a model");
        require(ai.setModel("local-test:9b"), "User could not select the installed model");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium") && server.chats == 1; }, 2000),
            "Selected model did not analyze the observed problem");
        return;
    }
    if (name == "related") {
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["node"] = "worker-a"; pod["volumeClaims"] = QJsonArray{"claim-data"};
        pod["secretKeys"] = QJsonArray{"DATABASE_PASSWORD"}; snapshot.replace(2, pod);
        snapshot.append(QJsonObject{{"kind", "Node"}, {"name", "worker-a"}, {"status", "NotReady"}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolumeClaim"}, {"name", "claim-data"}, {"namespace", "team"},
            {"status", "Pending"}, {"boundVolume", "volume-01"}, {"data", "c2VjcmV0LXZhbHVlLXRlc3Q="}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolume"}, {"name", "volume-01"}, {"status", "Released"}});
        {
            podlord::IncidentAi ai(profile, endpoint);
            require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000), "Related-context model unavailable");
            require(ai.setEnabled("context", true) && ai.setModel("local-test:9b"), "Cannot choose related-context model");
            ai.observe("context", snapshot, "de");
            require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
                "Related incident was not analyzed");
            require(server.submitted.contains("NotReady") && server.submitted.contains("Pending") && server.submitted.contains("Released"),
                "Related Node, claim, and volume statuses were omitted");
            require(!server.submitted.contains("worker-a") && !server.submitted.contains("claim-data")
                && !server.submitted.contains("volume-01") && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q="),
                "Related resource names or values reached the model");
            require(!server.submitted.contains("DATABASE_PASSWORD"), "Irrelevant Secret key reached an ImagePullBackOff analysis");
        }
        auto node = snapshot.at(4).toObject(); node["status"] = "Ready"; snapshot.replace(4, node);
        auto claim = snapshot.at(5).toObject(); claim["status"] = "Bound"; snapshot.replace(5, claim);
        auto volume = snapshot.at(6).toObject(); volume["status"] = "Bound"; snapshot.replace(6, volume);
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000), "Related-context model unavailable after restart");
        reopened.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 2; }, 2000), "Changed related status reused the stale answer");
        return;
    }
    if (name == "referenced_keys") {
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["issue"] = "CreateContainerConfigError";
        pod["status"] = "CreateContainerConfigError";
        pod["secretKeys"] = QJsonArray{"DATABASE_PASSWORD", "c2VjcmV0LXZhbHVlLXRlc3Q=", "c2VjcmV0", "sk-private-token"};
        pod["configMapKeys"] = QJsonArray{"SERVER_PORT"};
        snapshot.replace(2, pod);
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setEnabled("context", true) && ai.setModel("local-test:9b"), "Referenced-key model unavailable");
        ai.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 1
            && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
            "Referenced keys did not reach analysis");
        require(server.submitted.contains("DATABASE_PASSWORD") && server.submitted.contains("SERVER_PORT")
            && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q=")
            && !server.submitted.contains("c2VjcmV0")
            && !server.submitted.contains("sk-private-token")
            && !server.submitted.contains("private-status-marker"), "Referenced keys were omitted or private values leaked");
        pod["secretKeys"] = QJsonArray{"NEXT_PASSWORD"};
        snapshot.replace(2, pod);
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000),
            "Referenced-key model unavailable after restart");
        reopened.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 2; }, 2000)
            && server.submitted.contains("NEXT_PASSWORD"), "Changed key references reused a stale analysis");
        return;
    }
    if (name == "events") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000) && ai.setModel("local-test:9b"), "Event test model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Event model capabilities unavailable");
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
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000) && ai.setModel("local-test:9b"), "Fallback model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Fallback model capabilities unavailable");
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
        require(QTest::qWaitFor([&] { return first.models().contains("local-test:9b"); }, 2000) && first.setModel("local-test:9b"), "First model unavailable");
        require(QTest::qWaitFor([&] { return first.reasoningOptions().contains("medium"); }, 2000), "First model capabilities unavailable");
        podlord::IncidentAi stale(profile, endpoint);
        require(QTest::qWaitFor([&] { return stale.reasoningOptions().contains("medium"); }, 2000), "Second model unavailable");
        require(first.setEnabled("context", true), "First writer failed");
        require(!stale.setEnabled("other", true) && !stale.enabled("other"), "Stale writer overwrote context consent");
        return;
    }
    {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000) && ai.setModel("local-test:9b"), "Local model was not discovered or chosen");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Local model capabilities were not discovered");
        ai.observe("context", rows(), "de");
        QTest::qWait(50);
        require(server.chats == 0, "Analysis ran before consent");
        require(ai.setEnabled("context", true), "Cannot enable this context");
        ai.observe("context", rows(), "de");
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000), "Problem did not reach local model");
        require(!server.submitted.contains("private-") && !server.submitted.contains("pod-one")
            && !server.submitted.contains("sk_test_0123456789") && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q=")
            && !server.submitted.contains("registry-token")
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
            if (name == "failure") {
                server.failure = false;
                require(ai.refresh("context", rows().at(2).toObject()["path"].toString()), "Explicit retry was rejected");
                require(QTest::qWaitFor([&] { return server.chats == 2 && !ai.result("context", rows().at(2).toObject()["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
                    "Explicit retry remained blocked by automatic failure cooldown");
            }
            return;
        }
        require(QTest::qWaitFor([&] { return !ai.result("context", rows().at(2).toObject()["path"].toString()).value("summary").toString().isEmpty(); }, 2000), "AI result not shown");
        if (name == "refresh") {
            const auto path = rows().at(2).toObject()["path"].toString();
            require(ai.refresh("context", path), "Cannot refresh the inspected analysis");
            require(ai.result("context", path).value("summary").toString().isEmpty(), "Refresh kept the old answer visible");
            require(QTest::qWaitFor([&] { return server.chats == 2 && !ai.result("context", path).value("summary").toString().isEmpty(); }, 2000),
                "Explicit refresh did not bypass the cached answer and passive request spacing");
            require(!ai.refresh("context", "/unknown"), "Unknown resource was refreshed");
            return;
        }
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
    if (name == "ttl") {
        const auto path = QDir(profile).filePath("incident-ai.json");
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "Cannot inspect persisted cache");
        auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto records = root["records"].toArray();
        require(records.size() == 1, "Expected one cached analysis");
        auto record = records.first().toObject();
        record["created"] = QDateTime::currentDateTimeUtc().addDays(-91).toString(Qt::ISODateWithMs);
        records.replace(0, record); root["records"] = records;
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(QJsonDocument(root).toJson()) > 0, "Cannot age persisted cache");
        file.close();
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000), "Model unavailable after cache expiry");
        reopened.observe("context", rows(), "de");
        const auto target = rows().at(2).toObject()["path"].toString();
        require(reopened.result("context", target).value("summary").toString().isEmpty(), "Expired answer was displayed");
        require(QTest::qWaitFor([&] { return server.chats == 2 && !reopened.result("context", target).value("summary").toString().isEmpty(); }, 2000),
            "Expired answer did not trigger new analysis");
    }
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    try { scenario(QString::fromLocal8Bit(argv[1])); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
