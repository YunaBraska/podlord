#include "workspace.h"
#include "ui_input.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <stdexcept>

using namespace podlord;
namespace {
bool require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    return true;
}
QByteArray contents(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read public persistence output.");
    return file.readAll();
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void settle(Workspace& workspace) {
    require(QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Local operation did not finish.");
}
bool action(Workspace& workspace, const char* name) {
    bool accepted = false;
    require(QMetaObject::invokeMethod(&workspace, name, Q_RETURN_ARG(bool, accepted)), "Required source-removal action is missing.");
    return accepted;
}
bool prepare(Workspace& workspace, const QString& context) {
    bool accepted = false;
    require(QMetaObject::invokeMethod(&workspace, "requestSourceRemoval", Q_RETURN_ARG(bool, accepted), Q_ARG(QString, context)), "Source-removal confirmation is missing.");
    return accepted;
}
void run(const QString& scenario) {
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot isolate local test data.");
    // The only simulated boundary is the external Kubernetes HTTP service.
    // It denies authentication: deleting local sources must not retry or log in.
    QTcpServer api;
    require(api.listen(QHostAddress::LocalHost), "Cannot isolate external API boundary.");
    int requests = 0;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (api.hasPendingConnections()) {
            auto* socket = api.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                if (!socket->peek(socket->bytesAvailable()).contains("\r\n\r\n")) return;
                socket->readAll();
                ++requests;
                socket->write("HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QString profile = temporary.filePath("profile");
    const QString original = temporary.filePath("original.yaml");
    const auto yaml = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:%1'}\ncontexts:\n- name: alpha\n  context: {cluster: local}\n- name: beta\n  context: {cluster: local}\n").arg(api.serverPort()).toUtf8();
    require(write(original, yaml), "Cannot create real kubeconfig input.");
    const KubeconfigStore sources(profile);
    const auto imported = sources.importFile(original);
    require(std::holds_alternative<SourceSnapshot>(imported), "Real source import failed.");
    const auto snapshot = std::get<SourceSnapshot>(imported);
    QString alpha, beta;
    for (const auto& context : snapshot.contexts) (context.name == "alpha" ? alpha : beta) = context.id;
    require(!alpha.isEmpty() && !beta.isEmpty(), "Imported contexts are missing.");
    const SessionStore sessions(profile);
    QList<QUuid> removed;
    const auto create = [&](const QString& context, const QString& name) {
        const auto current = sessions.list();
        require(std::holds_alternative<SessionCatalog>(current), "Cannot read the tab catalog.");
        const auto result = sessions.openTab(context);
        require(std::holds_alternative<SessionCatalog>(result), "Cannot create real session.");
        const auto id = std::get<SessionCatalog>(result).sessions.last().id;
        require(std::holds_alternative<SessionCatalog>(sessions.activate(id)), "Cannot open real saved session.");
        return id;
    };
    if (scenario != "empty") { removed.append(create(alpha, "Alpha one")); removed.append(create(alpha, "Alpha two")); }
    const auto kept = create(beta, "Beta retained");
    if (scenario == "active") require(std::holds_alternative<SessionCatalog>(sessions.activate(removed.first())), "Cannot select removed session.");
    Workspace workspace(profile);
    settle(workspace);
    require(workspace.activate((scenario == "active" ? removed.first() : kept).toString(QUuid::WithoutBraces)), "Cannot explicitly open the saved source-removal session.");
    settle(workspace);
    require(QTest::qWaitFor([&] { return requests > 0; }, 5000), "Real workspace did not reach external boundary.");
    QTest::qWait(100);
    const int requestsBefore = requests;
    const auto sourceBytes = contents(snapshot.ownedPath);
    auto sessionBytes = contents(profile + "/tabs.json");
    if (scenario.startsWith("contract_")) {
        auto context = alpha;
        QStringList ids;
        for (const auto& id : removed) ids.append(id.toString(QUuid::WithoutBraces));
        auto expected = StoreError::InvalidInput;
        if (scenario == "contract_empty") context.clear();
        else if (scenario == "contract_context") context = "ctx:invalid";
        else if (scenario == "contract_missing") { context = "ctx:" + QString(64, '0'); expected = StoreError::NotFound; }
        else if (scenario == "contract_session") ids[0] = "not-a-session";
        else if (scenario == "contract_canonical") ids[0] = "{" + ids[0] + "}";
        else if (scenario == "contract_duplicate") ids.append(ids.first());
        else if (scenario == "contract_unconfirmed") { ids.clear(); expected = StoreError::Conflict; }
        else throw std::runtime_error("Unknown contract scenario.");
        const auto result = sources.removeContext(context, ids);
        require(std::holds_alternative<Failure>(result) && std::get<Failure>(result).code == expected, "Invalid public confirmation was accepted or failed ambiguously.");
        require(contents(profile + "/tabs.json") == sessionBytes && contents(snapshot.ownedPath) == sourceBytes && contents(original) == yaml, "Invalid confirmation changed persistence.");
        return;
    }
    if (scenario == "unknown") {
        require(!prepare(workspace, "ctx:missing") && !workspace.sourceImportError().isEmpty(), "Unknown context was accepted.");
        require(contents(profile + "/tabs.json") == sessionBytes && contents(snapshot.ownedPath) == sourceBytes, "Rejected removal changed local files.");
        return;
    }
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    const bool ui = scenario == "ui" || scenario == "keyboard" || scenario == "narrow" || scenario == "cancel_ui";
    const auto click = [&](const QString& name, bool keyboard = false) {
        if (name == "settingsWorkspaceButton") {
            require(podlord::test::clickVisible(window,name), "Sources settings navigation is unavailable.");
            return;
        }
        QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
        require(frames.wait(1000), "Source controls did not render.");
        auto* control = podlord::test::visibleItem(window->contentItem(), name);
        if (!control || !control->isVisible() || !control->isEnabled())
            throw std::runtime_error(QString("Source action %1 is not reachable.").arg(name).toStdString());
        for (auto* item = control; item; item = item->parentItem()) item->ensurePolished();
        require(podlord::test::scrollIntoView(window, control), "Source action cannot be revealed by scrolling.");
        const auto point = control->mapToScene({control->width()/2, control->height()/2}).toPoint();
        require(window->contentItem()->contains(point), "Source action lies outside the window.");
        if (keyboard) { control->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); }
        else QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
    };
    if (ui) {
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        require(!engine.rootObjects().isEmpty(), "Cannot load the shipped UI.");
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        require(window, "Application window is missing.");
        if (scenario == "narrow") { window->setWidth(390); window->setHeight(720); }
        require(QTest::qWaitForWindowExposed(window, 5000), "Application window is not exposed.");
        click("settingsWorkspaceButton"); require(podlord::test::selectSettingsSection(window, "sources"), "Sources section is not reachable.");
        QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
        require(frames.wait(1000), "Settings did not render.");
        click("removeSource_" + alpha, scenario == "keyboard");
    } else require(prepare(workspace, alpha), "Context confirmation was rejected.");
    const auto preview = workspace.property("sourceRemoval").toMap();
    require(preview["contextId"].toString() == alpha && preview["name"].toString() == "alpha", "Confirmation identifies the wrong context.");
    QStringList affected;
    for (const auto& id : removed) affected.append(id.toString(QUuid::WithoutBraces));
    require(preview["sessionIds"].toStringList() == affected, "Confirmation omits affected tabs.");
    require(contents(profile + "/tabs.json") == sessionBytes, "Preparing removal changed saved sessions.");
    require(requests == requestsBefore, "Confirmation retried external authentication.");
    if (scenario == "cancel" || scenario == "cancel_ui") {
        if (ui) click("cancelSourceRemoval"); else require(action(workspace, "cancelSourceRemoval"), "Cancellation was rejected.");
        require(workspace.property("sourceRemoval").toMap().isEmpty() && contents(profile + "/tabs.json") == sessionBytes
            && workspace.contexts().size() == 2, "Cancellation changed sources or sessions.");
        return;
    }
    QLockFile lock(profile + (scenario == "source_locked" ? "/kubeconfigs/sources.lock" : "/tabs.lock"));
    if (scenario == "locked" || scenario == "source_locked") require(lock.tryLock(0), "Cannot hold the real writer lock.");
    if (scenario == "conflict") { removed.append(create(alpha, "Added after confirmation")); sessionBytes = contents(profile + "/tabs.json"); }
    if (scenario == "corrupt") { require(write(profile + "/tabs.json", "{invalid"), "Cannot exercise corrupt local input."); sessionBytes = contents(profile + "/tabs.json"); }
    if (ui) {
        const auto frame = qEnvironmentVariable("PODLORD_SOURCE_REMOVAL_FRAME");
        if (!frame.isEmpty()) {
            QSignalSpy frames(window, &QQuickWindow::frameSwapped); window->update();
            require(frames.wait(1000) && window->grabWindow().save(frame), "Cannot retain the actual confirmation frame.");
        }
        click("confirmSourceRemoval", scenario == "keyboard");
    }
    else require(action(workspace, "confirmSourceRemoval"), "Confirmed deletion was not admitted.");
    settle(workspace);
    if (scenario == "locked" || scenario == "source_locked" || scenario == "conflict" || scenario == "corrupt") {
        require(!workspace.sourceImportError().isEmpty() && !workspace.property("sourceRemoval").toMap().isEmpty(), "Failed deletion was presented as success.");
        require(contents(profile + "/tabs.json") == sessionBytes && contents(snapshot.ownedPath) == sourceBytes, "Failed cascade partially changed persistence.");
        if (scenario == "corrupt") return;
        lock.unlock();
        if (scenario == "conflict") { require(action(workspace, "cancelSourceRemoval") && prepare(workspace, alpha), "Cannot refresh stale confirmation."); }
        require(action(workspace, "confirmSourceRemoval"), "Retry after a local failure was rejected."); settle(workspace);
    }
    const auto saved = sessions.list();
    require(std::holds_alternative<SessionCatalog>(saved), "Removal corrupted the saved catalog.");
    const auto catalog = std::get<SessionCatalog>(saved);
    require(catalog.sessions.size() == 1 && catalog.sessions.first().id == kept && catalog.activeSession == kept, "Cascade removed unrelated sessions or lost active placement.");
    require(workspace.tabs().size() == 1 && workspace.currentSession() == kept.toString(QUuid::WithoutBraces), "Removed sessions remained open.");
    require(workspace.contexts().size() == 1 && workspace.contexts().first().toMap()["id"].toString() == beta, "The wrong context remained visible.");
    require(std::holds_alternative<Failure>(sources.connection(alpha)), "Removed credentials still resolve.");
    require(std::holds_alternative<Failure>(sources.connection(alpha)), "Removed context silently accepted a new session.");
    require(contents(original) == yaml && contents(snapshot.ownedPath) == sourceBytes, "Cascade changed original kubeconfig or shared immutable content.");
    require(workspace.property("sourceRemoval").toMap().isEmpty() && requests == requestsBefore, "Completed removal retained confirmation or retried authentication.");
    if (scenario == "reimport") {
        const auto restored = sources.importFile(original);
        require(std::holds_alternative<SourceSnapshot>(restored) && std::get<SourceSnapshot>(restored).contexts.size() == 2, "Explicit reimport did not restore context availability.");
        require(std::holds_alternative<ClusterConnection>(sources.connection(alpha)) && std::get<SessionCatalog>(sessions.list()).sessions.size() == 1, "Reimport resurrected deleted sessions or retained the context exclusion.");
    }
    if (scenario == "restart") {
        Workspace restarted(profile); settle(restarted);
        require(restarted.contexts().size() == 1 && restarted.tabs().size() == 1 && restarted.currentSession() == kept.toString(QUuid::WithoutBraces), "Restart resurrected deleted state.");
    }
}
}
int main(int argc, char* argv[]) {
    const QGuiApplication app(argc, argv);
    try { require(argc == 2, "One named scenario is required."); run(QString::fromLocal8Bit(argv[1])); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
