#include "incident_ai.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
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
        require(ai.setEnabled(true), "Cannot enable local analysis");
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
    server.hold = name == "cancel" || name == "close" || name == "resolve" || name == "status_isolated";
    server.failure = name == "failure";
    server.malformed = name == "malformed";
    if (name == "auto_backlog") {
        server.hold = true;
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true)
            && QTest::qWaitFor([&] { return ai.ready(); }, 2000), "Automatic model unavailable");
        QJsonArray snapshot;
        for (int index = 0; index < 40; ++index) {
            const auto name = QString("failed-%1").arg(index);
            snapshot.append(QJsonObject{{"kind", "Pod"}, {"name", name}, {"namespace", "team"},
                {"path", "/api/v1/namespaces/team/pods/" + name}, {"uid", name},
                {"status", "ImagePullBackOff"}, {"issue", "ImagePullBackOff"}, {"problemSeverity", 2}});
        }
        ai.observe("context", snapshot, "en");
        const auto lastPath = snapshot.last().toObject()["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000)
            && ai.result("context", lastPath).value("pending").toBool(),
            "A later distinct problem was discarded instead of queued for automatic analysis");
        return;
    }
    if (name == "auto_context_churn") {
        server.answer = R"({"summary":"Node worker-a is Ready.","cause":"The image failure is unexplained.","action":"Check the Pod events."})";
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true), "Automatic model unavailable");
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["node"] = "worker-a";
        snapshot.replace(2, pod);
        snapshot.append(QJsonObject{{"kind", "Node"}, {"name", "worker-a"}, {"uid", "node-a"},
            {"path", "/api/v1/nodes/worker-a"}, {"status", "Ready"}});
        ai.observe("context", snapshot, "en");
        const auto path = pod["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1
            && !ai.result("context", path).value("summary").toString().isEmpty(); }, 2000),
            "Initial problem was not analyzed");
        snapshot.append(QJsonObject{{"kind", "Pod"}, {"name", "healthy-neighbor"}, {"uid", "healthy-neighbor"},
            {"namespace", "team"}, {"path", "/api/v1/namespaces/team/pods/healthy-neighbor"},
            {"node", "worker-a"}, {"status", "Running"}, {"problemSeverity", 0}});
        ai.observe("context", snapshot, "en");
        require(server.chats == 1 && ai.result("context", path).value("summary").toString().contains("worker-a"),
            "A healthy neighbor invalidated the problem cache");
        return;
    }
    if (name == "auto_global") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true), "Automatic model unavailable");
        ai.observe("second", rows(), "en");
        require(ai.enabled()
            && QTest::qWaitFor([&] { return server.chats == 1; }, 2000),
            "Global Auto did not analyze a different imported context");
        podlord::IncidentAi reopened(profile, endpoint);
        require(reopened.enabled() && reopened.setProvider("chatgpt") && reopened.enabled(),
            "Global Auto changed when the selected provider changed");
        return;
    }
    if (name == "auto_toggle") {
        server.hold = true;
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true)
            && QTest::qWaitFor([&] { return ai.ready(); }, 2000), "Automatic model unavailable");
        ai.observe("first", rows(), "en");
        ai.observe("second", rows(), "en");
        const auto path = rows().at(2).toObject()["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000)
            && ai.result("second", path).value("pending").toBool(), "Second context was not queued");
        require(ai.setEnabled(false) && ai.result("second", path).isEmpty(), "Auto opt-out left a queued result visible");
        require(ai.setEnabled(true) && ai.result("second", path).value("pending").toBool(),
            "Re-enabling Auto did not reconsider another open context");
        return;
    }
    if (name == "auto_timeout") {
        server.hold = true;
        podlord::IncidentAi ai(profile, endpoint, nullptr, {}, nullptr, 120);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true)
            && QTest::qWaitFor([&] { return ai.ready(); }, 2000), "Automatic model unavailable");
        ai.observe("context", rows(), "en");
        const auto path = rows().at(2).toObject()["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000), "Automatic request did not start");
        require(QTest::qWaitFor([&] { return ai.result("context", path).value("failed").toBool(); }, 500),
            "An unresponsive provider left the resource analyzing indefinitely");
        return;
    }
    if (name == "large_observation_close") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true)
            && QTest::qWaitFor([&] { return ai.ready(); }, 2000), "Local model unavailable");
        auto snapshot = rows();
        for (int index = 0; index < 8000; ++index) {
            const auto resource = QString("config-%1").arg(index);
            snapshot.append(QJsonObject{{"kind", "ConfigMap"}, {"name", resource}, {"namespace", "team"},
                {"path", "/api/v1/namespaces/team/configmaps/" + resource}, {"uid", resource}});
        }
        auto* pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int maximum = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        auto held = QtConcurrent::run([&] { entered.release(); release.acquire(); });
        const bool started = entered.tryAcquire(1, 1000);
        bool preparing = false;
        if (started) {
            ai.observe("context", snapshot, "en");
            preparing = ai.preparing("context");
            ai.retainContexts({});
        }
        release.release(); held.waitForFinished(); pool->setMaxThreadCount(maximum);
        require(started && preparing, "Large observation did not start in the background");
        QTest::qWait(300);
        require(!ai.preparing("context") && server.chats == 0
            && ai.result("context", "/api/v1/namespaces/team/pods/pod-one").isEmpty(),
            "A closed context was analyzed after its background observation finished");
        return;
    }
    if (name == "large_dynamic_update") {
        server.hold = true;
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true)
            && QTest::qWaitFor([&] { return ai.ready(); }, 2000), "Local model unavailable");
        auto snapshot = rows();
        for (int index = 0; index < 20; ++index) {
            const auto name = QString("failed-%1").arg(index);
            snapshot.append(QJsonObject{{"kind", "Pod"}, {"name", name}, {"namespace", "team"},
                {"path", "/api/v1/namespaces/team/pods/" + name}, {"uid", name},
                {"status", "ImagePullBackOff"}, {"issue", "ImagePullBackOff"}, {"problemSeverity", 2}});
        }
        for (int index = 0; index < 8000; ++index) {
            const auto name = QString("config-%1").arg(index);
            snapshot.append(QJsonObject{{"kind", "ConfigMap"}, {"name", name}, {"namespace", "team"},
                {"path", "/api/v1/namespaces/team/configmaps/" + name}, {"uid", name}});
        }
        QElapsedTimer clock; clock.start();
        qint64 lastTick = 0, longestGap = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &ai, [&] {
            const auto tick = clock.elapsed();
            longestGap = std::max(longestGap, tick - lastTick);
            lastTick = tick;
        });
        heartbeat.start();
        QTest::qWait(50);
        longestGap = 0; lastTick = clock.elapsed();
        QElapsedTimer initial; initial.start();
        ai.observe("context", snapshot, "en");
        std::printf("AI initial observation for %lld resources: %lld ms\n",
            static_cast<long long>(snapshot.size()), static_cast<long long>(initial.elapsed()));
        require(initial.elapsed() < 40 && QTest::qWaitFor([&] { return !ai.preparing("context"); }, 5000),
            "Large AI snapshot blocked the caller or did not finish");
        QTest::qWait(50);
        heartbeat.stop();
        std::printf("AI first observation, longest UI event gap: %lld ms\n", static_cast<long long>(longestGap));
        require(longestGap < 80, "AI observation blocked the UI event loop");
        QSignalSpy changed(&ai, &podlord::IncidentAi::changed);
        ai.prepareManual("context", "/api/v1/namespaces/team/pods/failed-0");
        require(changed.isEmpty(), "An automatic incident was prepared again as a manual review");
        QElapsedTimer repeated; repeated.start();
        ai.observe("context", snapshot, "en");
        std::printf("AI repeated observation for %lld resources: %lld ms\n",
            static_cast<long long>(snapshot.size()), static_cast<long long>(repeated.elapsed()));
        require(changed.isEmpty(), "An identical snapshot repeated incident analysis");
        auto pod = snapshot.at(2).toObject();
        pod["restarts"] = 1;
        pod["cpu"] = 100;
        pod["metricAt"] = QDateTime::currentDateTimeUtc().addSecs(-5).toString(Qt::ISODateWithMs);
        snapshot.replace(2, pod);
        ai.observe("context", snapshot, "en");
        require(QTest::qWaitFor([&] { return !ai.preparing("context"); }, 5000), "First metric update did not finish");
        changed.clear();
        pod["restarts"] = 2;
        pod["cpu"] = 120;
        pod["metricAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        snapshot.replace(2, pod);
        QElapsedTimer timer; timer.start();
        ai.observe("context", snapshot, "en");
        std::printf("AI dynamic update for %lld resources: %lld ms\n",
            static_cast<long long>(snapshot.size()), static_cast<long long>(timer.elapsed()));
        require(timer.elapsed() < 40 && QTest::qWaitFor([&] { return !ai.preparing("context"); }, 5000),
            "Large AI metric update blocked the caller or did not finish");
        require(changed.isEmpty(), "A counter or metric update recomputed unchanged incident evidence");
        pod["issue"] = "CrashLoopBackOff";
        snapshot.replace(2, pod);
        ai.observe("context", snapshot, "en");
        require(QTest::qWaitFor([&] { return !ai.preparing("context") && !changed.isEmpty(); }, 5000),
            "A changed incident was not reconsidered");
        const auto path = snapshot.at(2).toObject()["path"].toString();
        ai.observe("context", snapshot, "en", true);
        require(ai.preparing("context") && ai.refresh("context", path)
            && QTest::qWaitFor([&] { return !ai.preparing("context"); }, 5000),
            "Manual refresh did not wait for the current observation");
        return;
    }
    if (name == "uuid_node_name") {
        const QString nodeName = "12345678-1234-1234-1234-123456789abc-vs284-worker-9g67q-tbw98";
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["node"] = nodeName;
        snapshot.replace(2, pod);
        snapshot.append(QJsonObject{{"kind", "Node"}, {"name", nodeName},
            {"path", "/api/v1/nodes/" + nodeName}, {"status", "Ready"}});
        server.answer = QJsonDocument(QJsonObject{
            {"summary", "The assigned Node " + nodeName + " is Ready."},
            {"cause", "An unrelated identifier 87654321-4321-4321-4321-cba987654321 is unverified."},
            {"action", "Inspect the Node."}}).toJson(QJsonDocument::Compact);
        const auto path = pod["path"].toString();
        {
            podlord::IncidentAi ai(profile, endpoint);
            require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
                && ai.setModel("local-test:9b") && ai.setEnabled(true),
                "Node-name test model unavailable");
            require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000),
                "Node-name test model not ready");
            ai.observe("context", snapshot, "en");
            require(QTest::qWaitFor([&] {
                return server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty();
            }, 2000), "Node-name analysis missing");
            const auto result = ai.result("context", path);
            require(server.submitted.contains(nodeName.toUtf8())
                && result.value("summary").toString().contains("Node " + nodeName)
                && result.value("summaryHtml").toString().contains("href=\"R")
                && !result.value("summary").toString().contains("[ID]")
                && result.value("cause").toString().contains("[ID]"),
                "A known Node name was cut apart by UUID redaction");
        }
        QFile cache(QDir(profile).filePath("incident-ai.json"));
        require(cache.open(QIODevice::ReadOnly), "Cannot read cached analysis");
        auto saved = QJsonDocument::fromJson(cache.readAll()).object();
        cache.close();
        auto records = saved["records"].toArray();
        require(records.size() == 1, "Expected one cached analysis");
        auto record = records.first().toObject();
        auto answer = record["answer"].toObject();
        answer["summary"] = QString("The assigned Node [ID]-vs284-worker-9g67q-tbw98 is Ready.");
        record["answer"] = answer;
        records.replace(0, record);
        saved["records"] = records;
        const auto bytes = QJsonDocument(saved).toJson();
        require(cache.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && cache.write(bytes) == bytes.size(), "Cannot prepare legacy cached answer");
        cache.close();
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.reasoningOptions().contains("medium"); }, 2000),
            "Cached Node-name model unavailable");
        reopened.observe("context", snapshot, "en");
        const auto cached = reopened.result("context", path);
        require(server.chats == 1 && cached.value("summary").toString().contains("Node " + nodeName)
            && cached.value("summaryHtml").toString().contains("href=\"R"),
            "Existing cached answer still shows a broken Node name or sent another AI request");
        return;
    }
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
            && ai.setModel("local-test:9b") && ai.setEnabled(true), "Small model unavailable");
        ai.observe("context", snapshot, "de");
        const auto path = selected["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", path).isEmpty(); }, 2000), "Small analysis missing");
        const auto submittedResources = [&] {
            const auto messages = QJsonDocument::fromJson(server.submitted).object()["messages"].toArray();
            return QJsonDocument::fromJson(messages.at(1).toObject()["content"].toString().toUtf8()).object()["resources"].toArray().size();
        };
        const auto small = submittedResources();
        server.showContextTokens = 24000;
        require(ai.setModel("local-test:10b") && QTest::qWaitFor([&] { return ai.ready(); }, 2000)
            && ai.refresh("context", path), "Large model unavailable");
        require(QTest::qWaitFor([&] { return server.chats == 2 && !ai.result("context", path).isEmpty(); }, 2000), "Large analysis missing");
        const auto large = submittedResources();
        require(small > 0 && large > small && large > 20,
            qPrintable(QString("Evidence did not adapt to the model context: small=%1 large=%2").arg(small).arg(large)));
        return;
    }
    if (name == "invalid") {
        require(QDir().mkpath(profile), "Cannot prepare profile");
        QFile invalid(QDir(profile).filePath("incident-ai.json"));
        require(invalid.open(QIODevice::WriteOnly) && invalid.write("invalid") == 7, "Cannot prepare invalid settings");
        invalid.close();
        podlord::IncidentAi ai(profile, endpoint);
        require(!ai.setEnabled(true) && !ai.enabled(), "Invalid settings enabled analysis");
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
        require(!ai.enabled(), "Legacy consent silently covered resource-name disclosure");
        require(ai.setProvider("chatgpt") && !ai.enabled(), "Local consent authorized ChatGPT analysis");
        require(ai.setProvider("claude") && !ai.enabled(), "Local consent authorized Claude analysis");
        require(ai.setProvider("openrouter") && !ai.enabled(), "Local consent authorized OpenRouter analysis");
        require(ai.setProvider("ollama") && !ai.enabled() && ai.setEnabled(true),
            "Fresh consent could not be recorded");
        QFile stored(QDir(profile).filePath("incident-ai.json"));
        require(stored.open(QIODevice::ReadOnly), "Cannot read migrated settings");
        const auto root = QJsonDocument::fromJson(stored.readAll()).object();
        require(root.value("version") == 5 && root.value("autoEnabled") == true, "Consent migration is invalid");
        return;
    }
    if (name == "consent_upgrade") {
        require(QDir().mkpath(profile), "Cannot prepare previous profile");
        QFile previous(QDir(profile).filePath("incident-ai.json"));
        const auto data = QJsonDocument(QJsonObject{{"version", 3}, {"enabledByProvider", QJsonObject{{"ollama", QJsonArray{"context"}},
            {"chatgpt", QJsonArray{"context"}}}}, {"provider", "ollama"}, {"model", "local-test:9b"},
            {"reasoning", "auto"}, {"records", QJsonArray{}}}).toJson();
        require(previous.open(QIODevice::WriteOnly) && previous.write(data) == data.size(), "Cannot prepare previous consent");
        previous.close();
        podlord::IncidentAi ai(profile, endpoint);
        require(ai.model() == "local-test:9b" && !ai.enabled() && ai.setProvider("chatgpt") && !ai.enabled(),
            "Existing automatic consent was reused after widening model input");
        require(ai.setProvider("ollama") && ai.setEnabled(true), "Fresh automatic consent failed");
        QFile migrated(QDir(profile).filePath("incident-ai.json"));
        require(migrated.open(QIODevice::ReadOnly), "Cannot read upgraded profile");
        const auto root = QJsonDocument::fromJson(migrated.readAll()).object();
        require(root["version"] == 5 && root["autoEnabled"] == true, "Fresh global consent was not stored");
        return;
    }
    if (name == "global_upgrade") {
        require(QDir().mkpath(profile), "Cannot prepare previous profile");
        QFile previous(QDir(profile).filePath("incident-ai.json"));
        const auto data = QJsonDocument(QJsonObject{{"version", 4}, {"enabledByProvider", QJsonObject{{"ollama", QJsonArray{"first"}}}},
            {"provider", "ollama"}, {"model", "local-test:9b"}, {"reasoning", "auto"}, {"records", QJsonArray{}}}).toJson();
        require(previous.open(QIODevice::WriteOnly) && previous.write(data) == data.size(), "Cannot prepare per-context consent");
        previous.close();
        podlord::IncidentAi ai(profile, endpoint);
        require(!ai.enabled() && ai.model() == "local-test:9b", "Per-context consent silently widened to every cluster");
        require(ai.setEnabled(true) && ai.enabled(), "Fresh global consent failed");
        QFile migrated(QDir(profile).filePath("incident-ai.json"));
        require(migrated.open(QIODevice::ReadOnly), "Cannot read global consent");
        const auto root = QJsonDocument::fromJson(migrated.readAll()).object();
        require(root["version"] == 5 && root["autoEnabled"] == true
            && !root.contains("enabledByProvider"), "Old per-context consent remained after migration");
        return;
    }
    if (name == "model_choice") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000), "Model catalog unavailable");
        require(ai.model().isEmpty(), "A local model was selected without a user decision");
        require(ai.setEnabled(true), "Cannot enable model-choice context");
        ai.observe("context", rows(), "de");
        QTest::qWait(50);
        require(server.chats == 0, "Analysis ran before the user chose a model");
        require(ai.setModel("local-test:9b"), "User could not select the installed model");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium") && server.chats == 1; }, 2000),
            "Selected model did not analyze the observed problem");
        return;
    }
    if (name == "auto_generic_warning") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b") && ai.setEnabled(true), "Automatic model unavailable");
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject();
        pod["status"] = "Running";
        pod["issue"] = "Ready 0/1";
        pod["problemSeverity"] = 1;
        pod["ready"] = "0/1";
        snapshot.replace(2, pod);
        ai.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 1
            && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 3000),
            "A consented warning needed a manual refresh");
        require(server.submitted.contains("Ready 0/1"), "Warning evidence was not sent");
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
        pod["lastTerminations"] = QJsonArray{QJsonObject{{"reason", "OOMKilled"}, {"exitCode", 137}}};
        snapshot.replace(2, pod);
        ai.observe("context", snapshot, "de");
        QTest::qWait(50);
        const auto path = pod["path"].toString();
        require(server.chats == 0 && ai.result("context", path).isEmpty(), "Unapproved automatic analysis ran");
        ai.prepareManual("context", path);
        const auto preview = QJsonDocument::fromJson(ai.requestPreview("context", path).toUtf8()).object();
        require(server.chats == 0 && preview["model"] == "local-test:9b" && preview["system"].isString()
            && preview["user"].toObject()["resources"].toArray().first().toObject()["name"] == "pod-one"
            && !ai.requestPreview("context", "/unknown").size(), "Request preview was unavailable before sending");
        require(ai.refresh("context", path), "Explicit analysis was rejected");
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty(); }, 2000),
            "Explicit analysis did not appear");
        const auto manualInput = QJsonDocument::fromJson(QJsonDocument::fromJson(server.submitted).object()["messages"].toArray()
            .at(1).toObject()["content"].toString().toUtf8()).object();
        require(preview["system"] == QJsonDocument::fromJson(server.submitted).object()["messages"].toArray().first().toObject()["content"]
            && preview["user"] == manualInput, "Preview differs from the actual model content");
        require(!ai.enabled() && manualInput["observedSignal"] == "Manual review"
            && manualInput["selectedResource"].toObject()["name"] == "pod-one"
            && server.submitted.contains("Then analyze the selected Kubernetes resource and its linked resources")
            && server.submitted.contains("Do not invent facts or commands")
            && server.submitted.contains("Resource status: Running")
            && server.submitted.contains("Pod containers ready: 0/1")
            && manualInput["resources"].toArray().first().toObject()["restarts"] == "1"
            && server.submitted.contains("OOMKilled (exit 137)")
            && !server.submitted.contains("private-") && server.submitted.contains("pod-one"),
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
        require(!reopened.enabled() && !reopened.result("context", path).value("summary").toString().isEmpty()
            && server.chats == 1, "Restart repeated or lost the manual analysis");
        auto changed = snapshot;
        pod["status"] = "Failed"; changed.replace(2, pod);
        reopened.observe("context", changed, "de");
        require(reopened.result("context", path).isEmpty(), "Changed evidence retained a stale manual analysis");
        return;
    }
    if (name == "draft_error") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setModel("local-test:9b"), "Draft model unavailable");
        require(QTest::qWaitFor([&] { return ai.ready(); }, 2000), "Draft model not ready");
        const auto snapshot = rows();
        const auto path = snapshot.at(2).toObject()["path"].toString();
        ai.observe("context", snapshot, "de");
        ai.setDraftError("context", path, "YAML syntax error at line 4, column 2");
        require(ai.refresh("context", path) && QTest::qWaitFor([&] {
            return server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty();
        }, 2000),
            "Local draft error was not analyzed");
        require(server.submitted.contains("Local YAML draft check: YAML syntax error at line 4, column 2")
            && !server.submitted.contains("private-") && !server.submitted.contains("c2VjcmV0"),
            "Draft diagnosis lost the safe error or leaked resource content");
        ai.setDraftError("context", path, "YAML syntax error at line 4, column 2");
        require(!ai.result("context", path).value("summary").toString().isEmpty() && server.chats == 1,
            "Repeating the same draft check lost its cached answer");
        ai.setDraftError({}, {}, {});
        require(ai.result("context", path).isEmpty(), "Draft analysis remained after the draft changed");
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
        snapshot.append(QJsonObject{{"kind", "Node"}, {"name", "worker-a"}, {"path", "/api/v1/nodes/worker-a"}, {"status", "NotReady"}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolumeClaim"}, {"name", "claim-data"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/persistentvolumeclaims/claim-data"},
            {"status", "Pending"}, {"boundVolume", "volume-01"}, {"data", "c2VjcmV0LXZhbHVlLXRlc3Q="}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolume"}, {"name", "volume-01"}, {"path", "/api/v1/persistentvolumes/volume-01"}, {"status", "Released"}});
        {
            podlord::IncidentAi ai(profile, endpoint);
            require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000), "Related-context model unavailable");
            require(ai.setEnabled(true) && ai.setModel("local-test:9b"), "Cannot choose related-context model");
            ai.observe("context", snapshot, "de");
            require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
                "Related incident was not analyzed");
            require(server.submitted.contains("NotReady") && server.submitted.contains("Pending") && server.submitted.contains("Released"),
                "Related Node, claim, and volume statuses were omitted");
            const auto sent = QJsonDocument::fromJson(server.submitted).object()["messages"].toArray().at(1).toObject()["content"].toString();
            QStringList sentNames;
            for (const auto& value : QJsonDocument::fromJson(sent.toUtf8()).object()["resources"].toArray())
                sentNames.append(value.toObject()["name"].toString());
            require(server.submitted.contains("worker-a") && server.submitted.contains("claim-data")
                && server.submitted.contains("volume-01") && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q="),
                qPrintable(QString("Related resource names were missing or a value reached the model: ") + sentNames.join(", ")));
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
        const QString podUid = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        auto deployment = snapshot.at(0).toObject();
        deployment["apiVersion"] = "apps/v1";
        deployment["path"] = "/apis/apps/v1/namespaces/team/deployments/web";
        deployment["rollout"] = QJsonObject{{"desired", 3}, {"updatedReplicas", 2}, {"readyReplicas", 1},
            {"availableReplicas", 1}, {"generation", 4}, {"observedGeneration", 4}};
        snapshot.replace(0, deployment);
        auto replica = snapshot.at(1).toObject();
        replica["apiVersion"] = "apps/v1";
        replica["path"] = "/apis/apps/v1/namespaces/team/replicasets/web-abc123";
        replica["owners"] = QJsonArray{QJsonObject{{"uid", "deploy-uid"}, {"kind", "Deployment"}, {"apiVersion", "apps/v1"}}};
        snapshot.replace(1, replica);
        auto pod = snapshot.at(2).toObject();
        pod["uid"] = podUid;
        pod["owners"] = QJsonArray{QJsonObject{{"uid", "replica-uid"}, {"kind", "ReplicaSet"}, {"apiVersion", "apps/v1"}}};
        pod["node"] = "worker-a";
        pod["volumeClaims"] = QJsonArray{"claim-data"};
        pod["secretRefs"] = QJsonArray{"runtime-secret"};
        pod["configMapRefs"] = QJsonArray{"runtime-config"};
        pod["restarts"] = 4;
        pod["createdAt"] = "2026-11-19T11:54:00Z";
        pod["qosClass"] = "Burstable";
        pod["containers"] = QJsonArray{"web"};
        pod["image"] = "private.registry.example/team/app:tag";
        pod["metricReferences"] = QJsonObject{{"cpu", QJsonObject{{"limit", 500}, {"limitComplete", true}}},
            {"memory", QJsonObject{{"limit", 268435456}, {"limitComplete", false}}}};
        pod["cpu"] = 500;
        pod["memory"] = 134217728;
        pod["metricAt"] = "2026-11-19T12:07:00Z";
        pod["metricStale"] = false;
        pod["metricComplete"] = QJsonObject{{"cpu", true}, {"memory", true}};
        pod["conditions"] = QJsonArray{QJsonObject{{"type", "PodScheduled"}, {"status", "True"},
            {"reason", "Scheduled"}, {"lastTransitionTime", "2026-11-19T11:54:02Z"}}};
        pod["containerStates"] = QJsonArray{QJsonObject{{"role", "app"}, {"index", 0}, {"name", "web"},
            {"ready", false}, {"restarts", 4}, {"state", "waiting"}, {"reason", "ImagePullBackOff"},
            {"imagePullPolicy", "Always"}, {"image", "private.registry.example/team/app:tag"},
            {"message", "Failed to pull image https://private.registry.example/team/app:tag: HTTP 401 token=private-value"}}};
        pod["lastTerminations"] = QJsonArray{QJsonObject{{"reason", "OOMKilled"}, {"exitCode", 137}},
            QJsonObject{{"reason", "token=private-value"}, {"exitCode", 1}}};
        snapshot.replace(2, pod);
        auto event = snapshot.at(3).toObject();
        event["name"] = "pull-warning";
        event["path"] = "/api/v1/namespaces/team/events/pull-warning";
        event["eventType"] = "Warning";
        event["eventTargetUid"] = podUid;
        event["eventReason"] = "FailedToRetrieveImagePullSecret";
        event["eventReporter"] = "kubelet";
        event["eventAction"] = "Pulling";
        event["eventTargetFieldPath"] = "spec.containers{web}";
        event["eventMessage"] = "Failed to pull image \"private.registry.example/team/app:tag\": HTTP 401 unauthorized token=private-value for runtime-secret AWS_SECRET_ACCESS_KEY=short-secret user@example.org 10.1.2.3 c2VjcmV0LXZhbHVlLXRlc3Q=";
        event["eventFirstTime"] = "2026-11-19T11:55:00Z";
        event["eventLastTime"] = "2026-11-19T12:07:00Z";
        snapshot.replace(3, event);
        snapshot.append(QJsonObject{{"uid", "custom-event"}, {"kind", "Event"}, {"name", "custom-warning"},
            {"namespace", "team"}, {"path", "/api/v1/namespaces/team/events/custom-warning"},
            {"kubernetesEvent", true}, {"eventType", "Warning"}, {"eventTargetUid", podUid},
            {"eventReason", "RegistryPolicyDenied"}, {"eventReporter", "custom-controller"},
            {"eventMessage", "Registry returned HTTP 403 for private.registry.example"}});
        snapshot.append(QJsonObject{{"uid", "encoded-event"}, {"kind", "Event"}, {"name", "encoded-warning"},
            {"namespace", "team"}, {"path", "/api/v1/namespaces/team/events/encoded-warning"},
            {"kubernetesEvent", true}, {"eventType", "Warning"}, {"eventTargetUid", podUid},
            {"eventReason", "eyJhbGciOiJIUzI1NiJ9abcdefghijklmnop"}, {"eventMessage", "token=private-value"}});
        snapshot.append(QJsonObject{{"kind", "Node"}, {"name", "worker-a"}, {"path", "/api/v1/nodes/worker-a"}, {"status", "NotReady"},
            {"apiVersion", "v1"}, {"cpu", 1500}, {"memory", 2147483648.0}, {"metricAt", "2026-11-19T12:07:00Z"},
            {"metricStale", false}, {"metricComplete", QJsonObject{{"cpu", true}, {"memory", true}}},
            {"metricReferences", QJsonObject{{"cpu", QJsonObject{{"capacity", 2000}}},
                {"memory", QJsonObject{{"capacity", 4294967296.0}}}}},
            {"conditions", QJsonArray{QJsonObject{{"type", "DiskPressure"}, {"status", "True"}, {"reason", "KubeletHasDiskPressure"}}}}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolumeClaim"}, {"name", "claim-data"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/persistentvolumeclaims/claim-data"}, {"status", "Pending"}, {"boundVolume", "volume-01"},
            {"accessModes", QJsonArray{"ReadWriteOnce"}}, {"volumeMode", "Filesystem"},
            {"metricReferences", QJsonObject{{"storage", QJsonObject{{"request", 1073741824}}}}}});
        snapshot.append(QJsonObject{{"kind", "PersistentVolume"}, {"name", "volume-01"},
            {"path", "/api/v1/persistentvolumes/volume-01"}, {"status", "Released"}});
        snapshot.append(QJsonObject{{"kind", "CiliumEndpoint"}, {"name", "pod-one"}, {"namespace", "team"},
            {"path", "/apis/cilium.io/v2/namespaces/team/ciliumendpoints/pod-one"}, {"status", "NotReady"},
            {"ciliumState", "waiting-for-identity"}, {"ciliumHealth", "Failure"}});
        snapshot.append(QJsonObject{{"uid", "11111111-2222-4333-8444-555555555555"}, {"kind", "Secret"}, {"name", "runtime-secret"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/secrets/runtime-secret"}, {"status", "Ready"},
            {"secretType", "kubernetes.io/dockerconfigjson"}, {"registryAuthKeyPresent", true},
            {"data", QJsonObject{{"password", "c2VjcmV0LXZhbHVlLXRlc3Q="}}}});
        snapshot.append(QJsonObject{{"kind", "ConfigMap"}, {"name", "runtime-config"}, {"namespace", "team"},
            {"path", "/api/v1/namespaces/team/configmaps/runtime-config"}, {"status", "Ready"}});
        server.answer = R"({"summary":"Der Image-Pull scheitert bei Pod team/pod-one.","cause":"Prüfe Secret team/runtime-secret.","action":"Prüfe Pod team/pod-one."})";
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000)
            && ai.setEnabled(true) && ai.setModel("local-test:9b"), "Evidence model unavailable");
        ai.observe("context", snapshot, "de");
        const auto path = pod["path"].toString();
        require(QTest::qWaitFor([&] { return server.chats == 1 && !ai.result("context", path).value("summary").toString().isEmpty(); }, 2000),
            "Evidence analysis missing");
        const auto userText = QJsonDocument::fromJson(server.submitted).object()["messages"].toArray().at(1).toObject()["content"].toString();
        const auto input = QJsonDocument::fromJson(userText.toUtf8()).object();
        const auto resources = input["resources"].toArray();
        require(input["selectedResource"].toObject()["name"] == "pod-one" && resources.size() >= 8
            && !server.submitted.contains("\"R1\"") && !server.submitted.contains("\"C1\""),
            "Structured resource and Event evidence is missing");
        bool eventLinked = false, secretDescribed = false, containerDescribed = false;
        for (const auto& value : resources) {
            const auto resource = value.toObject();
            require(resource.contains("name") && !resource.contains("path") && !resource.contains("id"),
                "Resource name missing or an internal identifier reached the model");
            if (resource["kind"] == "Event" && resource["reason"] == "FailedToRetrieveImagePullSecret") {
                eventLinked = resource["linkedResource"].toObject()["name"] == "pod-one" && resource["action"] == "Pulling"
                    && resource["containerName"] == "web";
            }
            if (resource["kind"] == "Secret") {
                secretDescribed = resource["uid"] == "11111111-2222-4333-8444-555555555555"
                    && resource["secretType"] == "kubernetes.io/dockerconfigjson" && resource["registryAuthKeyPresent"] == true;
            }
            if (resource["kind"] == "Pod")
                containerDescribed = resource["uid"] == podUid
                    && resource["name"] == "pod-one" && resource["namespace"] == "team"
                    && resource["images"].toArray().contains("private.registry.example/team/app:tag")
                    && resource["containerStates"].toArray().first().toObject()["imagePullPolicy"] == "Always"
                    && resource["containerStates"].toArray().first().toObject()["name"] == "web"
                    && resource["containerStates"].toArray().first().toObject()["image"] == "private.registry.example/team/app:tag"
                    && resource["cpuUsageMilli"] == "500" && resource["cpuLimitReached"] == true
                    && resource["memoryUsageBytes"] == "134217728" && resource["metricStale"] == false;
            if (resource["kind"] == "Node")
                require(resource["cpuUsageMilli"] == "1500" && resource["cpuCapacityMilli"] == "2000"
                    && resource["memoryUsageBytes"] == "2147483648", "Node usage or capacity was omitted");
        }
        require(eventLinked && secretDescribed && containerDescribed, "Critical linked evidence was omitted");
        require(!ai.result("context", path).value("cause").toString().contains("11111111-2222-4333-8444-555555555555")
            && ai.result("context", path).value("cause").toString().contains("Secret team/runtime-secret")
            && !ai.result("context", path).value("cause").toString().contains("[R"),
            "Resource citation did not resolve to its real name");
        require(ai.result("context", path).value("summaryHtml").toString().contains("<a href=\"R1\">Pod team/pod-one</a>")
            && ai.result("context", path).value("causeHtml").toString().contains("Secret team/runtime-secret</a>"),
            qPrintable(QString("Named resources were not linked inside the report: ")
                + ai.result("context", path).value("summaryHtml").toString() + " | "
                + ai.result("context", path).value("causeHtml").toString()));
        const auto references = ai.result("context", path).value("references").toList();
        require(!references.isEmpty() && references.first().toMap().value("id") == "R1"
            && references.first().toMap().value("path") == path, "Model citation did not resolve to the inspected resource");
        bool secretReference = false;
        for (const auto& reference : references)
            if (reference.toMap().value("name") == "runtime-secret"
                && reference.toMap().value("path") == "/api/v1/namespaces/team/secrets/runtime-secret")
                secretReference = true;
        QStringList referenceDetails;
        for (const auto& reference : references) referenceDetails.append(
            reference.toMap().value("id").toString() + " " + reference.toMap().value("path").toString());
        require(references.size() >= 2 && secretReference,
            qPrintable(QString("Cited Secret did not resolve to its local resource: refs=") + referenceDetails.join(", ")));
        require(!ai.result("context", path).contains("evidence"), "Raw evidence reached the inspector");
        auto changedUsage = snapshot;
        auto changedPod = changedUsage.at(2).toObject();
        changedPod["cpu"] = 550;
        changedUsage.replace(2, changedPod);
        ai.observe("context", changedUsage, "de");
        require(server.chats == 1 && ai.result("context", path).contains("summary"),
            "A usage update within the same limit state repeated analysis");
        changedPod["cpu"] = 400;
        changedUsage.replace(2, changedPod);
        ai.observe("context", changedUsage, "de");
        require(!ai.result("context", path).contains("summary"),
            "Crossing the observed CPU limit reused a stale diagnosis");
        ai.observe("context", snapshot, "de");
        for (const auto& field : {"CiliumEndpoint", "PersistentVolumeClaim",
                                  "Secret", "ConfigMap", "FailedToRetrieveImagePullSecret",
                                  "waiting-for-identity", "networkHealth", "Failure", "restarts", "lastTermination", "OOMKilled (exit 137)",
                                  "createdAt", "2026-11-19T11:54:00Z", "qosClass", "Burstable", "PodScheduled", "2026-11-19T11:54:02Z",
                                  "ImagePullBackOff", "HTTP 401", "eventFirst", "2026-11-19T11:55:00Z", "eventLast", "2026-11-19T12:07:00Z",
                                  "RegistryPolicyDenied", "kubelet", "HTTP 403", "DiskPressure", "KubeletHasDiskPressure",
                                  "rollout", "desired", "cpuLimitMilli", "500", "cpuLimitCoverage", "complete",
                                  "memoryLimitBytes", "268435456", "memoryLimitCoverage", "partial",
                                  "ReadWriteOnce", "storageRequestBytes", "1073741824"})
            require(server.submitted.contains(field), qPrintable(QString("Missing evidence: ") + field));
        for (const auto& field : {"private-value", "short-secret", "user@example.org",
                                  "10.1.2.3", "c2VjcmV0LXZhbHVlLXRlc3Q=",
                                  "eyJhbGciOiJIUzI1NiJ9abcdefghijklmnop"})
            require(!server.submitted.contains(field), qPrintable(QString("Leaked evidence: ") + field));
        event["eventLastTime"] = "2026-11-19T12:08:00Z";
        event["eventCount"] = 9;
        snapshot.replace(3, event);
        ai.observe("context", snapshot, "de");
        require(server.chats == 1, "Repeated Event observation caused another automatic analysis");
        event["eventMessage"] = "Manifest unknown: HTTP 404 for private.registry.example";
        snapshot.replace(3, event);
        ai.observe("context", snapshot, "de");
        require(!ai.result("context", path).contains("summary"), "Changed Event cause reused the old analysis");
        server.answer = "{\"summary\":\"Der Abruf scheitert [R999].\",\"cause\":\"Mögliche Netzwerkstörung.\",\"action\":\"Prüfe den Node.\",\"refs\":[\"R2\",\"FAKE\"]}";
        require(ai.refresh("context", path) && QTest::qWaitFor([&] { return server.chats == 2
            && ai.result("context", path).value("summary") == "Der Abruf scheitert."; }, 2000), "Explicit analysis missing");
        require(ai.result("context", path).size() <= 7, "Inspector exposed data beyond the model answer and local links");
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
                && ai.setEnabled(true) && ai.setModel("local-test:9b"), "Cluster-context model unavailable");
            ai.observe("context", snapshot, "de");
            require(QTest::qWaitFor([&] { return server.chats == 1
                && !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
                "Cluster-context incident was not analyzed");
            require(server.submitted.contains("FailedToRetrieveImagePullSecret")
                && server.submitted.contains("Same namespace problem: Pod CrashLoopBackOff")
                && server.submitted.contains("Image pull Secret reference: not verified among observed resources")
                && server.submitted.contains("private.registry.example/team/secret-image:v1")
                && !server.submitted.contains("registry-auth")
                && !server.submitted.contains("private-event-marker"),
                "Cluster context omitted evidence or sent private names and messages");
            const auto messages = QJsonDocument::fromJson(server.submitted).object()["messages"].toArray();
            const auto instructions = messages.at(0).toObject()["content"].toString();
            const auto input = messages.at(1).toObject()["content"].toString();
            const auto inputObject = QJsonDocument::fromJson(input.toUtf8()).object();
            require(instructions.contains("semicolons or long dashes")
                && instructions.contains("Explain what the selected resource does")
                && instructions.contains("likely workload role")
                && instructions.contains("Do not mention snapshots")
                && !instructions.contains("cache", Qt::CaseInsensitive)
                && !input.contains("cache", Qt::CaseInsensitive) && inputObject["language"] == "de"
                && inputObject["clusterObservations"].toArray().contains("Observed warning event: FailedToRetrieveImagePullSecret")
                && inputObject["resources"].isArray(), "AI request lost language, writing guidance, or separated evidence");
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
            && server.submitted.contains("Image pull Secret reference: seen among observed resources")
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
            && ai.setEnabled(true) && ai.setModel("local-test:9b"), "Referenced-key model unavailable");
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
        require(reopened.setEnabled(false), "Could not disable automatic analysis");
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
        require(ai.setEnabled(true), "Cannot enable Event analysis");
        auto snapshot = rows();
        auto pod = snapshot.at(2).toObject(); pod["issue"] = ""; pod["status"] = "Running"; pod["problemSeverity"] = 0; snapshot.replace(2, pod);
        auto event = snapshot.at(3).toObject(); event["eventReason"] = "FailedMount"; event["eventType"] = "Warning";
        event["eventTargetUid"] = "pod-one"; snapshot.replace(3, event);
        ai.observe("context", snapshot, "de");
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000), "Warning Event reason was not analyzed");
        require(server.submitted.contains("FailedMount") && !server.submitted.contains("private-event-marker"), "Event sent free message or lost its reason");
        require(QTest::qWaitFor([&] { return !ai.result("context", pod["path"].toString()).value("summary").toString().isEmpty(); }, 2000)
            && server.submitted.contains("FailedMount"), "Event result missing from target resource");
        event["eventCount"] = 100; snapshot.replace(3, event);
        ai.observe("context", snapshot, "de");
        require(server.chats == 1, "Event retry count caused a new analysis");
        return;
    }
    if (name == "missing_owner") {
        podlord::IncidentAi ai(profile, endpoint);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000) && ai.setModel("local-test:9b"), "Fallback model unavailable");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Fallback model capabilities unavailable");
        require(ai.setEnabled(true), "Cannot enable fallback analysis");
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
        require(first.setEnabled(true), "First writer failed");
        require(!stale.setEnabled(true) && !stale.enabled(), "Stale writer overwrote context consent");
        return;
    }
    if (name == "persist") server.answer = R"({"summary":"Pod team/pod-one hat ein Problem.","cause":"Ursache unbekannt.","action":"Prüfe Pod team/pod-one."})";
    {
        podlord::IncidentAi ai(profile, endpoint);
        QSignalSpy completed(&ai, &podlord::IncidentAi::analysisCompleted);
        require(QTest::qWaitFor([&] { return ai.models().contains("local-test:9b"); }, 2000) && ai.setModel("local-test:9b"), "Local model was not discovered or chosen");
        require(QTest::qWaitFor([&] { return ai.reasoningOptions().contains("medium"); }, 2000), "Local model capabilities were not discovered");
        ai.observe("context", rows(), "de");
        QTest::qWait(50);
        require(server.chats == 0, "Analysis ran before consent");
        require(ai.setEnabled(true), "Cannot enable this context");
        ai.observe("context", rows(), "de");
        require(QTest::qWaitFor([&] { return server.chats == 1; }, 2000), "Problem did not reach local model");
        require(!server.submitted.contains("private-") && server.submitted.contains("pod-one")
            && !server.submitted.contains("sk_test_0123456789") && !server.submitted.contains("c2VjcmV0LXZhbHVlLXRlc3Q=")
            && !server.submitted.contains("registry-token")
            && server.submitted.contains("ImagePullBackOff") && server.submitted.contains("de"), "AI payload exposed untrusted data or omitted evidence");
        require(QJsonDocument::fromJson(server.submitted).object()["think"] == "medium", "Automatic reasoning did not choose the advertised medium level");
        if (name == "status_isolated") {
            const auto path = rows().at(2).toObject()["path"].toString();
            require(ai.result("context", path).value("pending").toBool() && ai.status("context", path).isEmpty()
                && ai.status("context", "/unrelated").isEmpty(),
                "An unrelated resource inherited another analysis status");
            return;
        }
        if (name == "success")
            require(QTest::qWaitFor([&] { return ai.result("context", rows().at(2).toObject()["path"].toString()).value("cause") == "Ursache unbekannt."; }, 2000),
                "Structured cause did not reach the public result");
        if (name == "success")
            require(completed.size() == 1 && completed.first().at(0).toString() == "context"
                && completed.first().at(1).toString() == rows().at(2).toObject()["path"].toString(),
                "Completed analysis did not announce its visible resource once");
        if (name == "cancel") {
            require(ai.setEnabled(false) && ai.result("context", rows().at(2).toObject()["path"].toString()).isEmpty(), "Opt-out left analysis visible");
            QTest::qWait(50);
            require(!ai.enabled() && ai.result("context", rows().at(2).toObject()["path"].toString()).isEmpty(), "In-flight response outlived consent");
            return;
        }
        if (name == "close") {
            ai.retainContexts({});
            QTest::qWait(50);
            require(ai.enabled() && ai.result("context", rows().at(2).toObject()["path"].toString()).isEmpty(), "Closed context retained active work or lost opt-in");
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
            require(completed.isEmpty(), "Failed analysis announced completion");
            require(ai.result("context", rows().at(2).toObject()["path"].toString()).value("failed").toBool(),
                "Failed analysis looked like a temporary empty result");
            ai.observe("context", rows(), "de");
            require(server.chats == 1, "Failed analysis retried without cooldown");
            if (name == "failure") {
                server.failure = false;
                require(ai.refresh("context", rows().at(2).toObject()["path"].toString()), "Explicit retry was rejected");
                require(QTest::qWaitFor([&] { return server.chats == 2 && !ai.result("context", rows().at(2).toObject()["path"].toString()).value("summary").toString().isEmpty(); }, 2000),
                    "Explicit retry remained blocked by automatic failure cooldown");
                require(completed.size() == 1, "Successful retry did not announce completion");
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
            require(replacement.size() <= 7 && !replacement.contains("evidence") && !replacement.contains("command"),
                "Cached analysis exposed internal facts or an external command");
        }
    }
    if (name == "persist") {
        podlord::IncidentAi reopened(profile, endpoint);
        require(QTest::qWaitFor([&] { return reopened.models().contains("local-test:9b") && reopened.reasoningOptions().contains("medium"); }, 2000), "Reopened model catalog unavailable");
        require(reopened.enabled(), "Consent was not restored");
        const auto recreated = rows("pod-two", "/api/v1/namespaces/team/pods/pod-two");
        reopened.observe("context", recreated, "de");
        require(!reopened.result("context", recreated.at(2).toObject()["path"].toString()).value("summary").toString().isEmpty()
            && server.chats == 1, "Restart repeated a cached analysis");
        require(reopened.result("context", recreated.at(2).toObject()["path"].toString()).value("summary").toString().contains("Pod team/pod-two")
            && !reopened.result("context", recreated.at(2).toObject()["path"].toString()).value("summary").toString().contains("pod-one")
            && !reopened.result("context", recreated.at(2).toObject()["path"].toString()).value("action").toString().contains("pod-one"),
            "Cached citation did not resolve against the current Pod name");
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
