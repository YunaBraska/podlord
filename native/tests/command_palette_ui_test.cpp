#include "workspace.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    return true;
}
// Only the external Kubernetes HTTP boundary is simulated.
class Kubernetes final : public QTcpServer {
public:
    int requests = 0;
    explicit Kubernetes(bool problem) {
        const QJsonObject pod{{"apiVersion", "v1"}, {"kind", "Pod"},
            {"metadata", QJsonObject{{"name", "alpha"}, {"namespace", "default"}, {"uid", "palette-pod"}, {"resourceVersion", "1"}}},
            {"spec", QJsonObject{{"containers", QJsonArray{QJsonObject{{"name", "app"}, {"image", "busybox:1.37"}, {"ports", QJsonArray{QJsonObject{{"containerPort", 8080}}}}}}}}},
            {"status", QJsonObject{{"phase", problem ? "Failed" : "Running"}}}};
        connect(this, &QTcpServer::newConnection, this, [this, pod] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto input = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, input, pod] {
                    input->append(socket->readAll());
                    if (!input->contains("\r\n\r\n")) return;
                    socket->disconnect(this); ++requests;
                    const auto path = QUrl::fromEncoded(input->split(' ')[1]).path();
                    QJsonObject body;
                    if (path == "/api") body = {{"versions", QJsonArray{"v1"}}};
                    else if (path == "/apis") body = {{"groups", QJsonArray{}}};
                    else if (path == "/api/v1") body = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else if (path == "/api/v1/pods") body = {{"metadata", QJsonObject{}}, {"items", QJsonArray{pod}}};
                    else body = pod;
                    const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        require(argc == 2, "Expected command palette scenario.");
        const QString scenario = QString::fromLocal8Bit(argv[1]);
        require(QStringList{"button", "keyboard", "keyboard_meta", "keyboard_arrows", "keyboard_default", "escape", "close", "repeat", "empty", "resources", "events", "sources", "settings", "forward", "problems", "disabled_forward", "disabled_problems", "narrow"}.contains(scenario), "Unknown palette scenario.");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create private command profile.");
        Kubernetes server(scenario == "problems");
        const bool populated = scenario == "forward" || scenario == "problems";
        QString sessionId;
        if (populated) {
            require(server.listen(QHostAddress::LocalHost, 0), "Cannot start external Kubernetes boundary.");
            const podlord::KubeconfigStore sources(directory.filePath("profile"));
            const auto imported = sources.importText(directory.filePath("config"), QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: http://127.0.0.1:%1\ncontexts:\n- name: local\n  context:\n    cluster: local\n").arg(server.serverPort()));
            require(std::holds_alternative<podlord::SourceSnapshot>(imported), "Cannot seed real source store.");
            const podlord::SessionStore sessions(directory.filePath("profile"));
            const auto created = sessions.create({std::get<podlord::SourceSnapshot>(imported).contexts.first().id, {}});
            require(std::holds_alternative<podlord::SessionCatalog>(created), "Cannot create real session.");
            sessionId = std::get<podlord::SessionCatalog>(created).sessions.first().id.toString(QUuid::WithoutBraces);
            require(std::holds_alternative<podlord::SessionCatalog>(sessions.activate(std::get<podlord::SessionCatalog>(created).sessions.first().id)), "Cannot activate real session.");
        }
        podlord::Workspace workspace(directory.filePath("profile"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("workspace", &workspace);
        engine.load(QUrl("qrc:/podlord/Main.qml"));
        require(!engine.rootObjects().isEmpty(), "Cannot create actual palette UI.");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        require(window && QTest::qWaitFor([&] { return !workspace.busy(); }, 5000), "Palette workspace did not settle.");
        if (populated) {
            require(workspace.activate(sessionId), "Explicit activation failed.");
            if (!QTest::qWaitFor([&] { return workspace.resourceCount() == 1; }, 8000)) {
                std::fprintf(stderr, "Cache activation: requests=%d contexts=%lld sessions=%lld active=%s status=%s error=%s\n", server.requests, static_cast<long long>(workspace.contexts().size()), static_cast<long long>(workspace.sessions().size()), workspace.currentSession().toUtf8().constData(), workspace.status().toUtf8().constData(), workspace.error().toUtf8().constData());
                require(false, "Real cache did not populate from HTTP.");
            }
        }
        if (scenario == "forward") {
            require(workspace.inspectRow(0), "Cannot select actual cached Pod.");
            require(QTest::qWaitFor([&] { return workspace.canPortForward(); }, 5000), "Selected Pod cannot prepare a forward.");
            QTest::qWait(1000);
        }
        const auto requestsBefore = server.requests;
        const std::function<QQuickItem*(QQuickItem*, const QString&)> visibleItem = [&](QQuickItem* current, const QString& name) -> QQuickItem* {
            if (current->objectName() == name && current->isVisible()) return current;
            for (auto* child : current->childItems()) if (auto* found = visibleItem(child, name)) return found;
            return nullptr;
        };
        const auto item = [&](const char* name) {
            QQuickItem* target = nullptr;
            require(QTest::qWaitFor([&] { target = visibleItem(window->contentItem(), QString::fromLatin1(name)); return target != nullptr; }, 2000), "Palette control is not visible.");
            return target;
        };
        const auto click = [&](const char* name) {
            auto* target = item(name);
            require(target->isEnabled(), "Palette action is disabled.");
            target->ensurePolished();
            const auto point = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
            require(window->contentItem()->contains(point), "Palette action lies outside the scene.");
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
            QCoreApplication::processEvents();
        };
        if (scenario == "narrow") {
            window->setWidth(390); window->setHeight(720);
            require(QTest::qWaitFor([&] { return window->contentItem()->width() == 390; }, 2000), "Actual narrow scene did not resize.");
        }
        if (scenario == "resources" || scenario == "keyboard_default") click("settingsWorkspaceButton");
        if (scenario == "keyboard" || scenario == "keyboard_meta") QTest::keyClick(window, Qt::Key_K, scenario == "keyboard_meta" ? Qt::MetaModifier : Qt::ControlModifier);
        else click("commandPaletteButton");
        auto* palette = window->findChild<QObject*>("commandPalette");
        require(palette && QTest::qWaitFor([&] { return palette->property("opened").toBool(); }, 2000), "Palette did not finish opening.");
        auto* search = item("commandPaletteSearch");
        require(QTest::qWaitFor([&] { return search->hasActiveFocus(); }, 2000), "Palette search did not receive keyboard focus.");
        require(item("commandPaletteList")->property("count").toInt() == 7, "Migrated command catalog is incomplete.");
        const auto evidence = qEnvironmentVariable("PODLORD_TEST_EVIDENCE_DIR");
        if (!evidence.isEmpty()) require(window->grabWindow().save(evidence + "/command-palette-" + scenario + "-" + qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") + ".png"), "Cannot capture actual palette scene.");
        const auto query = [&](const QByteArray& text) {
            search->forceActiveFocus();
            QTest::keySequence(window, QKeySequence::SelectAll);
            if (text.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
            for (const auto character : text) QTest::keyClick(window, character);
            QCoreApplication::processEvents();
        };
        if (scenario == "escape" || scenario == "repeat") {
            query("events"); QTest::keyClick(window, Qt::Key_Escape);
            require(QTest::qWaitFor([&] { return !palette->property("visible").toBool(); }, 2000), "Escape did not close palette.");
            if (scenario == "repeat") {
                click("commandPaletteButton");
                require(QTest::qWaitFor([&] { return palette->property("visible").toBool(); }, 2000)
                    && item("commandPaletteSearch")->property("text").toString().isEmpty()
                    && item("commandPaletteList")->property("count").toInt() == 7, "Reopened palette retained an obsolete query.");
            }
        } else if (scenario == "close") {
            click("commandPaletteClose");
            require(QTest::qWaitFor([&] { return !palette->property("visible").toBool(); }, 2000), "Close did not dismiss palette.");
        } else if (scenario == "empty") {
            query("no-such-command");
            require(item("commandPaletteList")->property("count").toInt() == 0
                && item("commandPaletteEmpty")->property("text").toString() == "No commands match.", "Unknown command has no explicit feedback.");
            QTest::keyClick(window, Qt::Key_Return);
            require(palette->property("visible").toBool(), "Unknown command silently executed or closed.");
        } else if (scenario == "disabled_forward" || scenario == "disabled_problems") {
            query(scenario == "disabled_forward" ? "port" : "problems");
            require(item("commandPaletteList")->property("count").toInt() == 1, "Disabled command disappeared.");
            require(!item(scenario == "disabled_forward" ? "commandEntry_Open Port Forwards" : "commandEntry_Toggle Problems")->isEnabled(), "Unavailable command is enabled.");
            QTest::keyClick(window, Qt::Key_Return);
            require(palette->property("visible").toBool() && workspace.currentSession().isEmpty(), "Disabled command performed a side effect.");
        } else if (scenario == "keyboard_default") {
            QTest::keyClick(window, Qt::Key_Return);
            require(QTest::qWaitFor([&] { return !palette->property("visible").toBool(); }, 2000)
                && workspace.workspacePage() == "resources", "Empty-query Enter did not execute the first command.");
        } else if (scenario == "keyboard_arrows") {
            QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Up); QTest::keyClick(window, Qt::Key_Down);
            QTest::keyClick(window, Qt::Key_Return);
            require(QTest::qWaitFor([&] { return !palette->property("visible").toBool(); }, 2000)
                && workspace.workspacePage() == "events", "Arrow-key command selection did not execute Events.");
        } else if (scenario == "forward" || scenario == "problems") {
            query(scenario == "forward" ? "port" : "problems");
            require(server.requests == requestsBefore, "Opening/searching the palette fetched data.");
            QTest::keyClick(window, Qt::Key_Return);
            require(QTest::qWaitFor([&] { return !palette->property("visible").toBool(); }, 2000), "Enabled action did not dismiss palette.");
            if (scenario == "forward") {
                require(workspace.portForwardTarget().value("name").toString() == "alpha", "Palette did not prepare the selected Pod.");
                require(item("portForwardLocal")->isEnabled(), "Actual forwarding confirmation did not open.");
                require(workspace.portForwards().isEmpty(), "Preparing a forward started it without confirmation.");
            } else {
                require(workspace.problemsOnly() && workspace.resourceCount() == 1, "Palette did not toggle cached Problems.");
                click("commandPaletteButton");
                require(QTest::qWaitFor([&] { return palette->property("opened").toBool(); }, 2000), "Cannot reopen palette for repeated toggle.");
                query("problems"); QTest::keyClick(window, Qt::Key_Return);
                require(!workspace.problemsOnly() && workspace.resourceCount() == 1 && server.requests == requestsBefore, "Repeated Problems toggle lost cache or fetched data.");
            }
        } else if (QStringList{"resources", "events", "sources", "settings"}.contains(scenario)) {
            query("  " + scenario.toUpper().toUtf8() + "  ");
            QTest::keyClick(window, Qt::Key_Return);
            require(QTest::qWaitFor([&] { return !palette->property("visible").toBool(); }, 2000), "Command did not close palette.");
            require(workspace.workspacePage() == (scenario == "sources" ? "settings" : scenario), "Command navigated to the wrong workspace.");
            if (scenario == "sources") require(item("importHomeButton")->isEnabled(), "Sources command did not select Sources settings.");
        } else if (scenario == "narrow") {
            const auto withinScene = [&] {
                const auto point = search->mapToScene(QPointF());
                return point.x() >= 0 && point.x() + search->width() <= window->width();
            };
            const bool settled = QTest::qWaitFor(withinScene, 2000);
            const auto origin = search->mapToScene(QPointF());
            if (origin.x() < 0 || origin.x() + search->width() > window->width())
                std::fprintf(stderr, "Palette search bounds: scene=%d x=%.2f width=%.2f dialog=%.2f\n", window->width(), origin.x(), search->width(), palette->property("width").toDouble());
            require(settled, "Narrow palette search is clipped.");
            click("commandPaletteClose");
        }
        if (!populated) require(workspace.currentSession().isEmpty() && workspace.contexts().isEmpty(), "Palette navigation imported data or opened a session.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
