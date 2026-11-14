#include "window_host.h"
#include "workspace.h"
#include "kubeconfig_store.h"
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QDir>
#include <QEvent>
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <QLockFile>
#include <QtTest/QTest>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <vector>
#include <limits>

using namespace podlord;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition); return false; } } while (false)

// Delay Qt's completion delivery, not the owned catalog operation or its data.
class DeferredCompletions final : public QObject {
public:
    explicit DeferredCompletions(QObject* owner) : owner_(owner) { QCoreApplication::instance()->installEventFilter(this); }
    ~DeferredCompletions() override { release(); QCoreApplication::instance()->removeEventFilter(this); }
    bool completed() const {
        return std::any_of(events_.begin(), events_.end(), [](const auto& entry) {
            const auto* watcher = qobject_cast<QFutureWatcherBase*>(entry.first.data());
            return watcher && watcher->isFinished();
        });
    }
    void release() {
        holding_ = false;
        for (auto& [receiver, event] : events_) if (receiver) QCoreApplication::postEvent(receiver, event.release());
        events_.clear();
    }
protected:
    bool eventFilter(QObject* receiver, QEvent* event) override {
        if (holding_ && receiver->parent() == owner_ && event->type() == QEvent::FutureCallOut) {
            events_.emplace_back(QPointer<QObject>(receiver), std::unique_ptr<QEvent>(event->clone()));
            return true;
        }
        return false;
    }
private:
    QObject* owner_;
    bool holding_ = true;
    std::vector<std::pair<QPointer<QObject>, std::unique_ptr<QEvent>>> events_;
};

