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
    int showContextTokens = 0;
    QByteArray submitted;
    QByteArray answer;
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
                if (tags) response = {{"models", QJsonArray{QJsonObject{{"name", "local-test:9b"}, {"size", 7500000000.0}},
                    QJsonObject{{"name", "local-test:10b"}, {"size", 8500000000.0}}}}};
                else if (show) {
                    response = {{"capabilities", QJsonArray{"completion", "thinking"}},
                        {"thinking", QJsonObject{{"values", QJsonArray{"low", "medium", "high"}}, {"default", "high"}}}};
                    if (showContextTokens > 0) response["parameters"] = QString("num_ctx %1").arg(showContextTokens);
                }
                else {
                    ++chats; submitted = bytes->mid(boundary + 4, length);
                    if (hold) { held = socket; return; }
                    response = {{"message", QJsonObject{{"content", malformed ? QString("not json") : answer.isEmpty()
                        ? QString("{\"summary\":\"Der Image-Abruf scheitert.\",\"cause\":\"Ursache unbekannt.\",\"action\":\"Prüfe Registry und Image-Referenz.\"}") : QString::fromUtf8(answer)}}}};
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
    if (name == "capacity") {
        auto snapshot = rows();
        auto selected = snapshot.at(2).toObject(); selected["node"] = "worker-a"; snapshot.replace(2, selected);
        snapshot.append(QJsonObject{{"apiVersion", "v1"}, {"kind", "Node"}, {"name", "worker-a"},
            {"path", "/api/v1/nodes/worker-a"}, {"status", "Ready"}});
        for (int index = 0; index < 100; ++index) {
            const auto name = QString("peer-%1").arg(index);
            snapshot.append(QJsonObject{{"apiVersion", "v1"}, {"kind", "Pod"}, {"name", name},
                {"uid", name}, {"namespace", "team"}, {"path", "/api/v1/namespaces/team/pods/" + name},
                {"node", "worker-a"}, {"status", "Pending"}, {"ready", QString("0/%1").arg(index + 1)}});
        }
        server.showContextTokens = 2400;
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled("context", true), "Small model unavailable");
        ai.observe("context", snapshot, "de");
        const auto path = selected["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", path).isEmpty(); }, 2000), "Small analysis missing");
        const auto small = ai.result("context", path).value("evidence").toList().size();
        server.showContextTokens = 24000;
        require(ai.setModel("local-test:10b") && QTest::qWaitFor([&] { return ai.ready(); }, 2000)
            && ai.refresh("context", path), "Large model unavailable");
        require(QTest::qWaitFor([&] { return server.chats == 2 && !ai.result("context", path).isEmpty(); }, 2000), "Large analysis missing");
        const auto large = ai.result("context", path).value("evidence").toList().size();
        require(small > 0 && large > small && large > 64, "Evidence did not adapt to the model context");
        return;
    }
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
    if (name == "manual_healthy") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b"), "Manual model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Manual model not ready");
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["issue"] = ""; pod["status"] = "Running"; pod["problemSeverity"] = 1; pod["ready"] = "0/1"; pod["restarts"] = 1;
        snapshot.replace(2, pod);
        ai.observe("context", snapshot, "de");
        QTest::qWait(50);
        const auto path = pod["path"].toString();
        require(server.chats == 0 && ai.result("context", path).isEmpty(), "Unapproved automatic analysis ran");
        require(ai.refresh("context", path), "Explicit analysis was rejected");
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty(); }, 2000),
            "Explicit analysis did not appear");
        require(!ai.enabled("context") && server.submitted.contains("Observed signal: Manual review")
            && server.submitted.contains("Resource status: Running")
            && server.submitted.contains("Pod containers ready: 0/1")
            && !server.submitted.contains("private-") && !server.submitted.contains("pod-one"),
            "Manual analysis changed consent or leaked private source data");
        ai.observe("context", snapshot, "de");
        require(server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty(),
            "Unchanged snapshot lost the cached manual result");
        pod["restarts"] = 2; snapshot.replace(2, pod);
        ai.observe("context", snapshot, "de");
        pod["restarts"] = 3; snapshot.replace(2, pod);
        ai.observe("context", snapshot, "de");
        require(server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty(),
            "Repeated restarts invalidated the same manual problem");
        require(!ai.refresh("context", "/unknown"), "Unknown resource was analyzed");
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000),
            "Cached manual model unavailable after restart");
        reopened.observe("context", snapshot, "de");
        reopened.prepareManual("context", path);
        require(!reopened.enabled("context") && !reopened.result("context", path).value("summary").toString().isEmpty()
            && server.chats == 1, "Restart repeated or lost the manual analysis");
        auto changed = snapshot;
        pod["status"] = "Failed"; changed.replace(2, pod);
        reopened.observe("context", changed, "de");
        require(reopened.result("context", path).isEmpty(), "Changed evidence retained a stale manual analysis");
        return;
    }
    if (name == "manual_model_switch") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b") && ai.models().contains("local-test:10b"); }, 2000)
            && ai.setModel("local-test:9b"), "Manual model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Manual model not ready");
        const auto snapshot = rows();
        const auto path = snapshot.at(2).toObject()["path"].toString();
        ai.observe("context", snapshot, "de");
        require(ai.refresh("context", path) && QTest::qWaitFor([&] {
            return !ai.result("context", path).value("summary").toString().isEmpty();
        }, 2000), "First manual answer missing");
        require(ai.setModel("local-test:10b") && ai.result("context", path).value("summary").toString().isEmpty(),
            "Old model answer remained visible after selecting a different model");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "New model not ready");
        ai.prepareManual("context", path);
        require(ai.result("context", path).value("summary").toString().isEmpty(), "Old answer returned for new model");
        require(ai.refresh("context", path) && QTest::qWaitFor([&] { return server.chats == 2; }, 2000),
            "New model did not receive its own manual request");
        require(ai.setProvider("claude") && ai.result("context", path).isEmpty(),
            "Signed-out provider retained a manual answer");
        return;
    }
    if (name == "diagnostic_exec_env") {
        const QString message = "The kubeconfig's authentication settings are invalid. Under users[].user.exec.env, use a list of name/value entries or omit env.";
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b"), "Diagnostic model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Diagnostic model not ready");
        ai.observeDiagnostic(message, "de");
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.diagnosticResult().value("summary").toString().isEmpty(); }, 2000),
            "Diagnostic was not explained");
        require(server.submitted.contains("exec.env") && !server.submitted.contains(message.toUtf8()),
            "Diagnostic sent raw error or lost the safe field code");
        ai.observeDiagnostic(message, "de");
        require(server.chats == 1, "Unchanged diagnostic repeated the request");
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000),
            "Diagnostic model unavailable after restart");
        reopened.observeDiagnostic(message, "de");
        require(server.chats == 1 && !reopened.diagnosticResult().value("summary").toString().isEmpty(),
            "Diagnostic cache was lost after restart");
        require(reopened.setModel("local-test:10b") && reopened.diagnosticResult().isEmpty(),
            "Old diagnostic remained visible after changing models");
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000),
            "Replacement diagnostic model was not ready");
        reopened.observeDiagnostic(message, "de");
        require(QTest::qWaitFor([&] { return server.chats == 2; }, 2000),
            "Changed model reused another model's diagnostic");
        reopened.observeDiagnostic("Other, untrusted error: token=private", "de");
        require(reopened.diagnosticResult().isEmpty() && server.chats == 2, "Unknown diagnostic was transmitted");
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
    if (name == "evidence_graph") {
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["node"] = "worker-a";
        pod["volumeClaims"] = QJsonArray{"claim-data"};
        pod["secretRefs"] = QJsonArray{"runtime-secret"};
        pod["configMapRefs"] = QJsonArray{"runtime-config"};
        snapshot.replace(2, pod);
        auto event = snapshot.at(3).toObject();
        event["name"] = "pull-warning";
        event["path"] = "/api/v1/namespaces/team/events/pull-warning";
        event["eventType"] = "Warning";
        event["eventTargetUid"] = "pod-one";
        event["eventReason"] = "FailedToRetrieveImagePullSecret";
        event["eventMessage"] = "private-event-marker unauthorized token=private-value";
        snapshot.replace(3, event);
        snapshot.append(QJsonObject{{"uid", "encoded-event"}, {"kind", "Event"}, {"name", "encoded-warning"},
            {"namespace", "team"}, {"path", "/api/v1/namespaces/team/events/encoded-warning"},
            {"kubernetesEvent", true}, {"eventType", "Warning"}, {"eventTargetUid", "pod-one"},
            {"eventReason", "eyJhbGciOiJIUzI1NiJ9abcdefghijklmnop"}, {"eventMessage", "token=private-value"}});
        snapshot.append(QJsonObject{{"kind", "Node"}, {"name", "worker-a"}, {"path", "/api/v1/nodes/worker-a"}, {"status", "NotReady"}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolumeClaim"}, {"name", "claim-data"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/persistentvolumeclaims/claim-data"}, {"status", "Pending"}, {"boundVolume", "volume-01"}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolume"}, {"name", "volume-01"},
            {"path", "/api/v1/persistentvolumes/volume-01"}, {"status", "Released"}});
        snapshot.append(QJsonObject{{"kind", "CiliumEndpoint"}, {"name", "pod-one"}, {"namespace", "team"},
            {"path", "/apis/cilium.io/v2/namespaces/team/ciliumendpoints/pod-one"}, {"status", "NotReady"},
            {"ciliumState", "waiting-for-identity"}, {"ciliumHealth", "Failure"}});
        snapshot.append(QJsonObject{{"kind", "Secret"}, {"name", "runtime-secret"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/secrets/runtime-secret"}, {"status", "Ready"},
            {"data", QJsonObject{{"password", "c2VjcmV0LXZhbHVlLXRlc3Q="}}}});
        snapshot.append(QJsonObject{{"kind", "ConfigMap"}, {"name", "runtime-config"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/configmaps/runtime-config"}, {"status", "Ready"}});
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setEnabled("context", true) && ai.setModel("local-test:9b"), "Evidence model unavailable");
        ai.observe("context", snapshot, "de");
        const auto path = pod["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty(); }, 2000),
            "Evidence analysis missing");
        const auto result = ai.result("context", path);
        const auto evidence = result.value("evidence").toList();
        require(evidence.size() >= 8 && !result.contains("command"), "Linked evidence or in-app-only action missing");
        for (const auto& value : evidence) {
            const auto link = value.toMap();
            require(!link.value("id").toString().isEmpty() && !link.value("path").toString().isEmpty(), "Evidence cannot be opened");
            require(server.submitted.contains(link.value("id").toString().toUtf8()), "Evidence ID was omitted from the request");
            require(!server.submitted.contains(link.value("name").toString().toUtf8()), "Resource name reached the model");
        }
        require(server.submitted.contains("CiliumEndpoint") && server.submitted.contains("PersistentVolumeClaim")
            && server.submitted.contains("Pod > PersistentVolumeClaim > PersistentVolume")
            && server.submitted.contains("Secret") && server.submitted.contains("ConfigMap")
            && server.submitted.contains("FailedToRetrieveImagePullSecret") && server.submitted.contains("registry-auth-rejected")
            && server.submitted.contains("waiting-for-identity") && server.submitted.contains("networkHealth=Failure")
            && !server.submitted.contains("private-event-marker") && !server.submitted.contains("private-value")
            && !server.submitted.contains("runtime-secret") && !server.submitted.contains("runtime-config")
            && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q=")
            && !server.submitted.contains("eyJhbGciOiJIUzI1NiJ9abcdefghijklmnop"),
            "Related network, storage, and event facts were not sent safely");
        server.answer = "{\"summary\":\"Der Abruf scheitert.\",\"cause\":\"Mögliche Netzwerkstörung.\",\"action\":\"Prüfe den Node.\",\"refs\":[\"R2\",\"FAKE\"]}";
        require(ai.refresh("context", path) && QTest::qWaitFor([&] { return server.chats == 2
            && ai.result("context", path).value("summary") == "Der Abruf scheitert."; }, 2000), "Explicit cited analysis missing");
        const auto cited = ai.result("context", path).value("evidence").toList();
        require(cited.size() == 1 && cited.first().toMap().value("id") == "R2",
            "Model references were not checked against local evidence");
        return;
    }
    if (name == "node_graph") {
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["node"] = "worker-a";
        pod["volumeClaims"] = QJsonArray{"claim-data"};
        snapshot.replace(2, pod);
        auto event = snapshot.at(3).toObject();
        event["name"] = "mount-warning";
        event["path"] = "/api/v1/namespaces/team/events/mount-warning";
        event["eventType"] = "Warning";
        event["eventReason"] = "FailedMount";
        event["eventTargetUid"] = "pod-one";
        snapshot.replace(3, event);
        const QString nodePath = "/api/v1/nodes/worker-a";
        snapshot.append(QJsonObject{{"uid", "node-uid"}, {"kind", "Node"}, {"name", "worker-a"},
            {"path", nodePath}, {"status", "NotReady"}, {"problemSeverity", 2}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolumeClaim"}, {"name", "claim-data"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/persistentvolumeclaims/claim-data"}, {"status", "Pending"}, {"boundVolume", "volume-01"}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolume"}, {"name", "volume-01"},
            {"path", "/api/v1/persistentvolumes/volume-01"}, {"status", "Released"}});
        snapshot.append(QJsonObject{{"kind", "Pod"}, {"name", "cilium-abc"}, {"namespace", "kube-system"},
            {"path", "/api/v1/namespaces/kube-system/pods/cilium-abc"}, {"node", "worker-a"}, {"status", "Running"}});
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b"), "Node evidence model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Node evidence model not ready");
        ai.observe("context", snapshot, "de");
        require(ai.refresh("context", nodePath) && QTest::qWaitFor([&] { return server.chats == 1
            && !ai.result("context", nodePath).value("summary").toString().isEmpty(); }, 2000), "Node analysis missing");
        require(server.submitted.contains("Pod on node") && server.submitted.contains("Volume claim")
            && server.submitted.contains("Volume") && server.submitted.contains("Cilium agent on node")
            && server.submitted.contains("FailedMount"), "Node investigation lost dependent resources");
        return;
    }
    if (name == "cluster_context") {
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["imagePullSecrets"] = QJsonArray{"registry-auth"};
        pod["image"] = "private.registry.example/team/secret-image:v1";
        snapshot.replace(2, pod);
        auto event = snapshot.at(3).toObject();
        event["eventType"] = "Warning";
        event["eventTargetUid"] = "pod-one";
        event["eventReason"] = "FailedToRetrieveImagePullSecret";
        event["eventCount"] = 50;
        snapshot.replace(3, event);
        snapshot.append(QJsonObject{{"uid", "other-pod"}, {"kind", "Pod"}, {"name", "other-pod"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/pods/other-pod"}, {"issue", "CrashLoopBackOff"},
            {"status", "CrashLoopBackOff"}, {"problemSeverity", 2}});
        {
            podlord::IncidentAi ai(profile, endpoint);
            require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
                && ai.setEnabled("context", true) && ai.setModel("local-test:9b"), "Cluster-context model unavailable");
            ai.observe("context", snapshot, "de");
            require(QTest::qWaitFor([&] { return server.chats == 1
                && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
                "Cluster-context incident was not analyzed");
            require(server.submitted.contains("FailedToRetrieveImagePullSecret")
                && server.submitted.contains("Same namespace problem: Pod CrashLoopBackOff")
                && server.submitted.contains("Image pull Secret reference: not verified in cached resources")
                && !server.submitted.contains("private.registry.example")
                && !server.submitted.contains("registry-auth")
                && !server.submitted.contains("private-event-marker"),
                "Cluster context omitted evidence or sent private names and messages");
            const auto messages = QJsonDocument::fromJson(server.submitted).object()["messages"].toArray();
            const auto instructions = messages.at(0).toObject()["content"].toString();
            const auto input = messages.at(1).toObject()["content"].toString();
            require(instructions.contains("semicolons or long dashes") && input.startsWith("Language: de\n")
                && input.contains("\n- Observed warning event: FailedToRetrieveImagePullSecret")
                && !input.contains("; "), "AI request lost language, writing guidance, or separated evidence");
            snapshot.append(QJsonObject{{"uid", "replica-two"}, {"kind", "Pod"}, {"name", "replica-two"}, {"namespace", "team"},
                {"path", "/api/v1/namespaces/team/pods/replica-two"}, {"issue", "ImagePullBackOff"},
                {"status", "ImagePullBackOff"}, {"problemSeverity", 2}});
            ai.observe("context", snapshot, "de");
            require(server.chats == 1 && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(),
                "A matching replica invalidated the problem cache");
        }
        snapshot.append(QJsonObject{{"uid", "secret-uid"}, {"kind", "Secret"}, {"name", "registry-auth"},
            {"namespace", "team"}, {"data", QJsonObject{{"private", "c2VjcmV0LXZhbHVlLXRlc3Q="}}}});
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.models().contains("local-test:9b"); }, 2000),
            "Cluster-context model unavailable after restart");
        reopened.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 2; }, 2000)
            && server.submitted.contains("Image pull Secret reference: observed in cached resources")
            && !server.submitted.contains("registry-auth") && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q="),
            "A newly observed Secret did not update the redacted context");
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
        require(QTest::qWaitFor([&] { return server.chats == 2
            && !reopened.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000)
            && server.submitted.contains("NEXT_PASSWORD"), "Changed key references reused a stale analysis");
        require(reopened.setEnabled("context", false), "Could not disable automatic analysis");
        reopened.observe("context", snapshot, "de");
        require(reopened.refresh("context", pod["path"].toString())
            && QTest::qWaitFor([&] { return server.chats == 3; }, 2000)
            && server.submitted.contains("NEXT_PASSWORD")
            && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q="),
            "Manual analysis lost screened key evidence");
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
        if (name == "success")
            require(QTest::qWaitFor([&] { return ai.result("context", rows().at(2).toObject()["path"].toString()).value("cause") == "Ursache unbekannt."; }, 2000),
                "Structured cause did not reach the public result");
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
            const auto replacement = ai.result("context", recreated.at(2).toObject()["path"].toString());
            require(!replacement.contains("command") && !replacement.value("evidence").toList().isEmpty()
                && replacement.value("evidence").toList().first().toMap().value("path").toString() == recreated.at(2).toObject()["path"].toString(),
                "Cached evidence points to the previous Pod or an external command");
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
