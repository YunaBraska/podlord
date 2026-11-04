#include "window_host.h"
#include "window_layout.h"
#include "read_settings.h"
#include "workspace.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QEvent>
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
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThreadPool>
#include <QUrlQuery>
#include <QtTest/QTest>
#include <qpa/qplatformnativeinterface.h>
#include <cstdio>
#include <memory>

QT_BEGIN_NAMESPACE
class QOffscreenPlatformNativeInterface;
QT_END_NAMESPACE

#define REQUIRE(value) do { if (!(value)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #value); return false; } } while (false)
bool unregisterWorkspaceResource() { Q_CLEANUP_RESOURCE(podlord_workspace_raw_qml_0); Q_CLEANUP_RESOURCE(workspace_ui); return true; }
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
QByteArray fileBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Only the external Kubernetes HTTP/WebSocket boundary is simulated.
class Kubernetes final : public QTcpServer {
public:
    QWebSocketServer streams{"Kubernetes boundary", QWebSocketServer::NonSecureMode};
    int requests = 0, upgrades = 0;
    QString nameSpace = "default";
    QStringList names{"alpha", "beta"};
    bool hasEvents = false;
    bool authorized = true;
    QJsonObject pod(const QString& name) const {
        return {{"apiVersion", "v1"}, {"kind", "Pod"},
            {"metadata", QJsonObject{{"name", name}, {"namespace", nameSpace}, {"uid", nameSpace + name + "-uid"}, {"resourceVersion", "1"}}},
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
                    else if (path == "/api/v1") {
                        QJsonArray resources{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"list", "get"}}}};
                        if (hasEvents) resources.append(QJsonObject{{"name", "events"}, {"kind", "Event"}, {"namespaced", true}, {"verbs", QJsonArray{"list", "get"}}});
                        body = {{"resources", resources}};
                    }
                    else if (path == "/api/v1/pods") {
                        QJsonArray rows; for (const auto& name : names) rows.append(pod(name));
                        body = {{"apiVersion", "v1"}, {"kind", "PodList"}, {"metadata", QJsonObject{}}, {"items", rows}};
                    }
                    else if (path == "/api/v1/events") body = {{"apiVersion", "v1"}, {"kind", "EventList"}, {"metadata", QJsonObject{}},
                        {"items", QJsonArray{QJsonObject{{"apiVersion", "v1"}, {"kind", "Event"},
                            {"metadata", QJsonObject{{"name", "first-event"}, {"namespace", nameSpace}, {"uid", "first-event-uid"}, {"resourceVersion", "1"}}},
                            {"involvedObject", QJsonObject{{"kind", "Pod"}, {"name", "alpha"}, {"namespace", nameSpace}}},
                            {"type", "Normal"}, {"reason", "Scheduled"}, {"message", "First session only"}, {"count", 1},
                            {"lastTimestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}}};
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
    const bool handoff = scenario == "handoff" || scenario == "handoff_grow" || scenario == "handoff_shrink" || scenario == "handoff_events" || scenario == "handoff_sort" || scenario == "snapshot_detach";
    Kubernetes other;
    if (handoff) REQUIRE(other.listen(QHostAddress::LocalHost));
    if (scenario == "handoff_grow") api.names.append("gamma");
    api.hasEvents = scenario == "handoff_events";
    other.nameSpace = "second";
    if (scenario == "handoff_shrink") other.names.append("gamma");
    const int firstCount = api.names.size() + int(api.hasEvents), secondCount = other.names.size();
    const auto extraCluster = handoff ? QString("- name: other\n  cluster:\n    server: http://127.0.0.1:%1\n").arg(other.serverPort()) : QString{};
    const auto yaml = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\n%2users:\n- name: local\n  user:\n    token: window-test-token\ncontexts:\n- name: first\n  context:\n    cluster: local\n    user: local\n- name: second\n  context:\n    cluster: %3\n    user: local\n").arg(api.serverPort()).arg(extraCluster, handoff ? QString("other") : QString("local"));
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
    if (scenario == "layout_restore_tiny_screen" || scenario == "layout_restore_screen_removed") {
        REQUIRE(QGuiApplication::platformName() == "offscreen");
        auto* native = QGuiApplication::platformNativeInterface();
        REQUIRE(native);
        // Qt's offscreen plugin exposes these test-only OS configuration entrypoints.
        // Keep their exact Qt 6.11 signatures. No WindowHost internals are changed.
        using Offscreen = QT_PREPEND_NAMESPACE(QOffscreenPlatformNativeInterface);
        const auto configure = reinterpret_cast<void (*)(const QJsonObject&, Offscreen*)>(
            native->nativeResourceForIntegration("setConfiguration"));
        const auto configuration = reinterpret_cast<QJsonObject (*)(Offscreen*)>(
            native->nativeResourceForIntegration("configuration"));
        REQUIRE(configure && configuration);
        auto* offscreen = reinterpret_cast<Offscreen*>(native);
        const auto originalConfiguration = configuration(offscreen);
        REQUIRE(QGuiApplication::screens().size() == 1);
        const podlord::WindowLayout layout{{"main", {firstId, secondId}, secondId, QRect(20, 20, 640, 480), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        podlord::Workspace restored(profile.path());
        REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && restored.tabs().size() == 2; }));
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        const auto layoutBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sourceBytes = fileBytes(source->ownedPath);
        auto* pool = QThreadPool::globalInstance();
        REQUIRE(pool->waitForDone(15000));
        const int previousMaximum = pool->maxThreadCount();
        const auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
        std::unique_ptr<podlord::WindowHost> restoredHost;
        bool blocked = true;
        const auto releaseWorkers = qScopeGuard([&] {
            if (blocked) release->release();
            pool->setMaxThreadCount(previousMaximum);
        });
        const auto restorePlatform = qScopeGuard([&] { configure(originalConfiguration, offscreen); });
        pool->setMaxThreadCount(1);
        pool->start([entered, release] { entered->release(); release->acquire(); });
        REQUIRE(waitFor([&] { return entered->available() == 1; }));
        restoredHost = std::make_unique<podlord::WindowHost>(restored);
        QSignalSpy opened(restoredHost.get(), &podlord::WindowHost::windowOpened);
        REQUIRE(restoredHost->open() && restoredHost->windows().size() == 1 && !restoredHost->restorationReady());
        auto* window = restoredHost->windows().first();
        QSignalSpy removed(qGuiApp, &QGuiApplication::screenRemoved);
        const bool tiny = scenario == "layout_restore_tiny_screen";
        auto changedConfiguration = originalConfiguration;
        if (tiny) {
            auto screens = changedConfiguration["screens"].toArray();
            REQUIRE(screens.size() == 1);
            auto screen = screens[0].toObject();
            screen["width"] = 1; screen["height"] = 1;
            screens[0] = screen; changedConfiguration["screens"] = screens;
        } else changedConfiguration["screens"] = QJsonArray{};
        configure(changedConfiguration, offscreen);
        REQUIRE(waitFor([&] { return tiny ? window->screen() && window->screen()->availableGeometry().size() == QSize(1, 1)
            : QGuiApplication::screens().isEmpty() && !window->screen() && removed.size() == 1; }));
        if (tiny) REQUIRE(window->screen()->availableGeometry().marginsRemoved(window->frameMargins()).isEmpty());
        REQUIRE(unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(profile.filePath("window-layout.json")) == layoutBytes && fileBytes(source->ownedPath) == sourceBytes
            && api.requests == 0 && api.upgrades == 0);
        release->release(); blocked = false; pool->setMaxThreadCount(previousMaximum);
        REQUIRE(waitFor([&] { return restoredHost->restorationReady() && !restored.busy(); }));
        const auto geometry = window->geometry();
        std::fprintf(stderr, "Platform restore: tiny=%d screens=%d windowScreen=%d geometry=%d,%d %dx%d frame=%d,%d %dx%d opened=%d requests=%d upgrades=%d\n",
            tiny, int(QGuiApplication::screens().size()), bool(window->screen()), geometry.x(), geometry.y(), geometry.width(), geometry.height(),
            window->frameGeometry().x(), window->frameGeometry().y(), window->frameGeometry().width(), window->frameGeometry().height(),
            int(opened.size()), api.requests, api.upgrades);
        REQUIRE(restoredHost->windows().size() == 1 && opened.size() == 1 && restored.currentSession() == b
            && restored.tabs().size() == 2 && restored.tabs()[0].toMap()["id"].toString() == a
            && restored.tabs()[1].toMap()["id"].toString() == b && restored.mapOverview() && restored.totalResourceCount() == 0
            && restored.error().isEmpty() && !restored.authenticationRequired() && !restored.authenticationRunning()
            && window->windowState() == Qt::WindowNoState && geometry.isValid());
        if (tiny) REQUIRE(geometry.size() == QSize(1, 1) && window->frameGeometry().topLeft() == QPoint(0, 0));
        else REQUIRE(!window->screen() && geometry == layout.first().geometry);
        REQUIRE(unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(profile.filePath("window-layout.json")) == layoutBytes && fileBytes(source->ownedPath) == sourceBytes
            && api.requests == 0 && api.upgrades == 0);
        configure(originalConfiguration, offscreen);
        REQUIRE(QGuiApplication::primaryScreen());
        window->setScreen(QGuiApplication::primaryScreen());
        REQUIRE(window->screen() == QGuiApplication::primaryScreen());
        const auto finalGeometry = window->geometry();
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(waitFor([&] { return restoredHost->windows().isEmpty(); }));
        const auto saved = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved));
        const auto value = std::get<podlord::WindowLayout>(saved);
        REQUIRE(value.size() == 1 && value.first().id == "main" && value.first().sessions == QSet<QUuid>({firstId, secondId})
            && value.first().activeSession == secondId && value.first().geometry == finalGeometry && !value.first().maximized
            && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
        return true;
    }
    if (scenario == "layout_restore_deferred_open" || scenario == "layout_restore_active_fallback" || scenario == "layout_restore_empty_main"
        || scenario == "layout_restore_view_error" || scenario == "layout_decision_repeat_quit"
        || scenario == "layout_stay_preserves_import_error") {
        const bool fallback = scenario == "layout_restore_active_fallback";
        const bool empty = scenario == "layout_restore_empty_main";
        const bool viewError = scenario == "layout_restore_view_error";
        const QString peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QUuid peerSession = firstId;
        podlord::WindowLayout layout{{"main", {secondId}, secondId, QRect(20, 20, 640, 480), false},
            {peerId, {firstId}, firstId, QRect(50, 50, 600, 420), false}};
        if (fallback) {
            const auto third = store.create({source->contexts[0].id, {}}, "Third");
            REQUIRE(std::holds_alternative<podlord::SessionCatalog>(third));
            peerSession = std::get<podlord::SessionCatalog>(third).sessions.last().id;
            REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(peerSession)));
            REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(secondId)));
            layout[0].sessions.insert(firstId); layout[0].activeSession.reset();
            layout[1].sessions = {peerSession}; layout[1].activeSession.reset();
        } else if (empty) {
            REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.closeSessions({firstId, secondId})));
            layout = {{"main", {}, {}, QRect(20, 20, 640, 480), false}};
        }
        const QString viewPath = profile.filePath("views/" + a + ".json");
        const QByteArray invalidViews("{invalid views}");
        if (viewError) {
            REQUIRE(QDir().mkpath(profile.filePath("views")));
            REQUIRE(QFile::setPermissions(profile.filePath("views"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
            QFile file(viewPath);
            REQUIRE(file.open(QIODevice::WriteOnly) && file.write(invalidViews) == invalidViews.size());
        }
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        const auto savedBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sourceBytes = fileBytes(source->ownedPath);
        REQUIRE(!sessionBytes.isEmpty() && !savedBytes.isEmpty() && !sourceBytes.isEmpty());
        podlord::Workspace restored(profile.path());
        if (scenario == "layout_restore_deferred_open")
            REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && restored.tabs().size() == 2; }));
        podlord::WindowHost restoredHost(restored);
        if (scenario == "layout_restore_deferred_open") {
            QSignalSpy opened(&restoredHost, &podlord::WindowHost::windowOpened);
            REQUIRE(QThreadPool::globalInstance()->waitForDone(15000));
            QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
            REQUIRE(restoredHost.windows().isEmpty() && opened.isEmpty() && !restoredHost.applicationClosing()
                && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
            REQUIRE(restoredHost.open() && restoredHost.windows().size() == 2 && opened.size() == 2);
            REQUIRE(restoredHost.open() && restoredHost.windows().size() == 2 && opened.size() == 2);
        } else REQUIRE(restoredHost.open());
        REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy()
            && restoredHost.windows().size() == (empty ? 1 : 2)
            && restored.tabs().size() == (empty ? 0 : fallback ? 2 : 1)
            && restored.currentSession() == (empty ? QString{} : b); }));
        QQuickWindow* rootWindow = nullptr;
        QQuickWindow* peerWindow = nullptr;
        podlord::Workspace* peerOwner = nullptr;
        for (auto* candidate : restoredHost.windows()) {
            auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(candidate)->contextProperty("workspace").value<QObject*>());
            REQUIRE(owner && owner->mapOverview() && owner->totalResourceCount() == 0);
            if (owner == &restored) rootWindow = candidate;
            else {
                REQUIRE(!peerOwner);
                peerOwner = owner; peerWindow = candidate;
                REQUIRE(owner->tabs().size() == 1 && owner->currentSession() == peerSession.toString(QUuid::WithoutBraces)
                    && owner->tabs()[0].toMap()["id"].toString() == owner->currentSession());
            }
        }
        REQUIRE(rootWindow && bool(peerOwner) == !empty && !restored.viewStateFailed() && restored.error().isEmpty());
        if (fallback) REQUIRE(restored.tabs()[0].toMap()["id"].toString() == a && restored.tabs()[1].toMap()["id"].toString() == b);
        REQUIRE(unchanged(store, before) && api.requests == 0 && api.upgrades == 0
            && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(profile.filePath("window-layout.json")) == savedBytes && fileBytes(source->ownedPath) == sourceBytes);
        if (viewError) {
            REQUIRE(peerOwner->viewStateFailed() && peerOwner->error().contains(a) && peerOwner->error().contains("Invalid saved views"));
            const auto error = peerOwner->error();
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(!quit.isAccepted() && waitFor([&] { return peerOwner->viewCloseNeedsDecision(); })
                && !restored.viewCloseNeedsDecision() && restoredHost.windows().size() == 2);
            REQUIRE(podlord::test::clickVisible(peerWindow, "stayViewClose"));
            REQUIRE(!restoredHost.applicationClosing() && !peerOwner->viewCloseNeedsDecision()
                && peerOwner->viewStateFailed() && peerOwner->error() == error && !restored.viewStateFailed()
                && restoredHost.windows().size() == 2 && unchanged(store, before)
                && fileBytes(viewPath) == invalidViews && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
        } else if (empty) {
            const auto geometry = rootWindow->geometry();
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(waitFor([&] { return restoredHost.windows().isEmpty(); }));
            const auto saved = podlord::WindowLayoutStore(profile.path()).load();
            REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved));
            const auto value = std::get<podlord::WindowLayout>(saved);
            REQUIRE(value.size() == 1 && value.first().id == "main" && value.first().sessions.isEmpty()
                && !value.first().activeSession && value.first().geometry == geometry && !value.first().maximized
                && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
        } else if (scenario == "layout_decision_repeat_quit" || scenario == "layout_stay_preserves_import_error") {
            const bool preserveImportError = scenario == "layout_stay_preserves_import_error";
            REQUIRE(waitFor([&] { return restored.sessionWindowActionsReady() && peerOwner->sessionWindowActionsReady(); }));
            {
                QObject frames;
                bool rendered = false;
                const QSize size(rootWindow->width() - 20, rootWindow->height());
                QObject::connect(rootWindow, &QQuickWindow::frameSwapped, &frames, [&] {
                    rendered = rootWindow->size() == size;
                }, Qt::QueuedConnection);
                rootWindow->resize(size); rootWindow->update();
                REQUIRE(waitFor([&] { return rendered; }));
            }
            const auto geometry = rootWindow->geometry();
            QString importError;
            if (preserveImportError) {
                QSignalSpy imported(&restored, &podlord::Workspace::sourceImportFinished);
                REQUIRE(restored.importFile(profile.filePath("missing-kubeconfig.yaml")));
                REQUIRE(waitFor([&] { return !restored.busy() && imported.size() == 1; }));
                importError = restored.error();
                REQUIRE(!imported.first().first().toBool() && !importError.isEmpty()
                    && !restored.authenticationRequired() && !restored.authenticationRunning()
                    && !peerOwner->authenticationRequired() && !peerOwner->authenticationRunning()
                    && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                    && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                    && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
            }
            QLockFile lock(profile.filePath("window-layout.json.lock")); lock.setStaleLockTime(0);
            REQUIRE(lock.tryLock(0));
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(!quit.isAccepted() && waitFor([&] { return restored.viewCloseNeedsDecision(); }));
            const auto error = restored.error();
            REQUIRE(!error.isEmpty() && !peerOwner->viewCloseNeedsDecision() && (!preserveImportError || error != importError));
            if (!preserveImportError) {
                QEvent repeated(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &repeated);
                REQUIRE(!repeated.isAccepted());
            }
            REQUIRE(restoredHost.applicationClosing() && restored.viewCloseNeedsDecision()
                && restored.error() == error && restoredHost.windows().size() == 2
                && restored.currentSession() == b && peerOwner->currentSession() == a
                && !peerOwner->confirmViewClose(false) && unchanged(store, before)
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                && fileBytes(profile.filePath("sessions.json")) == sessionBytes && fileBytes(source->ownedPath) == sourceBytes
                && api.requests == 0 && api.upgrades == 0);
            REQUIRE(podlord::test::clickVisible(rootWindow, "stayViewClose"));
            if (preserveImportError && restored.error() != importError)
                std::fprintf(stderr, "Layout Stay lost underlying import error: expected=\"%s\" actual=\"%s\" windows=%d requests=%d upgrades=%d\n",
                    qPrintable(importError), qPrintable(restored.error()), int(restoredHost.windows().size()), api.requests, api.upgrades);
            REQUIRE(!restoredHost.applicationClosing() && !restored.viewCloseNeedsDecision()
                && restoredHost.windows().size() == 2 && restored.error() == importError
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes);
            if (preserveImportError) {
                const auto sources = podlord::KubeconfigStore(profile.path()).list();
                REQUIRE(std::holds_alternative<podlord::SourceCatalog>(sources));
                const auto catalog = std::get<podlord::SourceCatalog>(sources);
                REQUIRE(catalog.errors.isEmpty() && catalog.sources.size() == 1 && catalog.sources.first().id == source->id
                    && catalog.sources.first().contexts.size() == 2 && restored.currentSession() == b && peerOwner->currentSession() == a
                    && !restored.authenticationRequired() && !restored.authenticationRunning()
                    && !peerOwner->authenticationRequired() && !peerOwner->authenticationRunning()
                    && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                    && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
                return true;
            }
            lock.unlock();
            QEvent retry(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &retry);
            REQUIRE(waitFor([&] { return restoredHost.windows().isEmpty(); }));
            const auto saved = podlord::WindowLayoutStore(profile.path()).load();
            REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved));
            const auto value = std::get<podlord::WindowLayout>(saved);
            REQUIRE(value.size() == 2);
            for (const auto& placement : value) {
                const bool root = placement.id == "main";
                REQUIRE(placement.id == (root ? QString("main") : peerId)
                    && placement.sessions == QSet<QUuid>({root ? secondId : firstId})
                    && placement.activeSession == (root ? secondId : firstId));
                if (root) REQUIRE(placement.geometry == geometry);
            }
            REQUIRE(unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
        }
        return true;
    }
    if (scenario == "layout_settings_rejected") {
        const podlord::WindowLayout layout{{"main", {secondId}, secondId, QRect(20, 20, 640, 480), false},
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {firstId}, firstId, QRect(50, 50, 600, 420), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        const QByteArray invalidSettings("{invalid settings}"), foreignBytes("{\"retained\":true}");
        {
            QFile settings(profile.filePath("read-settings.json")), foreign(profile.filePath("foreign.json"));
            REQUIRE(settings.open(QIODevice::WriteOnly) && settings.write(invalidSettings) == invalidSettings.size());
            REQUIRE(foreign.open(QIODevice::WriteOnly) && foreign.write(foreignBytes) == foreignBytes.size());
        }
        const auto rejected = podlord::ReadSettingsStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::Failure>(rejected));
        const auto error = std::get<podlord::Failure>(rejected).message;
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        const auto savedBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sourceBytes = fileBytes(source->ownedPath);
        {
            podlord::Workspace restored(profile.path());
            podlord::WindowHost restoredHost(restored);
            QSignalSpy opened(&restoredHost, &podlord::WindowHost::windowOpened);
            REQUIRE(restoredHost.open());
            REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && restored.error() == error; }));
            REQUIRE(QThreadPool::globalInstance()->waitForDone(15000));
            QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
            REQUIRE(restoredHost.windows().size() == 1 && opened.size() == 1 && restoredHost.windows().first()->isVisible()
                && restored.mapOverview() && restored.totalResourceCount() == 0 && restored.error() == error
                && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                && fileBytes(profile.filePath("read-settings.json")) == invalidSettings
                && fileBytes(profile.filePath("foreign.json")) == foreignBytes && fileBytes(source->ownedPath) == sourceBytes
                && api.requests == 0 && api.upgrades == 0);
        }
        REQUIRE(unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(profile.filePath("window-layout.json")) == savedBytes
            && fileBytes(profile.filePath("read-settings.json")) == invalidSettings
            && fileBytes(profile.filePath("foreign.json")) == foreignBytes && fileBytes(source->ownedPath) == sourceBytes
            && api.requests == 0 && api.upgrades == 0);
        return true;
    }
    if (scenario == "layout_session_catalog_rejected") {
        const podlord::WindowLayout layout{{"main", {secondId}, secondId, QRect(20, 20, 640, 480), false},
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {firstId}, firstId, QRect(50, 50, 600, 420), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        const auto layoutBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sourceBytes = fileBytes(source->ownedPath);
        const QByteArray invalidSessions("{invalid session catalog}");
        {
            QFile file(profile.filePath("sessions.json"));
            REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                && file.write(invalidSessions) == invalidSessions.size());
        }
        const auto rejected = store.list();
        REQUIRE(std::holds_alternative<podlord::Failure>(rejected));
        podlord::Workspace restored(profile.path());
        REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && !restored.error().isEmpty(); }));
        const auto startupError = restored.error();
        REQUIRE(startupError.contains(std::get<podlord::Failure>(rejected).message));
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(QThreadPool::globalInstance()->waitForDone(15000));
        QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
        REQUIRE(restoredHost.open() && restoredHost.windows().size() == 1 && restored.tabs().isEmpty()
            && restored.currentSession().isEmpty() && restored.totalResourceCount() == 0 && restored.mapOverview()
            && restored.error() == startupError && !restored.authenticationRequired() && !restored.authenticationRunning()
            && fileBytes(profile.filePath("sessions.json")) == invalidSessions
            && fileBytes(profile.filePath("window-layout.json")) == layoutBytes
            && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
        return true;
    }
    if (scenario == "layout_error_preserves_startup") {
        const auto importedBroken = podlord::KubeconfigStore(profile.path()).importText(profile.filePath("broken-source.yaml"), yaml);
        REQUIRE(std::holds_alternative<podlord::SourceSnapshot>(importedBroken));
        const auto brokenPath = std::get<podlord::SourceSnapshot>(importedBroken).ownedPath;
        const QByteArray invalidSource("{invalid source}");
        {
            QFile file(brokenPath);
            REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(invalidSource) == invalidSource.size());
        }
        const auto catalog = podlord::KubeconfigStore(profile.path()).list();
        REQUIRE(std::holds_alternative<podlord::SourceCatalog>(catalog)
            && std::get<podlord::SourceCatalog>(catalog).sources.size() == 1
            && std::get<podlord::SourceCatalog>(catalog).errors.size() == 1);
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        const auto sourceBytes = fileBytes(source->ownedPath);
        podlord::Workspace restored(profile.path());
        REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && restored.tabs().size() == 2; }));
        const auto startupError = restored.error();
        REQUIRE(!startupError.isEmpty());
        const QByteArray invalidLayout("{invalid layout}");
        {
            QFile file(profile.filePath("window-layout.json"));
            REQUIRE(file.open(QIODevice::WriteOnly) && file.write(invalidLayout) == invalidLayout.size());
        }
        const auto rejected = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::Failure>(rejected));
        const auto layoutError = std::get<podlord::Failure>(rejected).message;
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(waitFor([&] { return restored.error() == startupError + "\n" + layoutError; }));
        REQUIRE(restoredHost.windows().size() == 1 && restored.tabs().size() == 2 && restored.currentSession() == a
            && restored.mapOverview() && restored.totalResourceCount() == 0 && unchanged(store, before)
            && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(profile.filePath("window-layout.json")) == invalidLayout
            && fileBytes(brokenPath) == invalidSource && fileBytes(source->ownedPath) == sourceBytes
            && api.requests == 0 && api.upgrades == 0);
        return true;
    }
    if (scenario == "layout_no_pending_decision") {
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        podlord::Workspace restored(profile.path());
        REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && restored.tabs().size() == 2; }));
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        for (int repeat = 0; repeat < 2; ++repeat) {
            REQUIRE(!restoredHost.cancelClose() && !restoredHost.confirmLayoutClose(restored, false)
                && !restoredHost.confirmLayoutClose(restored, true) && !restored.confirmViewClose(true));
            REQUIRE(!restoredHost.applicationClosing() && !restored.viewCloseNeedsDecision()
                && restoredHost.layoutCloseError().isEmpty() && restored.error().isEmpty()
                && restoredHost.windows().size() == 1 && restoredHost.windows().first()->isVisible()
                && restored.tabs().size() == 2 && restored.currentSession() == a
                && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && !QFileInfo::exists(profile.filePath("window-layout.json"))
                && api.requests == 0 && api.upgrades == 0);
        }
        return true;
    }
    if (scenario == "layout_peer_view_error") {
        const podlord::WindowLayout layout{{"main", {firstId}, firstId, QRect(20, 20, 640, 480), false},
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {secondId}, secondId, QRect(50, 50, 600, 420), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        REQUIRE(QDir().mkpath(profile.filePath("views")));
        const auto brokenView = profile.filePath("views/" + b + ".json");
        const QByteArray invalidViews("{invalid saved views}");
        {
            QFile file(brokenView);
            REQUIRE(file.open(QIODevice::WriteOnly) && file.write(invalidViews) == invalidViews.size());
        }
        const auto before = store.list();
        const auto layoutBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        podlord::Workspace restored(profile.path());
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(waitFor([&] { return !restored.busy() && restoredHost.windows().size() == 2; }));
        int peers = 0;
        for (auto* window : restoredHost.windows()) {
            auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(window)->contextProperty("workspace").value<QObject*>());
            REQUIRE(owner && owner->mapOverview() && owner->totalResourceCount() == 0);
            if (owner == &restored) { REQUIRE(owner->error().isEmpty() && owner->currentSession() == a); continue; }
            ++peers;
            REQUIRE(owner->currentSession() == b && owner->tabs().size() == 1
                && owner->viewStateFailed() && owner->error().contains("Invalid saved views"));
        }
        REQUIRE(peers == 1 && unchanged(store, before)
            && fileBytes(profile.filePath("window-layout.json")) == layoutBytes
            && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && fileBytes(brokenView) == invalidViews && api.requests == 0 && api.upgrades == 0);
        return true;
    }
    if (scenario == "layout_close_then_quit") {
        podlord::Workspace restored(profile.path());
        REQUIRE(waitFor([&] { return !restored.busy() && !restored.filterPresetsBusy() && restored.tabs().size() == 2; }));
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(QThreadPool::globalInstance()->waitForDone(15000));
        QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
        REQUIRE(restoredHost.detach(restored, b));
        REQUIRE(waitFor([&] { return restoredHost.windows().size() == 2 && restored.tabs().size() == 1; }));
        QQuickWindow* peerWindow = nullptr;
        podlord::Workspace* peerOwner = nullptr;
        for (auto* window : restoredHost.windows()) {
            auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(window)->contextProperty("workspace").value<QObject*>());
            if (owner != &restored) { peerWindow = window; peerOwner = owner; }
        }
        REQUIRE(peerWindow && peerOwner && peerOwner->currentSession() == b);
        bool quitRequested = false, quitDeferred = false;
        QObject::connect(peerOwner, &podlord::Workspace::windowClosed, &restoredHost, [&] {
            quitRequested = true;
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            quitDeferred = !quit.isAccepted();
        });
        peerWindow->close();
        REQUIRE(waitFor([&] { return quitRequested && restoredHost.windows().isEmpty(); }));
        const auto saved = podlord::WindowLayoutStore(profile.path()).load();
        const auto sessions = store.list();
        REQUIRE(quitDeferred && std::holds_alternative<podlord::WindowLayout>(saved)
            && std::holds_alternative<podlord::SessionCatalog>(sessions));
        const auto value = std::get<podlord::WindowLayout>(saved);
        REQUIRE(value.size() == 1 && value.first().id == "main"
            && value.first().sessions == QSet<QUuid>({firstId}) && value.first().activeSession == firstId);
        const auto catalog = std::get<podlord::SessionCatalog>(sessions);
        REQUIRE(catalog.sessions.size() == 2 && catalog.sessions[0].open && !catalog.sessions[1].open
            && api.requests == 0 && api.upgrades == 0);
        return true;
    }
    if (scenario == "layout_restore_active_missing") {
        const auto third = store.create({source->contexts[0].id, {}}, "Third");
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(third));
        const auto thirdId = std::get<podlord::SessionCatalog>(third).sessions.last().id;
        const auto fourth = store.create({source->contexts[1].id, {}}, "Fourth");
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(fourth));
        const auto fourthId = std::get<podlord::SessionCatalog>(fourth).sessions.last().id;
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(thirdId)));
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(fourthId)));
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.activate(firstId)));
        const QString peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const podlord::WindowLayout layout{{"main", {firstId, secondId, fourthId}, firstId, QRect(20, 20, 640, 480), false},
            {peerId, {thirdId}, thirdId, QRect(50, 50, 600, 420), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.close(firstId)));
        const auto before = store.list();
        const auto savedBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        podlord::Workspace restored(profile.path());
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(waitFor([&] { return !restored.busy() && restoredHost.windows().size() == 2; }));
        REQUIRE(waitFor([&] { return restored.tabs().size() == 2 && restored.currentSession() == b; }));
        REQUIRE(restored.tabs()[0].toMap()["id"].toString() == b
            && restored.tabs()[1].toMap()["id"].toString() == fourthId.toString(QUuid::WithoutBraces));
        int peers = 0;
        for (auto* candidate : restoredHost.windows()) {
            auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(candidate)->contextProperty("workspace").value<QObject*>());
            REQUIRE(owner && owner->mapOverview() && owner->totalResourceCount() == 0);
            if (owner == &restored) continue;
            ++peers;
            REQUIRE(owner->tabs().size() == 1 && owner->tabs()[0].toMap()["id"].toString() == thirdId.toString(QUuid::WithoutBraces)
                && owner->currentSession() == thirdId.toString(QUuid::WithoutBraces));
        }
        REQUIRE(peers == 1 && unchanged(store, before) && api.requests == 0 && api.upgrades == 0);
        REQUIRE(!savedBytes.isEmpty() && !sessionBytes.isEmpty()
            && fileBytes(profile.filePath("window-layout.json")) == savedBytes && fileBytes(profile.filePath("sessions.json")) == sessionBytes);
        return true;
    }
    if (scenario == "layout_quit_load_pending" || scenario == "layout_detach_load_pending") {
        const podlord::WindowLayout layout{{"main", {secondId}, secondId, QRect(20, 20, 640, 480), false},
            {QUuid::createUuid().toString(QUuid::WithoutBraces), {firstId}, firstId, QRect(50, 50, 600, 420), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
        podlord::Workspace pending(profile.path());
        REQUIRE(waitFor([&] { return !pending.busy() && !pending.filterPresetsBusy() && pending.tabs().size() == 2; }));
        const auto before = store.list();
        const auto savedBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        auto* pool = QThreadPool::globalInstance();
        REQUIRE(pool->waitForDone(15000));
        const int previousMaximum = pool->maxThreadCount();
        const auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
        std::unique_ptr<podlord::WindowHost> pendingHost;
        bool blocked = true;
        const auto releaseWorkers = qScopeGuard([&] {
            if (blocked) release->release();
            pool->setMaxThreadCount(previousMaximum);
        });
        pool->setMaxThreadCount(1);
        pool->start([entered, release] { entered->release(); release->acquire(); });
        REQUIRE(waitFor([&] { return entered->available() == 1; }));
        pendingHost = std::make_unique<podlord::WindowHost>(pending);
        QSignalSpy opened(pendingHost.get(), &podlord::WindowHost::windowOpened);
        REQUIRE(pendingHost->open() && pendingHost->windows().size() == 1);
        if (scenario == "layout_detach_load_pending") {
            const auto sourceBytes = fileBytes(source->ownedPath);
            const auto tabsBefore = pending.tabs();
            REQUIRE(!pending.sessionWindowActionsReady() && pending.currentSession() == a
                && pending.tabs().size() == 2 && opened.size() == 1 && !sourceBytes.isEmpty()
                && api.requests == 0 && api.upgrades == 0);
            const bool detachedWhilePending = pending.detachSession(b);
            const bool unchangedPending = !detachedWhilePending && pendingHost->windows().size() == 1 && opened.size() == 1
                && pending.currentSession() == a && pending.tabs() == tabsBefore
                && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes && fileBytes(source->ownedPath) == sourceBytes
                && !pending.authenticationRequired() && !pending.authenticationRunning() && api.requests == 0 && api.upgrades == 0;
            if (!unchangedPending)
                std::fprintf(stderr, "Pending layout detach: detached=%d windows=%d opened=%d tabs=%d tabs_same=%d session=%s catalog_same=%d sessions_same=%d layout_same=%d source_same=%d auth=%d/%d requests=%d upgrades=%d\n",
                    detachedWhilePending, int(pendingHost->windows().size()), int(opened.size()), int(pending.tabs().size()), pending.tabs() == tabsBefore,
                    qPrintable(pending.currentSession()), unchanged(store, before), fileBytes(profile.filePath("sessions.json")) == sessionBytes,
                    fileBytes(profile.filePath("window-layout.json")) == savedBytes, fileBytes(source->ownedPath) == sourceBytes,
                    pending.authenticationRequired(), pending.authenticationRunning(), api.requests, api.upgrades);
            REQUIRE(unchangedPending);
            release->release(); blocked = false; pool->setMaxThreadCount(previousMaximum);
            REQUIRE(waitFor([&] { return !pending.busy() && pendingHost->windows().size() == 2
                && pending.currentSession() == b && pending.tabs().size() == 1 && pending.sessionWindowActionsReady(); }));
            REQUIRE(opened.size() == 2 && pending.tabs()[0].toMap()["id"].toString() == b);
            podlord::Workspace* restoredPeer = nullptr;
            for (auto* candidate : pendingHost->windows()) {
                auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(candidate)->contextProperty("workspace").value<QObject*>());
                REQUIRE(owner && owner->tabs().size() == 1 && owner->totalResourceCount() == 0
                    && !owner->authenticationRequired() && !owner->authenticationRunning());
                if (owner != &pending) restoredPeer = owner;
            }
            REQUIRE(restoredPeer && restoredPeer->currentSession() == a
                && restoredPeer->tabs()[0].toMap()["id"].toString() == a
                && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                && fileBytes(source->ownedPath) == sourceBytes && api.requests == 0 && api.upgrades == 0);
            REQUIRE(pending.activate(b));
            REQUIRE(waitFor([&] { return !pending.busy() && !pending.loading() && pending.sessionWindowActionsReady()
                && pending.currentSession() == b && pending.totalResourceCount() == firstCount; }));
            const auto beforeDetach = store.list();
            REQUIRE(std::holds_alternative<podlord::SessionCatalog>(beforeDetach));
            const auto detachSessionBytes = fileBytes(profile.filePath("sessions.json"));
            const int requestsBeforeDetach = api.requests, upgradesBeforeDetach = api.upgrades;
            REQUIRE(pending.detachSession(b));
            REQUIRE(waitFor([&] { return opened.size() == 3 && pending.tabs().isEmpty(); }));
            QSet<QString> assigned;
            for (auto* candidate : pendingHost->windows()) {
                auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(candidate)->contextProperty("workspace").value<QObject*>());
                REQUIRE(owner && !owner->authenticationRequired() && !owner->authenticationRunning());
                const auto tabs = owner->tabs();
                REQUIRE(owner == &pending ? tabs.isEmpty() : tabs.size() == 1);
                for (const auto& tab : tabs) {
                    const auto id = tab.toMap()["id"].toString();
                    REQUIRE(!assigned.contains(id) && owner->currentSession() == id);
                    assigned.insert(id);
                }
            }
            REQUIRE(assigned == QSet<QString>({a, b}) && restoredPeer->currentSession() == a
                && unchanged(store, beforeDetach) && fileBytes(profile.filePath("sessions.json")) == detachSessionBytes
                && fileBytes(profile.filePath("window-layout.json")) == savedBytes
                && fileBytes(source->ownedPath) == sourceBytes && api.requests == requestsBeforeDetach && api.upgrades == upgradesBeforeDetach);
            return true;
        }
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(!quit.isAccepted() && pendingHost->applicationClosing() && pendingHost->windows().size() == 1);
        REQUIRE(unchanged(store, before) && fileBytes(profile.filePath("window-layout.json")) == savedBytes);
        release->release(); blocked = false; pool->setMaxThreadCount(previousMaximum);
        REQUIRE(waitFor([&] { return pendingHost->windows().isEmpty(); }));
        REQUIRE(pool->waitForDone(15000)); QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
        REQUIRE(opened.size() == 2 && pendingHost->windows().isEmpty() && unchanged(store, before));
        REQUIRE(fileBytes(profile.filePath("sessions.json")) == sessionBytes && api.requests == 0 && api.upgrades == 0);
        const auto retained = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(retained) && std::get<podlord::WindowLayout>(retained) == layout);
        return true;
    }
    if (scenario == "layout_quit_save_pending") {
        const podlord::WindowLayout layout{{"main", {firstId, secondId}, firstId, QRect(20, 20, 640, 480), false}};
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(layout, {})));
    }
    if (scenario == "layout_invalid") {
        QFile file(profile.filePath("window-layout.json"));
        REQUIRE(file.open(QIODevice::WriteOnly) && file.write("{invalid}") == 9);
    }
    podlord::Workspace primary(profile.path());
    if (scenario == "missing_resource") {
        REQUIRE(unregisterWorkspaceResource());
        podlord::WindowHost unavailable(primary);
        REQUIRE(!unavailable.open() && unavailable.windows().isEmpty()); return true;
    }
    podlord::WindowHost host(primary);
    if (scenario == "application_quit_empty") {
        const auto before = store.list();
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(quit.isAccepted() && host.windows().isEmpty() && unchanged(store, before) && api.requests == 0);
        return true;
    }
    REQUIRE(host.open() && host.windows().size() == 1);
    auto* main = host.windows().first();
    if (QGuiApplication::platformName() == "cocoa") {
        main->raise(); main->requestActivate();
        REQUIRE(QTest::qWaitForWindowExposed(main) && QTest::qWaitForWindowActive(main));
    }
    REQUIRE(waitFor([&] { return !primary.busy() && !primary.filterPresetsBusy() && primary.tabs().size()==2; }));
    REQUIRE(primary.resourceCount()==0 && api.requests==0 && primary.mapOverview());
    if (scenario == "root_restore") {
        main->close(); REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        const auto catalog = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(catalog));
        REQUIRE(std::get<podlord::SessionCatalog>(catalog).sessions[0].open && std::get<podlord::SessionCatalog>(catalog).sessions[1].open); return true;
    }
    REQUIRE(primary.activate(primary.currentSession()));
    REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading() && !primary.filterPresetsBusy() && primary.totalResourceCount() == firstCount; }));
    REQUIRE(primary.setWorkspacePage("resources"));
    REQUIRE(primary.windowActionsAvailable() && primary.tabs().size() == 2);
    if (scenario == "repeat") { REQUIRE(host.open() && host.windows().size() == 1 && primary.tabs().size() == 2); return true; }
    if (scenario == "missing") { REQUIRE(!primary.detachSession("missing") && host.windows().size() == 1); return true; }
    if (scenario == "snapshot_latest" || scenario == "snapshot_hidden" || scenario == "snapshot_detach" || scenario == "snapshot_close_window") {
        const auto matches = [&](QStringList expected, int total) {
            auto* table = primary.table();
            if (primary.totalResourceCount() != total || primary.resourceCount() != expected.size()
                || table->rowCount() != expected.size() || primary.visibleResourceTable()->rowCount() != expected.size()) return false;
            QStringList actual;
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto index = table->index(row, 0);
                const auto name = index.data().toString();
                if (index.data(Qt::UserRole).toString() != "/api/v1/namespaces/default/pods/" + name) return false;
                actual.append(name);
            }
            actual.sort(); expected.sort();
            if (actual != expected) return false;
            for (const auto& metric : primary.pulseMetrics()) {
                const auto value = metric.toMap();
                if (value["id"] == "pods") return value["usage"].toString() == QString::number(expected.size());
            }
            return false;
        };
        REQUIRE(matches({"alpha", "beta"}, 2));
        auto* pool = QThreadPool::globalInstance();
        const int previousMaximum = pool->maxThreadCount();
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        bool blocked = true;
        const auto restoreWorkers = qScopeGuard([&] {
            if (blocked) release->release();
            pool->setMaxThreadCount(previousMaximum);
        });
        pool->setMaxThreadCount(1);
        pool->start([entered, release] { entered->release(); release->acquire(); });
        REQUIRE(waitFor([&] { return entered->available() == 1; }));
        // All session/store setup is complete. Only HTTP ingress and synchronous
        // public projections are awaited while the global pool is held.
        api.names = {"queued", "superseded"};
        const int beforeQueued = api.requests;
        REQUIRE(primary.refresh());
        REQUIRE(waitFor([&] { return !primary.loading() && api.requests > beforeQueued && matches(api.names, 2); }));
        if (scenario == "snapshot_detach" || scenario == "snapshot_close_window") {
            podlord::Workspace* moved = nullptr;
            if (scenario == "snapshot_detach") {
                REQUIRE(primary.detachSession(a) && host.windows().size() == 2);
                for (auto* window : host.windows()) if (window != main)
                    moved = qobject_cast<podlord::Workspace*>(qmlContext(window)->contextProperty("workspace").value<QObject*>());
                REQUIRE(moved && moved->currentSession() == a && moved->resourceCount() == 2);
            } else {
                main->close();
            }
            release->release(); blocked = false; pool->setMaxThreadCount(previousMaximum);
            REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading() && (!moved || (!moved->busy()
                && primary.currentSession() == b && primary.resourceCount() == 2
                && primary.table()->index(0, 0).data(Qt::UserRole + 3).toString() == "second")); }));
            for (int phase = 0; phase < 3; ++phase) { REQUIRE(pool->waitForDone(15000)); QCoreApplication::processEvents(); }
            if (moved) {
                REQUIRE(moved->currentSession() == a && moved->resourceCount() == 2);
                REQUIRE(moved->table()->index(0, 0).data().toString() == "queued");
                REQUIRE(moved->table()->index(0, 0).data(Qt::UserRole + 3).toString() == "default");
                REQUIRE(primary.currentSession() == b && primary.table()->index(0, 0).data().toString() == "alpha");
                REQUIRE(primary.table()->index(0, 0).data(Qt::UserRole + 3).toString() == "second");
            } else {
                REQUIRE(waitFor([&] { return host.windows().isEmpty(); }) && primary.currentSession().isEmpty());
                // A late platform visibility notification cannot resurrect a closed workspace.
                REQUIRE(primary.setWindowVisible(true));
                REQUIRE(primary.table()->rowCount() == 0 && primary.visibleResourceTable()->rowCount() == 0);
            }
            return api.authorized && other.authorized;
        }
        const bool hidden = scenario == "snapshot_hidden";
        if (hidden) { main->hide(); REQUIRE(!main->isVisible()); }
        if (!hidden) REQUIRE(primary.filter("=latest") && matches({}, 2));
        api.names = {"fresh", "latest", "newest"};
        const int beforeLatest = api.requests;
        REQUIRE(primary.refresh());
        REQUIRE(waitFor([&] { return !primary.loading() && api.requests > beforeLatest
            && (hidden ? !primary.property("syncLoading").toBool() : matches({"latest"}, 3)); }));
        if (!hidden) REQUIRE(primary.filter("") && matches(api.names, 3));
        const int completedRequests = api.requests;
        release->release(); blocked = false;
        pool->setMaxThreadCount(previousMaximum);
        REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading(); }));
        // Finish the superseded task/callout, then the latest task it may queue.
        // No private preparation flags or synthetic ResourceClient rows are used.
        for (int phase = 0; phase < 2; ++phase) {
            REQUIRE(pool->waitForDone(15000));
            QCoreApplication::processEvents();
            if (!hidden) REQUIRE(matches(api.names, 3));
        }
        if (hidden) { main->show(); REQUIRE(waitFor([&] { return main->isVisible() && matches(api.names, 3); })); }
        REQUIRE(waitFor([&] {
            auto* cell = podlord::test::tableCell(podlord::test::visibleItem(main->contentItem(), "resourceTable"), 0, 0);
            return cell && cell->property("text").toString() == primary.visibleResourceTable()->index(0, 0).data().toString();
        }));
        REQUIRE(pool->waitForDone(15000));
        QCoreApplication::processEvents();
        REQUIRE(matches(api.names, 3));
        REQUIRE(primary.filter("=queued") && matches({}, 3));
        REQUIRE(primary.filter("=latest") && matches({"latest"}, 3));
        REQUIRE(primary.filter("") && matches(api.names, 3));
        REQUIRE(api.requests == completedRequests && api.authorized);
        return true;
    }
    if (scenario == "handoff_events") {
        REQUIRE(primary.setWorkspacePage("events"));
        REQUIRE(waitFor([&] { return primary.eventTable()->rowCount() == 1; }));
        const int firstRequests = api.requests;
        REQUIRE(primary.activate(b));
        REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading() && primary.totalResourceCount() == secondCount; }));
        REQUIRE(primary.setWorkspacePage("events") && primary.eventTable()->rowCount() == 0);
        const int secondRequests = other.requests;
        REQUIRE(primary.activate(a));
        REQUIRE(waitFor([&] { return !primary.busy() && primary.totalResourceCount() == firstCount && primary.eventTable()->rowCount() == 1; }));
        REQUIRE(api.requests == firstRequests && other.requests == secondRequests);
        return api.authorized && other.authorized;
    }
    const auto path = QString("/api/v1/namespaces/default/pods/alpha");
    REQUIRE(primary.inspectPath(path));
    REQUIRE(waitFor([&] { return primary.canEditYaml(); }));
    if (handoff) {
        REQUIRE(primary.filter("=alpha"));
        REQUIRE(primary.rememberRadarView({{"zoom", 0.75}, {"x", 12.0}, {"y", 18.0}}));
        const auto camera = primary.radarView();
        REQUIRE(primary.activate(b));
        REQUIRE(waitFor([&] { return !primary.busy() && !primary.loading() && primary.resourceCount() == secondCount; }));
        REQUIRE(primary.setWorkspacePage("resources"));
        if (scenario == "handoff_sort") { REQUIRE(primary.sortColumn(0)); REQUIRE(primary.sortColumn(0)); }
        // Alpha remains in both projections, but TableView may recycle its delegates.
        const int retainedRow = scenario == "handoff_sort" ? 1 : 0;
        const auto currentCell = [&](int row) { return podlord::test::tableCell(podlord::test::visibleItem(main->contentItem(), "resourceTable"), row, 0); };
        REQUIRE(waitFor([&] {
            return podlord::test::tableColumnsReady(podlord::test::visibleItem(main->contentItem(), "resourceTable"), primary.resourceColumns())
                && currentCell(retainedRow);
        }));
        if (scenario == "handoff_sort") REQUIRE(currentCell(retainedRow)->property("text").toString() == "alpha");
        const int retainedSortColumn = primary.sortColumnIndex();
        const auto retainedSortDirection = primary.sortDirection();
        auto* const displayed = primary.visibleResourceTable();
        QAbstractItemModel* rows = displayed;
        podlord::ResourceFilter* filtered = nullptr;
        while (auto* proxy = qobject_cast<QAbstractProxyModel*>(rows)) {
            if (auto* resourceFilter = qobject_cast<podlord::ResourceFilter*>(rows)) filtered = resourceFilter;
            rows = proxy->sourceModel();
        }
        const auto visibleCellMatches = [&](int row, const QString& session, const QString& expectedPath) {
            auto* view = podlord::test::visibleItem(main->contentItem(), "resourceTable");
            auto* cell = currentCell(row);
            if (!view || !cell || !cell->isVisible() || cell->width() <= 0 || cell->height() <= 0
                || primary.currentSession() != session || primary.visibleResourceTable() != displayed
                || view->property("model").value<QAbstractItemModel*>() != displayed) return false;
            QAbstractItemModel* source = displayed;
            while (auto* proxy = qobject_cast<QAbstractProxyModel*>(source)) source = proxy->sourceModel();
            const auto index = displayed->index(row, 0);
            return source == rows && index.data(Qt::UserRole).toString() == expectedPath && index.data().toString() == "alpha"
                && cell->property("resourcePath").toString() == expectedPath && cell->property("text").toString() == index.data().toString()
                && podlord::test::visibleBounds(main, cell).contains(cell->mapToScene({cell->width()/2, cell->height()/2}));
        };
        QSignalSpy removed(rows, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(rows, &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(rows, &QAbstractItemModel::modelReset);
        bool coherent = true;
        const auto observe = [&] {
            const bool first = primary.currentSession() == a;
            coherent = coherent && rows->rowCount() == (first ? firstCount : secondCount);
            for (int row = 0; row < rows->rowCount(); ++row)
                coherent = coherent && rows->index(row, 2).data().toString() == (first ? "default" : "second");
        };
        QObject observer;
        bool layoutCoherent = true;
        if (scenario == "handoff_sort") {
            REQUIRE(filtered);
            QObject::connect(filtered, &QAbstractItemModel::layoutChanged, &observer, [&] {
                const auto expected = primary.currentSession() == a ? "default" : "second";
                for (int row = 0; row < filtered->rowCount(); ++row)
                    layoutCoherent = layoutCoherent && filtered->index(row, 2).data() == expected;
            });
        }
        QObject::connect(rows, &QAbstractItemModel::rowsInserted, &observer, observe);
        QObject::connect(rows, &QAbstractItemModel::rowsRemoved, &observer, observe);
        QObject::connect(rows, &QAbstractItemModel::dataChanged, &observer, observe);
        const int firstRequests = api.requests, secondRequests = other.requests;
        REQUIRE(primary.activate(a));
        REQUIRE(primary.resourceCount() == 1 && primary.totalResourceCount() == firstCount);
        REQUIRE(primary.filterText() == "=alpha" && primary.inspectorPath() == path && primary.radarView() == camera);
        REQUIRE(coherent && reset.isEmpty() && layoutCoherent);
        if (firstCount == secondCount) {
            const bool shown = removed.isEmpty() && inserted.isEmpty()
                && waitFor([&] { return visibleCellMatches(0, a, path); });
            if (!shown) {
                auto* current = currentCell(0);
                std::fprintf(stderr,"Window handoff B-A: modelRetained=%d resets=%d removed=%d inserted=%d currentText=\"%s\" currentPath=%s expectedPath=%s actualSession=%s expectedSession=%s\n",
                    primary.visibleResourceTable()==displayed,int(reset.size()),int(removed.size()),int(inserted.size()),
                    qPrintable(current ? current->property("text").toString() : QString("<missing>")),
                    qPrintable(current ? current->property("resourcePath").toString() : QString("<missing>")),qPrintable(path),
                    qPrintable(primary.currentSession()),qPrintable(a));
            }
            REQUIRE(shown);
        }
        REQUIRE(waitFor([&] { return !primary.busy(); }));
        REQUIRE(primary.activate(b));
        REQUIRE(primary.resourceCount() == secondCount && primary.filterText().isEmpty());
        REQUIRE(coherent && reset.isEmpty() && layoutCoherent);
        if (firstCount == secondCount) {
            bool retentionPassed = false;
            const auto reportRetention = qScopeGuard([&] {
                if (retentionPassed) return;
                auto* current = currentCell(retainedRow);
                const auto expectedText = primary.visibleResourceTable()->index(retainedRow, 0).data().toString();
                std::fprintf(stderr,"Window handoff B-A-B: scenario=%s modelRetained=%d resets=%d removed=%d inserted=%d requests=%d/%d expectedRequests=%d/%d\n",
                    qPrintable(scenario),primary.visibleResourceTable()==displayed,int(reset.size()),int(removed.size()),int(inserted.size()),
                    api.requests,other.requests,firstRequests,secondRequests);
                std::fprintf(stderr,"Window handoff target: currentText=\"%s\" expectedText=\"%s\" currentPath=%s expectedPath=%s visible=%d size=%gx%g actualSession=%s expectedSession=%s actualSort=%d/%s expectedSort=%d/%s\n",
                    qPrintable(current ? current->property("text").toString() : QString("<missing>")),qPrintable(expectedText),
                    qPrintable(current ? current->property("resourcePath").toString() : QString("<missing>")),"/api/v1/namespaces/second/pods/alpha",
                    current && current->isVisible(),current ? current->width() : 0,current ? current->height() : 0,
                    qPrintable(primary.currentSession()),qPrintable(b),primary.sortColumnIndex(),qPrintable(primary.sortDirection()),
                    retainedSortColumn,qPrintable(retainedSortDirection));
            });
            REQUIRE(removed.isEmpty() && inserted.isEmpty()
                && waitFor([&] { return visibleCellMatches(retainedRow, b, "/api/v1/namespaces/second/pods/alpha"); })
                && primary.sortColumnIndex() == retainedSortColumn && primary.sortDirection() == retainedSortDirection
                && api.requests == firstRequests && other.requests == secondRequests);
            retentionPassed = true;
        }
        REQUIRE(api.requests == firstRequests && other.requests == secondRequests);
        return true;
    }
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
        if (scenario == "filter") {
            REQUIRE(primary.filter("=alpha") && !primary.sessionWindowActionsReady());
            auto* moving = podlord::test::visibleItem(main->contentItem(), "detachSession_" + a);
            REQUIRE(moving && !moving->isEnabled());
        }
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
        if (scenario == "layout_quit_save_pending") {
            REQUIRE(waitFor([&] { return primary.sessionWindowActionsReady(); }));
            REQUIRE(primary.detachSession(a));
        } else {
            REQUIRE(waitFor([&] {
                auto* button = podlord::test::visibleItem(main->contentItem(), "detachSession_" + a);
                return primary.sessionWindowActionsReady() && button && button->isEnabled();
            }));
            REQUIRE(podlord::test::clickVisible(main, "detachSession_" + a));
        }
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
    if (scenario == "focus_minimized_windowed") {
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        const auto sourceBytes = fileBytes(source->ownedPath);
        const auto savedBytes = fileBytes(profile.filePath("window-layout.json"));
        const bool savedExists = QFileInfo::exists(profile.filePath("window-layout.json"));
        const int requests = api.requests, upgrades = api.upgrades;
        QSignalSpy opened(&host, &podlord::WindowHost::windowOpened);
        REQUIRE(window->setProperty("visibility", QVariant::fromValue(QWindow::Windowed)));
        REQUIRE(waitFor([&] { return window->windowState() == Qt::WindowNoState; }));
        const QRect geometry(window->screen()->availableGeometry().topLeft() + QPoint(40, 50), QSize(600, 420));
        {
            QObject frames;
            bool rendered = false;
            QObject::connect(window, &QQuickWindow::frameSwapped, &frames, [&] {
                rendered = window->windowState() == Qt::WindowNoState && window->geometry() == geometry;
            }, Qt::QueuedConnection);
            window->setGeometry(geometry); window->update();
            REQUIRE(waitFor([&] { return rendered; }));
        }
        REQUIRE(window->setProperty("visibility", QVariant::fromValue(QWindow::Minimized)));
        REQUIRE(waitFor([&] { return window->windowState() == Qt::WindowMinimized; }));
        REQUIRE(host.focus(*detached));
        REQUIRE(waitFor([&] { return window->isVisible() && window->visibility() == QWindow::Windowed
            && window->windowState() == Qt::WindowNoState && window->geometry() == geometry; }));
        REQUIRE(host.windows().size() == 2 && host.windows().contains(window) && opened.isEmpty()
            && primary.currentSession() == b && detached->currentSession() == a
            && primary.tabs().size() == 1 && primary.tabs()[0].toMap()["id"].toString() == b
            && detached->tabs().size() == 1 && detached->tabs()[0].toMap()["id"].toString() == a
            && primary.resourceCount() == 2 && detached->resourceCount() == 2 && unchanged(store, before)
            && fileBytes(profile.filePath("sessions.json")) == sessionBytes && fileBytes(source->ownedPath) == sourceBytes
            && fileBytes(profile.filePath("window-layout.json")) == savedBytes
            && QFileInfo::exists(profile.filePath("window-layout.json")) == savedExists
            && api.requests == requests && api.upgrades == upgrades);
        return true;
    }
    if (scenario == "focus_maximized" || scenario == "focus_minimized_maximized") {
        REQUIRE(window->setProperty("visibility", QVariant::fromValue(QWindow::Maximized)));
        REQUIRE(waitFor([&] { return window->windowState() == Qt::WindowMaximized; }));
        if (scenario == "focus_minimized_maximized") {
            REQUIRE(window->setProperty("visibility", QVariant::fromValue(QWindow::Minimized)));
            REQUIRE(waitFor([&] { return window->windowState() == Qt::WindowMinimized; }));
        }
        REQUIRE(host.focus(*detached));
        REQUIRE(waitFor([&] { return window->windowState() == Qt::WindowMaximized; }));
        REQUIRE(primary.currentSession() == b && detached->currentSession() == a);
        return true;
    }
    if (scenario == "layout_closed_main_policy") {
        main->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
        REQUIRE(detached->saveWorkspaceRestore(false));
        REQUIRE(waitFor([&] { return !detached->busy() && !detached->workspaceRestoreEnabled(); }));
        QLockFile lock(profile.filePath("window-layout.json.lock")); REQUIRE(lock.tryLock(0));
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        const auto saved = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(saved));
        const auto catalog = std::get<podlord::SessionCatalog>(saved);
        REQUIRE(catalog.sessions[0].open && !catalog.sessions[1].open);
        return true;
    }
    if (scenario == "layout_quit_save_pending") {
        REQUIRE(waitFor([&] { return primary.sessionWindowActionsReady() && detached->sessionWindowActionsReady(); }));
        auto* pool = QThreadPool::globalInstance();
        REQUIRE(pool->waitForDone(15000));
        const auto before = store.list();
        const auto savedBytes = fileBytes(profile.filePath("window-layout.json"));
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        REQUIRE(!savedBytes.isEmpty() && !sessionBytes.isEmpty());
        const int requests = api.requests, upgrades = api.upgrades;
        const int previousMaximum = pool->maxThreadCount();
        const auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
        bool blocked = true;
        const auto releaseWorkers = qScopeGuard([&] {
            if (blocked) release->release();
            pool->setMaxThreadCount(previousMaximum);
        });
        pool->setMaxThreadCount(1);
        pool->start([entered, release] { entered->release(); release->acquire(); });
        REQUIRE(waitFor([&] { return entered->available() == 1; }));
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(!quit.isAccepted() && host.applicationClosing() && host.windows().size() == 2);
        REQUIRE(primary.sessionWindowActionsReady() && detached->sessionWindowActionsReady());
        QEvent repeated(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &repeated);
        REQUIRE(!repeated.isAccepted() && host.windows().size() == 2 && unchanged(store, before));
        REQUIRE(fileBytes(profile.filePath("window-layout.json")) == savedBytes && fileBytes(profile.filePath("sessions.json")) == sessionBytes);
        release->release(); blocked = false; pool->setMaxThreadCount(previousMaximum);
        REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        REQUIRE(pool->waitForDone(15000)); QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents();
        REQUIRE(host.windows().isEmpty() && !primary.viewCloseNeedsDecision() && unchanged(store, before)
            && fileBytes(profile.filePath("sessions.json")) == sessionBytes && api.requests == requests && api.upgrades == upgrades);
        const auto saved = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved) && std::get<podlord::WindowLayout>(saved).size() == 2
            && fileBytes(profile.filePath("window-layout.json")) != savedBytes);
        QSet<QUuid> assigned;
        for (const auto& placement : std::get<podlord::WindowLayout>(saved)) {
            REQUIRE(placement.sessions.size() == 1 && placement.activeSession && placement.sessions.contains(*placement.activeSession));
            assigned.unite(placement.sessions);
        }
        REQUIRE(assigned == QSet<QUuid>({firstId, secondId}));
        return true;
    }
    if (scenario == "layout_restore_without_main") {
        REQUIRE(waitFor([&] { return primary.sessionWindowActionsReady() && detached->sessionWindowActionsReady(); }));
        main->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
        const QRect geometry(window->screen()->availableGeometry().topLeft() + QPoint(40, 50), QSize(600, 420));
        window->setGeometry(geometry);
        REQUIRE(waitFor([&] { return window->geometry() == geometry; }));
        const auto before = store.list();
        const auto sessionBytes = fileBytes(profile.filePath("sessions.json"));
        const int requests = api.requests, upgrades = api.upgrades;
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        const auto saved = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved));
        const auto layout = std::get<podlord::WindowLayout>(saved);
        REQUIRE(layout.size() == 1 && layout.first().id != "main" && !QUuid(layout.first().id).isNull()
            && layout.first().sessions == QSet<QUuid>({firstId}) && layout.first().activeSession == firstId
            && layout.first().geometry == geometry && !layout.first().maximized);
        podlord::Workspace restored(profile.path()); podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(waitFor([&] { return !restored.busy() && restoredHost.windows().size() == 1
            && restored.currentSession() == a && restoredHost.windows().first()->geometry() == geometry; }));
        REQUIRE(restored.tabs().size() == 1 && restored.tabs()[0].toMap()["id"].toString() == a
            && restored.mapOverview() && restored.totalResourceCount() == 0);
        QEvent again(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &again);
        REQUIRE(waitFor([&] { return restoredHost.windows().isEmpty(); }));
        const auto repeated = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(repeated) && std::get<podlord::WindowLayout>(repeated) == layout
            && unchanged(store, before) && fileBytes(profile.filePath("sessions.json")) == sessionBytes
            && api.requests == requests && api.upgrades == upgrades);
        return true;
    }
    if (scenario == "layout_lock_stay" || scenario == "layout_lock_discard" || scenario == "layout_conflict" || scenario == "layout_invalid") {
        REQUIRE(waitFor([&] { return primary.sessionWindowActionsReady() && detached->sessionWindowActionsReady(); }));
        const auto before = store.list();
        const QString path = profile.filePath("window-layout.json");
        QLockFile lock(path + ".lock");
        const bool locked = scenario.startsWith("layout_lock");
        if (locked) REQUIRE(lock.tryLock(0));
        const podlord::WindowLayout external{{"main", {QUuid(a), QUuid(b)}, QUuid(a), QRect(20, 20, 640, 480), false}};
        if (scenario == "layout_conflict") REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(external, {})));
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(!quit.isAccepted() && waitFor([&] { return primary.viewCloseNeedsDecision(); }));
        REQUIRE(host.windows().size() == 2 && unchanged(store, before) && !primary.error().isEmpty());
        const bool stay = scenario == "layout_lock_stay";
        REQUIRE(podlord::test::clickVisible(main, stay ? "stayViewClose" : "discardViewClose"));
        if (stay) {
            REQUIRE(!primary.viewCloseNeedsDecision() && host.windows().size() == 2 && unchanged(store, before));
            lock.unlock();
            QEvent retry(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &retry);
        }
        REQUIRE(waitFor([&] { return host.windows().isEmpty(); }) && unchanged(store, before));
        const auto saved = podlord::WindowLayoutStore(profile.path()).load();
        if (stay) REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved) && std::get<podlord::WindowLayout>(saved).size() == 2);
        else if (scenario == "layout_conflict") REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved) && std::get<podlord::WindowLayout>(saved) == external);
        else if (scenario == "layout_invalid") {
            QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly) && file.readAll() == QByteArray("{invalid}"));
        } else REQUIRE(!QFileInfo::exists(path));
        return true;
    }
    if (scenario.startsWith("layout_restore")) {
        REQUIRE(waitFor([&] { return primary.sessionWindowActionsReady() && detached->sessionWindowActionsReady(); }));
        REQUIRE(main->setProperty("visibility", QVariant::fromValue(QWindow::Windowed))
            && window->setProperty("visibility", QVariant::fromValue(QWindow::Windowed)));
        REQUIRE(waitFor([&] { return main->windowState() == Qt::WindowNoState && window->windowState() == Qt::WindowNoState; }));
        const auto origin = main->screen()->availableGeometry().marginsRemoved(main->frameMargins()).topLeft();
        const QRect mainGeometry(origin + QPoint(20, 30), QSize(640, 480));
        const QRect peerGeometry(origin + QPoint(60, 80), QSize(600, 420));
        {
            QObject normalFrames;
            bool mainRendered = false, peerRendered = false;
            QObject::connect(main, &QQuickWindow::frameSwapped, &normalFrames, [&] {
                mainRendered = main->windowState() == Qt::WindowNoState && main->geometry() == mainGeometry;
            }, Qt::QueuedConnection);
            QObject::connect(window, &QQuickWindow::frameSwapped, &normalFrames, [&] {
                peerRendered = window->windowState() == Qt::WindowNoState && window->geometry() == peerGeometry;
            }, Qt::QueuedConnection);
            main->setGeometry(mainGeometry); window->setGeometry(peerGeometry);
            if (QGuiApplication::platformName() == "cocoa") {
                main->raise(); main->requestActivate(); main->update();
                const bool rendered = waitFor([&] { return main->isExposed() && mainRendered; });
                if (!rendered) qWarning() << "Native normal-window frame:" << "visible" << main->isVisible()
                    << "exposed" << main->isExposed() << "active" << main->isActive() << "rendered" << mainRendered
                    << "geometry" << main->geometry() << "expected" << mainGeometry << "minimum" << main->minimumSize()
                    << "frame" << main->frameMargins() << "screen" << main->screen()->availableGeometry();
                REQUIRE(rendered);
                window->raise(); window->requestActivate(); window->update();
            }
            main->update(); window->update();
            REQUIRE(waitFor([&] { return mainRendered && peerRendered; }));
        }
        window->showMaximized();
        REQUIRE(waitFor([&] { return window->windowState() == Qt::WindowMaximized; }));
        QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
        REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        const auto saved = podlord::WindowLayoutStore(profile.path()).load();
        REQUIRE(std::holds_alternative<podlord::WindowLayout>(saved) && std::get<podlord::WindowLayout>(saved).size() == 2);
        for (const auto& placement : std::get<podlord::WindowLayout>(saved)) {
            const bool peer = placement.sessions.contains(firstId);
            const auto expectedGeometry = peer ? peerGeometry : mainGeometry;
            if (placement.geometry != expectedGeometry || placement.maximized != peer)
                std::fprintf(stderr, "Saved window placement: owner=%s id=%s geometry=%d,%d %dx%d expected=%d,%d %dx%d maximized=%d expected=%d\n",
                    peer ? "First" : "Second", qPrintable(placement.id), placement.geometry.x(), placement.geometry.y(),
                    placement.geometry.width(), placement.geometry.height(), expectedGeometry.x(), expectedGeometry.y(),
                    expectedGeometry.width(), expectedGeometry.height(), placement.maximized, peer);
            REQUIRE(placement.sessions == QSet<QUuid>({peer ? firstId : secondId})
                && placement.activeSession == (peer ? firstId : secondId)
                && placement.geometry == (peer ? peerGeometry : mainGeometry) && placement.maximized == peer
                && (peer ? placement.id != "main" : placement.id == "main"));
        }
        if (scenario == "layout_restore_disabled") {
            const podlord::ReadSettingsStore settings(profile.path());
            const auto loaded = settings.load(); REQUIRE(std::holds_alternative<podlord::ReadSettings>(loaded));
            auto value = std::get<podlord::ReadSettings>(loaded); value.workspaceRestore = false;
            REQUIRE(std::holds_alternative<podlord::ReadSettings>(settings.save(value, std::get<podlord::ReadSettings>(loaded))));
        }
        if (scenario == "layout_restore_stale") REQUIRE(std::holds_alternative<podlord::SessionCatalog>(store.close(QUuid(a))));
        if (scenario == "layout_restore_offscreen") {
            auto value = std::get<podlord::WindowLayout>(saved);
            for (auto& placement : value) placement.geometry.moveTopLeft({75000, 75000});
            REQUIRE(std::holds_alternative<podlord::WindowLayout>(podlord::WindowLayoutStore(profile.path()).save(value, std::get<podlord::WindowLayout>(saved))));
        }
        const int requests = api.requests;
        const auto beforeRestore = store.list();
        const auto visibleGeometry = [](QQuickWindow* candidate, QRect saved) {
            const auto screen = candidate->screen()->availableGeometry();
            auto available = screen.marginsRemoved(candidate->frameMargins());
            if (available.isEmpty()) available = screen;
            const int width = qBound(qMin(candidate->minimumWidth(), available.width()), saved.width(), available.width());
            const int height = qBound(qMin(candidate->minimumHeight(), available.height()), saved.height(), available.height());
            return QRect(qBound(available.left(), saved.x(), available.right() - width + 1),
                qBound(available.top(), saved.y(), available.bottom() - height + 1), width, height);
        };
        const auto verify = [&](podlord::Workspace& current, podlord::WindowHost& currentHost) {
            const bool disabled = scenario == "layout_restore_disabled", stale = scenario == "layout_restore_stale";
            const int expected = disabled || stale ? 1 : 2;
            REQUIRE(waitFor([&] { return !current.busy() && currentHost.windows().size() == expected; }));
            if (disabled) return current.tabs().isEmpty() && api.requests == requests;
            REQUIRE(waitFor([&] {
                int complete = 0;
                for (auto* candidate : currentHost.windows()) {
                    auto* owner = qobject_cast<podlord::Workspace*>(qmlContext(candidate)->contextProperty("workspace").value<QObject*>());
                    if (!owner) continue;
                    const bool peer = owner->currentSession() == a;
                    auto expectedGeometry = mainGeometry;
                    if (scenario == "layout_restore_offscreen") expectedGeometry.moveTopLeft({75000, 75000});
                    if (peer ? candidate->windowState() == Qt::WindowMaximized
                        : candidate->windowState() == Qt::WindowNoState && candidate->geometry() == visibleGeometry(candidate, expectedGeometry)) ++complete;
                }
                return complete == expected;
            }));
            QSet<QString> sessions;
            int maximized = 0;
            for (auto* candidate : currentHost.windows()) {
                auto* owner = qobject_cast<podlord::Workspace*>(QQmlEngine::contextForObject(candidate)->contextProperty("workspace").value<QObject*>());
                REQUIRE(owner && owner->tabs().size() == 1 && owner->totalResourceCount() == 0 && owner->mapOverview());
                const bool peer = owner->currentSession() == a;
                REQUIRE(owner->currentSession() == (peer ? a : b) && (owner == &current) == !peer
                    && owner->tabs()[0].toMap()["id"].toString() == owner->currentSession());
                sessions.insert(owner->currentSession());
                auto expectedGeometry = peer ? peerGeometry : mainGeometry;
                if (scenario == "layout_restore_offscreen") expectedGeometry.moveTopLeft({75000, 75000});
                if (peer) {
                    REQUIRE(candidate->windowState() == Qt::WindowMaximized); ++maximized;
                    REQUIRE(candidate->setProperty("visibility", QVariant::fromValue(QWindow::Windowed)));
                    REQUIRE(waitFor([&] { return candidate->windowState() == Qt::WindowNoState
                        && candidate->geometry() == visibleGeometry(candidate, expectedGeometry); }));
                } else REQUIRE(candidate->windowState() == Qt::WindowNoState && candidate->geometry() == visibleGeometry(candidate, expectedGeometry));
                if (scenario == "layout_restore_offscreen") {
                    REQUIRE(candidate->screen()->availableGeometry().contains(candidate->frameGeometry()));
                } else REQUIRE(candidate->geometry() == visibleGeometry(candidate, expectedGeometry));
                if (peer) {
                    REQUIRE(candidate->setProperty("visibility", QVariant::fromValue(QWindow::Maximized)));
                    REQUIRE(waitFor([&] { return candidate->windowState() == Qt::WindowMaximized; }));
                }
            }
            REQUIRE(sessions == (stale ? QSet<QString>{b} : QSet<QString>{a, b}) && maximized == (stale ? 0 : 1) && api.requests == requests);
            return true;
        };
        podlord::Workspace restored(profile.path());
        podlord::WindowHost restoredHost(restored);
        REQUIRE(restoredHost.open());
        REQUIRE(verify(restored, restoredHost));
        if (scenario != "layout_restore_disabled") REQUIRE(unchanged(store, beforeRestore));
        if (scenario == "layout_restore_repeat") {
            QEvent quitAgain(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quitAgain);
            REQUIRE(waitFor([&] { return restoredHost.windows().isEmpty(); }));
            const auto repeated = podlord::WindowLayoutStore(profile.path()).load(); REQUIRE(std::holds_alternative<podlord::WindowLayout>(repeated));
            QSet<QString> firstIds, secondIds;
            for (const auto& placement : std::get<podlord::WindowLayout>(saved)) firstIds.insert(placement.id);
            for (const auto& placement : std::get<podlord::WindowLayout>(repeated)) secondIds.insert(placement.id);
            REQUIRE(firstIds == secondIds);
            podlord::Workspace third(profile.path()); podlord::WindowHost thirdHost(third);
            REQUIRE(thirdHost.open() && verify(third, thirdHost));
        }
        return true;
    }
    if (scenario.startsWith("application_quit")) {
        REQUIRE(waitFor([&] { return primary.sessionWindowActionsReady() && detached->sessionWindowActionsReady(); }));
        if (scenario == "application_quit_closed_window") {
            bool requested = false;
            QObject::connect(&primary, &podlord::Workspace::windowClosed, &host, [&] {
                QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
                requested = true;
            });
            main->close();
            REQUIRE(waitFor([&] { return host.windows().isEmpty(); }) && requested);
            const auto saved = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(saved));
            const auto catalog = std::get<podlord::SessionCatalog>(saved);
            REQUIRE(catalog.sessions[0].open && !catalog.sessions[1].open);
            return true;
        }
        if (scenario == "application_quit_tab_prompt" || scenario == "application_quit_cancel_retry"
            || scenario == "application_quit_other_window") {
            REQUIRE(detached->beginYamlEdit() && detached->setYamlDraft(detached->yamlText() + "\n# keep this draft\n"));
            if (scenario == "application_quit_tab_prompt") {
                detached->close(a);
                REQUIRE(detached->discardPending());
            }
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(!quit.isAccepted() && detached->discardPending() && host.windows().size() == 2);
            if (scenario == "application_quit_cancel_retry") {
                REQUIRE(!main->close());
                REQUIRE(detached->confirmDiscard(false));
            } else {
                if (scenario == "application_quit_other_window") {
                    QWindow auxiliary;
                    auxiliary.show();
                    REQUIRE(auxiliary.close() && !auxiliary.isVisible());
                    REQUIRE(host.windows().size() == 2 && detached->discardPending());
                }
                REQUIRE(podlord::test::clickVisible(window, "discardStay"));
            }
            QTest::qWait(10);
            REQUIRE(host.windows().size() == 2 && detached->yamlDirty() && !detached->discardPending());
            main->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
            const auto saved = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(saved));
            const auto catalog = std::get<podlord::SessionCatalog>(saved);
            REQUIRE(catalog.sessions[0].open && !catalog.sessions[1].open && detached->yamlDirty());
            return true;
        }
        if (scenario == "application_quit_stay" || scenario == "application_quit_discard") {
            REQUIRE(detached->beginYamlEdit() && detached->setYamlDraft(detached->yamlText() + "\n# keep this draft\n"));
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(!quit.isAccepted() && host.windows().size() == 2 && detached->discardPending());
            const bool discard = scenario == "application_quit_discard";
            REQUIRE(podlord::test::clickVisible(window, discard ? "discardAccept" : "discardStay"));
            if (!discard) {
                REQUIRE(detached->yamlDirty() && !detached->discardPending() && host.windows().size() == 2);
                main->close(); REQUIRE(waitFor([&] { return host.windows().size() == 1; }));
                const auto saved = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(saved));
                const auto catalog = std::get<podlord::SessionCatalog>(saved);
                REQUIRE(catalog.sessions[0].open && !catalog.sessions[1].open && detached->yamlDirty());
                return true;
            }
        } else if (scenario == "application_quit_wait") {
            auto* pool = QThreadPool::globalInstance();
            const int previousMaximum = pool->maxThreadCount();
            const auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
            bool blocked = true;
            const auto restoreWorkers = qScopeGuard([&] {
                if (blocked) release->release();
                pool->setMaxThreadCount(previousMaximum);
            });
            pool->setMaxThreadCount(1);
            pool->start([entered, release] { entered->release(); release->acquire(); });
            REQUIRE(waitFor([&] { return entered->available() == 1; }));
            REQUIRE(primary.filter("alpha") && !primary.sessionWindowActionsReady());
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(!quit.isAccepted() && host.windows().size() == 2);
            release->release(); blocked = false; pool->setMaxThreadCount(previousMaximum);
            REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        } else {
            REQUIRE(scenario == "application_quit");
            QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(qGuiApp, &quit);
            REQUIRE(!quit.isAccepted());
        }
        REQUIRE(waitFor([&] { return host.windows().isEmpty(); }));
        const auto saved = store.list(); REQUIRE(std::holds_alternative<podlord::SessionCatalog>(saved));
        const auto catalog = std::get<podlord::SessionCatalog>(saved);
        REQUIRE(catalog.sessions[0].open && catalog.sessions[1].open);
        const int requests = api.requests;
        podlord::Workspace restored(profile.path());
        REQUIRE(waitFor([&] { return !restored.busy() && restored.tabs().size() == 2; }));
        REQUIRE(restored.totalResourceCount() == 0 && restored.mapOverview() && api.requests == requests);
        return true;
    }
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
        REQUIRE(detached->saveReadSettings(600, 5, "5", detached->collectionCacheHours()));
        REQUIRE(waitFor([&] { return !detached->busy() && primary.requestLimit() == 600; }));
        REQUIRE(primary.saveRadarWater(false, 77));
        REQUIRE(waitFor([&] { return !primary.busy() && !detached->radarWaterEnabled() && detached->radarWaterSpeedPercent() == 77; })); return true;
    }
    if (scenario == "preset") {
        REQUIRE(detached->filter("=alpha") && detached->saveFilterPreset("Shared filter"));
        auto* moving = podlord::test::visibleItem(window->contentItem(), "detachSession_" + a);
        REQUIRE(moving && !moving->isEnabled() && !detached->sessionWindowActionsReady());
        REQUIRE(waitFor([&] { return !detached->filterPresetsBusy() && primary.filterPresets().contains("Shared filter"); }));
        REQUIRE(waitFor([&] { return detached->sessionWindowActionsReady() && moving->isEnabled(); }));
        REQUIRE(primary.loadFilterPreset("Shared filter") && primary.resourceCount() == 1); return true;
    }
    if (scenario == "columns") {
        auto columns = detached->resourceColumns(); REQUIRE(columns.size() > 1);
        auto firstColumn = columns[0].toMap(); firstColumn["visible"] = false; firstColumn["pinned"] = false; columns[0] = firstColumn;
        REQUIRE(detached->saveTableLayout("resource", columns));
        auto* moving = podlord::test::visibleItem(window->contentItem(), "detachSession_" + a);
        REQUIRE(moving && !moving->isEnabled() && !detached->sessionWindowActionsReady());
        REQUIRE(waitFor([&] { return detached->sessionWindowActionsReady() && moving->isEnabled(); }));
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
    podlord::Workspace primary(profile.path());
    REQUIRE(podlord::test::muteAlerts(primary.alerts()));
    podlord::WindowHost host(primary); REQUIRE(host.open());
    REQUIRE(waitFor([&] { return !primary.busy(); }));
    REQUIRE(primary.openContext(source->contexts.first().id));
    if (!waitFor([&] { return !primary.busy() && primary.loadingProgress() == 1 && primary.totalResourceCount() >= 1000; }, 120000)) {
        std::fprintf(stderr, "Kubernetes transfer preparation timed out: cached=%d visible=%d progress=%g busy=%d\n",
            primary.totalResourceCount(), primary.resourceCount(), primary.loadingProgress(), primary.busy());
        return false;
    }
    std::printf("Kubernetes transfer prepared: cached=%d visible=%d\n", primary.totalResourceCount(), primary.resourceCount());
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
