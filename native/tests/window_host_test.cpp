#include "window_host.h"
#include "workspace.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QQmlContext>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTemporaryDir>
#include <QLockFile>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest/QTest>
#include <cstdio>

#define REQUIRE(value) do { if (!(value)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #value); return false; } } while (false)
bool unregisterWorkspaceResource() { Q_CLEANUP_RESOURCE(workspace_ui); return true; }
namespace {
bool waitFor(const std::function<bool()>& condition, int limit = 15000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QTest::qWait(10); }
    return condition();
}
bool unchanged(const podlord::SessionStore& store, const podlord::Result<podlord::SessionCatalog>& before) {
    const auto after = store.list();
    return std::holds_alternative<podlord::SessionCatalog>(after) && std::holds_alternative<podlord::SessionCatalog>(before)
        && std::get<podlord::SessionCatalog>(after) == std::get<podlord::SessionCatalog>(before);
}

// Only the external Kubernetes HTTP/WebSocket boundary is simulated.
class Kubernetes final : public QTcpServer {
public:
    QWebSocketServer streams{"Kubernetes boundary", QWebSocketServer::NonSecureMode};
    int requests = 0, upgrades = 0;
    bool authorized = true;
    QJsonObject pod(const QString& name) const {
        return {{"apiVersion", "v1"}, {"kind", "Pod"},
            {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", name + "-uid"}, {"resourceVersion", "1"}}},
            {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "alpha"}, {"image", "boundary"},
                {"ports", QJsonArray{QJsonObject{{"containerPort", 8080}}}}}}}}},
            {"status", QJsonObject{{"phase", "Running"}}}};
    }
    Kubernetes() {
        streams.setSupportedSubprotocols({"v5.channel.k8s.io", "v4.channel.k8s.io"});
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (socket->property("handled").toBool()) return;
                    const auto bytes = socket->peek(65536);
                    if (!bytes.contains("\r\n\r\n")) return;
                    socket->setProperty("handled", true);
                    authorized = authorized && bytes.toLower().contains("authorization: bearer window-test-token\r\n");
                    if (bytes.toLower().contains("upgrade: websocket")) { ++upgrades; streams.handleConnection(socket); return; }
                    socket->readAll(); ++requests;
                    const QUrl url(QString::fromUtf8(bytes.split('\n').first().split(' ').value(1)));
                    const auto path = url.path(); QJsonObject body; QByteArray payload;
                    if (path == "/api") body = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") body = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") body = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"list", "get"}}}}}};
                    else if (path == "/api/v1/pods") body = {{"apiVersion", "v1"}, {"kind", "PodList"}, {"metadata", QJsonObject{}}, {"items", QJsonArray{pod("alpha"), pod("beta")}}};
                    else if (path.endsWith("/log")) payload = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + " log " + QByteArray::number(requests) + '\n';
                    else body = pod(path.section('/', -1));
                    if (payload.isEmpty()) payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        connect(&streams, &QWebSocketServer::newConnection, &streams, [this] {
            while (auto* peer = streams.nextPendingConnection()) {
                const bool terminal = peer->requestUrl().path().endsWith("/exec");
                if (terminal) peer->sendBinaryMessage(QByteArray(1, char(1)) + "terminal remains connected\r\n$ ");
                else {
                    const int port = QUrlQuery(peer->requestUrl()).queryItemValue("ports").toInt();
                    QByteArray number; number.append(char(port & 255)); number.append(char(port >> 8));
                    peer->sendBinaryMessage(QByteArray(1, char(0)) + number);
                    peer->sendBinaryMessage(QByteArray(1, char(1)) + number);
                }
                connect(peer, &QWebSocket::binaryMessageReceived, peer, [peer, terminal](const QByteArray& bytes) {
                    if (bytes.isEmpty() || bytes[0] != char(0)) return;
                    peer->sendBinaryMessage(terminal ? QByteArray(1, char(1)) + bytes.sliced(1) : bytes);
                });
                connect(peer, &QWebSocket::disconnected, peer, &QObject::deleteLater);
            }
        });
    }
};

