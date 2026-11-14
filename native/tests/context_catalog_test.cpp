#include "context_catalog.h"
#include "kubeconfig_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>
#include <limits>
#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace podlord;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "Failed line %d: %s\n", __LINE__, #value); return false; } } while (false)

#ifdef Q_OS_UNIX
class FileSizeLimit final {
public:
    explicit FileSizeLimit(rlim_t maximum) {
        if (getrlimit(RLIMIT_FSIZE, &previous_) != 0) return;
        handler_ = std::signal(SIGXFSZ, SIG_IGN);
        auto next = previous_;
        next.rlim_cur = std::min(previous_.rlim_cur, maximum);
        active = handler_ != SIG_ERR && setrlimit(RLIMIT_FSIZE, &next) == 0;
    }
    ~FileSizeLimit() {
        if (active) setrlimit(RLIMIT_FSIZE, &previous_);
        if (handler_ != SIG_ERR) std::signal(SIGXFSZ, handler_);
    }
    bool active = false;
private:
    rlimit previous_{};
    void (*handler_)(int) = SIG_ERR;
};
#endif

bool scenario(const QString& name) {
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const auto profile = temporary.filePath("profile");
    const auto origin = temporary.filePath("config");
    QString yaml = "apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: https://127.0.0.1:6443}\ncontexts:\n";
    const bool large = name.endsWith("large") || name == "record_write_failure";
    for (int index = 0; index < (large ? 100 : 1); ++index)
        yaml += QString("- name: context-%1\n  context: {cluster: local}\n").arg(index);
    const KubeconfigStore sources(profile);
    const auto imported = sources.importText(origin, yaml);
    CHECK(std::holds_alternative<SourceSnapshot>(imported));
    const auto source = std::get<SourceSnapshot>(imported);
    const auto id = source.contexts.first().id;
    const auto path = profile + "/kubeconfigs/catalog.json";
    const auto read = [](const QString& filePath) { QFile file(filePath); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; };
    const auto write = [](const QString& filePath, const QByteArray& bytes) {
        QFile file(filePath);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size() && file.flush();
    };
    const auto original = read(path), owned = read(source.ownedPath);
    const auto loaded = readContextCatalog(profile);
    CHECK(std::holds_alternative<ContextCatalog>(loaded));
    auto catalog = std::get<ContextCatalog>(loaded);
    const auto now = QDateTime::fromString("2026-11-13T09:00:00.000Z", Qt::ISODateWithMs);
    const ContextUsageMap increment{{id, {1, now}}};
    const auto publish = [&](const ContextCatalog& value) -> Result<bool> {
        QLockFile lock(profile + "/kubeconfigs/sources.lock"); lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) return Failure{StoreError::Busy, "Test could not acquire the catalog writer lock."};
        return writeContextCatalog(profile, value);
    };

    if (name == "usage_accumulation") {
        CHECK(std::holds_alternative<ContextUsageMap>(recordContextUsage(profile, {{id, {2, now}}})));
        const auto result = recordContextUsage(profile, {{id, {1, now.addSecs(10)}}});
        CHECK(std::holds_alternative<ContextUsageMap>(result));
        const auto use = std::get<ContextUsageMap>(result).value(id);
        CHECK(use.count == 3 && use.lastUsed == now.addSecs(10));
        CHECK(read(source.ownedPath) == owned);
        CHECK(std::holds_alternative<SourceSnapshot>(sources.renameContext(id, "Local label")));
        CHECK(std::holds_alternative<SourceSnapshot>(sources.importText(origin, yaml)));
        const auto retained = std::get<ContextCatalog>(readContextCatalog(profile)).usage.value(id);
        CHECK(retained.count == 3 && retained.lastUsed == use.lastUsed);
        const auto record = QJsonDocument::fromJson(read(path)).object()["usage"].toObject().value(id).toObject();
        CHECK(record.size() == 2 && record.contains("count") && record.contains("lastUsed"));
    } else if (name == "usage_saturation") {
        catalog.usage[id] = {std::numeric_limits<qint64>::max(), now};
        CHECK(std::holds_alternative<bool>(publish(catalog)));
        const auto result = recordContextUsage(profile, {{id, {3, now.addSecs(-10)}}});
        CHECK(std::holds_alternative<ContextUsageMap>(result));
        const auto use = std::get<ContextUsageMap>(result).value(id);
        CHECK(use.count == std::numeric_limits<qint64>::max() && use.lastUsed == now);
    } else if (name == "usage_removed") {
        CHECK(std::holds_alternative<ContextUsageMap>(recordContextUsage(profile, increment)));
        CHECK(std::holds_alternative<SessionCatalog>(sources.removeContext(id, {})));
        const auto removed = read(path);
        const auto result = recordContextUsage(profile, increment);
        CHECK(std::holds_alternative<ContextUsageMap>(result) && std::get<ContextUsageMap>(result).isEmpty());
        CHECK(read(path) == removed && std::get<ContextCatalog>(readContextCatalog(profile)).usage.isEmpty());
    } else if (name == "usage_invalid_increment") {
        for (const auto& value : {ContextUsage{0, now}, ContextUsage{-1, now}, ContextUsage{1, {}}}) {
            const auto result = recordContextUsage(profile, {{id, value}});
            CHECK(std::holds_alternative<Failure>(result) && std::get<Failure>(result).code == StoreError::InvalidInput);
            CHECK(read(path) == original);
        }
        CHECK(std::holds_alternative<ContextUsageMap>(recordContextUsage(profile, {})) && read(path) == original);
    } else if (name == "usage_schema") {
        CHECK(std::holds_alternative<ContextUsageMap>(recordContextUsage(profile, increment)));
        const auto valid = read(path);
        const auto root = QJsonDocument::fromJson(valid).object();
        QList<QJsonDocument> invalid{QJsonDocument(QJsonArray{})};
        auto changed = root; changed["extra"] = true; invalid.append(QJsonDocument(changed));
        changed = root; changed["usage"] = QJsonArray{}; invalid.append(QJsonDocument(changed));
        changed = root; changed["usage"] = QJsonObject{{id, 1}}; invalid.append(QJsonDocument(changed));
        const auto row = root["usage"].toObject().value(id).toObject();
        for (const auto& field : {QString("extra"), QString("count"), QString("lastUsed")}) {
            auto value = row;
            if (field == "extra") value["extra"] = true;
            else if (field == "count") value["count"] = 0;
            else value["lastUsed"] = "2026-11-13T10:00:00.000+01:00";
            changed = root; changed["usage"] = QJsonObject{{id, value}};
            invalid.append(QJsonDocument(changed));
        }
        for (const auto& document : invalid) {
            const auto bytes = document.toJson(); CHECK(write(path, bytes));
            CHECK(std::holds_alternative<Failure>(readContextCatalog(profile)));
            CHECK(std::holds_alternative<Failure>(recordContextUsage(profile, increment)));
            CHECK(read(path) == bytes);
        }
        CHECK(write(path, valid));
    } else if (name == "profile_input" || name == "profile_file" || name == "source_folder_file") {
        QStringList invalid;
        if (name == "profile_input") invalid = {QString{}, QString("relative-profile")};
        else if (name == "profile_file") {
            const auto file = temporary.filePath("not-a-directory"); CHECK(write(file, "keep")); invalid.append(file);
        } else {
            const auto folder = temporary.filePath("bad-profile"); CHECK(QDir().mkpath(folder));
            CHECK(write(folder + "/kubeconfigs", "keep")); invalid.append(folder);
        }
        for (const auto& target : invalid) {
            CHECK(std::holds_alternative<Failure>(readContextCatalog(target)));
            CHECK(std::holds_alternative<Failure>(writeContextCatalog(target, catalog)));
            CHECK(std::holds_alternative<Failure>(recordContextUsage(target, increment)));
            if (name != "source_folder_file") CHECK(std::holds_alternative<Failure>(SessionStore(target).openTab(id)));
        }
        CHECK(read(path) == original);
    } else if (name == "catalog_directory" || name == "lock_directory") {
        const auto target = name == "catalog_directory" ? path : profile + "/kubeconfigs/sources.lock";
        if (name == "catalog_directory") CHECK(QFile::remove(path));
        CHECK(QDir().mkdir(target));
        if (name == "catalog_directory") {
            CHECK(std::holds_alternative<Failure>(readContextCatalog(profile)));
            CHECK(std::holds_alternative<Failure>(writeContextCatalog(profile, catalog)));
        }
        CHECK(std::holds_alternative<Failure>(recordContextUsage(profile, increment)));
    } else if (name == "lock_busy") {
        QLockFile lock(profile + "/kubeconfigs/sources.lock"); CHECK(lock.tryLock(0));
        const auto result = recordContextUsage(profile, increment);
        CHECK(std::holds_alternative<Failure>(result) && std::get<Failure>(result).code == StoreError::Busy);
        CHECK(read(path) == original);
    } else if (name == "tab_inputs") {
        const SessionStore tabs(profile);
        const auto opened = tabs.openTab(id); CHECK(std::holds_alternative<SessionCatalog>(opened));
        const auto tab = std::get<SessionCatalog>(opened).sessions.first().id;
        const auto before = read(profile + "/tabs.json");
        CHECK(std::holds_alternative<Failure>(tabs.activate({})));
        CHECK(std::holds_alternative<Failure>(tabs.activate(QUuid::createUuid())));
        CHECK(std::holds_alternative<Failure>(tabs.closeSessions({})));
        CHECK(std::holds_alternative<Failure>(tabs.closeSessions({QUuid{}})));
        CHECK(std::holds_alternative<Failure>(tabs.closeSessions({tab, tab})));
        CHECK(std::holds_alternative<Failure>(tabs.close(QUuid::createUuid())));
        CHECK(std::holds_alternative<Failure>(tabs.openTab("ctx:invalid")));
        CHECK(read(profile + "/tabs.json") == before);
        const auto listed = tabs.list(); CHECK(std::holds_alternative<SessionCatalog>(listed));
        for (const auto& value : std::get<SessionCatalog>(listed).sessions) CHECK(value.open);
    } else if (name == "tab_parent_file") {
        const auto parent = temporary.filePath("parent-file"); CHECK(write(parent, "keep"));
        const auto result = SessionStore(parent + "/child").openTab(id);
        CHECK(std::holds_alternative<Failure>(result) && std::get<Failure>(result).code == StoreError::WriteFailed);
        CHECK(read(parent) == "keep");
#ifdef Q_OS_UNIX
    } else if (name == "catalog_symlink" || name == "lock_symlink" || name == "profile_symlink") {
        QString target = profile + "/kubeconfigs/sources.lock";
        if (name == "catalog_symlink") { target = path; CHECK(QFile::remove(path)); }
        if (name == "profile_symlink") {
            target = temporary.filePath("linked-profile"); CHECK(QFile::link(profile, target));
            CHECK(std::holds_alternative<Failure>(readContextCatalog(target)));
            CHECK(std::holds_alternative<Failure>(writeContextCatalog(target, catalog)));
            CHECK(std::holds_alternative<Failure>(recordContextUsage(target, increment)));
        } else {
            CHECK(QFile::link(source.ownedPath, target));
            if (name == "catalog_symlink") {
                CHECK(std::holds_alternative<Failure>(readContextCatalog(profile)));
                CHECK(std::holds_alternative<Failure>(writeContextCatalog(profile, catalog)));
            }
            CHECK(std::holds_alternative<Failure>(recordContextUsage(profile, increment)));
        }
        CHECK(read(source.ownedPath) == owned);
    } else if (name == "catalog_unreadable" || name == "write_denied" || name == "usage_lock_denied" || name == "tab_lock_denied") {
        const auto target = name == "catalog_unreadable" ? path : name == "tab_lock_denied" ? profile : profile + "/kubeconfigs";
        const auto permissions = QFileInfo(target).permissions();
        CHECK(QFile::setPermissions(target, name == "catalog_unreadable" ? QFile::WriteOwner : QFile::ReadOwner | QFile::ExeOwner));
        bool failed = false;
        if (name == "catalog_unreadable") failed = std::holds_alternative<Failure>(readContextCatalog(profile));
        else if (name == "write_denied") failed = std::holds_alternative<Failure>(writeContextCatalog(profile, catalog));
        else if (name == "tab_lock_denied") failed = std::holds_alternative<Failure>(SessionStore(profile).openTab(id));
        else failed = std::holds_alternative<Failure>(recordContextUsage(profile, increment));
        CHECK(QFile::setPermissions(target, permissions));
        CHECK(failed && read(path) == original);
    } else if (name == "write_size_limit_small" || name == "write_size_limit_large" || name == "record_write_failure") {
        const SessionStore tabs(profile);
        if (large) for (int index = 0; index < 40; ++index) CHECK(std::holds_alternative<SessionCatalog>(tabs.openTab(id)));
        const auto tabBytes = read(profile + "/tabs.json");
        bool failed = false, tabsFailed = !large;
        {
            FileSizeLimit limit(large ? 4096 : 128); CHECK(limit.active);
            if (name == "record_write_failure") failed = std::holds_alternative<Failure>(recordContextUsage(profile, increment));
            else failed = std::holds_alternative<Failure>(writeContextCatalog(profile, catalog));
            if (large) tabsFailed = std::holds_alternative<Failure>(tabs.openTab(id));
        }
        CHECK(failed && tabsFailed && read(path) == original && read(profile + "/tabs.json") == tabBytes);
#endif
    } else return false;
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_UNIX
    if (argc == 2 && geteuid() == 0 && (QString::fromLocal8Bit(argv[1]).contains("denied") || QString::fromLocal8Bit(argv[1]) == "catalog_unreadable")) {
        std::fputs("Permission-denial checks require the ordinary non-root test identity.\n", stderr);
        return 77;
    }
#endif
    return argc == 2 && scenario(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
