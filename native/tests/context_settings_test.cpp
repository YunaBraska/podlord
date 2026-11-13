#include "window_host.h"
#include "kubeconfig_store.h"
#include "view_state.h"
#include "workspace.h"
#include "ui_input.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>
#include <algorithm>
#include <memory>

using namespace podlord;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition); return false; } } while (false)
bool scenario(const QString& name) {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    QTcpServer api;
    CHECK(api.listen(QHostAddress::LocalHost));
    int requests = 0, connections = 0;
    QObject::connect(&api, &QTcpServer::newConnection, &api, [&] {
        while (auto* socket = api.nextPendingConnection()) {
            ++connections;
            auto pending = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, pending] {
                pending->append(socket->readAll());
                if (!pending->contains("\r\n\r\n")) return;
                const auto path = pending->split(' ').value(1).split('?').first();
                pending->clear(); ++requests;
                const QByteArray body = path == "/api" ? R"({"kind":"APIVersions","versions":["v1"]})"
                    : path == "/apis" ? R"({"kind":"APIGroupList","groups":[]})"
                    : path == "/api/v1" ? R"({"kind":"APIResourceList","groupVersion":"v1","resources":[{"name":"pods","kind":"Pod","namespaced":true,"verbs":["get","list"]}]})"
                    : path == "/api/v1/pods" ? R"({"kind":"PodList","apiVersion":"v1","metadata":{"resourceVersion":"1"},"items":[{"apiVersion":"v1","kind":"Pod","metadata":{"name":"alpha","namespace":"default","uid":"alpha"},"spec":{"containers":[{"name":"app","image":"busybox"}]},"status":{"phase":"Running"}}]})"
                    : R"({"kind":"Status","status":"Failure","reason":"NotFound","code":404})";
                const auto code = path == "/api" || path == "/apis" || path == "/api/v1" || path == "/api/v1/pods" ? "200 OK" : "404 Not Found";
                socket->write(QByteArray("HTTP/1.1 ") + code + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, [&, socket] { --connections; socket->deleteLater(); });
        }
    });
    const auto profile = directory.filePath("profile");
    const KubeconfigStore sources(profile);
    const SessionStore tabs(profile);
    const QString yaml = QString("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: http://127.0.0.1:%1}\ncontexts:\n- name: alpha\n  context: {cluster: local}\n- name: beta\n  context: {cluster: local}\n").arg(api.serverPort());
    const auto imported = sources.importText(directory.filePath("config"), yaml);
    CHECK(std::holds_alternative<SourceSnapshot>(imported));
    const auto source = std::get<SourceSnapshot>(imported);
    const auto alpha = source.contexts.first().id, beta = source.contexts.last().id;
    const auto catalogPath = profile + "/kubeconfigs/catalog.json";
    const auto catalogBytes = read(catalogPath), sourceBytes = read(source.ownedPath);
    CHECK(!catalogBytes.isEmpty());
    if (name.startsWith("tab_schema_")) {
        const auto opened = tabs.openTab(alpha); CHECK(std::holds_alternative<SessionCatalog>(opened));
        const auto path = profile + "/tabs.json";
        auto root = QJsonDocument::fromJson(read(path)).object();
        auto rows = root["sessions"].toArray(); auto row = rows.first().toObject();
        const auto id = row["id"].toString();
        if (name == "tab_schema_version") root["version"] = 99;
        else if (name == "tab_schema_version_type") root["version"] = "4";
        else if (name == "tab_schema_extra") root["extra"] = true;
        else if (name == "tab_schema_sessions") root["sessions"] = QJsonObject{};
        else if (name == "tab_schema_active_type") root["activeSessionId"] = false;
        else if (name == "tab_schema_active_missing") root.remove("activeSessionId");
        else if (name == "tab_schema_active_unknown") root["activeSessionId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        else if (name == "tab_schema_active_canonical") root["activeSessionId"] = "{" + id + "}";
        else if (name == "tab_schema_duplicate") root["sessions"] = QJsonArray{row, row};
        else if (name == "tab_schema_row_type") root["sessions"] = QJsonArray{42};
        else {
            if (name == "tab_schema_id") row["id"] = "not-a-uuid";
            else if (name == "tab_schema_id_type") row["id"] = 42;
            else if (name == "tab_schema_id_canonical") row["id"] = "{" + id + "}";
            else if (name == "tab_schema_context") row["contextId"] = "not-a-context";
            else if (name == "tab_schema_context_type") row["contextId"] = false;
            else if (name == "tab_schema_row_extra") row["extra"] = true;
            else return false;
            rows[0] = row; root["sessions"] = rows;
        }
        const auto bytes = QJsonDocument(root).toJson(); CHECK(write(path, bytes));
        const auto listed = tabs.list(), changed = tabs.openTab(beta);
        CHECK(std::holds_alternative<Failure>(listed) && std::holds_alternative<Failure>(changed));
        CHECK(std::get<Failure>(listed).code == (name == "tab_schema_version" ? StoreError::UnsupportedVersion : StoreError::InvalidData));
        CHECK(read(path) == bytes && read(catalogPath) == catalogBytes && read(source.ownedPath) == sourceBytes);
        return true;
    }
    if (name.startsWith("catalog_schema_")) {
        auto root = QJsonDocument::fromJson(catalogBytes).object();
        auto records = root["sources"].toObject();
        if (name == "catalog_schema_version") root["version"] = 99;
        else if (name == "catalog_schema_version_type") root["version"] = "1";
        else if (name == "catalog_schema_extra") root["extra"] = true;
        else if (name == "catalog_schema_sources") root["sources"] = QJsonArray{};
        else {
            if (name == "catalog_schema_source") { records["not-a-source"] = QJsonArray{}; records.remove(source.id); }
            else if (name == "catalog_schema_entry") records[source.id] = false;
            else if (name == "catalog_schema_context") records[source.id] = QJsonArray{"ctx:invalid"};
            else if (name == "catalog_schema_context_type") records[source.id] = QJsonArray{42};
            else if (name == "catalog_schema_duplicate") records[source.id] = QJsonArray{alpha, alpha};
            else if (name == "catalog_schema_order") { auto values = QStringList{alpha, beta}; values.sort(); std::reverse(values.begin(), values.end()); records[source.id] = QJsonArray::fromStringList(values); }
            else if (name == "catalog_schema_owner") records[source.id] = QJsonArray{"ctx:" + QString(64, '0')};
            else return false;
            root["sources"] = records;
        }
        const auto bytes = QJsonDocument(root).toJson(); CHECK(write(catalogPath, bytes));
        const auto listed = sources.list();
        if (name == "catalog_schema_owner") {
            CHECK(std::holds_alternative<SourceCatalog>(listed));
            CHECK(std::get<SourceCatalog>(listed).sources.isEmpty() && std::get<SourceCatalog>(listed).errors.size() == 1);
        } else CHECK(std::holds_alternative<Failure>(listed));
        CHECK(std::holds_alternative<Failure>(sources.connection(alpha)) && read(catalogPath) == bytes && read(source.ownedPath) == sourceBytes);
        return true;
    }
    if (name == "shared_context" || name == "context_isolation" || name == "close_reopen" || name.startsWith("cache_") || name == "preset_input_context") {
        Workspace workspace(profile);
        const auto wait = [&](const std::function<bool()>& condition) { return QTest::qWaitFor(condition, 10000); };
        CHECK(wait([&] { return !workspace.busy() && !workspace.filterPresetsBusy(); }));
        WindowHost host(workspace);
        CHECK(host.open() && wait([&] { return host.restorationReady(); }));
        auto* window = host.windows().value(0); CHECK(window);
        CHECK(workspace.openContext(alpha) && wait([&] { return !workspace.busy() && !workspace.loading() && workspace.totalResourceCount() == 1 && connections == 0; }));
        const auto first = workspace.currentSession();
        if (name.startsWith("cache_") || name == "close_reopen") {
            CHECK(workspace.filter("alpha") && wait([&] { return workspace.sessionWindowActionsReady(); }));
            const auto before = requests;
            CHECK(workspace.close(first) && wait([&] { return !workspace.busy() && workspace.tabs().isEmpty(); }));
            CHECK(workspace.openContext(name == "cache_context_isolation" ? beta : alpha));
            CHECK(wait([&] { return !workspace.busy() && !workspace.loading() && workspace.totalResourceCount() == 1 && connections == 0; }));
            if (name == "cache_context_isolation") CHECK(requests > before && workspace.filterText().isEmpty());
            else CHECK(requests == before && workspace.filterText() == "alpha");
        } else if (name == "preset_input_context") {
            CHECK(podlord::test::clickVisible(window, "saveFilterPreset"));
            auto* input = podlord::test::visibleItem(window->contentItem(), "filterPresetName"); CHECK(input);
            input->forceActiveFocus();
            for (const char character : QByteArrayLiteral("Unfinished name")) QTest::keyClick(window, character);
            CHECK(input->property("text").toString() == "Unfinished name");
            CHECK(workspace.openContext(beta, false, true) && wait([&] { return !workspace.busy() && workspace.currentSession() != first; }));
            CHECK(input->property("text").toString().isEmpty());
        } else {
            CHECK(workspace.filter("alpha") && wait([&] { return workspace.sessionWindowActionsReady(); }));
            CHECK(workspace.openContext(name == "context_isolation" ? beta : alpha, false, true));
            CHECK(wait([&] { return !workspace.busy() && workspace.currentSession() != first; }));
            if (name == "context_isolation") CHECK(workspace.filterText().isEmpty());
            else {
                CHECK(workspace.filterText() == "alpha" && workspace.filter("beta"));
                CHECK(wait([&] { return workspace.sessionWindowActionsReady(); }));
                CHECK(workspace.activate(first) && wait([&] { return !workspace.busy() && workspace.currentSession() == first; }));
                CHECK(workspace.filterText() == "beta");
            }
        }
        window->close();
        return true;
    }
    if (name == "catalog_membership" || name == "catalog_restart") {
        const auto listed = KubeconfigStore(profile).list();
        CHECK(std::holds_alternative<SourceCatalog>(listed));
        CHECK(std::get<SourceCatalog>(listed).sources.size() == 1 && std::get<SourceCatalog>(listed).sources.first().contexts.size() == 2);
        CHECK(!QFileInfo::exists(profile + "/sessions.json") && !QFileInfo::exists(profile + "/tabs.json"));
    } else if (name == "catalog_orphan" || name == "catalog_missing") {
        CHECK(QFile::remove(catalogPath));
        const auto listed = sources.list();
        CHECK(std::holds_alternative<SourceCatalog>(listed) && std::get<SourceCatalog>(listed).sources.isEmpty());
        CHECK(std::holds_alternative<Failure>(sources.connection(alpha)) && std::holds_alternative<Failure>(sources.ownedYaml(source.id, alpha)));
        CHECK(read(source.ownedPath) == sourceBytes && !QFileInfo::exists(catalogPath));
    } else if (name == "catalog_corrupt" || name == "catalog_symlink") {
        CHECK(QFile::remove(catalogPath));
        if (name == "catalog_symlink") CHECK(QFile::link(source.ownedPath, catalogPath));
        else CHECK(write(catalogPath, "{invalid"));
        CHECK(std::holds_alternative<Failure>(sources.list()) && std::holds_alternative<Failure>(sources.connection(alpha)));
        CHECK(std::holds_alternative<Failure>(sources.importText(directory.filePath("new"), yaml)));
        CHECK(read(source.ownedPath) == sourceBytes);
    } else if (name == "tab_current_only") {
        const auto oldPath = profile + "/sessions.json";
        CHECK(write(oldPath, "{\"version\":3,\"removedContexts\":[]}"));
        CHECK(std::get<SessionCatalog>(tabs.list()).sessions.isEmpty());
        CHECK(std::holds_alternative<SessionCatalog>(tabs.openTab(alpha)));
        CHECK(read(oldPath) == "{\"version\":3,\"removedContexts\":[]}" && !QFileInfo::exists(profile + "/context-settings-backup"));
    } else if (name == "tab_invalid") {
        CHECK(write(profile + "/tabs.json", "{invalid"));
        CHECK(std::holds_alternative<Failure>(tabs.list()));
        CHECK(read(profile + "/tabs.json") == "{invalid");
    } else if (name == "tab_lock") {
        QLockFile lock(profile + "/tabs.lock"); CHECK(lock.tryLock(0));
        CHECK(std::holds_alternative<Failure>(tabs.openTab(alpha)) && !QFileInfo::exists(profile + "/tabs.json"));
    } else if (name == "tab_lifecycle") {
        const auto first = tabs.openTab(alpha); CHECK(std::holds_alternative<SessionCatalog>(first));
        const auto id = std::get<SessionCatalog>(first).sessions.first().id;
        const auto second = tabs.openTab(beta); CHECK(std::holds_alternative<SessionCatalog>(second));
        CHECK(std::holds_alternative<SessionCatalog>(tabs.activate(id)));
        CHECK(std::get<SessionCatalog>(tabs.activate(id)) == std::get<SessionCatalog>(tabs.list()));
        CHECK(std::holds_alternative<Failure>(tabs.closeSessions({id, id})));
        CHECK(std::holds_alternative<SessionCatalog>(tabs.close(id)) && std::get<SessionCatalog>(tabs.list()).sessions.size() == 1);
        CHECK(std::holds_alternative<Failure>(tabs.activate(id)));
        CHECK(std::holds_alternative<SessionCatalog>(tabs.closeWorkspace()) && std::get<SessionCatalog>(tabs.list()).sessions.isEmpty());
    } else if (name.startsWith("context_")) {
        const ViewStateStore settings(profile, {{"resource", {"name"}}});
        const auto loaded = settings.load(alpha); CHECK(std::holds_alternative<TableViewStates>(loaded));
        auto state = std::get<TableViewStates>(loaded).value("resource");
        const auto expected = state; state.filter = "alpha";
        CHECK(std::holds_alternative<TableViewStates>(settings.save(alpha, "resource", state, expected)));
        CHECK(std::get<TableViewStates>(settings.load(alpha)).value("resource").filter == "alpha");
        CHECK(std::get<TableViewStates>(settings.load(beta)).value("resource").filter.isEmpty());
        CHECK(std::holds_alternative<Failure>(settings.save(alpha, "resource", expected, expected)));
        if (name == "context_removed_preferences") {
            CHECK(std::holds_alternative<SessionCatalog>(sources.removeContext(alpha, {})));
            CHECK(std::holds_alternative<Failure>(settings.save(alpha, "resource", state, expected)));
            CHECK(!QFileInfo::exists(profile + "/views/" + contextViewName(alpha)));
        }
    } else {
        const auto opened = tabs.openTab(alpha); CHECK(std::holds_alternative<SessionCatalog>(opened));
        const auto id = std::get<SessionCatalog>(opened).sessions.first().id.toString(QUuid::WithoutBraces);
        if (name == "removal_tab_write_failure") {
            const ViewStateStore settings(profile, {{"resource", {"name"}}});
            const auto baseline = std::get<TableViewStates>(settings.load(alpha)).value("resource");
            auto changed = baseline; changed.filter = "alpha";
            CHECK(std::holds_alternative<TableViewStates>(settings.save(alpha, "resource", changed, baseline)));
            const auto path = profile + "/tabs.json";
            const auto retained = read(path);
            CHECK(QFile::setPermissions(path, QFile::ReadOwner));
            const auto removed = sources.removeContext(alpha, {id});
            CHECK(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
            CHECK(std::holds_alternative<Failure>(removed) && std::get<Failure>(removed).code == StoreError::CleanupFailed);
            CHECK(std::holds_alternative<Failure>(sources.connection(alpha)) && read(path) == retained);
            CHECK(!QFileInfo::exists(profile + "/views/" + contextViewName(alpha)));
            return true;
        }
        if (name == "removal_conflict") {
            CHECK(std::holds_alternative<Failure>(sources.removeContext(alpha, {})) && read(catalogPath) == catalogBytes);
            return true;
        }
        if (name == "removal_busy") {
            QLockFile lock(profile + "/tabs.lock"); CHECK(lock.tryLock(0));
            CHECK(std::holds_alternative<Failure>(sources.removeContext(alpha, {id})) && read(catalogPath) == catalogBytes);
            return true;
        }
        CHECK(std::holds_alternative<SessionCatalog>(sources.removeContext(alpha, {id})));
        CHECK(std::holds_alternative<Failure>(sources.connection(alpha)));
        CHECK(std::get<SourceCatalog>(sources.list()).sources.first().contexts.size() == 1 && read(source.ownedPath) == sourceBytes);
        if (name == "remove_last") {
            CHECK(std::holds_alternative<SessionCatalog>(sources.removeContext(beta, {})));
            CHECK(std::get<SourceCatalog>(sources.list()).sources.isEmpty() && read(source.ownedPath) == sourceBytes);
        } else if (name == "explicit_reimport") {
            CHECK(std::holds_alternative<SourceSnapshot>(sources.importText(directory.filePath("config"), yaml)));
            CHECK(std::get<SourceCatalog>(sources.list()).sources.first().contexts.size() == 2 && std::get<SessionCatalog>(tabs.list()).sessions.isEmpty());
        } else return false;
    }
    return true;
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    return argc == 2 && scenario(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
