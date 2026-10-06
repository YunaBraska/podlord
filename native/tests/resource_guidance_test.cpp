#include "resource_guidance.h"
#include "workspace.h"
#include "ui_input.h"
#include "browser_boundary.h"
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <cstdio>
#include <stdexcept>

namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
QQuickItem* visibleItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name && root->isVisible() && root->width() > 0 && root->height() > 0) return root;
    for (auto* child : root->childItems()) if (auto* found = visibleItem(child, name)) return found;
    return nullptr;
}
QJsonObject document(const QString& mode) {
    QJsonObject status{{"phase", "Running"}, {"message", "diagnostic-secret <b>untrusted</b>"}};
    QJsonObject container{{"name", "app"}, {"ready", false}, {"restartCount", 2}};
    QString reason;
    if (mode == "image_backoff" || mode.startsWith("ui_")) reason = "ImagePullBackOff";
    else if (mode == "image_pull") reason = "ErrImagePull";
    else if (mode == "image_invalid") reason = "InvalidImageName";
    else if (mode == "crash") reason = "CrashLoopBackOff";
    else if (mode == "config_error") reason = "CreateContainerConfigError";
    else if (mode == "create_error") reason = "CreateContainerError";
    else if (mode == "unknown") reason = "NewUnrecognizedReason";
    if (!reason.isEmpty()) container["state"] = QJsonObject{{"waiting", QJsonObject{{"reason", reason}, {"message", "diagnostic-secret"}}}};
    if (mode == "oom") container["state"] = QJsonObject{{"terminated", QJsonObject{{"reason", "OOMKilled"}, {"exitCode", 137}}}};
    if (mode == "previous_oom" || mode == "previous_running") {
        container["lastState"] = QJsonObject{{"terminated", QJsonObject{{"reason", "OOMKilled"}}}};
        if (mode == "previous_running") container["state"] = QJsonObject{{"running", QJsonObject{{"startedAt", "2026-10-05T08:00:00Z"}}}};
    }
    if (mode == "evicted") status["reason"] = "Evicted";
    if (mode == "unschedulable" || mode == "scheduled") status["conditions"] = QJsonArray{QJsonObject{{"type", "PodScheduled"}, {"status", mode == "scheduled" ? "True" : "False"}, {"reason", "Unschedulable"}}};
    if (mode == "initial_container" || mode == "ephemeral_container" || mode == "unnamed_container" || mode == "duplicate" || mode == "bounded" || mode == "multi")
        container["state"] = QJsonObject{{"waiting", QJsonObject{{"reason", "CrashLoopBackOff"}}}};
    if (mode == "unnamed_container") container.remove("name");
    QJsonArray containers{container};
    if (mode == "duplicate") containers.append(container);
    if (mode == "bounded" || mode == "multi") {
        containers = {};
        for (int i = 0; i < (mode == "bounded" ? 200 : 2); ++i) {
            auto next = container; next["name"] = QString("app-%1").arg(i); containers.append(next);
        }
    }
    status[mode == "initial_container" ? "initContainerStatuses" : mode == "ephemeral_container" ? "ephemeralContainerStatuses" : "containerStatuses"] = containers;
    QJsonObject result{{"apiVersion", "v1"}, {"kind", "Pod"},
        {"metadata", QJsonObject{{"name", "diagnostic"}, {"namespace", "test"}, {"uid", "diagnostic-uid"}, {"resourceVersion", "1"}}},
        {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "app"}, {"image", "unavailable.invalid/app:1"}}}}}}, {"status", status}};
    if (mode.startsWith("node_") || mode == "ui_node_actions") {
        result["kind"] = "Node";
        QString type = "Ready";
        if (mode == "node_memory" || mode == "node_pressure_false" || mode == "ui_node_actions") type = "MemoryPressure";
        else if (mode == "node_disk") type = "DiskPressure";
        else if (mode == "node_pid") type = "PIDPressure";
        const QString state = mode == "node_unknown" ? "Unknown" : mode == "node_not_ready" || mode == "node_pressure_false" ? "False" : "True";
        result["status"] = QJsonObject{{"conditions", QJsonArray{QJsonObject{{"type", type}, {"status", state}}}}};
        auto metadata = result["metadata"].toObject(); metadata.remove("namespace"); result["metadata"] = metadata;
        result.remove("spec");
    }
    if (mode.startsWith("pvc_")) {
        result["kind"] = "PersistentVolumeClaim";
        result["status"] = QJsonObject{{"phase", mode == "pvc_pending" ? "Pending" : mode == "pvc_lost" ? "Lost" : "Bound"}};
    }
    if (mode.startsWith("deployment_")) {
        result["kind"] = "Deployment"; result["apiVersion"] = "apps/v1";
        result["status"] = QJsonObject{{"conditions", QJsonArray{QJsonObject{{"type", "Progressing"}, {"status", mode == "deployment_deadline" ? "False" : "True"}, {"reason", "ProgressDeadlineExceeded"}}}}};
    }
    if (mode == "custom_kind") { result["apiVersion"] = "custom.example/v1"; result["status"] = QJsonObject{{"reason", "Evicted"}}; }
    if (mode == "secret") { result["kind"] = "Secret"; result["data"] = QJsonObject{{"password", "diagnostic-secret"}}; }
    if (mode == "missing") result.remove("status");
    if (mode == "malformed") result["status"] = "not an object";
    if (mode == "wrong_status_type") result["status"] = QJsonObject{{"conditions", "invalid"}, {"containerStatuses", QJsonArray{42, "invalid"}}};
    return result;
}
// Only the external Kubernetes HTTP boundary is simulated; cache, inspector and UI are real.
class Boundary final : public QTcpServer {
public:
    QJsonObject pod;
    int requests = 0, writes = 0, logRequests = 0;
    bool failDetail = false;
    explicit Boundary(QJsonObject value) : pod(std::move(value)) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, input = QByteArray{}]() mutable {
                    input += socket->readAll();
                    if (!input.contains("\r\n\r\n")) return;
                    const auto first = input.left(input.indexOf("\r\n")).split(' ');
                    require(first.size() >= 2, "Malformed HTTP request");
                    ++requests; if (first[0] != "GET") ++writes;
                    const auto path = QUrl::fromEncoded(first[1]).path();
                    if (path == "/api/v1/namespaces/test/pods/diagnostic/log") {
                        require(QUrlQuery(QUrl::fromEncoded(first[1])).queryItemValue("container") == "app", "Logs did not select the actual container");
                        ++logRequests;
                        const QByteArray bytes("2026-10-06T09:00:00Z local diagnostic log\n");
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
                        socket->disconnectFromHost(); input.clear(); return;
                    }
                    QJsonObject response;
                    int code = 200;
                    const bool node = pod["kind"] == "Node";
                    const QString collection = node ? "nodes" : "pods";
                    if (path == "/api") response = {{"apiVersion", "v1"}, {"kind", "APIVersions"}, {"versions", QJsonArray{"v1"}}, {"serverAddressByClientCIDRs", QJsonArray{}}};
                    else if (path == "/apis") response = {{"apiVersion", "v1"}, {"kind", "APIGroupList"}, {"groups", QJsonArray{}}};
                    else if (path == "/api/v1") response = {{"apiVersion", "v1"}, {"kind", "APIResourceList"}, {"groupVersion", "v1"}, {"resources", QJsonArray{QJsonObject{{"name", collection}, {"singularName", node ? "node" : "pod"}, {"kind", node ? "Node" : "Pod"}, {"namespaced", !node}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else if (path == "/api/v1/" + collection) response = {{"apiVersion", "v1"}, {"kind", node ? "NodeList" : "PodList"}, {"metadata", QJsonObject{{"resourceVersion", "1"}}}, {"items", QJsonArray{pod}}};
                    else if (path == (node ? "/api/v1/nodes/diagnostic" : "/api/v1/namespaces/test/pods/diagnostic") && !failDetail) response = pod;
                    else { code = failDetail ? 503 : 404; response = {{"kind", "Status"}, {"apiVersion", "v1"}, {"status", "Failure"}, {"code", code}, {"message", "Local boundary unavailable"}}; }
                    const auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK\r\n" : " Failure\r\n")
                        + "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
                    socket->disconnectFromHost();
                    input.clear();
                });
            }
        });
    }
};
bool ui(const QString& mode) {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create private guidance profile");
    Boundary boundary(document(mode));
    require(boundary.listen(QHostAddress::LocalHost, 0), "Cannot start local Kubernetes HTTP boundary");
    const QString profile = directory.filePath("profile");
    const QString yaml = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: local-boundary-token\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n").arg(boundary.serverPort());
    const auto imported = podlord::KubeconfigStore(profile).importText(directory.filePath("origin"), yaml);
    require(std::holds_alternative<podlord::SourceSnapshot>(imported), "Cannot import guidance source");
    const podlord::SessionStore sessions(profile);
    const auto created = sessions.create({std::get<podlord::SourceSnapshot>(imported).contexts.first().id, {}});
    require(std::holds_alternative<podlord::SessionCatalog>(created), "Cannot create guidance session");
    require(std::holds_alternative<podlord::SessionCatalog>(sessions.activate(std::get<podlord::SessionCatalog>(created).sessions.first().id)), "Cannot activate guidance session");
    if (mode == "ui_context_reuse") {
        const auto copy = sessions.create({std::get<podlord::SourceSnapshot>(imported).contexts.first().id, {}}, "Named active copy");
        require(std::holds_alternative<podlord::SessionCatalog>(copy), "Cannot prepare matching active session copy");
        require(std::holds_alternative<podlord::SessionCatalog>(sessions.activate(std::get<podlord::SessionCatalog>(copy).sessions.last().id)), "Cannot activate matching session copy");
    }
    podlord::Workspace workspace(profile);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Cannot create real guidance UI");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window, "Guidance window is unavailable");
    if (mode == "ui_action_narrow") { window->setWidth(680); window->setHeight(680); }
    const auto item = [&](const char* name) -> QQuickItem* {
        if (QString::fromLatin1(name) == "cell_0_0") {
            if (auto* view = visibleItem(window->contentItem(), "resourceTable")) {
                QQmlExpression expression(qmlContext(view), view, "itemAtIndex(model.index(0, 0))");
                if (auto* cell = expression.evaluate().value<QQuickItem*>()) return cell;
            }
            return nullptr;
        }
        return visibleItem(window->contentItem(), QString::fromLatin1(name));
    };
    const auto press = [&](const char* name) {
        auto* target = item(name); require(target && target->isEnabled(), name);
        target->ensurePolished(); QCoreApplication::processEvents();
        require(podlord::test::scrollIntoView(target->window(), target), "Cannot reveal guidance action for real input");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    };
    if (!QTest::qWaitFor([&] { return workspace.table()->rowCount() == 1 && !workspace.busy() && !workspace.loading(); }, 10000)) {
        std::fprintf(stderr, "Discovery: rows=%d; requests=%d; active=%s; status=%s\n", workspace.table()->rowCount(), boundary.requests, qPrintable(workspace.currentSession()), qPrintable(workspace.status()));
        require(false, "Real API discovery/list did not populate the cache");
    }
    press("resourcesWorkspaceButton");
    require(QTest::qWaitFor([&] { return item("cell_0_0") != nullptr; }, 2000), "Resource workspace did not present its cached row");
    press("cell_0_0");
    require(QTest::qWaitFor([&] { return workspace.resourceGuidance().size() == 1 && item("resourceGuidanceToggle"); }, 5000), "Accepted detail did not publish cached guidance");
    const int requests = boundary.requests;
    press("resourceGuidanceToggle");
    if (!QTest::qWaitFor([&] { return item("guidanceTitle_0"); }, 2000)) {
        auto* toggle = item("resourceGuidanceToggle");
        const auto position = toggle ? toggle->mapToScene(QPointF{}) : QPointF{};
        std::fprintf(stderr, "Guidance expansion: checked=%d; x=%g; y=%g; overview=%d; contentY=%g\n", toggle && toggle->property("checked").toBool(), position.x(), position.y(), workspace.property("inspectorPage").toString() == "overview", item("overviewScroll") ? item("overviewScroll")->property("contentY").toDouble() : 0);
        const QString evidence = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR");
        if (!evidence.isEmpty()) require(window->grabWindow().save(evidence + "/guidance-expansion-failure.png"), "Cannot save failed UI frame");
        require(false, "Expanded guidance did not render");
    }
    require(item("guidanceTitle_0")->property("text") == (mode == "ui_node_actions" ? "Node reports MemoryPressure" : "Container image is unavailable"), "Guidance title differs from the observed reason");
    require(!item("guidanceDetail_0")->property("text").toString().contains("diagnostic-secret"), "Guidance UI exposed untrusted status contents");
    const QString evidence = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR");
    if (!evidence.isEmpty()) require(window->grabWindow().save(evidence + "/" + mode + ".png"), "Cannot save actual guidance frame");
    if (mode == "ui_open_events" || mode == "ui_open_yaml" || mode == "ui_action_keyboard" || mode == "ui_actions_restore" || mode == "ui_action_narrow" || mode == "ui_node_actions") {
        if (mode == "ui_node_actions") require(!workspace.podInspected() && !item("guidanceLogs_0"), "Non-Pod diagnostic offers an invalid log action");
        const bool yamlAction = mode == "ui_open_yaml" || mode == "ui_action_keyboard";
        const char* action = yamlAction ? "guidanceYaml_0" : "guidanceEvents_0";
        if (mode == "ui_action_keyboard") {
            auto* target = item(action); require(target && target->isEnabled(), action);
            require(podlord::test::scrollIntoView(target->window(), target), "Cannot reveal keyboard guidance action");
            target->forceActiveFocus(); QTest::keyClick(target->window(), Qt::Key_Space);
        } else press(action);
        require(QTest::qWaitFor([&] { return workspace.property("inspectorPage").toString() == (yamlAction ? "yaml" : "events"); }, 2000), "Guidance action did not open its inspector page");
        require(boundary.requests == requests, "Cached guidance navigation requested Kubernetes");
        if (mode == "ui_actions_restore" || mode == "ui_node_actions" || mode == "ui_action_narrow") {
            press("overviewButton");
            require(QTest::qWaitFor([&] { return item("guidanceTitle_0"); }, 2000), "Returning to Overview lost expanded diagnostics");
            press("guidanceYaml_0");
            require(workspace.property("inspectorPage").toString() == "yaml" && boundary.requests == requests, "Repeated diagnostic navigation changed transport or target");
        }
    } else if (mode == "ui_open_logs") {
        press("guidanceLogs_0");
        require(QTest::qWaitFor([&] { return workspace.logsVisible() && item("logEntry_0"); }, 3000), "Guidance did not open the real log stream");
        require(item("logEntry_0")->property("text").toString().contains("local diagnostic log") && boundary.logRequests == 1, "Log action did not render its single external response");
        press("overviewButton");
        const int settled = boundary.requests;
        QTest::qWait(150);
        require(!workspace.logsVisible() && boundary.requests == settled && item("guidanceTitle_0"), "Leaving logs did not stop polling and restore cached guidance");
    } else if (mode == "ui_docs") {
        BrowserBoundary browser;
        press("guidanceDocs_0");
        require(browser.urls.size() == 1 && browser.urls.first().toString() == workspace.resourceGuidance().first().toMap()["documentation"].toString(), "Documentation action did not hand off its authoritative URL");
        require(boundary.requests == requests && item("guidanceTitle_0"), "Documentation action changed inspector or Kubernetes transport");
    } else if (mode == "ui_docs_failure") {
        press("guidanceDocs_0");
        require(QTest::qWaitFor([&] { return item("guidanceLinkError_0"); }, 2000), "External documentation failure was silent");
        require(boundary.requests == requests && item("guidanceTitle_0"), "Documentation failure discarded diagnostics or requested Kubernetes");
        BrowserBoundary browser;
        press("guidanceDocs_0");
        require(browser.urls.size() == 1 && item("guidanceLinkError_0") == nullptr, "Explicit documentation retry did not clear its error");
    } else if (mode == "ui_refresh") {
        boundary.pod = document("crash");
        press("refreshInspector");
        require(QTest::qWaitFor([&] { return workspace.resourceGuidance().first().toMap()["code"] == "CrashLoopBackOff"; }, 5000), "Fresh detail did not replace cached guidance");
    } else if (mode == "ui_failure") {
        boundary.failDetail = true;
        press("refreshInspector");
        require(QTest::qWaitFor([&] { return workspace.property("inspectorStatus").toString().contains("503"); }, 5000), "Failed fresh read has no inspector error");
        require(workspace.resourceGuidance().first().toMap()["code"] == "ImagePullBackOff", "Failed refresh erased accepted cached guidance");
    } else if (mode == "ui_context_missing") {
        const QString active = workspace.currentSession();
        require(!workspace.openContext("not-an-imported-context"), "Unknown context was accepted and could create a ghost session");
        const auto saved = sessions.list();
        require(std::holds_alternative<podlord::SessionCatalog>(saved) && std::get<podlord::SessionCatalog>(saved).sessions.size() == 1
            && workspace.currentSession() == active && boundary.requests == requests, "Rejected context changed sessions or transport");
        require(workspace.openContext(std::get<podlord::SourceSnapshot>(imported).contexts.first().id)
            && !workspace.status().contains("Choose an available") && workspace.currentSession() == active,
            "Valid context reuse retained the earlier invalid-context error");
    } else if (mode == "ui_session_snapshot" || mode == "ui_session_duplicate" || mode == "ui_source_import" || mode == "ui_context_reuse") {
        const QString active = workspace.currentSession();
        press("yamlButton");
        require(QTest::qWaitFor([&] { return workspace.property("canEditYaml").toBool(); }, 5000), "Fresh YAML did not become editable");
        press("editYaml");
        const QString draft = workspace.property("yamlText").toString() + "\n# preserved local draft\n";
        require(workspace.setYamlDraft(draft), "Cannot enter real YAML draft");
        const bool submitted = mode == "ui_context_reuse" ? workspace.openContext(std::get<podlord::SourceSnapshot>(imported).contexts.first().id)
            : mode == "ui_session_duplicate" ? workspace.duplicateSession(active, "Active copy")
            : mode == "ui_source_import" ? workspace.importText(directory.filePath("another-origin"), yaml)
            : workspace.saveSessionConfiguration(active, std::get<podlord::SourceSnapshot>(imported).contexts.first().id, "team-a");
        require(submitted, "Cannot submit metadata action with active YAML draft");
        require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Active session snapshot did not settle");
        require(workspace.currentSession() == active && workspace.property("yamlText").toString() == draft
            && workspace.property("yamlDirty").toBool() && !workspace.property("discardPending").toBool(), "Metadata action switched active session, discarded its YAML draft or requested a needless discard");
        require(boundary.requests == requests, "Metadata-only session snapshot unexpectedly fetched cluster data");
    } else {
        QTest::mouseMove(window, item("guidanceTitle_0")->mapToScene(QPointF(10, 10)).toPoint());
        QTest::qWait(150);
        require(boundary.requests == requests, "Opening/hovering cached guidance triggered a request");
        press("resourceGuidanceToggle");
        require(QTest::qWaitFor([&] { return item("guidanceTitle_0") == nullptr; }, 2000), "Collapsed diagnostic content stayed active");
    }
    require(boundary.writes == 0, "Diagnostic guidance mutated Kubernetes");
    return true;
}
}
int main(int argc, char* argv[]) {
    const QGuiApplication app(argc, argv);
    try {
        require(argc == 2, "Expected one named diagnostic scenario");
        const QString mode = QString::fromLocal8Bit(argv[1]);
        if (mode.startsWith("ui_")) return ui(mode) ? 0 : 1;
        const auto input = mode == "empty" ? QJsonObject{} : document(mode);
        const auto findings = podlord::resourceGuidance(input);
        const bool empty = QStringList{"scheduled", "node_ready", "node_pressure_false", "pvc_bound", "deployment_progressing", "unknown", "custom_kind", "missing", "malformed", "wrong_status_type", "unnamed_container", "secret", "empty"}.contains(mode);
        require(findings.size() == (empty ? 0 : mode == "bounded" ? 8 : mode == "multi" ? 2 : 1), "Diagnostic count did not match observable status evidence");
        const auto output = QJsonDocument(QJsonArray::fromVariantList(findings)).toJson();
        require(!output.contains("diagnostic-secret") && !output.contains("<b>"), "Diagnostic output copied untrusted messages or Secret contents");
        require(podlord::resourceGuidance(input) == findings, "Repeated diagnosis changed deterministic output");
        for (const auto& value : findings) {
            const auto row = value.toMap();
            require(!row["code"].toString().isEmpty() && !row["title"].toString().isEmpty() && !row["detail"].toString().isEmpty(), "Recognized evidence has no actionable explanation");
            require(row["documentation"].toString().startsWith("https://kubernetes.io/docs/"), "Diagnostic documentation left the authoritative allowlist");
        }
        if (mode == "previous_oom" || mode == "previous_running") require(findings.first().toMap()["tone"] == "warning"
            && findings.first().toMap()["code"] == "PreviousOOMKilled", "Historical termination was presented as a current failure");
        if (mode == "node_not_ready" || mode == "oom") require(findings.first().toMap()["tone"] == "danger", "Current failure did not receive error presentation");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