bool run(const QString& scenario) {
    QTemporaryDir profile; Kubernetes api;
    REQUIRE(profile.isValid() && api.listen(QHostAddress::LocalHost));
    const auto yaml = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\nusers:\n- name: local\n  user:\n    token: window-test-token\ncontexts:\n- name: first\n  context:\n    cluster: local\n    user: local\n- name: second\n  context:\n    cluster: local\n    user: local\n").arg(api.serverPort());
    const auto imported = podlord::KubeconfigStore(profile.path()).importText(profile.filePath("source.yaml"), yaml);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported); REQUIRE(source && source->contexts.size() == 2);
    podlord::SessionStore store(profile.path());
    const auto first = store.create({source->contexts[0].id, {}}, "First");
    REQUIRE(std::holds_alternative<podlord::SessionCatalog>(first));
    const auto firstId = std::get<podlord::SessionCatalog>(first).sessions.last().id;
    const auto second = store.create({source->contexts[1].id, {}}, "Second");
    REQUIRE(std::holds_alternative<podlord::SessionCatalog>(second));
    const auto secondId = std::get<podlord::SessionCatalog>(second).sessions.last().id;
    const auto a = firstId.toString(QUuid::WithoutBraces), b = secondId.toString(QUuid::WithoutBraces);
    REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(secondId)));
    REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(firstId)));
    if (scenario.startsWith("batch_")) {
        const auto before = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(before));
        const auto ids = scenario == "batch_empty" ? QList<QUuid>{} : scenario == "batch_null" ? QList<QUuid>{firstId, {}}
            : scenario == "batch_duplicate" ? QList<QUuid>{firstId, firstId}
            : scenario == "batch_missing" ? QList<QUuid>{firstId, QUuid::createUuid()} : QList<QUuid>{firstId};
        const auto closed = store.closeSessions(ids);
        if (scenario != "batch_subset") {
            REQUIRE(std::holds_alternative<podlord::Failure>(closed));
            REQUIRE(unchanged(store, before)); return true;
        }
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(closed));
        const auto catalog = std::get<podlord::SessionCatalog>(closed);
        REQUIRE(!catalog.sessions[0].open && catalog.sessions[1].open && !catalog.activeSession);
        REQUIRE(catalog.sessions[0].usageAt == std::get<podlord::SessionCatalog>(before).sessions[0].usageAt);
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.closeSessions(ids))); return true;
    }
    podlord::Workspace primary(profile.path());
    if (scenario == "missing_resource") {
        REQUIRE(unregisterWorkspaceResource());
        podlord::WindowHost unavailable(primary);
        REQUIRE(!unavailable.open() && unavailable.windows().isEmpty()); return true;
    }
    podlord::WindowHost host(primary);
    REQUIRE(host.open() && host.windows().size() == 1);
    auto* main = host.windows().first();
    REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading() && !primary.filterPresetsBusy() && primary.resourceCount() == 2; }));
    REQUIRE(primary.windowActionsAvailable() && primary.tabs().size() == 2);
    if (scenario == "repeat") { REQUIRE(host.open() && host.windows().size() == 1 && primary.tabs().size() == 2); return true; }
    if (scenario == "missing") { REQUIRE(!primary.detachSession("missing") && host.windows().size() == 1); return true; }
    if (scenario == "root_restore") {
        REQUIRE(main->close()); REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        const auto catalog = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(catalog));
        REQUIRE(std::get<podlord::SessionCatalog>(catalog).sessions[0].open && std::get<podlord::SessionCatalog>(catalog).sessions[1].open); return true;
    }
    const auto path = QString("/api/v1/namespaces/default/pods/alpha");
    REQUIRE(primary.inspectPath(path));
    REQUIRE(waitFor([&] { return primary.canEditYaml(); }));
    if (scenario == "alerts_initial") {
        REQUIRE(waitFor([&] { return !primary.alerts()->busy(); }));
        REQUIRE(primary.alerts()->setPreferences(true, true));
        REQUIRE(waitFor([&] { return !primary.alerts()->busy() && primary.alerts()->muted(); }));
    }
    if (scenario == "reopen") {
        REQUIRE(primary.close(a)); REQUIRE(waitFor([&] { return !primary.busy(); }));
        REQUIRE(primary.activate(b)); REQUIRE(waitFor([&] { return !primary.busy() && primary.resourceCount() == 2; }));
        REQUIRE(primary.openSessionWindow(a));
    } else if (scenario == "context_window") {
        REQUIRE(primary.openContext(source->contexts[0].id, true));
    } else if (scenario == "menu_window") {
        const auto click = [&](const QString& name) {
            return podlord::test::clickVisible(main, name);
        };
        REQUIRE(click("quickOpenDropdown"));
        REQUIRE(waitFor([&] { return podlord::test::visibleItem(main->contentItem(), "quickOpenWindow") != nullptr; }));
        REQUIRE(click("quickOpenWindow"));
        REQUIRE(waitFor([&] {
            auto* menu = main->findChild<QObject*>("quickOpenWindowMenu");
            return menu && menu->property("opened").toBool();
        }));
        REQUIRE(waitFor([&] { return podlord::test::visibleItem(main->contentItem(), "quickWindowSession_" + a) != nullptr; }));
        const QPointer<QQuickItem> entry(podlord::test::visibleItem(main->contentItem(), "quickWindowSession_" + a));
        QSignalSpy triggered(entry, SIGNAL(triggered()));
        REQUIRE(triggered.isValid());
        REQUIRE(click("quickWindowSession_" + a));
        if (!waitFor([&] { return !triggered.isEmpty(); }, 2000)) {
            if (entry) std::fprintf(stderr, "Separate-window menu did not receive the pointer: position=(%g,%g) size=%gx%g sameWindow=%d enabled=%d\n",
                entry->mapToScene({entry->width()/2, entry->height()/2}).x(), entry->mapToScene({entry->width()/2, entry->height()/2}).y(),
                entry->width(), entry->height(), entry->window() == main, entry->isEnabled());
            return false;
        }
    } else if (scenario == "new_context_window") {
        REQUIRE(primary.close(b)); REQUIRE(waitFor([&] { return !primary.busy(); }));
        const auto current = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(current));
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.remove(secondId)));
        REQUIRE(primary.reload()); REQUIRE(waitFor([&] { return !primary.busy(); }));
        REQUIRE(primary.beginYamlEdit() && primary.setYamlDraft(primary.yamlText() + "\n# existing editor remains here\n"));
        REQUIRE(primary.openContext(source->contexts[1].id, true));
        REQUIRE(waitFor([&] { return host.windows().size() == 2; }));
        auto* opened = qobject_cast<podlord::Workspace*>(QQmlEngine::contextForObject(host.windows().last())->contextProperty("workspace").value<QObject*>());
        REQUIRE(opened && opened->currentSession() != a && primary.currentSession() == a && primary.tabs().size() == 1 && primary.yamlDirty() && !primary.discardPending());
        REQUIRE(waitFor([&] { return !opened->busy() && opened->resourceCount() == 2; })); return true;
    } else
    if (scenario == "dirty_stay" || scenario == "dirty_discard") {
        REQUIRE(primary.beginYamlEdit() && primary.setYamlDraft(primary.yamlText() + "\n# local draft\n"));
        REQUIRE(primary.yamlDirty() && !primary.detachSession(a) && primary.discardPending());
        REQUIRE(host.windows().size() == 1);
        REQUIRE(primary.confirmDiscard(scenario == "dirty_discard"));
        if (scenario == "dirty_stay") { REQUIRE(primary.yamlDirty() && host.windows().size() == 1); return true; }
    } else {
        if (scenario == "filter") { REQUIRE(primary.filter("=alpha")); QTest::qWait(100); }
        if (scenario == "logs") { REQUIRE(primary.setLogsVisible(true)); REQUIRE(waitFor([&] { return primary.logRows()->rowCount() > 0; })); }
        if (scenario == "terminal" || scenario == "close_terminal") {
            REQUIRE(primary.startContainerTerminal("alpha", "/bin/sh"));
            REQUIRE(waitFor([&] { return primary.containerTerminal() && primary.containerTerminal()->connected(); }));
        }
        if (scenario == "forward" || scenario == "close_isolation") {
            REQUIRE(primary.preparePortForward() && primary.startPreparedPortForward(primary.portForwardTarget()["localPort"].toString(), "8080"));
            REQUIRE(waitFor([&] { return primary.portForwards().size() == 1; }));
            if (scenario == "close_isolation") {
                REQUIRE(primary.activate(b)); REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading(); }));
                REQUIRE(primary.inspectPath(path) && primary.preparePortForward() && primary.startPreparedPortForward(primary.portForwardTarget()["localPort"].toString(), "8080"));
                REQUIRE(waitFor([&] { return primary.portForwards().size() == 1; }));
                REQUIRE(primary.activate(a)); REQUIRE(waitFor([&] { return !primary.busy(); }));
            }
        }
        if (scenario == "background" || scenario == "background_dirty") {
            REQUIRE(primary.activate(b)); REQUIRE(waitFor([&] { return !primary.busy() && primary.resourceCount() == 2 && primary.loadingProgress() == 1; }));
            if (scenario == "background_dirty") {
                REQUIRE(primary.inspectPath(path)); REQUIRE(waitFor([&] { return primary.canEditYaml(); }));
                REQUIRE(primary.beginYamlEdit() && primary.setYamlDraft(primary.yamlText() + "\n# unrelated draft\n"));
            }
        }
        auto* button = podlord::test::visibleItem(main->contentItem(), "detachSession_" + a); REQUIRE(button && button->isEnabled());
        QTest::mouseClick(main, Qt::LeftButton, Qt::NoModifier, button->mapToScene({button->width()/2, button->height()/2}).toPoint());
    }
    if (!waitFor([&] { return host.windows().size() == 2; })) {
        std::fprintf(stderr, "Separate window not opened: busy=%d presetBusy=%d discard=%d error=%s\n", primary.busy(), primary.filterPresetsBusy(), primary.discardPending(), qPrintable(primary.error()));
        const auto frame = qEnvironmentVariable("PODLORD_WINDOW_FAILURE_FRAME");
        if (!frame.isEmpty()) main->grabWindow().save(frame);
        return false;
    }
    auto* window = host.windows().last();
    auto* detached = qobject_cast<podlord::Workspace*>(QQmlEngine::contextForObject(window)->contextProperty("workspace").value<QObject*>());
    REQUIRE(detached && detached != &primary && detached->currentSession() == a);
    if (scenario == "alerts_initial") {
        REQUIRE(detached->alerts()->muted() && detached->alerts()->reducedMotion());
        REQUIRE(waitFor([&] { return !detached->alerts()->busy(); }));
        REQUIRE(detached->alerts()->muted() && detached->alerts()->reducedMotion()); return true;
    }
    if (scenario == "reopen") REQUIRE(waitFor([&] { return !detached->busy() && detached->canEditYaml(); }));
    REQUIRE(primary.tabs().size() == 1 && primary.tabs()[0].toMap()["id"].toString() == b);
    REQUIRE(detached->tabs().size() == 1 && detached->tabs()[0].toMap()["id"].toString() == a);
    REQUIRE(primary.currentSession() == b && detached->loadingProgress() == 1 && api.authorized);
    REQUIRE(waitFor([&] { return !primary.busy() && primary.resourceCount() == 2; }));
    if (scenario == "nested") {
        REQUIRE(detached->duplicateSession(a, "Third window"));
        QString third;
        REQUIRE(waitFor([&] {
            for (const auto& row : detached->sessions()) if (row.toMap()["name"].toString() == "Third window") third = row.toMap()["id"].toString();
            return !third.isEmpty() && !detached->busy();
        }));
        REQUIRE(detached->activate(third));
        REQUIRE(waitFor([&] { return !detached->busy() && detached->tabs().size() == 2 && detached->resourceCount() == 2; }));
        REQUIRE(detached->openSessionWindow(third) && host.windows().size() == 3);
        auto* thirdWindow = host.windows().last();
        auto* thirdWorkspace = qobject_cast<podlord::Workspace*>(QQmlEngine::contextForObject(thirdWindow)->contextProperty("workspace").value<QObject*>());
        REQUIRE(thirdWorkspace && thirdWorkspace->currentSession() == third && detached->currentSession() == a && primary.currentSession() == b);
        thirdWindow->close(); REQUIRE(waitFor([&] { return host.windows().size() == 2; }));
        REQUIRE(detached->tabs().size() == 1 && primary.tabs().size() == 1); return true;
    }
    if (scenario == "background_dirty") { REQUIRE(primary.yamlDirty() && !primary.discardPending() && !detached->yamlEditing()); return true; }
    if (scenario == "cross_focus") {
        const auto before = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(before));
        const auto prior = std::get<podlord::SessionCatalog>(before);
        REQUIRE(prior.activeSession == firstId);
        REQUIRE(primary.activate(b));
        REQUIRE(waitFor([&] {
            const auto saved = store.list();
            return !primary.busy() && std::holds_alternative<podlord::SessionCatalog>(saved)
                && std::get<podlord::SessionCatalog>(saved).activeSession == secondId;
        }));
        const auto selected = std::get<podlord::SessionCatalog>(store.list());
        REQUIRE(selected.sessions[1].usageAt.size() == prior.sessions[1].usageAt.size() + 1);
        REQUIRE(detached->activate(a));
        REQUIRE(waitFor([&] {
            const auto saved = store.list();
            return !detached->busy() && std::holds_alternative<podlord::SessionCatalog>(saved)
                && std::get<podlord::SessionCatalog>(saved).activeSession == firstId
                && primary.sessions().first().toMap()["id"].toString() == a
                && detached->sessions().first().toMap()["id"].toString() == a;
        }));
        const auto repeated = store.list();
        REQUIRE(primary.activate(a) && unchanged(store, repeated)); return true;
    }
    if (scenario == "focus") {
        const auto before = store.list();
        REQUIRE(primary.activate(a) && primary.currentSession() == b && host.windows().size() == 2);
        REQUIRE(detached->detachSession(a) && unchanged(store, before)); return true;
    }
    if (scenario == "filter") { REQUIRE(detached->filterText() == "=alpha" && detached->resourceCount() == 1 && primary.filterText().isEmpty()); return true; }
    if (scenario == "rename") {
        REQUIRE(primary.renameSession(a, "Detached renamed"));
        REQUIRE(waitFor([&] { return !primary.busy() && detached->title().contains("Detached renamed"); })); return true;
    }
    if (scenario == "settings") {
        REQUIRE(detached->saveReadSettings(600, 5, "5"));
        REQUIRE(waitFor([&] { return !detached->busy() && primary.requestLimit() == 600; }));
        REQUIRE(primary.saveRadarWater(false, 77));
        REQUIRE(waitFor([&] { return !primary.busy() && !detached->radarWaterEnabled() && detached->radarWaterSpeedPercent() == 77; })); return true;
    }
    if (scenario == "preset") {
        REQUIRE(detached->filter("=alpha") && detached->saveFilterPreset("Shared filter"));
        REQUIRE(waitFor([&] { return !detached->filterPresetsBusy() && primary.filterPresets().contains("Shared filter"); }));
        REQUIRE(primary.loadFilterPreset("Shared filter") && primary.resourceCount() == 1); return true;
    }
    if (scenario == "columns") {
        auto columns = detached->resourceColumns(); REQUIRE(columns.size() > 1);
        auto firstColumn = columns[0].toMap(); firstColumn["visible"] = false; firstColumn["pinned"] = false; columns[0] = firstColumn;
        REQUIRE(detached->saveTableLayout("resource", columns));
        REQUIRE(waitFor([&] { return !primary.resourceColumns()[0].toMap()["visible"].toBool(); })); return true;
    }
    if (scenario == "alerts") {
        REQUIRE(waitFor([&] { return !primary.alerts()->busy() && !detached->alerts()->busy(); }));
        REQUIRE(detached->alerts()->setPreferences(true, true));
        REQUIRE(waitFor([&] { return !detached->alerts()->busy() && primary.alerts()->muted() && primary.alerts()->reducedMotion(); })); return true;
    }
    if (scenario == "source" || scenario == "source_dirty") {
        if (scenario == "source_dirty") {
            REQUIRE(detached->beginYamlEdit() && detached->setYamlDraft(detached->yamlText() + "\n# preserve draft\n"));
        }
        REQUIRE(primary.requestSourceRemoval(source->contexts[0].id));
        if (scenario == "source_dirty") {
            REQUIRE(!primary.confirmSourceRemoval() && detached->yamlDirty() && detached->currentSession() == a); return true;
        }
        REQUIRE(primary.confirmSourceRemoval());
        REQUIRE(waitFor([&] { return !primary.busy() && detached->currentSession().isEmpty(); }));
        REQUIRE(detached->tabs().isEmpty() && primary.tabs().size() == 1 && detached->contexts().size() == 1 && primary.contexts().size() == 1); return true;
    }
    if (scenario == "logs") { REQUIRE(detached->logsVisible() && detached->logRows()->rowCount() > 0 && !primary.logsVisible()); return true; }
    if (scenario == "terminal" || scenario == "close_terminal") {
        REQUIRE(detached->containerTerminal() && detached->containerTerminal()->connected() && api.upgrades == 1);
        if (scenario == "terminal") {
            auto* surface = qobject_cast<podlord::TerminalSurface*>(podlord::test::visibleItem(window->contentItem(), "terminalSurface")); REQUIRE(surface);
            surface->forceActiveFocus(); QTest::keyClick(window, Qt::Key_X);
            REQUIRE(waitFor([&] { return surface->visibleText().contains("$ x"); })); return true;
        }
        QPointer<podlord::ContainerTerminal> terminal = detached->containerTerminal();
        window->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
        REQUIRE(!terminal || !terminal->active()); return true;
    }
    if (scenario == "forward" || scenario == "close_isolation") {
        REQUIRE(detached->portForwards().size() == 1);
        const auto port = detached->portForwards()[0].toMap()["localPort"].toUInt();
        QTcpSocket connection; connection.connectToHost(QHostAddress::LocalHost, port);
        REQUIRE(waitFor([&] { return connection.state() == QAbstractSocket::ConnectedState; }));
        REQUIRE(connection.write("still-open") == 10);
        REQUIRE(waitFor([&] { return connection.bytesAvailable() >= 10; })); REQUIRE(connection.readAll() == "still-open");
        if (scenario == "forward") { REQUIRE(api.upgrades == 1); return true; }
        const auto other = primary.portForwards()[0].toMap()["localPort"].toUInt();
        window->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
        QTcpServer probe; REQUIRE(probe.listen(QHostAddress::LocalHost, port));
        QTcpSocket retained; retained.connectToHost(QHostAddress::LocalHost, other);
        REQUIRE(waitFor([&] { return retained.state() == QAbstractSocket::ConnectedState; }));
        REQUIRE(retained.write("other-open") == 10);
        REQUIRE(waitFor([&] { return retained.bytesAvailable() >= 10; })); REQUIRE(retained.readAll() == "other-open"); return true;
    }
    if (scenario == "close_locked") {
        REQUIRE(waitFor([&] { return !detached->busy(); }));
        const auto before = store.list();
        QLockFile lock(profile.filePath("sessions.lock")); REQUIRE(lock.tryLock(0));
        REQUIRE(!window->close());
        REQUIRE(waitFor([&] { return !detached->busy() && !detached->error().isEmpty(); }));
        REQUIRE(host.windows().size() == 2 && detached->currentSession() == a && detached->tabs().size() == 1 && unchanged(store, before));
        lock.unlock(); window->close();
        REQUIRE(waitFor([&] { return host.windows().size() == 1; })); return true;
    }
    if (scenario == "close" || scenario == "main_close") {
        (scenario == "close" ? window : main)->close();
        REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
        const auto saved = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(saved));
        const auto catalog = std::get<podlord::SessionCatalog>(saved);
        REQUIRE(catalog.sessions[0].open == (scenario == "main_close") && catalog.sessions[1].open == (scenario == "close"));
        REQUIRE(scenario == "main_close" ? detached->resourceCount() == 2 : primary.resourceCount() == 2); return true;
    }
    REQUIRE(scenario == "active" || scenario == "background" || scenario == "dirty_discard" || scenario == "reopen" || scenario == "context_window" || scenario == "menu_window"); return true;
}

