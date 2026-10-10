#include "workspace.h"
#include "kubeconfig_store.h"
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QtTest/QTest>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 15000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) { QCoreApplication::processEvents(); QTest::qWait(10); }
    return ready();
}
bool run(const QString& scenario, const QString& kubeconfig) {
    const QStringList receiptCases{"receipt_api", "receipt_kind", "receipt_uid", "receipt_name", "receipt_namespace", "receipt_status_api"};
    const QStringList readCases{"lost_readback_api", "lost_readback_kind", "lost_readback_name", "lost_readback_namespace", "lost_readback_uid", "lost_readback_version", "lost_readback_auth", "lost_readback_forbidden", "lost_readback_rate", "lost_readback_server", "lost_readback_redirect", "lost_readback_array", "lost_readback_malformed"};
    const QStringList missingCases{"lost_notfound_api", "lost_notfound_kind", "lost_notfound_reason", "lost_notfound_code", "lost_notfound_code_string", "lost_notfound_truncated", "lost_notfound_chunked", "lost_notfound_chunk_truncated", "lost_notfound_gzip"};
    const QStringList existing{"real", "real_race", "auth", "cancel", "close_tab", "cluster", "conflict", "double_confirm", "draft", "escape", "failure_receipt", "finalizer", "forbidden", "hidden", "lost_absent", "lost_failed", "lost_failed_navigation", "lost_failed_recover", "lost_failed_refresh", "lost_invalid", "lost_notfound_malformed", "lost_present", "lost_present_retry", "lost_recreated", "lost_recreated_retry", "malformed", "narrow", "no_uid", "object_receipt", "preview", "queue_cancel", "queue_cancel_tab", "rate", "redirect", "selection", "server", "success", "unsupported", "validation"};
    if (!existing.contains(scenario) && !receiptCases.contains(scenario) && !readCases.contains(scenario) && !missingCases.contains(scenario)) {
        std::fputs("Unknown resource deletion scenario.\n", stderr); return false;
    }
    const bool real = scenario == "real" || scenario == "real_race";
    const QString realName = scenario == "real_race" ? "podlord-delete-race-e2e" : "podlord-delete-e2e";
    const bool clusterScoped = scenario == "cluster";
    const QString kind = clusterScoped ? "Node" : "ConfigMap";
    const QString collection = clusterScoped ? "nodes" : "configmaps";
    const QString prefix = "/api/v1/" + (clusterScoped ? QString{} : "namespaces/default/") + collection;
    QJsonObject metadata{{"name", "delete-target"}, {"uid", "delete-uid"}, {"resourceVersion", "2"}};
    if (!clusterScoped) metadata["namespace"] = "default";
    if (scenario == "no_uid") metadata.remove("uid");
    QJsonObject resource{{"apiVersion", "v1"}, {"kind", kind}, {"metadata", metadata}};
    auto other = resource; metadata["name"] = "other-target"; metadata["uid"] = "other-uid"; other["metadata"] = metadata;
    QTemporaryDir profile;
    QTcpServer server;
    if (!profile.isValid() || (!real && !server.listen(QHostAddress::LocalHost))) return false;
    int deletes = 0, details = 0;
    bool present = true, requestSafe = true;
    QStringList deletePaths;
    QJsonObject options;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                buffer->append(socket->readAll());
                const auto end = buffer->indexOf("\r\n\r\n");
                if (end < 0) return;
                const auto headers = buffer->left(end);
                int size = 0;
                for (const auto& h : headers.split('\n')) if (h.toLower().startsWith("content-length:")) size = h.mid(h.indexOf(':') + 1).trimmed().toInt();
                if (buffer->size() < end + 4 + size) return;
                const auto line = headers.split('\n').first().split(' ');
                const auto url = QUrl(QString::fromUtf8(line.value(1)));
                const auto path = url.path();
                const bool removing = line.value(0) == "DELETE";
                int http = 200;
                QJsonObject response;
                if (removing) {
                    ++deletes; deletePaths.append(path);
                    options = QJsonDocument::fromJson(buffer->mid(end + 4, size)).object();
                    requestSafe = requestSafe && path == prefix + "/delete-target" && url.query().isEmpty()
                        && headers.contains("Content-Type: application/json") && headers.contains("Bearer local-delete-token")
                        && options == QJsonObject{{"apiVersion", "v1"}, {"kind", "DeleteOptions"},
                            {"preconditions", QJsonObject{{"uid", scenario == "lost_recreated_retry" && deletes == 2 ? "replacement-uid" : "delete-uid"}}}};
                    if (scenario == "auth") http = 401;
                    else if (scenario == "forbidden") http = 403;
                    else if (scenario == "rate") http = 429;
                    else if (scenario == "redirect") http = 307;
                    else if (scenario == "conflict") http = 409;
                    else if (scenario == "validation") http = 422;
                    else if (scenario == "server") http = 500;
                    if (http == 200 && !scenario.startsWith("lost_present") && !scenario.startsWith("lost_failed") && !readCases.contains(scenario) && !(scenario.startsWith("lost_recreated") && deletes == 1) && scenario != "lost_invalid" && scenario != "finalizer") present = false;
                    if (scenario.startsWith("lost_recreated") && deletes == 1) { auto m = resource["metadata"].toObject(); m["uid"] = "replacement-uid"; resource["metadata"] = m; }
                    if (scenario == "finalizer") { auto m = resource["metadata"].toObject(); m["deletionTimestamp"] = "2026-10-04T12:00:00Z"; resource["metadata"] = m; }
                    response = {{"apiVersion", "v1"}, {"kind", "Status"}, {"status", http == 200 ? "Success" : "Failure"}, {"code", http}, {"message", "do-not-display-private-server-message"}};
                    if (scenario == "object_receipt") response = resource;
                    if (receiptCases.contains(scenario)) {
                        if (scenario == "receipt_status_api") response["apiVersion"] = "v2";
                        else {
                            response = resource;
                            if (scenario == "receipt_api") response["apiVersion"] = "v2";
                            else if (scenario == "receipt_kind") response["kind"] = "Secret";
                            else { auto m = response["metadata"].toObject(); m[scenario.mid(8)] = "wrong-identity"; response["metadata"] = m; }
                        }
                    }
                    if (scenario == "failure_receipt") response["status"] = "Failure";
                    if (scenario.startsWith("lost_") && !(scenario == "lost_recreated_retry" && deletes == 2)) { buffer->clear(); socket->abort(); return; }
                } else if (path == "/api") response = {{"versions", QJsonArray{"v1"}}};
                else if (path == "/apis") response = {{"groups", QJsonArray{}}};
                else if (path == "/api/v1") response = {{"resources", QJsonArray{QJsonObject{
                    {"name", collection}, {"kind", kind}, {"namespaced", !clusterScoped},
                    {"verbs", scenario == "unsupported" ? QJsonArray{"get", "list"} : QJsonArray{"get", "list", "delete"}}}}}};
                else if (path == "/api/v1/" + collection) response = {{"metadata", QJsonObject{}}, {"items", present ? QJsonArray{resource, other} : QJsonArray{other}}};
                else if (path == prefix + "/delete-target") {
                    ++details;
                    if (deletes && scenario.startsWith("lost_failed") && (details == 2 || scenario == "lost_failed" || scenario == "lost_failed_navigation")) http = 403;
                    else if (!present) http = 404;
                    response = http == 200 ? resource : QJsonObject{{"apiVersion", "v1"}, {"kind", "Status"}, {"status", "Failure"}, {"reason", http == 404 ? "NotFound" : "Forbidden"}, {"code", http}};
                    if (deletes && (scenario == "lost_invalid" || scenario == "lost_notfound_malformed")) response = {{"apiVersion", "v1"}, {"kind", kind}, {"metadata", QJsonObject{{"name", "wrong-target"}}}};
                    if (deletes && readCases.contains(scenario)) {
                        if (scenario == "lost_readback_api") response["apiVersion"] = "v2";
                        else if (scenario == "lost_readback_kind") response["kind"] = "Secret";
                        else if (scenario == "lost_readback_auth") http = 401;
                        else if (scenario == "lost_readback_forbidden") http = 403;
                        else if (scenario == "lost_readback_rate") http = 429;
                        else if (scenario == "lost_readback_server") http = 503;
                        else if (scenario == "lost_readback_redirect") http = 307;
                        else if (!scenario.endsWith("array") && !scenario.endsWith("malformed")) {
                            auto m = response["metadata"].toObject();
                            const auto field = scenario.mid(14);
                            m[field == "version" ? "resourceVersion" : field] = field == "uid" || field == "version" ? "" : "wrong-identity";
                            response["metadata"] = m;
                        }
                    }
                    if (deletes && missingCases.contains(scenario)) {
                        if (scenario == "lost_notfound_api") response["apiVersion"] = "v2";
                        else if (scenario == "lost_notfound_kind") response["kind"] = "ConfigMap";
                        else if (scenario == "lost_notfound_reason") response["reason"] = "Forbidden";
                        else if (scenario == "lost_notfound_code") response["code"] = 403;
                        else if (scenario == "lost_notfound_code_string") response["code"] = "404";
                    }
                } else if (path == prefix + "/other-target") response = other;
                else { http = 404; response = {{"apiVersion", "v1"}, {"kind", "Status"}, {"reason", "NotFound"}, {"code", 404}}; }
                auto body = removing && scenario == "malformed" ? QByteArray("{do-not-display-private-server-message") : QJsonDocument(response).toJson(QJsonDocument::Compact);
                const bool readBack = !removing && deletes && path == prefix + "/delete-target";
                if (readBack && scenario == "lost_readback_array") body = "[]";
                if (readBack && scenario == "lost_readback_malformed") body = "{do-not-display-private-server-message";
                const bool gzip = readBack && scenario == "lost_notfound_gzip";
                if (gzip) body = QByteArray::fromHex("1f8b080000000000001325cb310a80300c05d0bbfc398342a71cc0d145700f3643501a695a17f1ee826e6f7937e4b4556b981730ae1184dd4a066369d27a80103f1893d8d1ab825055e20bb3b7c97bc9206c9e159c86f4bc7d36050055000000");
                const auto length = body.size() + (readBack && scenario == "lost_notfound_truncated" ? 32 : 0);
                auto reply = QByteArray("HTTP/1.1 ") + QByteArray::number(http) + " Test\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(length) + (gzip ? "\r\nContent-Encoding: gzip" : "") + "\r\nRetry-After: 1\r\nConnection: close\r\n\r\n" + body;
                if (readBack && (scenario == "lost_notfound_chunked" || scenario == "lost_notfound_chunk_truncated"))
                    reply = QByteArray("HTTP/1.1 404 Test\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n")
                        + QByteArray::number(body.size(), 16) + "\r\n" + body + "\r\n"
                        + (scenario == "lost_notfound_chunked" ? "0\r\n\r\n" : "");
                if ((removing && (scenario == "hidden" || scenario == "close_tab")) || (!removing && details > 1 && (scenario == "queue_cancel" || scenario == "queue_cancel_tab"))) {
                    QTimer::singleShot(800, socket, [socket = QPointer<QTcpSocket>(socket), reply] { if (socket) { socket->write(reply); socket->disconnectFromHost(); } });
                } else { socket->write(reply); socket->disconnectFromHost(); }
                buffer->clear();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto configPath = profile.path() + "/external.yaml";
    if (!real) {
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly)) return false;
        config.write(QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: local-delete-token\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\ncurrent-context: local\n").arg(server.serverPort()).toUtf8());
    }
    const auto imported = podlord::KubeconfigStore(profile.path()).importFile(real ? kubeconfig : configPath);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported);
    if (!source || source->contexts.isEmpty()) return false;
    podlord::Workspace workspace(profile.path());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return !workspace.busy(); }) || !workspace.openContext(source->contexts.first().id)) return false;
    if (!waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
    if (scenario == "narrow") window->resize(720, 620);
    workspace.filter(real ? realName : "=delete-target");
    bool inspected = false;
    if (!waitFor([&] {
        if (!inspected && workspace.table()->rowCount() == 1) inspected = workspace.inspectRow(0);
        return inspected && !workspace.busy() && !workspace.loading();
    }, real ? 120000 : 15000)) { std::fprintf(stderr, "No inspected deletion target: rows=%d detail=%d\n", workspace.table()->rowCount(), details); return false; }
    const auto item = [&](const char* name) -> QQuickItem* {
        QList<QQuickItem*> pending{window->contentItem()};
        while (!pending.isEmpty()) { auto* next = pending.takeLast(); if (next->objectName() == QLatin1String(name)) return next; pending.append(next->childItems()); }
        return nullptr;
    };
    const auto click = [&](const char* name) {
        auto* target = item(name);
        if (!target || !target->isVisible() || !target->isEnabled()) { std::fprintf(stderr, "Unavailable visible action: %s\n", name); return false; }
        for (auto* next = target; next; next = next->parentItem()) next->ensurePolished();
        QCoreApplication::processEvents();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint());
        QCoreApplication::processEvents(); return true;
    };
    const auto pending = [&] { return workspace.property("deletionPending").toBool(); };
    const auto status = [&] { return workspace.property("deletionStatus").toString(); };
    const auto deleting = [&] { return workspace.property("deletionRunning").toBool(); };
    auto* deleteButton = item("deleteResource");
    if (!deleteButton) { std::fputs("Missing resource deletion UI.\n", stderr); return false; }
    if (scenario == "no_uid" || scenario == "unsupported") return !deleteButton->isEnabled() && deletes == 0;
    if (scenario == "draft") {
        if (!click("yamlButton") || !click("editYaml")) return false;
        return !deleteButton->isEnabled() && deletes == 0 && workspace.yamlEditing();
    }
    if (!click("deleteResource") || !waitFor(pending)) return false;
    const auto targetText = workspace.property("deletionTarget").toString();
    if (!targetText.contains(real ? realName : "delete-target") || !targetText.contains("Cluster:") || !targetText.contains("UID:") || deletes != 0) return false;
    if (clusterScoped && (!targetText.contains("cluster scoped") || targetText.contains("default"))) return false;
    if (!real && !clusterScoped && !targetText.contains("default")) return false;
    if (scenario == "preview" || scenario == "narrow") {
        return waitFor([&] { const auto* cancel = item("cancelResourceDelete"); return cancel && cancel->hasActiveFocus(); }) && deletes == 0;
    }
    if (scenario == "cancel") return click("cancelResourceDelete") && !pending() && deletes == 0;
    if (scenario == "escape") { QTest::keyClick(window, Qt::Key_Escape); return waitFor([&] { return !pending(); }) && deletes == 0; }
    if (scenario == "selection") {
        if (!workspace.inspectPath(prefix + "/other-target")) return false;
        if (pending() && !click("confirmResourceDelete")) return false;
        return !pending() && deletes == 0 && workspace.inspectorName() == "other-target";
    }
    if (scenario == "queue_cancel" || scenario == "queue_cancel_tab") {
        workspace.refreshInspector();
        if (!waitFor([&] { return details > 1; }) || !click("confirmResourceDelete")) return false;
        if (scenario == "queue_cancel_tab") {
            if (!workspace.close(workspace.currentSession()) || !waitFor([&] { return !workspace.busy(); })) return false;
            QTest::qWait(1100); return deletes == 0 && workspace.tabs().isEmpty();
        }
        return workspace.closeInspector() && waitFor([&] { return !workspace.loading(); }) && deletes == 0 && workspace.inspectorPath().isEmpty();
    }
    if (real) {
        const auto frame = qEnvironmentVariable("PODLORD_DELETE_FRAME");
        if (!frame.isEmpty()) { QTest::qWait(120); if (!window->grabWindow().save(frame)) return false; }
    }
    if (scenario == "real_race") {
        QProcess external;
        external.start("kubectl", {"--kubeconfig", kubeconfig, "delete", "configmap", realName, "-n", "visual-a", "--wait=true"});
        if (!external.waitForFinished(30000) || external.exitCode() != 0) return false;
        external.start("kubectl", {"--kubeconfig", kubeconfig, "create", "configmap", realName, "-n", "visual-a", "--from-literal=identity=replacement"});
        if (!external.waitForFinished(30000) || external.exitCode() != 0 || !click("confirmResourceDelete")) return false;
        if (!waitFor([&] { return !deleting() && status().contains("rejected"); }, 60000)) return false;
        external.start("kubectl", {"--kubeconfig", kubeconfig, "get", "configmap", realName, "-n", "visual-a", "-o", "jsonpath={.data.identity}"});
        if (!external.waitForFinished(30000) || external.exitCode() != 0 || external.readAllStandardOutput() != "replacement") return false;
        if (!click("refreshInspector") || !waitFor([&] { return !workspace.loading() && deleteButton->isEnabled(); }, 60000)
            || !click("deleteResource") || !waitFor(pending) || workspace.property("deletionTarget").toString() == targetText) return false;
    }
    if (!click("confirmResourceDelete")) return false;
    if (scenario == "double_confirm" && workspace.confirmDeletion(true)) return false;
    if (scenario == "hidden") {
        if (!waitFor([&] { return deletes == 1; }) || !workspace.closeInspector()) return false;
        return waitFor([&] { return !workspace.loading(); }) && deletes == 1 && details == 2 && workspace.inspectorPath().isEmpty();
    }
    if (scenario == "close_tab") {
        if (!waitFor([&] { return deletes == 1; }) || !workspace.close(workspace.currentSession())) return false;
        return waitFor([&] { return details == 2 && !workspace.busy(); }) && deletes == 1 && workspace.tabs().isEmpty();
    }
    if (!waitFor([&] { return !pending() && !deleting() && !status().isEmpty(); }, real ? 120000 : 15000)) { std::fprintf(stderr, "Delete not complete: %s\n", qPrintable(status())); return false; }
    if (real) {
        QProcess external;
        external.start("kubectl", {"--kubeconfig", kubeconfig, "get", "configmap", realName, "-n", "visual-a", "--ignore-not-found", "-o", "name"});
        const bool absent = external.waitForFinished(30000) && external.exitCode() == 0 && external.readAllStandardOutput().trimmed().isEmpty();
        const bool passed = absent && status().contains("acknowledged") && status().contains("absent") && workspace.table()->rowCount() == 0;
        if (!passed) std::fprintf(stderr, "Real deletion result: serverAbsent=%d cachedRows=%d status=%s\n", absent, workspace.table()->rowCount(), qPrintable(status()));
        return passed;
    }
    if (!requestSafe || deletes != 1 || status().contains("do-not-display-private-server-message")) return false;
    if (receiptCases.contains(scenario) || readCases.contains(scenario) || missingCases.contains(scenario)) {
        QTest::qWait(1300);
        if (deletes != 1 || details != 2 || !status().contains("uncertain")) return false;
        if (receiptCases.contains(scenario) || scenario == "lost_notfound_chunked" || scenario == "lost_notfound_gzip") return status().contains("absent") && workspace.table()->rowCount() == 0;
        return status().contains("unavailable") && !deleteButton->isEnabled() && workspace.table()->rowCount() == 1;
    }
    if (scenario == "lost_recreated_retry") {
        if (!status().contains("different UID") || !click("deleteResource") || !waitFor(pending)
            || !workspace.property("deletionTarget").toString().contains("replacement-uid") || !click("confirmResourceDelete")) return false;
        if (!waitFor([&] { return deletes == 2 && details == 3 && !deleting(); })) return false;
        const bool passed = requestSafe && status().contains("acknowledged") && status().contains("absent") && workspace.table()->rowCount() == 0;
        if (!passed) std::fprintf(stderr, "Recreated target deletion: deletes=%d safe=%d cachedRows=%d status=%s\n", deletes, requestSafe, workspace.table()->rowCount(), qPrintable(status()));
        return passed;
    }
    if (scenario == "lost_failed_recover" || scenario == "lost_failed_refresh") {
        if (!status().contains("uncertain") || deleteButton->isEnabled()) return false;
        if (!click(scenario == "lost_failed_recover" ? "readBackResourceDelete" : "refreshInspector")) return false;
        if (!waitFor([&] { return details == 3 && !workspace.loading() && !deleting(); })
            || !status().contains("uncertain") || !deleteButton->isEnabled() || deletes != 1) return false;
        return click("deleteResource") && waitFor(pending) && deletes == 1 && click("cancelResourceDelete") && deletes == 1;
    }
    if (scenario == "lost_present_retry") {
        if (!status().contains("uncertain") || !deleteButton->isEnabled() || !click("deleteResource") || !waitFor(pending)
            || deletes != 1 || !click("confirmResourceDelete")) return false;
        return waitFor([&] { return deletes == 2 && details == 3 && !deleting(); }) && requestSafe && status().contains("uncertain");
    }
    if (scenario == "lost_failed_navigation") {
        if (!workspace.inspectPath(prefix + "/other-target") || !click("deleteResource") || !waitFor(pending)
            || !click("cancelResourceDelete") || !workspace.inspectPath(prefix + "/delete-target")) return false;
        return waitFor([&] { return !workspace.loading(); }) && deletes == 1 && !deleteButton->isEnabled() && status().contains("uncertain");
    }
    if (scenario == "auth") return workspace.authenticationRequired() && details == 1 && status().contains("rejected");
    if (scenario == "forbidden" || scenario == "rate" || scenario == "redirect" || scenario == "validation" || scenario == "conflict") return details == 1 && status().contains("rejected");
    if (scenario == "lost_failed" || scenario == "lost_invalid" || scenario == "lost_notfound_malformed") return details == 2 && status().contains("uncertain") && status().contains("unavailable") && !deleteButton->isEnabled() && workspace.table()->rowCount() == 1;
    if (scenario == "lost_recreated") return details == 2 && status().contains("uncertain") && status().contains("different UID") && present;
    if (scenario == "lost_present" || scenario == "server") return details == 2 && status().contains("uncertain") && status().contains("present") && present;
    if (scenario == "lost_absent" || scenario == "malformed" || scenario == "failure_receipt") return details == 2 && status().contains("uncertain") && status().contains("absent") && workspace.table()->rowCount() == 0;
    if (scenario == "finalizer") return details == 2 && status().contains("acknowledged") && status().contains("terminating") && workspace.table()->rowCount() == 1;
    return details == 2 && status().contains("acknowledged") && status().contains("absent") && workspace.table()->rowCount() == 0;
}
}
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    Q_INIT_RESOURCE(workspace_ui);
    const auto args = application.arguments();
    const auto scenario = args.value(1);
    if (!run(scenario, args.value(2))) { std::fprintf(stderr, "Resource deletion scenario failed: %s\n", qPrintable(scenario)); return 1; }
    std::printf("Resource deletion public UI scenario passed: %s\n", qPrintable(scenario)); return 0;
}
