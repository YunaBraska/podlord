#include "session_store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace podlord {
namespace {
Result<SessionCatalog> readCatalog(const QString& profile) {
    if (const auto failure = profileFailure(profile)) return *failure;
    const auto path = QDir(profile).filePath("tabs.json");
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Saved tabs must not be symbolic links."};
    if (!QFileInfo::exists(path)) return SessionCatalog{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Failure{StoreError::ReadFailed, "Cannot read saved tabs."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (file.error() != QFile::NoError) return Failure{StoreError::ReadFailed, "Cannot read saved tabs."};
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || root.size() != 3
        || !root["version"].isDouble() || !root["sessions"].isArray() || !root["activeSessionId"].isString())
        return Failure{StoreError::InvalidData, "Saved tabs could not be read. Existing files were retained."};
    if (root["version"] != 4) return Failure{StoreError::UnsupportedVersion, "Unsupported saved-tab format."};
    static const QRegularExpression contextId("\\Actx:[0-9a-f]{64}\\z");
    SessionCatalog result;
    QSet<QUuid> ids;
    for (const auto value : root["sessions"].toArray()) {
        const auto row = value.toObject();
        const QUuid id(row["id"].toString());
        if (!value.isObject() || row.size() != 2 || !row["id"].isString() || id.isNull()
            || id.toString(QUuid::WithoutBraces) != row["id"].toString() || ids.contains(id)
            || !row["contextId"].isString() || !contextId.match(row["contextId"].toString()).hasMatch())
            return Failure{StoreError::InvalidData, "Invalid saved tab. Existing files were retained."};
        ids.insert(id);
        result.sessions.append(Session{id, {row["contextId"].toString(), {}}, true});
    }
    const auto active = root["activeSessionId"].toString();
    if (!active.isEmpty()) {
        const QUuid id(active);
        if (id.isNull() || id.toString(QUuid::WithoutBraces) != active || !ids.contains(id))
            return Failure{StoreError::InvalidData, "The selected tab no longer exists."};
        result.activeSession = id;
    }
    return result;
}
auto findSession(SessionCatalog& catalog, QUuid id) {
    return std::find_if(catalog.sessions.begin(), catalog.sessions.end(), [id](const auto& tab) { return tab.id == id; });
}
Failure missing() { return {StoreError::NotFound, "This tab no longer exists."}; }
}
std::optional<Failure> profileFailure(const QString& profile) {
    if (profile.isEmpty() || !QDir::isAbsolutePath(profile) || QFileInfo(profile).isSymLink())
        return Failure{StoreError::InvalidInput, "Profile must be an absolute, non-symlink directory."};
    if (QFileInfo(profile).exists() && !QFileInfo(profile).isDir())
        return Failure{StoreError::ReadFailed, "Profile is not a directory."};
    if (QFileInfo(profile).exists() && !readableDirectory(profile))
        return Failure{StoreError::ReadFailed, "Cannot read local profile."};
    return {};
}

bool readableDirectory(const QString& path) {
    if (!QDir(path).isReadable()) return false;
#ifdef Q_OS_UNIX
    return QFileInfo(path).isExecutable();
#else
    return true;
#endif
}


SessionStore::SessionStore(QString profile) : profile_(std::move(profile)) {}
Result<SessionCatalog> SessionStore::list() const { return readCatalog(profile_); }
Result<SessionCatalog> SessionStore::mutate(const std::function<Result<SessionCatalog>(SessionCatalog)>& operation) const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const bool exists = QFileInfo::exists(profile_);
    if (!QDir().mkpath(profile_) || (!exists && !QFile::setPermissions(profile_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)))
        return Failure{StoreError::WriteFailed, "Cannot create private tab storage."};
    const auto directory = QFileInfo(profile_).canonicalFilePath();
    const auto lockPath = QDir(directory).filePath("tabs.lock");
    if (QFileInfo(lockPath).isSymLink() || QFileInfo(lockPath).isDir()) return Failure{StoreError::WriteFailed, "Cannot safely lock saved tabs."};
    QLockFile lock(lockPath); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed, "Saved tabs are in use. Try again when saving finishes."};
    const auto before = readCatalog(directory);
    if (const auto* failure = std::get_if<Failure>(&before)) return *failure;
    auto after = operation(std::get<SessionCatalog>(before));
    if (const auto* failure = std::get_if<Failure>(&after)) return *failure;
    if (std::get<SessionCatalog>(after) == std::get<SessionCatalog>(before)) return after;
    QSaveFile file(QDir(directory).filePath("tabs.json"));
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(catalogJson(std::get<SessionCatalog>(after))).toJson();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot save tabs. Previous data was retained."};
    return after;
}
Result<SessionCatalog> SessionStore::openTab(const QString& context) const {
    static const QRegularExpression identifier("\\Actx:[0-9a-f]{64}\\z");
    if (!identifier.match(context).hasMatch()) return Failure{StoreError::InvalidInput, "Choose a valid imported context."};
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        const auto id = QUuid::createUuid();
        catalog.sessions.append(Session{id, {context, {}}, true});
        catalog.activeSession = id;
        return catalog;
    });
}
Result<SessionCatalog> SessionStore::activate(QUuid id) const {
    if (id.isNull()) return Failure{StoreError::InvalidInput, "Choose a valid tab."};
    return mutate([id](SessionCatalog catalog) -> Result<SessionCatalog> {
        if (findSession(catalog, id) == catalog.sessions.end()) return missing();
        catalog.activeSession = id;
        return catalog;
    });
}
Result<SessionCatalog> SessionStore::close(QUuid id) const { return closeSessions({id}); }
Result<SessionCatalog> SessionStore::closeSessions(const QList<QUuid>& ids) const {
    QSet<QUuid> selected;
    for (const auto& id : ids) {
        if (id.isNull() || selected.contains(id)) return Failure{StoreError::InvalidInput, "Choose distinct valid tabs."};
        selected.insert(id);
    }
    if (selected.isEmpty()) return Failure{StoreError::InvalidInput, "Choose at least one tab."};
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        for (const auto& id : selected) if (findSession(catalog, id) == catalog.sessions.end()) return missing();
        catalog.sessions.erase(std::remove_if(catalog.sessions.begin(), catalog.sessions.end(), [&](const auto& tab) { return selected.contains(tab.id); }), catalog.sessions.end());
        if (catalog.activeSession && selected.contains(*catalog.activeSession)) catalog.activeSession.reset();
        return catalog;
    });
}
Result<SessionCatalog> SessionStore::closeWorkspace() const { return mutate([](SessionCatalog) -> Result<SessionCatalog> { return SessionCatalog{}; }); }
Result<SessionCatalog> SessionStore::removeContext(const QString& context, const QStringList& expected, const std::function<Result<bool>()>& remove) const {
    bool committed = false;
    auto result = mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        QStringList current;
        for (const auto& tab : catalog.sessions) if (tab.config.contextId == context) current.append(tab.id.toString(QUuid::WithoutBraces));
        current.sort();
        if (current != expected) return Failure{StoreError::Conflict, "The affected tabs changed. Reopen the confirmation."};
        const auto removed = remove();
        if (const auto* failure = std::get_if<Failure>(&removed)) return *failure;
        committed = true;
        catalog.sessions.erase(std::remove_if(catalog.sessions.begin(), catalog.sessions.end(), [&](const auto& tab) { return tab.config.contextId == context; }), catalog.sessions.end());
        if (catalog.activeSession && findSession(catalog, *catalog.activeSession) == catalog.sessions.end()) {
            catalog.activeSession.reset();
            if (!catalog.sessions.isEmpty()) catalog.activeSession = catalog.sessions.first().id;
        }
        return catalog;
    });
    if (committed && std::holds_alternative<Failure>(result))
        return Failure{StoreError::CleanupFailed, "The context was removed, but its old tab references could not be deleted."};
    return result;
}
QJsonObject catalogJson(const SessionCatalog& catalog) {
    QJsonArray tabs;
    for (const auto& tab : catalog.sessions) tabs.append(QJsonObject{{"id", tab.id.toString(QUuid::WithoutBraces)}, {"contextId", tab.config.contextId}});
    return {{"version", 4}, {"sessions", tabs}, {"activeSessionId", catalog.activeSession ? catalog.activeSession->toString(QUuid::WithoutBraces) : QString{}}};
}
QString errorName(StoreError error) {
    switch (error) {
    case StoreError::InvalidInput: return "InvalidInput";
    case StoreError::NotFound: return "NotFound";
    case StoreError::Busy: return "Busy";
    case StoreError::ReadFailed: return "ReadFailed";
    case StoreError::InvalidData: return "InvalidData";
    case StoreError::UnsupportedVersion: return "UnsupportedVersion";
    case StoreError::WriteFailed: return "WriteFailed";
    case StoreError::Conflict: return "Conflict";
    case StoreError::CleanupFailed: return "CleanupFailed";
    }
    return "InvalidData";
}
} // namespace podlord
