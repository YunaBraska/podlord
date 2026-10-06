#include "workspace.h"
#include "ui_input.h"
#include <QFile>
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <QWheelEvent>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 8000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) QTest::qWait(10);
    return ready();
}
QQuickItem* item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* found = item(child, name)) return found;
    return nullptr;
}
QQuickItem* item(QQuickWindow* window, const QString& name) { return item(window->contentItem(), name); }
bool click(QQuickWindow* window, const QString& name) {
    if (!waitFor([&] { auto* target=item(window,name); return target && target->isVisible() && target->isEnabled(); })) {
        std::fprintf(stderr,"Unavailable control: %s\n",qPrintable(name)); return false;
    }
    auto* target = item(window, name);
    if (!target || !target->isVisible() || !target->isEnabled()) { std::fprintf(stderr, "Unavailable control: %s\n", qPrintable(name)); return false; }
    if (!podlord::test::scrollIntoView(window, target)) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene({target->width() / 2, target->height() / 2}).toPoint());
    return true;
}
QJsonObject pod(const QString& name, bool single, int generation = 1, bool literalAll = false, bool replaced = false) {
    QJsonArray containers{QJsonObject{{"name", replaced ? "gamma" : literalAll ? "all" : "alpha"}, {"image", "local-image"}}};
    if (!single) containers.append(QJsonObject{{"name", replaced ? "delta" : "beta"}, {"image", "local-image"}});
    return {{"apiVersion", "v1"}, {"kind", "Pod"}, {"metadata", QJsonObject{{"name", name}, {"namespace", "default"}, {"uid", "uid-" + name + '-' + QString::number(generation)}}}, {"spec", QJsonObject{{"containers", containers}}}};
}
// Only the external Kubernetes HTTP boundary is simulated. Native stores, queue,
// authentication state, log retention, timers, model and QML are the real implementations.
class Boundary final : public QTcpServer {
public:
    QString scenario;
    QList<QUrl> logs;
    QList<qint64> starts;
    QElapsedTimer clock;
    bool delivered = false;
    int coreReads = 0;
    int detailReads = 0;
    int workloadReads = 0;
    bool paginationStarted = false, paginationFinished = false;
    explicit Boundary(QString name) : scenario(std::move(name)) {
        clock.start();
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                const auto request = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, request] {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n")) return;
                    socket->disconnect(this);
                    const auto url = QUrl::fromEncoded(request->split(' ')[1]);
                    const auto path = url.path();
                    QByteArray bytes;
                    int status = 200;
                    int delay = 0;
                    QJsonObject document;
                    if (path == "/api") { ++coreReads; document = {{"versions", QJsonArray{"v1"}}}; }
                    else if (path == "/apis") {
                        const QJsonObject version{{"groupVersion", "load.test/v1"}, {"version", "v1"}};
                        document = {{"groups", scenario == "priority" ? QJsonArray{QJsonObject{{"name", "load.test"}, {"versions", QJsonArray{version}}, {"preferredVersion", version}}} : QJsonArray{}}};
                    }
                    else if (path == "/apis/load.test/v1") {
                        QJsonArray resources;
                        for (int index = 0; index < 64; ++index) resources.append(QJsonObject{{"name", "items" + QString::number(index)}, {"kind", "Item" + QString::number(index)}, {"namespaced", true}, {"verbs", QJsonArray{"list"}}});
                        document = {{"resources", resources}};
                    }
                    else if (path.startsWith("/apis/load.test/v1/items")) { ++workloadReads; document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{}}}; }
                    else if (path == "/api/v1") document = {{"resources", QJsonArray{QJsonObject{{"name", "pods"}, {"kind", "Pod"}, {"namespaced", true}, {"verbs", QJsonArray{"get", "list"}}}}}};
                    else if (path == "/api/v1/pods") {
                        const bool replaced = scenario == "detail_expiry" && detailReads > 0;
                        document = {{"metadata", QJsonObject{}}, {"items", QJsonArray{pod("first", scenario == "single", replaced ? 2 : scenario == "recreate" ? coreReads : 1, scenario == "literal_all", replaced), pod("second", false)}}};
                        if (scenario == "pagination" && coreReads > 1) {
                            const bool last = QUrlQuery(url).queryItemValue("continue") == "page-2";
                            if (last) paginationFinished = true; else paginationStarted = true;
                            document = {{"metadata", last ? QJsonObject{} : QJsonObject{{"continue", "page-2"}}}, {"items", QJsonArray{pod(last ? "second" : "first", false)}}};
                        }
                    }
                    else if (path.endsWith("/log")) {
                        logs.append(url); starts.append(clock.elapsed());
                        const auto container = QUrlQuery(url).queryItemValue("container");
                        if (container != "alpha" && container != "beta" && !(container == "all" && scenario == "literal_all")) status = 400;
                        if (scenario.startsWith("detail_") || ((scenario == "open_detail_recreate" || scenario == "pagination") && detailReads > 1)) status = container == "gamma" || container == "delta" ? 200 : 400;
                        if (scenario == "partial" && container == "beta") status = 403;
                        if (scenario == "auth" && container == "beta") status = 401;
                        if (scenario == "backoff" && logs.size() == 1) status = 429;
                        if (scenario == "negotiation" && !request->toLower().contains("\r\naccept: application/json\r\n")) status = 406;
                        const auto timestamp = container == "alpha" ? "2026-10-03T10:00:00.000000100Z" : "2026-10-03T10:00:00.000000050Z";
                        bytes = QByteArray(timestamp) + " " + container.toUtf8() + " output\n";
                        if (scenario == "recreate" && coreReads > 1) bytes = QByteArray(timestamp) + " " + container.toUtf8() + " new instance\n";
                        if (scenario == "malformed" && container == "beta") bytes = "not-a-timestamp output\n";
                        if (scenario == "invalid_utf8" && container == "beta") bytes = QByteArray(timestamp) + " " + QByteArray(1, '\xff');
                        if (scenario == "repeat") bytes += bytes;
                        if (scenario == "limit") bytes = QByteArray(timestamp) + " " + QByteArray(700000, 'x') + "\n";
                        if (scenario.startsWith("preview_") || scenario.startsWith("entry_"))
                            bytes = QByteArray(timestamp) + " " + (scenario == "preview_unicode"
                                ? QByteArray("x") + QByteArray("\xf0\x9f\x9a\x80").repeated(8000) : QByteArray(50000, 'x')) + "\n";
                        if (scenario == "entry_unicode_copy") bytes = QByteArray(timestamp) + " " + QByteArray("\xe6\xbc\xa2\xe5\xad\x97\xf0\x9f\x9a\x80").repeated(8000) + "\n";
                        if (scenario == "entry_bom_copy") bytes = QByteArray("\xef\xbb\xbf") + timestamp + " \xef\xbb\xbf" + container.toUtf8() + "\n";
                        if (scenario == "entry_empty_copy") bytes = QByteArray(timestamp) + " \n";
                        if (scenario == "entry_crlf_copy") bytes = QByteArray(timestamp) + " " + container.toUtf8() + "\r\n";
                        if (scenario == "scroll" || scenario == "eviction" || scenario == "expiry") {
                            bytes.clear();
                            const auto start = QDateTime::fromString("2026-10-03T10:00:00Z", Qt::ISODate);
                            for (int index = 0; index < (logs.size() > 2 && scenario == "scroll" ? 51 : 50); ++index)
                                bytes += start.addSecs(index).toString(Qt::ISODate).toUtf8() + " " + (scenario == "eviction" ? QByteArray(13000, 'x') : QByteArray("entry ") + QByteArray::number(index)) + '\n';
                        }
                        if (scenario == "markup") bytes = QByteArray(timestamp) + " <img src=\"http://127.0.0.1:9/leak\"/>\n";
                        if ((scenario == "late" || scenario == "hide_window" || scenario == "queued_select" || scenario == "resource_refresh") && container == "alpha") delay = 750;
                    } else if (path.startsWith("/api/v1/namespaces/default/pods/")) {
                        ++detailReads;
                        const bool replaced = scenario.startsWith("detail_") || ((scenario == "open_detail_recreate" || scenario == "pagination") && detailReads > 1);
                        document = pod(path.section('/', -1), scenario == "single", replaced ? 2 : 1, scenario == "literal_all", replaced);
                    }
                    else status = 404;
                    if (bytes.isEmpty()) bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
                    const auto response = "HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: text/plain\r\nConnection: close\r\nRetry-After: 2\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes;
                    const QPointer<QTcpSocket> live(socket);
                    QTimer::singleShot(delay, this, [this, live, response, delay] {
                        if (live && live->state() == QAbstractSocket::ConnectedState) { live->write(response); if (delay) delivered = true; live->disconnectFromHost(); }
                    });
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                });
            }
        });
    }
};
bool execute(const QString& scenario, const QString& realFile = {}) {
    QTemporaryDir temporary;
    Boundary server(scenario);
    if (!temporary.isValid() || !server.listen(QHostAddress::LocalHost)) return false;
    const auto source = realFile.isEmpty() ? temporary.filePath("source.config") : realFile;
    QFile file(source);
    const auto bytes = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'http://127.0.0.1:" + QByteArray::number(server.serverPort()) + "'}\nusers:\n- name: local\n  user: {token: local-test}\ncontexts:\n- name: local\n  context: {cluster: local, user: local}\n";
    if (realFile.isEmpty()) {
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
        file.close();
    }
    auto utc = QDateTime::currentDateTimeUtc();
    const bool frozenClock = scenario == "expiry" || scenario == "detail_clock_same" || scenario == "detail_clock_backwards" || scenario == "detail_expiry";
    podlord::Workspace workspace(temporary.filePath("profile"), nullptr, frozenClock ? std::function<QDateTime()>{[&utc] { return utc; }} : std::function<QDateTime()>{QDateTime::currentDateTimeUtc});
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspace", &workspace);
    engine.load(QUrl("qrc:/podlord/Main.qml"));
    if (engine.rootObjects().isEmpty()) return false;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return false;
    QTest::qWait(30);
    if (!waitFor([&] { return !workspace.busy(); })) return false;
    item(window, "sourcePath")->setProperty("text", source);
    if (!click(window, "importButton") || !waitFor([&] { return workspace.contexts().size() == 1 && !workspace.busy(); }) || !click(window, "openContext")) return false;
    if (!realFile.isEmpty()) {
        auto* filter = item(window, "resourceFilter");
        if (!waitFor([&] { return filter->isEnabled(); })) return false;
        filter->forceActiveFocus();
        for (const auto c : QString("podlord-native-log-e2e")) QTest::keyClick(window, c.toLatin1());
    }
    int selectedRow = 0;
    const bool resourceReady = waitFor([&] {
        if (workspace.busy()) return false;
        if (realFile.isEmpty()) return workspace.table()->rowCount() == 2;
        for (int row = 0; row < workspace.table()->rowCount(); ++row) {
            bool podKind = false, podName = false;
            for (int column = 0; column < workspace.table()->columnCount(); ++column) {
                const auto value = workspace.table()->data(workspace.table()->index(row, column)).toString();
                podKind = podKind || value == "Pod";
                podName = podName || value == "podlord-native-log-e2e";
            }
            if (podKind && podName) { selectedRow = row; return true; }
        }
        return false;
    }, realFile.isEmpty() ? 8000 : 70000);
    if (scenario == "detail_clock_backwards") utc = utc.addSecs(-30);
    if (!resourceReady || !click(window, QString("cell_%1_0").arg(selectedRow))) {
        if (!realFile.isEmpty()) std::fprintf(stderr, "Real Pod selection failed: rows=%d error=%s\n", workspace.table()->rowCount(), qPrintable(workspace.error()));
        return false;
    }
    if (!realFile.isEmpty()) std::fprintf(stderr, "Real Pod selected from %d matching resource rows.\n", workspace.table()->rowCount());
    if (realFile.isEmpty() && scenario != "priority" && !waitFor([&] { return !workspace.loading(); })) return false;
    if (scenario == "priority") {
        QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
        window->requestUpdate();
        if (rendered.isEmpty() && !rendered.wait(1000)) return false;
    }
    QList<qint64> logStarts;
    QObject::connect(&workspace, &podlord::Workspace::requestStarted, &workspace,
        [&](const QString&, const QString& path, qint64 at) { if (path.endsWith("/log")) logStarts.append(at); });
    if (!click(window, "logsButton")) return false;
    auto* model = workspace.property("logRows").value<QAbstractItemModel*>();
    if (!model) return false;
    if (scenario == "priority") {
        const bool ready = waitFor([&] { return server.logs.size() == 2 && model->rowCount() == 2; }, 2200) && server.workloadReads < 64;
        if (!ready) std::fprintf(stderr, "Priority logs: requests=%lld rows=%d detailReads=%d selection=%s status=%s inspector=%s loading=%d error=%s\n", static_cast<long long>(server.logs.size()), model->rowCount(), server.detailReads, qPrintable(workspace.property("logSelection").toString()), qPrintable(workspace.property("logStatus").toString()), qPrintable(workspace.inspectorPath()), workspace.loading(), qPrintable(workspace.error()));
        return ready;
    }
    if (!realFile.isEmpty()) {
        const bool ready = waitFor([&] {
            if (model->rowCount() < 2) return false;
            QStringList lines;
            for (int row = 0; row < model->rowCount(); ++row) lines.append(model->data(model->index(row, 0)).toString());
            return item(window, "logContainers")->property("displayText").toString() == "all" && lines.join('\n').contains("[alpha] podlord-alpha-real-log") && lines.join('\n').contains("[beta] podlord-beta-real-log");
        }, 20000);
        if (!ready) {
            std::fprintf(stderr, "Real Pod logs failed: rows=%d selection=%s status=%s error=%s\n", model->rowCount(), qPrintable(workspace.property("logSelection").toString()), qPrintable(workspace.property("logStatus").toString()), qPrintable(workspace.error()));
            for (int row = 0; row < model->rowCount(); ++row) std::fprintf(stderr, "%s\n", qPrintable(model->data(model->index(row, 0)).toString().left(512)));
        }
        return ready;
    }
    if (scenario == "late" || scenario == "hide_window") {
        if (!waitFor([&] { return server.logs.size() == 1; })) return false;
        if (scenario == "hide_window") window->hide();
        else if (!click(window, "overviewButton")) return false;
        if (!waitFor([&] { return server.delivered; })) return false;
        QTest::qWait(3500);
        if (server.logs.size() != 1 || workspace.property("logsVisible").toBool() || model->rowCount() != 0) return false;
        if (scenario == "hide_window") window->show();
        else if (!click(window, "logsButton")) return false;
        return model->rowCount() == 1 && model->data(model->index(0, 0)).toString().contains("[alpha]");
    }
    if (scenario == "queued_select" || scenario == "resource_refresh") {
        if (!waitFor([&] { return server.logs.size() == 1; })) return false;
        if (scenario == "resource_refresh") {
            const auto before = server.coreReads;
            return click(window, "refreshButton") && waitFor([&] { return server.coreReads > before; });
        }
        auto* selector = item(window, "logContainers"); selector->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Down);
        if (!waitFor([&] { return server.delivered && !workspace.loading(); })) return false;
        return server.logs.size() == 1 && model->rowCount() == 1 && workspace.property("logSelection").toString() == "alpha";
    }
    const int expected = scenario == "single" ? 1 : 2;
    if (!waitFor([&] { return server.logs.size() >= expected && !workspace.loading(); })) return false;
    if (scenario.startsWith("preview_") || scenario.startsWith("entry_")) {
        if (!waitFor([&] { return model->rowCount() == 2 && item(window,"logEntry_0") != nullptr; }) || !workspace.pauseLogs(true)) return false;
        const auto full = workspace.logEntry(0);
        const auto requests = server.logs.size();
        if (scenario == "entry_unicode_copy" || scenario == "entry_bom_copy" || scenario == "entry_empty_copy" || scenario == "entry_crlf_copy") {
            const QString expectedBody = scenario == "entry_unicode_copy" ? QString::fromUtf8("\xe6\xbc\xa2\xe5\xad\x97\xf0\x9f\x9a\x80").repeated(8000)
                : scenario == "entry_bom_copy" ? QString(QChar(0xfeff)) + "beta"
                : scenario == "entry_crlf_copy" ? QStringLiteral("beta\r") : QString{};
            const auto expected = QStringLiteral("2026-10-03T10:00:00.000000050Z [beta] ") + expectedBody;
            return full == expected && click(window,"logEntry_0") && click(window,"copyLogEntry")
                && QGuiApplication::clipboard()->text() == expected && server.logs.size() == requests;
        }
        auto* row = item(window,"logEntry_0");
        if (!row || !podlord::test::scrollIntoView(window,row)) return false;
        if (scenario.startsWith("preview_")) {
            const auto shown = row->property("text").toString();
            const auto end = shown.indexOf(" [Open entry for full text]");
            const bool passed = end > 0 && end <= 1200 && shown.left(end) == full.left(end) && !shown[end-1].isHighSurrogate();
            if (!passed) std::fprintf(stderr,"Log preview: displayed prefix=%lld, full characters=%lld, intact UTF-16=%d\n",static_cast<long long>(end),static_cast<long long>(full.size()),end>0 && !shown[end-1].isHighSurrogate());
            return passed && server.logs.size() == requests;
        }
        if (!click(window,"logEntry_0")) return false;
        if (scenario == "entry_copy") return click(window,"copyLogEntry") && QGuiApplication::clipboard()->text() == full && full.size() > 50000 && server.logs.size() == requests;
        row->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Return);
        return waitFor([&] { auto* text=item(window,"fullLogEntry"); return text && text->isVisible() && text->property("text").toString() == full; })
            && full.size() > 50000 && server.logs.size() == requests;
    }
    if (scenario == "single") return workspace.property("logSelection").toString() == "alpha" && model->rowCount() == 1 && QUrlQuery(server.logs.first()).queryItemValue("container") == "alpha";
    if (scenario == "open_detail_recreate" || scenario == "pagination") {
        if (scenario == "pagination" && (!click(window, "refreshButton") || !waitFor([&] { return server.paginationStarted; }))) return false;
        if (!click(window, "cell_0_0") || !waitFor([&] { return model->rowCount() == 2 && model->data(model->index(0, 0)).toString().contains("[delta]") && model->data(model->index(1, 0)).toString().contains("[gamma]"); })) return false;
        if (scenario == "pagination") return waitFor([&] { return server.paginationFinished && !workspace.loading(); }) && workspace.inspected().contains("uid-first-2") && model->data(model->index(0, 0)).toString().contains("[delta]");
        return workspace.inspected().contains("uid-first-2");
    }
    if (scenario.startsWith("detail_")) {
        const bool ready = model->rowCount() == 2 && model->data(model->index(0, 0)).toString().contains("[delta]") && model->data(model->index(1, 0)).toString().contains("[gamma]");
        if (!ready) std::fprintf(stderr, "Fresh detail containers ignored: %s\n", qPrintable(workspace.property("logStatus").toString()));
        if (ready && scenario == "detail_expiry") {
            if (!click(window, "pauseLogs")) return false;
            utc = utc.addSecs(301);
            if (!click(window, "refreshButton")) return false;
            return workspace.inspected().contains("uid-first-2") && model->rowCount() == 2 && workspace.property("logsPaused").toBool() && model->data(model->index(0, 0)).toString().contains("[delta]");
        }
        return ready;
    }
    if (scenario == "recreate") {
        return click(window, "refreshButton") && waitFor([&] { return server.coreReads > 1 && model->rowCount() == 2 && model->data(model->index(0, 0)).toString().contains("new instance") && model->data(model->index(1, 0)).toString().contains("new instance"); });
    }
    if (scenario == "scroll" || scenario == "eviction" || scenario == "expiry") {
        if (model->rowCount() != 100) return false;
        auto* list = item(window, "podLogEntries");
        QTest::qWait(40);
        const auto position = list->mapToScene({list->width() / 2, list->height() / 2});
        QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), {}, {0, scenario == "eviction" ? 12000 : 1200}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel); QTest::qWait(50);
        if (scenario == "eviction" || scenario == "expiry") {
            list->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
            if (!waitFor([&] { return list->property("atYBeginning").toBool(); })) return false;
            QTest::qWait(30);
        }
        if (workspace.property("logFollow").toBool() || workspace.property("logAnchor").toString().isEmpty()) return false;
        if (scenario == "expiry") {
            utc = utc.addSecs(61);
            if (!click(window, "refreshButton")) return false;
            return waitFor([&] { return model->rowCount() == 0 && !workspace.property("logFollow").toBool()
                && workspace.property("logPositionNotice").toString().contains("cache expiry")
                && !workspace.property("logPositionNotice").toString().contains("log-size limit"); }, 1000);
        }
        if (scenario == "scroll") {
            const auto before = list->property("contentY").toDouble();
            if (!waitFor([&] { return model->rowCount() == 102; }, 5500)) return false;
            QTest::qWait(50);
            if (std::abs(list->property("contentY").toDouble() - before) > 1 || list->property("atYEnd").toBool() || !click(window, "liveFollow")) return false;
            return waitFor([&] { return list->property("atYEnd").toBool() && workspace.property("logFollow").toBool(); });
        }
    }
    if (scenario == "partial" || scenario == "malformed" || scenario == "invalid_utf8") return model->rowCount() == 1 && workspace.property("logStatus").toString().contains("beta");
    if (scenario.startsWith("invalid_") || scenario == "limit" || scenario == "restore" || scenario == "eviction") {
        if (!click(window, "settingsWorkspaceButton") || !click(window, "settingsSyncSection")) return false;
        auto* input = item(window, "inlineLogLimit");
        if (input->property("text").toString() != "5") return false;
        const QMap<QString, QString> values{{"invalid_zero", "0"}, {"invalid_negative", "-1"}, {"invalid_fraction", "1.5"}, {"invalid_empty", ""}, {"invalid_text", "nope"}, {"limit", "1"}, {"restore", "6"}, {"eviction", "1"}};
        input->forceActiveFocus(); QTest::keySequence(window, QKeySequence::SelectAll); QTest::keyClick(window, Qt::Key_Backspace);
        for (const auto c : values.value(scenario)) QTest::keyClick(window, c.toLatin1());
        if (input->property("text").toString() != values.value(scenario)) return false;
        QTest::qWait(30);
        if (!click(window, "inlineSaveSync")) return false;
        if (scenario.startsWith("invalid_")) return workspace.property("logLimitMb").toInt() == 5 && workspace.error().contains("positive whole");
        if (!waitFor([&] { return !workspace.busy() && workspace.property("logLimitMb").toInt() == values.value(scenario).toInt(); })) return false;
        if (!click(window, "resourcesWorkspaceButton")) return false;
        if (scenario == "eviction") {
            auto* list = item(window, "podLogEntries");
            const bool ready = waitFor([&] { return model->rowCount() < 100 && !workspace.property("logFollow").toBool() && !workspace.property("logPositionNotice").toString().isEmpty() && list->property("atYBeginning").toBool(); });
            if (!ready) std::fprintf(stderr, "Eviction result: rows=%d limit=%d entryBytes=%lld follow=%d notice=%s beginning=%d y=%.1f anchor=%s status=%s error=%s\n", model->rowCount(), workspace.property("logLimitMb").toInt(), static_cast<long long>(workspace.logEntry(0).toUtf8().size()), workspace.property("logFollow").toBool(), qPrintable(workspace.property("logPositionNotice").toString()), list->property("atYBeginning").toBool(), list->property("contentY").toDouble(), qPrintable(workspace.property("logAnchor").toString()), qPrintable(workspace.property("logStatus").toString()), qPrintable(workspace.error()));
            return ready;
        }
        if (scenario == "restore") {
            podlord::Workspace reopened(temporary.filePath("profile"));
            QTest::qWait(30);
            return waitFor([&] { return !reopened.busy(); }) && reopened.property("logLimitMb").toInt() == 6;
        }
        if (model->rowCount() != 1 || !workspace.property("logStatus").toString().contains("limited") || !click(window, "logEntry_0") || !click(window, "copyLogEntry")) return false;
        return QGuiApplication::clipboard()->text().size() > 700000 && QGuiApplication::clipboard()->text().contains("[alpha]");
    }
    if (scenario == "auth") { const auto count = server.logs.size(); QTest::qWait(3500); return workspace.authenticationRequired() && server.logs.size() == count; }
    if (scenario == "pause") {
        if (!click(window, "pauseLogs")) return false;
        const auto count = server.logs.size();
        QTest::qWait(3500);
        if (server.logs.size() != count || !workspace.property("logsPaused").toBool() || !click(window, "pauseLogs")) return false;
        return waitFor([&] { return server.logs.size() > count; }, 1500) && !workspace.property("logsPaused").toBool();
    }
    if (scenario == "cadence") {
        const bool complete = waitFor([&] { return server.logs.size() >= 4 && logStarts.size() >= 4 && model->rowCount() == 2; }, 5500);
        const bool correct = complete && logStarts[2] - logStarts[0] >= 3000;
        if (!correct) {
            std::fprintf(stderr, "Log cadence: requests=%lld rows=%d selection=%s status=%s\n", static_cast<long long>(server.logs.size()), model->rowCount(), qPrintable(workspace.property("logSelection").toString()), qPrintable(workspace.property("logStatus").toString()));
            for (int index = 0; index < server.logs.size(); ++index) std::fprintf(stderr, "  %lld ms: %s\n", server.starts[index], qPrintable(server.logs[index].toString()));
            for (int index = 0; index < logStarts.size(); ++index) std::fprintf(stderr, "  Local dispatch %d: %lld ms\n", index, logStarts[index]);
        }
        return correct;
    }
    if (scenario == "repeat") return waitFor([&] { return server.logs.size() >= 4; }, 5500) && model->rowCount() == 4;
    if (scenario == "backoff") return server.starts[1] - server.starts[0] >= 1990 && workspace.property("logStatus").toString().contains("alpha");
    if (scenario == "markup") return model->rowCount() == 2 && model->data(model->index(0, 0)).toString().contains("<img");
    if (scenario == "select" || scenario == "literal_all") {
        auto* selector = item(window, "logContainers");
        if (!selector) return false;
        selector->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Home); QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        const auto name = scenario == "literal_all" ? "all" : "alpha";
        return waitFor([&] { return workspace.property("logSelection").toString() == name && model->rowCount() == 1; }) && model->data(model->index(0, 0)).toString().contains('[' + QString(name) + ']');
    }
    return item(window, "logContainers")->property("displayText").toString() == "all" && model->rowCount() == 2 && model->data(model->index(0, 0)).toString().contains("[beta]") && model->data(model->index(1, 0)).toString().contains("[alpha]")
        && QUrlQuery(server.logs[0]).queryItemValue("timestamps") == "true" && QUrlQuery(server.logs[1]).queryItemValue("container") == "beta";
}
} // namespace
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    if (application.arguments().size() == 3 && application.arguments()[1] == "real") return execute("real", application.arguments()[2]) ? 0 : 1;
    if (application.arguments().size() != 2) return 2;
    return execute(application.arguments()[1]) ? 0 : 1;
}
