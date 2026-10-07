#include "yaml_draft_check.h"
#include "workspace.h"
#include "kubeconfig_store.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>
#include <QProcess>
#include <QtTest/QTest>
#include <cstdio>
#include <functional>
#include <memory>
#include <algorithm>

namespace {
const QString baseline = "apiVersion: v1\nkind: ConfigMap\nmetadata:\n  name: checked\n  namespace: default\n  uid: check-uid\n  resourceVersion: 'opaque/version:2'\ndata:\n  setting: before\n";
const QJsonObject original{{"apiVersion", "v1"}, {"kind", "ConfigMap"}, {"metadata", QJsonObject{
    {"name", "checked"}, {"namespace", "default"}, {"uid", "check-uid"}, {"resourceVersion", "opaque/version:2"}}},
    {"data", QJsonObject{{"setting", "before"}}}};
bool await(const std::function<bool()>& ready, int limit = 12000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QTest::qWait(10); }
    return ready();
}
QJsonValue externalValue(QJsonObject object, const QStringList& keys) {
    if (keys.isEmpty()) return object;
    return keys.size() == 1 ? object.value(keys.first()) : externalValue(object.value(keys.first()).toObject(), keys.mid(1));
}
QJsonObject externalChange(QJsonObject object, const QStringList& keys, QJsonValue value) {
    if (keys.size() == 1) { if (value.isUndefined()) object.remove(keys.first()); else object[keys.first()] = value; }
    else object[keys.first()] = externalChange(object[keys.first()].toObject(), keys.mid(1), value);
    return object;
}
bool ui(const QString& scenario, const QString& kubeconfig = {}) {
    const QStringList replacementCases{"ui_apply_ack_recreated", "ui_apply_ack_recreated_equal"};
    const QStringList readCases{"ui_apply_read_api", "ui_apply_read_kind", "ui_apply_read_name", "ui_apply_read_namespace", "ui_apply_read_uid", "ui_apply_read_version", "ui_apply_read_auth", "ui_apply_read_forbidden", "ui_apply_read_rate", "ui_apply_read_server", "ui_apply_read_redirect", "ui_apply_read_malformed", "ui_apply_read_truncated"};
    const QStringList receiptCases{"ui_apply_receipt_api", "ui_apply_receipt_kind", "ui_apply_receipt_uid", "ui_apply_receipt_version", "ui_apply_receipt_name", "ui_apply_receipt_namespace"};
    const bool real = scenario == "real" || scenario == "real_apply";
    QTemporaryDir profile;
    QTcpServer server;
    if (!profile.isValid() || (!real && !server.listen(QHostAddress::LocalHost))) return false;
    const bool secret = scenario == "ui_secret" || scenario == "ui_apply_secret" || scenario == "ui_apply_secret_input";
    const QString collection = secret ? "secrets" : "configmaps";
    const QString kind = secret ? "Secret" : "ConfigMap";
    auto resource = original;
    resource["kind"] = kind;
    if (secret) resource["data"] = QJsonObject{{"setting", "cHJpdmF0ZS10ZXN0"}};
    QStringList methods;
    QStringList paths;
    int details = 0;
    int patches = 0;
    QJsonArray lastPatch;
    bool patchHeaders = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                buffer->append(socket->readAll());
                const auto headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                const auto headers = buffer->left(headerEnd);
                int contentLength = 0;
                for (const auto& header : headers.split('\n')) if (header.toLower().startsWith("content-length:")) contentLength = header.mid(header.indexOf(':') + 1).trimmed().toInt();
                if (buffer->size() < headerEnd + 4 + contentLength) return;
                const auto line = buffer->split('\n').first().split(' ');
                const auto path = QUrl(QString::fromUtf8(line.value(1))).path();
                paths.append(path);
                methods.append(QString::fromUtf8(line.value(0)));
                QJsonObject response;
                int http = 200;
                const bool patch = line.value(0) == "PATCH";
                if (patch) {
                    ++patches;
                    lastPatch = QJsonDocument::fromJson(buffer->mid(headerEnd + 4, contentLength)).array();
                    patchHeaders = headers.contains("application/json-patch+json") && headers.contains("Bearer local-test-token");
                    if (patches == 1 && (scenario == "ui_apply_conflict" || scenario == "ui_apply_overlap" || scenario == "ui_apply_recreate")) {
                        auto metadata = resource["metadata"].toObject(); metadata["resourceVersion"] = "server/version:3";
                        if (scenario == "ui_apply_recreate") metadata["uid"] = "recreated-uid";
                        resource["metadata"] = metadata;
                        auto data = resource["data"].toObject(); data["retained"] = "foreign-change";
                        if (scenario == "ui_apply_overlap") data["setting"] = "server-change";
                        resource["data"] = data;
                    }
                    auto updated = resource;
                    for (const auto& value : lastPatch) {
                        const auto operation = value.toObject();
                        auto keys = operation["path"].toString().mid(1).split('/');
                        for (auto& key : keys) key.replace("~1", "/").replace("~0", "~");
                        if (operation["op"] == "test") { if (externalValue(updated, keys) != operation["value"]) http = 422; }
                        else updated = externalChange(updated, keys, operation["op"] == "remove" ? QJsonValue(QJsonValue::Undefined) : operation["value"]);
                    }
                    if (scenario == "ui_apply_auth") http = 401;
                    else if (scenario == "ui_apply_forbidden") http = 403;
                    else if (scenario == "ui_apply_rate") http = 429;
                    else if (scenario == "ui_apply_validation") http = 422;
                    else if (scenario == "ui_apply_unobserved") http = 500;
                    else if (scenario == "ui_apply_redirect") http = 307;
                    if (http == 200) {
                        resource = updated;
                        auto metadata = resource["metadata"].toObject(); metadata["resourceVersion"] = "applied/version:" + QString::number(patches + 3); resource["metadata"] = metadata;
                    }
                    response = http == 200 ? resource : QJsonObject{{"kind", "Status"}, {"status", "Failure"}, {"code", http}, {"message", "operator-private-value"}};
                    if (receiptCases.contains(scenario)) {
                        if (scenario == "ui_apply_receipt_api") response["apiVersion"] = "v2";
                        else if (scenario == "ui_apply_receipt_kind") response["kind"] = "Secret";
                        else { auto metadata = response["metadata"].toObject(); const auto field = scenario.mid(17); metadata[field == "version" ? "resourceVersion" : field] = field == "version" ? "" : "wrong-identity"; response["metadata"] = metadata; }
                    }
                    if (replacementCases.contains(scenario)) {
                        auto metadata = resource["metadata"].toObject(); metadata["uid"] = "replacement-uid"; metadata["resourceVersion"] = "replacement/version:1"; resource["metadata"] = metadata;
                        if (scenario == "ui_apply_ack_recreated") resource["data"] = original["data"];
                    }
                    if (scenario == "ui_apply_lost") { buffer->clear(); socket->abort(); return; }
                }
                else if (path == "/api") response = {{"kind", "APIVersions"}, {"versions", QJsonArray{"v1"}}};
                else if (path == "/apis") response = {{"kind", "APIGroupList"}, {"groups", QJsonArray{}}};
                else if (path == "/api/v1") response = {{"kind", "APIResourceList"}, {"groupVersion", "v1"}, {"resources", QJsonArray{QJsonObject{
                    {"name", collection}, {"kind", kind}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                else if (path.endsWith('/' + collection)) response = {{"apiVersion", "v1"}, {"kind", kind + "List"},
                    {"metadata", QJsonObject{{"resourceVersion", "2"}}}, {"items", QJsonArray{resource}}};
                else if (path.endsWith('/' + collection + "/checked")) {
                    ++details; response = resource;
                    if (patches && readCases.contains(scenario)) {
                        if (scenario == "ui_apply_read_api") response["apiVersion"] = "v2";
                        else if (scenario == "ui_apply_read_kind") response["kind"] = "Secret";
                        else if (scenario == "ui_apply_read_auth") http = 401;
                        else if (scenario == "ui_apply_read_forbidden") http = 403;
                        else if (scenario == "ui_apply_read_rate") http = 429;
                        else if (scenario == "ui_apply_read_server") http = 503;
                        else if (scenario == "ui_apply_read_redirect") http = 307;
                        else if (scenario != "ui_apply_read_malformed" && scenario != "ui_apply_read_truncated") {
                            auto metadata = response["metadata"].toObject(); const auto field = scenario.mid(14);
                            metadata[field == "version" ? "resourceVersion" : field] = field == "uid" || field == "version" ? "" : "wrong-identity";
                            response["metadata"] = metadata;
                        }
                    }
                }
                else response = {{"kind", "Status"}, {"status", "Failure"}, {"code", 404}};
                if (response["code"] == 404) http = 404;
                const bool readBack = !patch && patches && path.endsWith('/' + collection + "/checked");
                const auto body = (patch && scenario == "ui_apply_malformed") || (readBack && scenario == "ui_apply_read_malformed") ? QByteArray("{operator-private-value") : QJsonDocument(response).toJson(QJsonDocument::Compact);
                const QByteArray reply = QByteArray("HTTP/1.1 ") + QByteArray::number(http) + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size() + (readBack && scenario == "ui_apply_read_truncated" ? 32 : 0)) + "\r\nRetry-After: 1\r\nConnection: close\r\n\r\n" + body;
                const bool delay = (patch && scenario == "ui_apply_hidden") || (!patch && details > 1 && scenario == "ui_apply_queue_cancel");
                if (delay) QTimer::singleShot(800, socket, [socket = QPointer<QTcpSocket>(socket), reply] { if (socket) { socket->write(reply); socket->disconnectFromHost(); } });
                else { socket->write(reply); socket->disconnectFromHost(); }
                buffer->clear();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto path = profile.path() + "/external.yaml";
    QFile config(path);
    if (!real && !config.open(QIODevice::WriteOnly)) return false;
    if (!real) {
    config.write(QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: local-test-token\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n    namespace: default\ncurrent-context: local\n").arg(server.serverPort()).toUtf8());
    config.close();
    }
    const auto imported = podlord::KubeconfigStore(profile.path()).importFile(real ? kubeconfig : path);
    if (!std::holds_alternative<podlord::SourceSnapshot>(imported)) return false;
    const auto& contexts = std::get<podlord::SourceSnapshot>(imported).contexts;
    if (contexts.isEmpty()) return false;
    podlord::Workspace workspace(profile.path());
    if (scenario == "ui_early_context" && (!workspace.busy() || workspace.openContext(contexts.first().id))) {
        std::fputs("Startup accepted a context before settings/catalog initialization.\n", stderr);
        return false;
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !await([&] { return !workspace.busy(); }) || !workspace.openContext(contexts.first().id)) return false;
    if (!await([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
    workspace.filter(real ? "podlord-native-e2e" : "checked");
    bool inspected = false;
    if (!await([&] {
        if (!inspected && workspace.table()->rowCount() == 1) inspected = workspace.inspectRow(0);
        return inspected && workspace.canEditYaml();
    }, real ? 60000 : 12000)) {
        std::fprintf(stderr, "Fresh detail missing: inspected=%d details=%d local paths=%s\n", inspected, details, paths.join(',').toUtf8().constData());
        std::fprintf(stderr, "Local workspace state: busy=%d loading=%d selected=%d errorPresent=%d\n", workspace.busy(), workspace.loading(), !workspace.currentSession().isEmpty(), !workspace.error().isEmpty());
        return false;
    }
    const auto itemNamed = [&](const char* name) {
        auto* item = window->findChild<QQuickItem*>(name);
        if (item) return item;
        QList<QQuickItem*> pending{window->contentItem()};
        while (!pending.isEmpty()) {
            auto* next = pending.takeLast();
            if (next->objectName() == QLatin1String(name)) return next;
            pending.append(next->childItems());
        }
        return static_cast<QQuickItem*>(nullptr);
    };
    const auto click = [&](const char* name) {
        auto* item = itemNamed(name);
        if (!item || !item->isVisible() || !item->isEnabled()) return false;
        QList<QQuickItem*> ancestors;
        for (auto* parent = item; parent; parent = parent->parentItem()) ancestors.prepend(parent);
        for (auto* parent : ancestors) parent->ensurePolished();
        QCoreApplication::processEvents();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        QCoreApplication::processEvents();
        if (QLatin1String(name) == "checkYaml") return await([&] { return !workspace.yamlChecking(); });
        return true;
    };
    if (!click("yamlButton")) return false;
    auto* button = window->findChild<QQuickItem*>("checkYaml");
    if (!button) { std::fputs("Missing visible Check YAML action.\n", stderr); return false; }
    if (scenario == "ui_readonly") return !button->isVisible() && !workspace.checkYamlDraft() && workspace.yamlCheckStatus().isEmpty();
    if (!click("editYaml") || !workspace.yamlEditing()) return false;
    const auto before = methods.size();
    const auto replace = [&](const QString& text) {
        if (!click("inspectorYaml")) return false;
        QTest::keySequence(window, QKeySequence(QKeySequence::SelectAll));
        QGuiApplication::clipboard()->setText(text);
        QTest::keySequence(window, QKeySequence(QKeySequence::Paste));
        return workspace.yamlText() == text;
    };
    if (scenario == "real_apply") {
        const auto apply = [&](const QString& text) {
            return replace(text) && click("previewYaml") && await([&] { return workspace.yamlApplyPreview(); }) && click("confirmYamlApply")
                && await([&] { return !workspace.yamlEditing() && workspace.yamlApplyStatus().contains("acknowledged"); }, 60000);
        };
        auto first = workspace.yamlText(); first.replace("native-api", "native-applied");
        if (!apply(first) || !workspace.yamlText().contains("native-applied") || !click("editYaml")) { std::fputs("Real ConfigMap apply failed.\n", stderr); return false; }
        auto draft = workspace.yamlText(); draft.replace("native-applied", "native-reconciled");
        if (!replace(draft) || !click("previewYaml") || !await([&] { return workspace.yamlApplyPreview(); })) return false;
        QProcess external;
        external.start("kubectl", {"--kubeconfig", kubeconfig, "patch", "configmap", "podlord-native-e2e", "--type=merge", "-p", "{\"metadata\":{\"labels\":{\"foreign\":\"retained\"}}}"});
        if (!external.waitForFinished(30000) || external.exitStatus() != QProcess::NormalExit || external.exitCode() != 0) return false;
        if (!click("confirmYamlApply") || !await([&] { return !workspace.yamlApplyLocked() && workspace.canReconcileYaml(); }, 60000)
            || workspace.yamlText() != draft || !click("reconcileYaml") || !workspace.yamlReconcileReady()
            || !click("continueYamlReconcile") || !await([&] { return workspace.yamlApplyPreview(); }) || !click("confirmYamlApply")
            || !await([&] { return !workspace.yamlEditing() && workspace.yamlApplyStatus().contains("acknowledged"); }, 60000)
            || !workspace.yamlText().contains("native-reconciled") || !workspace.yamlText().contains("retained")) {
            std::fputs("Real concurrent ConfigMap change was not safely reconciled.\n", stderr); return false;
        }
        const auto screenshot = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
        if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) return false;
        if (!workspace.closeInspector() || !workspace.filter("podlord-native-secret-e2e")) return false;
        bool selected = false;
        if (!await([&] { if (!selected && workspace.table()->rowCount() == 1) selected = workspace.inspectRow(0); return selected && workspace.canEditYaml(); }, 60000)
            || !click("yamlButton") || !click("editYaml")) return false;
        if (!apply(workspace.yamlText() + "\nimmutable: false\n")
            || workspace.yamlText().contains("podlord-alpha-real-secret") || workspace.yamlText().contains("cG9kbG9yZC1hbHBoYS")) {
            std::fputs("Real Secret metadata apply or redaction failed.\n", stderr); return false;
        }
        const QString entered = "local-entered-secret-e2e";
        const auto encoded = entered.toUtf8().toBase64();
        if (!click("editYaml") || !replace(workspace.yamlText() + "\nstringData:\n  local-input: \"" + entered + "\"\n")
            || !workspace.yamlText().contains(entered) || !click("previewYaml") || !await([&] { return workspace.yamlApplyPreview(); })
            || workspace.yamlApplyDiff().contains(entered) || workspace.yamlApplyDiff().contains(QString::fromLatin1(encoded))
            || !click("confirmYamlApply") || !await([&] { return !workspace.yamlEditing() && workspace.yamlApplyStatus().contains("acknowledged"); }, 60000)
            || workspace.yamlText().contains(entered) || workspace.yamlText().contains(QString::fromLatin1(encoded))) {
            std::fputs("Real user-entered Secret write or masking failed.\n", stderr); return false;
        }
        QProcess verifySecret;
        verifySecret.start("kubectl", {"--kubeconfig", kubeconfig, "get", "secret", "podlord-native-secret-e2e", "-o", "jsonpath={.data.local-input}"});
        if (!verifySecret.waitForFinished(30000) || verifySecret.exitStatus() != QProcess::NormalExit || verifySecret.exitCode() != 0
            || verifySecret.readAllStandardOutput() != encoded) {
            std::fputs("Independent Secret read-back did not match the entered value.\n", stderr); return false;
        }
        auto* secretEditor = itemNamed("inspectorYaml");
        if (!secretEditor) return false;
        secretEditor->forceActiveFocus(); QTest::keySequence(window, QKeySequence(QKeySequence::MoveToStartOfDocument));
        QTest::qWait(50);
        if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot + ".secret-entered.png")) return false;
        if (!click("editYaml")) return false;
        auto cleanup = workspace.yamlText();
        cleanup.replace("  \"local-input\": \"[hidden]\"\n", QString{});
        if (cleanup == workspace.yamlText() || !apply(cleanup)) {
            std::fputs("Real Secret test value cleanup failed.\n", stderr); return false;
        }
        secretEditor->forceActiveFocus(); QTest::keySequence(window, QKeySequence(QKeySequence::MoveToStartOfDocument));
        QTest::qWait(50);
        return screenshot.isEmpty() || window->grabWindow().save(screenshot + ".secret.png");
    }
    if (scenario.startsWith("ui_apply_")) {
        auto draft = workspace.yamlText();
        if (secret) draft += "\nimmutable: false\n";
        else draft.replace("before", "after");
        if (scenario == "ui_apply_secret_input") draft += "\nstringData:\n  local-input: \"local-entered-secret\"\n";
        if (scenario == "ui_apply_preparing") {
            if (!replace(draft) || !workspace.previewYamlApply() || workspace.yamlApplyPreview() || !workspace.yamlApplyLocked()) {
                std::fputs("Local preparation did not yield the UI event loop.\n", stderr); return false;
            }
            return await([&] { return workspace.yamlApplyPreview(); }) && patches == 0 && click("cancelYamlApply") && workspace.yamlText() == draft;
        }
        if (!replace(draft) || !click("previewYaml") || !await([&] { return workspace.yamlApplyPreview(); }) || !workspace.yamlApplyLocked()
            || patches != 0 || !workspace.yamlApplyTarget().contains("default") || !workspace.yamlApplyTarget().contains("checked")) return false;
        if (secret && (workspace.yamlApplyDiff().contains("cHJpdmF0") || workspace.yamlApplyDiff().contains("private-test"))) return false;
        if (scenario == "ui_apply_secret_input" && (!workspace.yamlText().contains("local-entered-secret")
            || workspace.yamlApplyDiff().contains("local-entered-secret")
            || workspace.yamlApplyDiff().contains("bG9jYWwtZW50ZXJlZC1zZWNyZXQ="))) return false;
        if (scenario == "ui_apply_preview") {
            auto* cancel = window->findChild<QQuickItem*>("cancelYamlApply");
            if (!cancel || !await([&] { return cancel->isVisible() && cancel->hasActiveFocus() && cancel->property("visualFocus").toBool(); })) {
                std::fputs("YAML preview did not show keyboard focus on Cancel.\n", stderr);
                return false;
            }
            QTest::keyClick(window, Qt::Key_Escape);
            auto* readBack = itemNamed("readBackYaml");
            return !workspace.yamlApplyPreview() && !workspace.yamlApplyLocked() && workspace.yamlText() == draft && workspace.yamlDirty() && patches == 0 && readBack && !readBack->isVisible();
        }
        if (scenario == "ui_apply_queue_cancel") {
            if (!click("cancelYamlApply")) return false;
            const auto previous = details;
            if (!workspace.refreshInspector() || !await([&] { return details > previous; }) || !click("previewYaml") || !await([&] { return workspace.yamlApplyPreview(); }) || !click("confirmYamlApply")) return false;
            if (workspace.leaveYamlEdit() || !workspace.discardPending() || !workspace.confirmDiscard(true)) return false;
            return await([&] { return !workspace.loading(); }) && patches == 0 && !workspace.yamlEditing();
        }
        if (!click("confirmYamlApply") || workspace.yamlApplyPreview()) return false;
        if (scenario == "ui_apply_hidden") {
            if (!await([&] { return patches == 1; }) || workspace.leaveYamlEdit() || !workspace.confirmDiscard(true) || !workspace.closeInspector()) return false;
            return await([&] { return !workspace.loading(); }) && patches == 1 && details >= 2 && workspace.inspectorPath().isEmpty() && !workspace.yamlEditing();
        }
        if (!await([&] { return patches == 1 && !workspace.yamlApplyLocked(); })) {
            std::fprintf(stderr, "Apply completion missing: patches=%d phase=%s\n", patches, qPrintable(workspace.yamlApplyStatus())); return false;
        }
        if (!patchHeaders || lastPatch[0].toObject()["path"] != "/metadata/uid" || lastPatch[1].toObject()["path"] != "/metadata/resourceVersion"
            || workspace.yamlApplyStatus().contains("operator-private-value")) return false;
        if (replacementCases.contains(scenario) || readCases.contains(scenario) || receiptCases.contains(scenario)) {
            QTest::qWait(1300);
            if (patches != 1 || details != 2 || !workspace.yamlEditing() || workspace.yamlText() != draft) return false;
            if (replacementCases.contains(scenario)) return !workspace.canReconcileYaml() && workspace.yamlApplyStatus().contains("identity changed");
            if (receiptCases.contains(scenario)) return workspace.yamlApplyStatus().contains("Desired changes observed");
            return workspace.yamlApplyStatus().contains("Read-back unavailable") && !workspace.canReconcileYaml()
                && (scenario != "ui_apply_read_auth" || workspace.authenticationRequired());
        }
        if (scenario == "ui_apply_secret_input") return !workspace.yamlEditing() && workspace.yamlApplyStatus().contains("acknowledged")
            && resource["data"].toObject()["local-input"] == "bG9jYWwtZW50ZXJlZC1zZWNyZXQ="
            && resource["data"].toObject()["setting"] == "cHJpdmF0ZS10ZXN0"
            && !workspace.yamlText().contains("local-entered-secret") && !workspace.yamlText().contains("bG9jYWwtZW50ZXJlZC1zZWNyZXQ=")
            && workspace.yamlText().contains("[hidden]") && workspace.yamlApplyDiff().isEmpty();
        if (scenario == "ui_apply_success" || secret) return !workspace.yamlEditing() && workspace.yamlApplyStatus().contains("acknowledged") && workspace.yamlApplyDiff().isEmpty()
            && (secret ? resource["data"].toObject()["setting"] == "cHJpdmF0ZS10ZXN0" : resource["data"].toObject()["setting"] == "after");
        if (!workspace.yamlEditing() || workspace.yamlText() != draft) return false;
        if (scenario == "ui_apply_auth") return workspace.authenticationRequired() && details == 1 && patches == 1;
        if (scenario == "ui_apply_forbidden" || scenario == "ui_apply_rate" || scenario == "ui_apply_redirect") return details == 1 && patches == 1;
        if (scenario == "ui_apply_malformed" || scenario == "ui_apply_lost") return workspace.yamlApplyStatus().contains("Desired changes observed") && patches == 1 && details == 2;
        if (scenario == "ui_apply_unobserved") return workspace.yamlApplyStatus().contains("uncertain") && patches == 1 && details == 2;
        if (scenario == "ui_apply_validation") return workspace.yamlApplyStatus().contains("rejected") && patches == 1 && details == 2;
        if (scenario == "ui_apply_recreate") return !workspace.canReconcileYaml() && patches == 1 && details == 2;
        if (scenario == "ui_apply_conflict" || scenario == "ui_apply_overlap") {
            if (!workspace.canReconcileYaml() || !click("reconcileYaml") || !workspace.yamlReconcilePending()) { std::fprintf(stderr, "Reconcile admission failed: available=%d pending=%d\n", workspace.canReconcileYaml(), workspace.yamlReconcilePending()); return false; }
            if (scenario == "ui_apply_overlap" && !workspace.yamlReconcileReady()) {
                const bool clicked = click("chooseYamlMine_0");
                if (!clicked) std::fputs("Reconcile choice was not reachable.\n", stderr);
            }
            if (!workspace.yamlReconcileReady()) {
                auto* choice = itemNamed("chooseYamlMine_0");
                const auto point = choice ? choice->mapToScene(QPointF(choice->width()/2, choice->height()/2)) : QPointF{};
                std::fprintf(stderr, "Overlap choice not accepted: present=%d visible=%d width=%.1f height=%.1f x=%.1f y=%.1f choices=%d\n", choice != nullptr, choice && choice->isVisible(), choice ? choice->width() : 0, choice ? choice->height() : 0, point.x(), point.y(), static_cast<int>(workspace.yamlConflicts().size()));
                const auto screenshot = qEnvironmentVariable("PODLORD_UI_FAILURE_IMAGE");
                if (!screenshot.isEmpty()) window->grabWindow().save(screenshot);
                return false;
            }
            if (!workspace.yamlReconcileReady() || !click("continueYamlReconcile") || !await([&] { return workspace.yamlApplyPreview(); }) || patches != 1
                || !workspace.yamlText().contains("foreign-change") || !click("confirmYamlApply")) return false;
            return await([&] { return patches == 2 && !workspace.yamlEditing(); }) && resource["data"].toObject()["setting"] == "after"
                && resource["data"].toObject()["retained"] == "foreign-change" && lastPatch[1].toObject()["value"] == "server/version:3";
        }
        return false;
    }
    if (scenario == "ui_invalid" && !replace("data: [operator-private-value")) return false;
    if (scenario == "ui_keyboard") {
        button->forceActiveFocus(Qt::TabFocusReason);
        if (!button->hasActiveFocus() || !button->property("visualFocus").toBool()) return false;
        QTest::keyClick(window, Qt::Key_Space);
    } else if (!click("checkYaml")) return false;
    if (!await([&] { return !workspace.yamlChecking(); })) return false;
    if (workspace.yamlCheckStatus().isEmpty()) return false;
    const auto checked = workspace.yamlText();
    const auto checkedStatus = workspace.yamlCheckStatus();
    auto* status = window->findChild<QQuickItem*>("yamlCheckStatus");
    if (!status || !status->isVisible() || status->property("text").toString() != checkedStatus) return false;
    if (scenario == "ui_invalid") {
        if (!checkedStatus.contains("syntax error") || checkedStatus.contains("operator-private-value")) return false;
    } else if (!checkedStatus.contains("Local YAML syntax")) return false;
    if (scenario == "ui_invalidate") {
        if (!replace(checked + "\n# changed after check\n")) return false;
        if (!workspace.yamlCheckStatus().isEmpty()) return false;
    } else if (scenario == "ui_refresh") {
        const auto oldDetails = details;
        if (!workspace.refreshInspector() || !await([&] { return details > oldDetails && !workspace.inspectorStatus().contains("Refreshing detail."); })) return false;
        if (!workspace.yamlCheckStatus().isEmpty() || workspace.yamlText() != checked) return false;
    } else if (scenario == "ui_check_refresh") {
        const auto oldDetails = details;
        if (!workspace.checkYamlDraft() || !workspace.yamlChecking() || !workspace.refreshInspector()
            || !await([&] { return !workspace.yamlChecking(); }) || !workspace.yamlCheckStatus().isEmpty()) return false;
        if (!await([&] { return details > oldDetails && !workspace.inspectorStatus().contains("Refreshing detail."); })) return false;
    } else if (scenario == "ui_discard") {
        if (!workspace.leaveYamlEdit() || workspace.yamlEditing() || !workspace.yamlCheckStatus().isEmpty()) return false;
    } else if (scenario == "ui_pending") {
        if (!replace(checked + "\n# unapplied\n")) { std::fputs("Pending guard: text input failed.\n", stderr); return false; }
        const bool clicked = click("leaveYamlEdit");
        if (!clicked || !workspace.discardPending() || button->isEnabled()) {
            std::fprintf(stderr, "Pending guard: clicked=%d pending=%d checkEnabled=%d dirty=%d\n", clicked,
                workspace.discardPending(), button->isEnabled(), workspace.yamlDirty());
            return false;
        }
        if (workspace.checkYamlDraft() || !workspace.confirmDiscard(false)) return false;
    } else if (workspace.yamlText() != checked || !workspace.yamlEditing()) return false;
    if (scenario == "ui_repeat" && (!click("checkYaml") || !await([&] { return !workspace.yamlChecking(); }) || workspace.yamlCheckStatus() != checkedStatus)) return false;
    if (real) {
        const auto screenshot = qEnvironmentVariable("PODLORD_E2E_SCREENSHOT");
        if (!replace(checked + "\n# local checked draft\n") || !click("checkYaml")
            || !await([&] { return !workspace.yamlChecking(); })
            || !workspace.yamlCheckStatus().contains("Local YAML syntax")) return false;
        QTest::qWait(80);
        if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) return false;
        if (!replace("data: [operator-private-value") || !click("checkYaml")
            || !await([&] { return !workspace.yamlChecking(); })
            || !workspace.yamlCheckStatus().contains("syntax error")
            || workspace.yamlCheckStatus().contains("operator-private-value")) return false;
        if (!click("leaveYamlEdit") || !workspace.discardPending() || !workspace.confirmDiscard(true)
            || workspace.yamlEditing() || !workspace.closeInspector()) return false;
        workspace.filter("podlord-native-secret-e2e");
        if (!await([&] { return workspace.table()->rowCount() == 1; }, 60000)
            || !workspace.inspectRow(0) || !await([&] { return workspace.canEditYaml(); }, 60000)
            || !click("yamlButton") || !click("editYaml") || !click("checkYaml")
            || !workspace.yamlCheckStatus().contains("Local YAML syntax")
            || workspace.yamlText().contains("podlord-alpha-real-secret")
            || workspace.yamlText().contains("podlord-beta-real-secret")
            || workspace.yamlText().contains(QString::fromLatin1(QByteArray("podlord-alpha-real-secret").toBase64()))) return false;
        QTest::qWait(80);
        if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot + ".secret.png")) return false;
    }
    if (scenario != "ui_refresh" && scenario != "ui_check_refresh" && methods.size() != before) return false;
    for (const auto& method : methods) if (method != "GET") return false;
    return true;
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const auto name = QCoreApplication::arguments().value(1);
    if (name.startsWith("ui_") || name == "real" || name == "real_apply") {
        if (name.startsWith("real") && QCoreApplication::arguments().size() != 3) return 2;
        const auto clipboard = QGuiApplication::clipboard()->text();
        const auto result = ui(name, QCoreApplication::arguments().value(2));
        QGuiApplication::clipboard()->setText(clipboard);
        return result ? 0 : 1;
    }
    QString input = baseline;
    auto resource = original;
    bool valid = false;
    if (name == "valid") valid = true;
    else if (name == "comment") { input += "\n# user annotation\n"; valid = true; }
    else if (name == "empty") input.clear();
    else if (name == "whitespace") input = " \n\t\n";
    else if (name == "multiple") input += "---\n" + baseline;
    else if (name == "sequence") input = "- a\n- b\n";
    else if (name == "scalar") input = "operator-private-value";
    else if (name == "null") input = "null\n";
    else if (name == "malformed") input += "bad: [operator-private-value\n";
    else if (name == "nul") input += QChar{0};
    else if (name == "duplicate_root") input += "kind: ConfigMap\n";
    else if (name == "duplicate_nested") input += "nested: {a: 1, a: 2}\n";
    else if (name == "complex_key") input += "? [a, b]\n: value\n";
    else if (name == "alias_cycle") input += "nested: &self {next: *self}\n";
    else if (name == "alias_sequence_cycle") input += "nested: &self [*self]\n";
    else if (name == "alias_shared") { input += "nested: &shared {a: 1}\nother: *shared\n"; valid = true; }
    else if (name == "duplicate_alias") input += "nested: &shared {a: 1, a: 2}\nother: *shared\n";
    else if (name == "api_version") input.replace("apiVersion: v1", "apiVersion: example.test/v1");
    else if (name == "kind") input.replace("kind: ConfigMap", "kind: Secret");
    else if (name == "name") input.replace("name: checked", "name: other");
    else if (name == "namespace") input.replace("namespace: default", "namespace: other");
    else if (name == "uid") input.replace("uid: check-uid", "uid: other");
    else if (name == "version") input.replace("opaque/version:2", "opaque/version:3");
    else if (name == "missing_name") input.replace("  name: checked\n", "");
    else if (name == "missing_uid") input.replace("  uid: check-uid\n", "");
    else if (name == "missing_version") input.replace("  resourceVersion: 'opaque/version:2'\n", "");
    else if (name == "missing_namespace") input.replace("  namespace: default\n", "");
    else if (name == "metadata_scalar") input.replace("metadata:\n", "metadata: text\nignored:\n");
    else if (name == "missing_api") input.replace("apiVersion: v1\n", "");
    else if (name == "missing_kind") input.replace("kind: ConfigMap\n", "");
    else if (name == "missing_metadata") input.replace("metadata:\n", "other:\n");
    else if (name == "metadata_null") input.replace("metadata:\n", "metadata: null\nother:\n");
    else if (name == "metadata_sequence") input.replace("metadata:\n", "metadata: []\nother:\n");
    else if (name == "identity_null") input.replace("name: checked", "name: null");
    else if (name == "identity_sequence") input.replace("uid: check-uid", "uid: []");
    else if (name == "namespace_null") input.replace("namespace: default", "namespace: null");
    else if (name == "namespace_sequence") input.replace("namespace: default", "namespace: []");
    else if (name == "sequence_body") { input += "nested: [1, null, {value: two}]\n"; valid = true; }
    else if (name == "unicode") { input += "unicode: \"\\u20ac\"\n"; valid = true; }
    else if (name == "invalid_unicode") { input += "invalid: "; input += QChar{0xd800}; }
    else if (name == "baseline_kind") resource.remove("kind");
    else if (name == "baseline_name" || name == "baseline_uid" || name == "baseline_version" || name == "baseline_namespace_invalid") {
        auto metadata = resource["metadata"].toObject();
        if (name == "baseline_namespace_invalid") { metadata["namespace"] = 7; input.replace("  namespace: default\n", ""); }
        else metadata.remove(name == "baseline_name" ? "name" : name == "baseline_uid" ? "uid" : "resourceVersion");
        resource["metadata"] = metadata;
    }
    else if (name == "deep_input") input += "deep: " + QString(3000, '[') + "0" + QString(3000, ']') + '\n';
    else if (name == "deep_alias") {
        input += "layer0: &layer0 [0]\n";
        for (int index = 1; index <= 25; ++index) input += QString("layer%1: &layer%1 [*layer%2, *layer%2]\n").arg(index).arg(index - 1);
        valid = true;
    } else if (name == "large_mapping") {
        input += "large:\n";
        for (int index = 0; index < 10000; ++index) input += QString("  key%1: text\n").arg(index);
        valid = true;
    }
    else if (name == "no_baseline") resource = {};
    else if (name == "cluster_scope" || name == "cluster_empty_scope") {
        auto metadata = resource["metadata"].toObject(); metadata.remove("namespace"); resource["metadata"] = metadata;
        input.replace("  namespace: default\n", name == "cluster_scope" ? "" : "  namespace: ''\n"); valid = true;
    } else if (name == "cluster_added_scope") {
        auto metadata = resource["metadata"].toObject(); metadata.remove("namespace"); resource["metadata"] = metadata;
    } else if (name == "secret") {
        resource["kind"] = "Secret"; input.replace("kind: ConfigMap", "kind: Secret"); input += "secret: operator-private-value\n"; valid = true;
    } else return 2;
    const auto before = input;
    const auto originalBefore = resource;
    const auto result = podlord::checkYamlDraft(input, resource);
    if ((name == "missing_name" || name == "missing_uid" || name == "missing_version" || name == "missing_namespace")
        && !result.message.contains(name == "missing_namespace" ? "scope" : "original target")) {
        std::fprintf(stderr, "Missing identity reported as the wrong failure: %s\n", result.message.toUtf8().constData());
        return 1;
    }
    if (result.valid != valid || result.message.isEmpty() || result.message.contains("operator-private-value")
        || result.message.contains("opaque/version:2") || input != before || resource != originalBefore) {
        std::fprintf(stderr, "Local draft check failed for %s: %s\n", name.toUtf8().constData(), result.message.toUtf8().constData());
        return 1;
    }
    const auto repeated = podlord::checkYamlDraft(input, resource);
    if (name == "large_mapping") {
        std::vector<qint64> samples;
        for (int index = 0; index < 50; ++index) {
            QElapsedTimer timer; timer.start();
            if (!podlord::checkYamlDraft(input, resource).valid) return 1;
            samples.push_back(timer.nsecsElapsed());
        }
        std::sort(samples.begin(), samples.end());
        std::printf("Local YAML check: 10000 fields, %lld UTF-8 bytes, 50 samples, p50 %.3f ms, p95 %.3f ms\n",
            static_cast<long long>(input.toUtf8().size()), samples[25] / 1000000.0, samples[47] / 1000000.0);
    }
    return repeated.valid == result.valid && repeated.message == result.message ? 0 : 1;
}