bool realTransfer(const QString& configPath) {
    QTemporaryDir profile; REQUIRE(profile.isValid());
    const podlord::KubeconfigStore sources(profile.path());
    const auto imported = sources.importFile(configPath);
    const auto* source = std::get_if<podlord::SourceSnapshot>(&imported); REQUIRE(source && !source->contexts.isEmpty());
    const auto connection = sources.connection(source->contexts.first().id);
    const auto* local = std::get_if<podlord::ClusterConnection>(&connection);
    REQUIRE(local && (local->server.host() == "127.0.0.1" || local->server.host() == "localhost" || local->server.host() == "::1"));
    podlord::Workspace primary(profile.path()); podlord::WindowHost host(primary); REQUIRE(host.open());
    REQUIRE(waitFor([&] { return !primary.busy(); }));
    REQUIRE(primary.openContext(source->contexts.first().id));
    REQUIRE(waitFor([&] { return !primary.busy() && primary.loadingProgress() == 1 && primary.resourceCount() >= 1000; }, 120000));
    const auto a = primary.currentSession();
    const auto echo = QString("/api/v1/namespaces/visual-a/pods/podlord-forward-echo");
    const auto forward = [&] {
        return primary.inspectPath(echo) && waitFor([&] { return !primary.loading(); }, 60000) && primary.preparePortForward()
            && primary.startPreparedPortForward(primary.portForwardTarget()["localPort"].toString(), "8080")
            && waitFor([&] { return primary.portForwards().size() == 1 && !primary.loading(); }, 60000);
    };
    REQUIRE(forward());
    const auto firstTask = primary.portForwards().first().toMap(); const auto firstPort = firstTask["localPort"].toUInt();
    REQUIRE(primary.duplicateSession(a, "Independent")); REQUIRE(waitFor([&] { return !primary.busy(); }));
    QString b;
    REQUIRE(waitFor([&] {
        for (const auto& row : primary.sessions()) if (row.toMap()["name"].toString() == "Independent") b = row.toMap()["id"].toString();
        return !b.isEmpty() && !primary.busy();
    }));
    REQUIRE(primary.activate(b));
    REQUIRE(waitFor([&] { return !primary.busy() && primary.loadingProgress() == 1; }, 120000)); REQUIRE(forward());
    const auto secondTask = primary.portForwards().first().toMap(); const auto secondPort = secondTask["localPort"].toUInt();
    REQUIRE(primary.activate(a)); REQUIRE(waitFor([&] { return !primary.busy(); }));
    REQUIRE(primary.inspectPath("/api/v1/namespaces/visual-a/pods/visual-multi-container"));
    REQUIRE(waitFor([&] { return !primary.loading(); }, 60000) && primary.startContainerTerminal("alpha", "/bin/sh"));
    REQUIRE(waitFor([&] { return primary.containerTerminal() && primary.containerTerminal()->connected(); }, 60000));
    QPointer<podlord::ContainerTerminal> terminal = primary.containerTerminal();
    auto* main = host.windows().first();
    auto* before = qobject_cast<podlord::TerminalSurface*>(podlord::test::visibleItem(main->contentItem(), "terminalSurface")); REQUIRE(before);
    const auto type = [](QQuickWindow* window, const QByteArray& command) {
        for (const char letter : command) QTest::keyClick(window, letter);
        QTest::keyClick(window, Qt::Key_Return);
    };
    REQUIRE(podlord::test::clickVisible(main, "terminalSurface"));
    type(main, "export PODLORD_WINDOW_COOKIE=stays_in_same_shell; printf '\\nBEFORE_%s_END\\n' \"$PODLORD_WINDOW_COOKIE\"");
    if (!waitFor([&] { return before->visibleText().contains("BEFORE_stays_in_same_shell_END"); }, 15000)) {
        std::fprintf(stderr, "Before transfer shell output: %s\n", qPrintable(before->visibleText())); return false;
    }
    REQUIRE(waitFor([&] { return primary.detachSession(a); })); REQUIRE(host.windows().size() == 2);
    auto* window = host.windows().last();
    auto* detached = qobject_cast<podlord::Workspace*>(QQmlEngine::contextForObject(window)->contextProperty("workspace").value<QObject*>());
    REQUIRE(detached && detached->containerTerminal() == terminal && terminal->connected() && detached->loadingProgress() == 1);
    REQUIRE(detached->portForwards().first().toMap()["id"] == firstTask["id"] && detached->portForwards().first().toMap()["localPort"].toUInt() == firstPort);
    auto* after = qobject_cast<podlord::TerminalSurface*>(podlord::test::visibleItem(window->contentItem(), "terminalSurface")); REQUIRE(after);
    REQUIRE(podlord::test::clickVisible(window, "terminalSurface"));
    type(window, "printf '\\nCOOKIE_%s_END\\n' \"$PODLORD_WINDOW_COOKIE\"");
    if (!waitFor([&] { return after->visibleText().contains("COOKIE_stays_in_same_shell_END"); }, 15000)) {
        std::fprintf(stderr, "After transfer shell output: %s\n", qPrintable(after->visibleText())); return false;
    }
    const auto traffic = [&](quint16 port) {
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, port);
        if (!waitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; })) return false;
        socket.write("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"); QByteArray result;
        return waitFor([&] { result += socket.readAll(); return result.contains("podlord-native-forward"); }, 60000);
    };
    REQUIRE(traffic(firstPort));
    const auto evidence = qEnvironmentVariable("PODLORD_WINDOW_EVIDENCE");
    if (!evidence.isEmpty()) {
        REQUIRE(main->grabWindow().save(evidence + "-main.png"));
        REQUIRE(window->grabWindow().save(evidence + "-detached.png"));
    }
    window->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
    REQUIRE(!terminal || !terminal->active());
    QTcpServer probe; REQUIRE(probe.listen(QHostAddress::LocalHost, firstPort));
    REQUIRE(primary.currentSession() == b && primary.portForwards().first().toMap()["id"] == secondTask["id"] && traffic(secondPort));
    std::fprintf(stdout, "Real Kubernetes: 1000+ resources, same remote shell and first forward after transfer; detached close releases only its port and terminal.\n");
    return true;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); Q_INIT_RESOURCE(workspace_ui);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "real_transfer") return realTransfer(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
    return argc == 2 && run(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