bool scenario(const QString& name) {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    QTcpServer api;
    CHECK(api.listen(QHostAddress::LocalHost));
    int requests = 0;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            auto input = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, input] {
                input->append(socket->readAll());
                if (!input->contains("\r\n\r\n")) return;
                const auto path = input->split(' ').value(1).split('?').first();
                input->clear(); ++requests;
                const QByteArray body = path == "/api" ? R"({"kind":"APIVersions","versions":["v1"]})"
                    : path == "/apis" ? R"({"kind":"APIGroupList","groups":[]})"
                    : R"({"kind":"APIResourceList","groupVersion":"v1","resources":[]})";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto profile = directory.filePath("profile");
    const auto origin = directory.filePath("config");
    auto yaml = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: http://127.0.0.1:%1}\ncontexts:\n- name: alpha\n  context: {cluster: local}\n- name: beta\n  context: {cluster: local}\n- name: gamma\n  context: {cluster: local}\n").arg(api.serverPort());
    if (name == "scale_1000") for (int index = 3; index < 1000; ++index)
        yaml += QString("- name: context-%1\n  context: {cluster: local}\n").arg(index);
    const KubeconfigStore store(profile);
    const auto imported = store.importText(origin, yaml);
    CHECK(std::holds_alternative<SourceSnapshot>(imported));
    const auto source = std::get<SourceSnapshot>(imported);
    const auto alpha = source.contexts[0].id, beta = source.contexts[1].id, gamma = source.contexts[2].id;
    auto now = QDateTime::fromString("2026-11-13T09:00:00.000Z", Qt::ISODateWithMs);
    if (name == "counter_saturation")
        CHECK(std::holds_alternative<ContextUsageMap>(recordContextUsage(profile, {{beta, {std::numeric_limits<qint64>::max(), now}}})));
    std::unique_ptr<QLockFile> shutdownLock;
    const auto wait = [](auto condition) { return QTest::qWaitFor(condition, 10000); };
    const auto order = [](QQuickWindow* window) {
        QStringList result;
        for (const auto& row : window->property("mapContexts").toList()) result.append(row.toMap().value("id").toString());
        return result;
    };
    const auto capture = [&](QQuickWindow* window) {
        const auto folder = qEnvironmentVariable("PODLORD_CONTEXT_USAGE_EVIDENCE");
        if (folder.isEmpty()) return true;
        window->requestUpdate(); QCoreApplication::processEvents();
        const auto image = window->grabWindow();
        return QDir().mkpath(folder) && !image.isNull() && image.save(QDir(folder).filePath(name + ".png"));
    };
    {
        Workspace workspace(profile, nullptr, [&] { return now; });
        WindowHost host(workspace);
        CHECK(host.open() && wait([&] { return host.restorationReady() && !workspace.busy(); }));
        auto* window = host.windows().value(0); CHECK(window);
        if (name == "scale_1000") {
            const auto expected = order(window);
            CHECK(expected.size() == 1000 && requests == 0);
            QList<qint64> samples;
            for (int index = 0; index < 35; ++index) {
                CHECK(workspace.setWorkspacePage("settings"));
                QElapsedTimer timer; timer.start();
                CHECK(workspace.showMapOverview());
                if (index >= 5) samples.append(timer.nsecsElapsed());
                CHECK(order(window) == expected && requests == 0);
                QCoreApplication::processEvents();
            }
            std::sort(samples.begin(), samples.end());
            std::printf("1000-context overview action, 5 warmups/30 samples: p50=%.3f ms p95=%.3f ms, no cluster requests\n",
                samples[15] / 1000000.0, samples[28] / 1000000.0);
            CHECK(capture(window));
            return true;
        }
        const QStringList initial = name == "counter_saturation" ? QStringList{beta, alpha, gamma} : QStringList{alpha, beta, gamma};
        CHECK(order(window) == initial);
        CHECK(requests == 0);
        const auto visit = [&](const QString& context) {
            now = now.addSecs(1);
            return workspace.openContext(context) && wait([&] { return !workspace.busy() && !workspace.loading(); });
        };
        if (name == "frequency" || name == "restart" || name == "remove" || name == "remove_live") {
            CHECK(visit(beta) && visit(alpha) && visit(beta) && visit(gamma));
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{beta, gamma, alpha}));
            if (name == "remove_live") {
                CHECK(workspace.requestSourceRemoval(beta) && workspace.confirmSourceRemoval());
                CHECK(wait([&] { return !workspace.busy() && workspace.contexts().size() == 2; }));
                CHECK(workspace.importText(origin, yaml));
                CHECK(wait([&] { return !workspace.busy() && workspace.contexts().size() == 3; }));
                CHECK(workspace.setWorkspacePage("settings") && workspace.showMapOverview());
                CHECK((order(window) == QStringList{gamma, alpha, beta}));
            }
        } else if (name == "recency") {
            CHECK(visit(alpha) && visit(beta));
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{beta, alpha, gamma}));
        } else if (name == "active_repeat") {
            CHECK(visit(beta));
            for (int index = 0; index < 4; ++index) CHECK(visit(beta));
            CHECK(visit(alpha));
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{alpha, beta, gamma}));
        } else if (name == "tab_switch") {
            CHECK(visit(beta)); const auto first = workspace.currentSession();
            CHECK(visit(alpha));
            const auto before = workspace.tabs();
            now = now.addSecs(1);
            CHECK(workspace.activate(first) && wait([&] { return !workspace.busy(); }));
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{beta, alpha, gamma}));
            const auto after = workspace.tabs();
            CHECK(before.size() == after.size());
            for (qsizetype index = 0; index < before.size(); ++index)
                CHECK(before[index].toMap().value("id") == after[index].toMap().value("id"));
        } else if (name == "remove_reload_race") {
            CHECK(visit(beta) && visit(alpha) && visit(beta) && visit(alpha));
            now = now.addSecs(1);
            CHECK(workspace.openContext(gamma, true) && wait([&] { return !workspace.busy() && host.windows().size() == 2; }));
            auto* second = host.windows().last();
            auto* other = QQmlEngine::contextForObject(second)->contextProperty("workspace").value<Workspace*>(); CHECK(other);
            CHECK(wait([&] { return !other->busy() && !other->loading(); }));
            DeferredCompletions delayed(other);
            CHECK(other->reload());
            CHECK(!other->reload());
            CHECK(QThreadPool::globalInstance()->waitForDone(10000));
            CHECK(wait([&] { return delayed.completed(); }));
            CHECK(other->busy());
            CHECK(workspace.requestSourceRemoval(beta) && workspace.confirmSourceRemoval());
            CHECK(wait([&] { return !workspace.busy() && workspace.contexts().size() == 2; }));
            delayed.release();
            CHECK(wait([&] { return !other->busy(); }));
            CHECK(other->contexts().size() == 2 && workspace.contexts().size() == 2);
            CHECK(workspace.importText(origin, yaml));
            CHECK(wait([&] { return !workspace.busy() && workspace.contexts().size() == 3; }));
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{alpha, gamma, beta}));
        } else if (name == "visible_stability" || name == "shared_windows") {
            now = now.addSecs(1);
            CHECK(workspace.openContext(beta, true) && wait([&] { return !workspace.busy() && host.windows().size() == 2; }));
            CHECK(workspace.mapOverview());
            CHECK((order(window) == QStringList{alpha, beta, gamma}));
            if (name == "shared_windows") {
                auto* second = host.windows().last();
                auto* other = QQmlEngine::contextForObject(second)->contextProperty("workspace").value<Workspace*>(); CHECK(other);
                CHECK(wait([&] { return !other->busy() && !other->loading(); }));
                now = now.addSecs(1);
                CHECK(other->openContext(alpha) && wait([&] { return !other->busy(); }));
                now = now.addSecs(1);
                CHECK(other->openContext(beta) && wait([&] { return !other->busy(); }));
                CHECK((order(window) == QStringList{alpha, beta, gamma}));
            }
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{alpha, beta, gamma}));
            CHECK(workspace.setWorkspacePage("settings") && workspace.showMapOverview());
            CHECK((order(window) == QStringList{beta, alpha, gamma}));
        } else if (name == "close_overview") {
            CHECK(visit(beta) && visit(alpha) && visit(beta) && workspace.showMapOverview());
            CHECK((order(window) == QStringList{beta, alpha, gamma}));
            CHECK(visit(alpha));
            CHECK(workspace.close(workspace.currentSession()) && wait([&] { return !workspace.busy() && workspace.tabs().size() == 1; }));
            const auto remaining = workspace.tabs().first().toMap().value("id").toString();
            CHECK(workspace.close(remaining) && wait([&] { return !workspace.busy() && workspace.tabs().isEmpty(); }));
            CHECK(workspace.mapOverview() && (order(window) == QStringList{alpha, beta, gamma}));
        } else if (name == "invalid_action" || name.startsWith("metadata_")) {
            CHECK(!workspace.openContext("ctx:invalid"));
            CHECK(visit(alpha) && workspace.showMapOverview());
            const auto before = requests;
            CHECK(workspace.showMapOverview());
            CHECK((order(window) == QStringList{alpha, beta, gamma}) && requests == before);
        } else if (name == "counter_saturation") {
            CHECK(visit(beta) && workspace.showMapOverview());
            CHECK((order(window) == QStringList{beta, alpha, gamma}));
        } else if (name == "catalog_lock" || name == "shutdown_write_failure") {
            auto reported = std::make_shared<bool>(false);
            QObject::connect(&workspace, &Workspace::changed, &workspace, [&, reported] {
                if (workspace.error().contains("usage", Qt::CaseInsensitive)) *reported = true;
            });
            shutdownLock = std::make_unique<QLockFile>(profile + "/kubeconfigs/sources.lock");
            CHECK(shutdownLock->tryLock(0));
            CHECK(visit(beta));
            CHECK(wait([&] { return *reported; }));
            if (name == "catalog_lock") {
                shutdownLock->unlock();
                CHECK(visit(alpha));
                CHECK(workspace.showMapOverview());
                CHECK((order(window) == QStringList{alpha, beta, gamma}));
            }
        } else return false;
        CHECK(capture(window));
    }
    if (name == "shutdown_write_failure") {
        CHECK(std::get<ContextCatalog>(readContextCatalog(profile)).usage.isEmpty());
        shutdownLock->unlock();
    }
    if (name.startsWith("metadata_")) {
        const auto path = profile + "/kubeconfigs/catalog.json";
        QFile file(path); CHECK(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto usage = root["usage"].toObject(); auto value = usage[alpha].toObject();
        if (name == "metadata_count") value["count"] = 1.5;
        else if (name == "metadata_time") value["lastUsed"] = "not-a-time";
        else if (name == "metadata_member") { usage.remove(alpha); usage["ctx:" + QString(64, 'f')] = value; }
        else return false;
        if (name != "metadata_member") usage[alpha] = value;
        root["usage"] = usage; const auto invalid = QJsonDocument(root).toJson();
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(invalid) == invalid.size()); file.close();
        CHECK(std::holds_alternative<Failure>(store.list()));
        CHECK(std::holds_alternative<Failure>(store.importText(origin, yaml)));
        CHECK(file.open(QIODevice::ReadOnly) && file.readAll() == invalid);
    }
    if (name == "restart" || name == "remove" || name == "shared_windows" || name == "catalog_lock") {
        if (name == "remove") {
            QStringList ids;
            const auto listed = SessionStore(profile).list(); CHECK(std::holds_alternative<SessionCatalog>(listed));
            for (const auto& tab : std::get<SessionCatalog>(listed).sessions)
                if (tab.config.contextId == beta) ids.append(tab.id.toString(QUuid::WithoutBraces));
            CHECK(std::holds_alternative<SessionCatalog>(store.removeContext(beta, ids)));
            CHECK(std::holds_alternative<SourceSnapshot>(store.importText(origin, yaml)));
        }
        // Finish delivery from the old HTTP clients before measuring a new startup.
        QCoreApplication::processEvents();
        CHECK(wait([&] {
            const auto sockets = api.findChildren<QTcpSocket*>();
            return std::all_of(sockets.cbegin(), sockets.cend(), [](const auto* socket) {
                return socket->state() == QAbstractSocket::UnconnectedState;
            });
        }));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QFile catalog(profile + "/kubeconfigs/catalog.json"); CHECK(catalog.open(QIODevice::ReadOnly));
        const auto before = catalog.readAll(); catalog.close();
        requests = 0;
        Workspace restored(profile, nullptr, [&] { return now; });
        std::unique_ptr<DeferredCompletions> deferredCatalog;
        if (name == "shared_windows") deferredCatalog = std::make_unique<DeferredCompletions>(&restored);
        WindowHost host(restored);
        CHECK(host.open());
        if (deferredCatalog) {
            CHECK(QThreadPool::globalInstance()->waitForDone(10000));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::FutureCallOut);
            CHECK(deferredCatalog->completed());
            deferredCatalog->release();
        }
        CHECK(wait([&] { return host.restorationReady() && !restored.busy(); }));
        const auto expected = name == "remove" ? QStringList{gamma, alpha, beta}
            : name == "catalog_lock" ? QStringList{alpha, beta, gamma} : QStringList{beta, gamma, alpha};
        const auto actual = order(host.windows().value(0));
        if (name == "shared_windows") CHECK((actual == QStringList{beta, alpha, gamma}));
        else CHECK(actual == expected);
        if (requests != 0 || !restored.mapOverview())
            std::fprintf(stderr, "Restored overview: requests=%d, overview=%s\n", requests, restored.mapOverview() ? "true" : "false");
        CHECK(requests == 0);
        CHECK(restored.mapOverview());
        CHECK(catalog.open(QIODevice::ReadOnly) && catalog.readAll() == before);
    }
    return true;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    return argc == 2 && scenario(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
