#include "workspace.h"
#include "kubeconfig_store.h"
#include <QGuiApplication>
#include <QClipboard>
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
#include <QtTest/QTest>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
bool waitFor(const std::function<bool()>& predicate, int limit = 10000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QTest::qWait(10); }
    return predicate();
}
bool run(const QString& scenario, const QString& configPath) {
    const bool real = scenario == "real";
    QTemporaryDir profile;
    QTcpServer server;
    if (!profile.isValid() || (!real && !server.listen(QHostAddress::LocalHost))) return false;
    QJsonArray resources;
    for (int n = 0; n < 40; ++n) {
        const auto name = QString("entry-%1").arg(n, 2, 10, QLatin1Char('0'));
        resources.append(QJsonObject{{"apiVersion", "v1"}, {"kind", "ConfigMap"}, {"metadata", QJsonObject{
            {"name", name}, {"namespace", "default"}, {"uid", "uid-" + name}, {"resourceVersion", "1"}}},
            {"data", QJsonObject{{"setting", "before"}}}});
    }
    bool reject = false;
    int requests = 0, reads = 0, writes = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            auto bytes = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes] {
                bytes->append(socket->readAll());
                if (!bytes->contains("\r\n\r\n")) return;
                ++requests;
                const auto line = bytes->split('\n').first().split(' ');
                if (line.value(0) != "GET") ++writes;
                const auto path = QUrl(QString::fromUtf8(line.value(1))).path();
                QJsonObject result;
                int http = 200;
                if (path == "/api") result = {{"versions", QJsonArray{"v1"}}};
                else if (path == "/apis") result = {{"groups", QJsonArray{}}};
                else if (path == "/api/v1") result = {{"resources", QJsonArray{QJsonObject{
                    {"name", "configmaps"}, {"kind", "ConfigMap"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list", "delete"}}}}}};
                else if (path == "/api/v1/configmaps") result = {{"metadata", QJsonObject{}}, {"items", resources}};
                else {
                    ++reads;
                    for (const auto& value : resources) if (value.toObject()["metadata"].toObject()["name"].toString() == path.section('/', -1)) result = value.toObject();
                    if (reject) { http = scenario == "auth" ? 401 : 503; result = {{"kind", "Status"}, {"apiVersion", "v1"}, {"code", http}}; }
                    else if (result.isEmpty()) { http = 404; result = {{"kind", "Status"}, {"apiVersion", "v1"}, {"code", 404}, {"reason", "NotFound"}}; }
                }
                const auto body = QJsonDocument(result).toJson(QJsonDocument::Compact);
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(http) + " Test\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost(); bytes->clear();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto path = profile.path() + "/external.yaml";
    if (!real) {
        QFile config(path);
        if (!config.open(QIODevice::WriteOnly)) return false;
        config.write(QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: local-navigation-token\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\n- name: independent\n  context:\n    cluster: local\n    user: local\ncurrent-context: local\n").arg(server.serverPort()).toUtf8());
    }
    const auto imported = podlord::KubeconfigStore(profile.path()).importFile(real ? configPath : path);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported);
    if (!source || source->contexts.isEmpty()) return false;
    podlord::Workspace workspace(profile.path());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !waitFor([&] { return !workspace.busy(); }) || !workspace.openContext(source->contexts.first().id)
        || !waitFor([&] { return !workspace.busy() && !workspace.currentSession().isEmpty(); })) return false;
    if (scenario == "narrow") window->resize(720, 620);
    if (!waitFor([&] { return workspace.table()->rowCount() > 1 && !workspace.loading(); }, real ? 120000 : 10000)) return false;
    const QString prefix = real ? "/api/v1/namespaces/visual-a/configmaps/visual-config-" : "/api/v1/namespaces/default/configmaps/entry-";
    const auto target = [&](int n) { return prefix + QString("%1").arg(real ? n + 1 : n, real ? 4 : 2, 10, QLatin1Char('0')); };
    const auto open = [&](int n) {
        return workspace.inspectPath(target(n)) && waitFor([&] { return workspace.canEditYaml() && !workspace.loading(); }, real ? 60000 : 10000);
    };
    if (!open(0)) return false;
    const auto named = [&](const char* name) -> QQuickItem* {
        QList<QQuickItem*> pending{window->contentItem()};
        while (!pending.isEmpty()) { auto* next = pending.takeLast(); if (next->objectName() == QLatin1String(name)) return next; pending.append(next->childItems()); }
        return nullptr;
    };
    const auto click = [&](const char* name) {
        auto* item = named(name);
        if (!item || !item->isVisible() || !item->isEnabled()) return false;
        QList<QQuickItem*> parents;
        for (auto* next = item; next; next = next->parentItem()) parents.prepend(next);
        for (auto* next : parents) next->ensurePolished();
        QCoreApplication::processEvents();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
        QCoreApplication::processEvents(); return true;
    };
    const auto back = [&] { return workspace.property("canInspectBack").toBool(); };
    const auto forward = [&] { return workspace.property("canInspectForward").toBool(); };
    if (!named("inspectorBack") || !named("inspectorForward")) { std::fputs("Missing visible inspector history controls.\n", stderr); return false; }
    if (scenario.startsWith("invalid_")) {
        const auto before = requests;
        const int step = scenario == "invalid_positive" ? 2 : scenario == "invalid_negative" ? -2 : 0;
        return !workspace.navigateInspector(step) && workspace.inspectorPath() == target(0) && requests == before && !back() && !forward();
    }
    if (scenario == "initial") return !back() && !forward() && writes == 0;
    if (scenario == "repeat") return open(0) && !back() && !forward() && writes == 0;
    if (!open(1)) return false;
    if (scenario == "missing") {
        if (!open(2)) return false;
        resources.removeAt(1);
        if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && workspace.table()->rowCount() == 39; })) {
            std::fprintf(stderr, "Missing history refresh: rows=%d loading=%d requests=%d status=%s\n", workspace.table()->rowCount(), workspace.loading(), requests, qPrintable(workspace.status())); return false;
        }
        if (!click("inspectorBack") || !waitFor([&] { return workspace.inspectorPath() == target(0) && !workspace.loading(); })) {
            std::fprintf(stderr, "Missing history navigation: selected=%s back=%d forward=%d reads=%d status=%s\n", qPrintable(workspace.inspectorPath()), back(), forward(), reads, qPrintable(workspace.status())); return false;
        }
        return !back() && forward() && click("inspectorForward") && workspace.inspectorPath() == target(2) && writes == 0;
    }
    if (scenario == "filter") workspace.filter("no-visible-rows");
    if (scenario == "cached") {
        const auto before = requests;
        QCoreApplication::processEvents();
        return back() && !forward() && named("inspectorBack")->isEnabled() && requests == before;
    }
    if (scenario == "limit") {
        for (int i = 2; i < 40; ++i) if (!open(i)) return false;
        int visited = 0;
        while (back()) {
            if (!click("inspectorBack") || !waitFor([&] { return !workspace.loading(); })) return false;
            ++visited;
        }
        const bool passed = visited == 31 && workspace.inspectorPath() == target(8) && forward() && writes == 0;
        if (!passed) std::fprintf(stderr, "History cap: visits=%d selected=%s\n", visited, qPrintable(workspace.inspectorPath()));
        return passed;
    }
    if (scenario == "draft_stay" || scenario == "draft_discard" || scenario == "draft_escape" || scenario == "draft_missing") {
        if (!click("yamlButton") || !click("editYaml")) return false;
        auto* editor = named("inspectorYaml");
        if (!editor || !click("inspectorYaml")) return false;
        const auto text = workspace.yamlText() + "\n# local-navigation-draft\n";
        QGuiApplication::clipboard()->setText(text);
        QTest::keySequence(window, QKeySequence(QKeySequence::SelectAll));
        QTest::keySequence(window, QKeySequence(QKeySequence::Paste));
        if (!workspace.yamlDirty()) return false;
        const auto before = reads;
        if (!click("inspectorBack") || !workspace.discardPending() || workspace.inspectorPath() != target(1)) return false;
        if (scenario == "draft_missing") {
            resources.removeAt(0);
            if (!workspace.refresh() || !waitFor([&] { return !workspace.loading() && workspace.table()->rowCount() == 39; }) || !click("discardAccept")) return false;
            return !workspace.discardPending() && !workspace.yamlEditing() && workspace.inspectorPath() == target(1)
                && reads == before && workspace.error().contains("no longer available") && writes == 0;
        }
        if (scenario == "draft_escape") { QTest::keyClick(window, Qt::Key_Escape); }
        else if (!click(scenario == "draft_stay" ? "discardStay" : "discardAccept")) return false;
        if (scenario != "draft_discard") return !workspace.discardPending() && workspace.yamlText() == text && workspace.inspectorPath() == target(1) && reads == before;
        return waitFor([&] { return workspace.inspectorPath() == target(0) && !workspace.loading(); }) && !workspace.yamlEditing() && !back() && forward() && writes == 0;
    }
    if (scenario == "failure" || scenario == "auth") reject = true;
    if (scenario == "keyboard") {
        const auto keys = QKeySequence::keyBindings(QKeySequence::Back);
        if (keys.isEmpty()) return false;
        QTest::keySequence(window, keys.first());
    } else if (!click("inspectorBack")) return false;
    if (!waitFor([&] { return workspace.inspectorPath() == target(0) && !workspace.loading(); }, real ? 60000 : 10000)
        || back() || !forward()) return false;
    if (scenario == "back" || scenario == "filter" || scenario == "narrow") return writes == 0 && workspace.inspectorName().endsWith(real ? "0001" : "00");
    if (scenario == "failure" || scenario == "auth") {
        if (scenario == "auth" && !workspace.authenticationRequired()) return false;
        const auto before = reads;
        if (!click("inspectorForward") || workspace.inspectorPath() != target(1)) return false;
        if (scenario == "auth") return reads == before && back() && !forward() && !workspace.canEditYaml();
        return waitFor([&] { return !workspace.loading(); }) && back() && !forward() && writes == 0;
    }
    if (scenario == "branch") {
        if (!open(2) || !back() || !forward() || !click("inspectorForward")) return false;
        return workspace.inspectorPath() == target(1) && back() && !forward() && writes == 0;
    }
    if (scenario == "session") {
        const auto first = workspace.currentSession();
        if (!workspace.openContext(source->contexts.last().id) || !waitFor([&] { return !workspace.busy() && workspace.currentSession() != first; })) return false;
        if (back() || forward() || !waitFor([&] { return workspace.table()->rowCount() == 40 && !workspace.loading(); }) || !open(2) || back() || forward()) return false;
        if (!workspace.activate(first) || !waitFor([&] { return !workspace.busy(); })) return false;
        return workspace.inspectorPath() == target(0) && !back() && forward() && writes == 0;
    }
    if (scenario == "close") {
        if (!workspace.closeInspector() || back() || forward() || !open(0)) return false;
        return !back() && forward() && writes == 0;
    }
    if (!click("inspectorForward") || !waitFor([&] { return workspace.inspectorPath() == target(1) && !workspace.loading(); }, real ? 60000 : 10000)) return false;
    if (real) {
        const auto frame = qEnvironmentVariable("PODLORD_HISTORY_FRAME");
        if (!frame.isEmpty()) { QTest::qWait(120); if (!window->grabWindow().save(frame)) return false; }
    }
    return back() && !forward() && writes == 0;
}
}
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    Q_INIT_RESOURCE(workspace_ui);
    const auto args = application.arguments();
    if (!run(args.value(1), args.value(2))) { std::fprintf(stderr, "Inspector navigation scenario failed: %s\n", qPrintable(args.value(1))); return 1; }
    std::printf("Inspector navigation public UI scenario passed: %s\n", qPrintable(args.value(1))); return 0;
}
