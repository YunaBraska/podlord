#include "session_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <limits>

namespace podlord {
namespace {
Failure invalidData() { return {StoreError::InvalidData, "Invalid session store; original data retained."}; }

bool exactKeys(const QJsonObject& object, const QStringList& keys) {
    const QStringList actual = object.keys();
    return QSet<QString>(actual.cbegin(), actual.cend())
        == QSet<QString>(keys.cbegin(), keys.cend());
}

bool validName(const QString& name) {
    return std::none_of(name.cbegin(), name.cend(), [](QChar c) {
        return c.category() == QChar::Other_Control || c.category() == QChar::Separator_Line
            || c.category() == QChar::Separator_Paragraph;
    });
}

Result<SessionConfig> canonicalConfig(SessionConfig config) {
    config.contextId = config.contextId.trimmed();
    if (config.contextId.isEmpty() || !validName(config.contextId))
        return Failure{StoreError::InvalidInput, "A non-empty context identifier is required."};
    const QRegularExpression namespacePattern("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
    for (auto& name : config.namespaces) {
        name = name.trimmed();
        if (!namespacePattern.match(name).hasMatch())
            return Failure{StoreError::InvalidInput, "Namespace must be a Kubernetes DNS label."};
    }
    std::sort(config.namespaces.begin(), config.namespaces.end());
    config.namespaces.removeDuplicates();
    return config;
}

bool unsignedValue(const QJsonValue& value, quint64& number) {
    if (!value.isString()) return false;
    const QString text = value.toString();
    if (text.isEmpty() || std::any_of(text.cbegin(), text.cend(), [](QChar c) {
        return c < QChar('0') || c > QChar('9');
    })) return false;
    bool ok = false;
    number = text.toULongLong(&ok);
    return ok && QString::number(number) == text;
}

Result<SessionCatalog> readCatalog(const QString& profile) {
    if (const auto failure = profileFailure(profile)) return *failure;
    const QString path = QDir(profile).filePath("sessions.json");
    if (QFileInfo(path).isSymLink()) return invalidData();
    if (!QFileInfo(path).exists()) return SessionCatalog{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Failure{StoreError::ReadFailed, "Cannot read session store."};
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return Failure{StoreError::ReadFailed, "Cannot read session store."};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return invalidData();
    const QJsonObject root = document.object();
    const bool exclusions = root["version"].toDouble() == 3;
    auto keys = QStringList{"version", "sessions", "activeSessionId"};
    if (exclusions) keys.append("removedContexts");
    if (!exactKeys(root, keys)
        || !root["version"].isDouble() || !root["sessions"].isArray()
        || !root["activeSessionId"].isString()) return invalidData();
    if (root["version"].toDouble() != 2 && !exclusions)
        return Failure{StoreError::UnsupportedVersion, "Unsupported session store version; original data retained."};
    SessionCatalog catalog;
    if (exclusions) {
        if (!root["removedContexts"].isArray()) return invalidData();
        static const QRegularExpression contextId("\\Actx:[0-9a-f]{64}\\z");
        for (const auto value : root["removedContexts"].toArray()) {
            if (!value.isString() || !contextId.match(value.toString()).hasMatch()) return invalidData();
            const auto id = value.toString();
            if (!catalog.removedContexts.isEmpty() && id <= catalog.removedContexts.last()) return invalidData();
            catalog.removedContexts.append(id);
        }
    }
    QSet<QUuid> ids;
    QSet<QString> ordinals;
    QSet<QString> titles;
    QMap<QUuid, QString> bases;
    for (const auto value : root["sessions"].toArray()) {
        if (!value.isObject()) return invalidData();
        const QJsonObject row = value.toObject();
        if (!exactKeys(row, {"id", "sequenceId", "name", "ordinal", "contextId", "namespaces", "open",
                             "createdAt", "usageAt"})) return invalidData();
        for (const QString& key : QStringList{"id", "sequenceId", "name", "contextId", "createdAt"})
            if (!row[key].isString()) return invalidData();
        if (!row["namespaces"].isArray() || !row["open"].isBool() || !row["usageAt"].isArray()) return invalidData();
        Session session;
        session.id = QUuid(row["id"].toString());
        session.sequenceId = QUuid(row["sequenceId"].toString());
        session.name = row["name"].toString();
        session.config.contextId = row["contextId"].toString();
        session.open = row["open"].toBool();
        session.createdAt = QDateTime::fromString(row["createdAt"].toString(), Qt::ISODateWithMs);
        if (session.id.isNull() || session.sequenceId.isNull() || ids.contains(session.id) || session.name != session.name.trimmed()
            || !validName(session.name) || !unsignedValue(row["ordinal"], session.ordinal)
            || session.ordinal == 0) return invalidData();
        const QString position = session.sequenceId.toString() + ":" + QString::number(session.ordinal);
        if (ordinals.contains(position) || titles.contains(session.displayName())
            || (bases.contains(session.sequenceId) && bases[session.sequenceId] != session.name)) return invalidData();
        for (const auto value : row["usageAt"].toArray()) {
            if (!value.isString()) return invalidData();
            const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
            if (!time.isValid()) return invalidData();
            session.usageAt.append(time.toUTC());
        }
        for (const auto ns : row["namespaces"].toArray()) {
            if (!ns.isString()) return invalidData();
            session.config.namespaces.append(ns.toString());
        }
        const auto config = canonicalConfig(session.config);
        if (!std::holds_alternative<SessionConfig>(config)
            || std::get<SessionConfig>(config) != session.config
            || !session.createdAt.isValid() || (session.usageAt.isEmpty() && session.open)
            || catalog.removedContexts.contains(session.config.contextId))
            return invalidData();
        ids.insert(session.id);
        ordinals.insert(position);
        titles.insert(session.displayName());
        bases.insert(session.sequenceId, session.name);
        catalog.sessions.append(std::move(session));
    }
    const QString active = root["activeSessionId"].toString();
    if (!active.isEmpty()) {
        const QUuid id(active);
        const auto selected = std::find_if(catalog.sessions.cbegin(), catalog.sessions.cend(),
            [id](const Session& session) { return session.id == id && session.open; });
        if (id.isNull() || selected == catalog.sessions.cend()) return invalidData();
        catalog.activeSession = id;
    }
    return catalog;
}

Result<quint64> nextOrdinal(const SessionCatalog& catalog, QUuid sequence) {
    quint64 highest = 1;
    for (const auto& session : catalog.sessions)
        if (session.sequenceId == sequence) highest = std::max(highest, session.ordinal);
    if (highest == std::numeric_limits<quint64>::max())
        return Failure{StoreError::Conflict, "Session sequence numbers are exhausted."};
    return highest + 1;
}

auto findSession(SessionCatalog& catalog, QUuid id) {
    return std::find_if(catalog.sessions.begin(), catalog.sessions.end(),
        [id](const Session& session) { return session.id == id; });
}

Failure missing() { return {StoreError::NotFound, "Session does not exist."}; }
Result<Session> availableTitle(const SessionCatalog& catalog, Session session) {
    QSet<QString> occupied;
    for (const auto& existing : catalog.sessions) occupied.insert(existing.displayName());
    while (occupied.contains(session.displayName())) {
        if (session.ordinal == std::numeric_limits<quint64>::max())
            return Failure{StoreError::Conflict, "Session sequence numbers are exhausted."};
        ++session.ordinal;
    }
    return session;
}
struct Usage final { qsizetype count; QDateTime last; };
Usage usage(const Session& session, const QDateTime& asOf) {
    Usage result{0, {}};
    const auto cutoff = asOf.addDays(-30);
    for (const auto& time : session.usageAt) {
        if (time >= cutoff && time <= asOf) ++result.count;
        if (!result.last.isValid() || time > result.last) result.last = time;
    }
    return result;
}
} // namespace

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

QString Session::displayName() const {
    const QString base = name.isEmpty() ? QString("Unnamed") : name;
    return ordinal == 1 ? base : QString("%1 %2").arg(base).arg(ordinal);
}

SessionStore::SessionStore(QString profile) : profile_(std::move(profile)) {}
Result<SessionCatalog> SessionStore::list() const { return readCatalog(profile_); }
Result<SessionCatalog> SessionStore::selection(QDateTime asOf) const {
    if (!asOf.isValid()) return Failure{StoreError::InvalidInput, "A valid selection reference time is required."};
    asOf = asOf.toUTC();
    auto result = list();
    if (std::holds_alternative<Failure>(result)) return result;
    auto& catalog = std::get<SessionCatalog>(result);
    QMap<QUuid, Usage> summaries;
    for (const auto& session : catalog.sessions) summaries.insert(session.id, usage(session, asOf));
    std::sort(catalog.sessions.begin(), catalog.sessions.end(), [&](const Session& a, const Session& b) {
        const auto& left = summaries[a.id];
        const auto& right = summaries[b.id];
        if (left.last.isValid() != right.last.isValid()) return left.last.isValid();
        if (left.count != right.count) return left.count > right.count;
        if (left.last != right.last) return left.last > right.last;
        if (!left.last.isValid() && a.createdAt != b.createdAt) return a.createdAt > b.createdAt;
        return a.id.toString(QUuid::WithoutBraces) < b.id.toString(QUuid::WithoutBraces);
    });
    return result;
}

Result<SessionCatalog> SessionStore::mutate(
    const std::function<Result<SessionCatalog>(SessionCatalog)>& operation) const {
    if (const auto failure = profileFailure(profile_)) return *failure;
    const bool exists = QDir(profile_).exists();
    if (!QDir().mkpath(profile_))
        return Failure{StoreError::WriteFailed, "Cannot create session profile."};
    if (!exists && !QFile::setPermissions(profile_, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return Failure{StoreError::WriteFailed, "Cannot protect session profile."};
    const QString directory = QFileInfo(profile_).canonicalFilePath();
    QLockFile lock(QDir(directory).filePath("sessions.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        if (lock.error() == QLockFile::LockFailedError)
            return Failure{StoreError::Busy, "Session profile is in use; operation was not applied."};
        return Failure{StoreError::WriteFailed, "Cannot lock session profile."};
    }
    const auto before = readCatalog(directory);
    if (std::holds_alternative<Failure>(before)) return std::get<Failure>(before);
    auto after = operation(std::get<SessionCatalog>(before));
    if (std::holds_alternative<Failure>(after)) return std::get<Failure>(after);
    if (std::get<SessionCatalog>(before) == std::get<SessionCatalog>(after)) return after;
    QSaveFile file(QDir(directory).filePath("sessions.json"));
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return Failure{StoreError::WriteFailed, "Cannot create protected session store."};
    const QByteArray bytes = QJsonDocument(catalogJson(std::get<SessionCatalog>(after))).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot publish session store; previous data retained."};
    return after;
}

Result<SessionCatalog> SessionStore::create(SessionConfig config, QString name) const {
    const auto canonical = canonicalConfig(std::move(config));
    name = name.trimmed();
    if (std::holds_alternative<Failure>(canonical)) return std::get<Failure>(canonical);
    if (!validName(name)) return Failure{StoreError::InvalidInput, "Session name cannot contain control characters."};
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        if (catalog.removedContexts.contains(std::get<SessionConfig>(canonical).contextId))
            return Failure{StoreError::NotFound, "The imported context was removed. Explicitly import it again before creating sessions."};
        const auto session = availableTitle(catalog, Session{QUuid::createUuid(), QUuid::createUuid(), name, 1,
            std::get<SessionConfig>(canonical), false, QDateTime::currentDateTimeUtc(), {}});
        if (std::holds_alternative<Failure>(session)) return std::get<Failure>(session);
        catalog.sessions.append(std::get<Session>(session));
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::snapshot(QUuid id, SessionConfig config) const {
    if (id.isNull()) return Failure{StoreError::InvalidInput, "A valid session identifier is required."};
    const auto canonical = canonicalConfig(std::move(config));
    if (std::holds_alternative<Failure>(canonical)) return std::get<Failure>(canonical);
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        const auto original = findSession(catalog, id);
        if (original == catalog.sessions.end()) return missing();
        if (catalog.removedContexts.contains(std::get<SessionConfig>(canonical).contextId))
            return Failure{StoreError::NotFound, "The target context was removed. Explicitly import it again before saving a session."};
        if (std::any_of(catalog.sessions.cbegin(), catalog.sessions.cend(), [&](const Session& session) {
            return session.sequenceId == original->sequenceId && session.config == std::get<SessionConfig>(canonical);
        })) return catalog;
        const auto number = nextOrdinal(catalog, original->sequenceId);
        if (std::holds_alternative<Failure>(number)) return std::get<Failure>(number);
        const auto copy = availableTitle(catalog, Session{QUuid::createUuid(), original->sequenceId,
            original->name, std::get<quint64>(number), std::get<SessionConfig>(canonical), false,
            QDateTime::currentDateTimeUtc(), {}});
        if (std::holds_alternative<Failure>(copy)) return std::get<Failure>(copy);
        catalog.sessions.append(std::get<Session>(copy));
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::activate(QUuid id) const {
    if (id.isNull()) return Failure{StoreError::InvalidInput, "A valid session identifier is required."};
    return mutate([id](SessionCatalog catalog) -> Result<SessionCatalog> {
        const auto session = findSession(catalog, id);
        if (session == catalog.sessions.end()) return missing();
        if (catalog.activeSession == id && session->open) return catalog;
        session->open = true;
        session->usageAt.append(QDateTime::currentDateTimeUtc());
        catalog.activeSession = id;
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::close(QUuid id) const {
    if (id.isNull()) return Failure{StoreError::InvalidInput, "A valid session identifier is required."};
    return mutate([id](SessionCatalog catalog) -> Result<SessionCatalog> {
        const auto session = findSession(catalog, id);
        if (session == catalog.sessions.end()) return missing();
        session->open = false;
        if (catalog.activeSession == id) catalog.activeSession.reset();
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::closeWorkspace() const {
    return mutate([](SessionCatalog catalog) -> Result<SessionCatalog> {
        for (auto& session : catalog.sessions) session.open = false;
        catalog.activeSession.reset();
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::remove(QUuid id) const {
    if (id.isNull()) return Failure{StoreError::InvalidInput, "A valid session identifier is required."};
    return mutate([id](SessionCatalog catalog) -> Result<SessionCatalog> {
        const auto session = findSession(catalog, id);
        if (session == catalog.sessions.end()) return missing();
        catalog.sessions.erase(session);
        if (catalog.activeSession == id) catalog.activeSession.reset();
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::removeContext(const QString& contextId, const QStringList& expectedSessions) const {
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        QStringList current;
        for (const auto& session : catalog.sessions)
            if (session.config.contextId == contextId) current.append(session.id.toString(QUuid::WithoutBraces));
        std::sort(current.begin(), current.end());
        if (current != expectedSessions)
            return Failure{StoreError::Conflict, "The affected sessions changed. Reopen the confirmation before removing this context."};
        catalog.sessions.erase(std::remove_if(catalog.sessions.begin(), catalog.sessions.end(),
            [&](const auto& session) { return session.config.contextId == contextId; }), catalog.sessions.end());
        if (catalog.activeSession && findSession(catalog, *catalog.activeSession) == catalog.sessions.end()) {
            catalog.activeSession.reset();
            const auto next = std::find_if(catalog.sessions.cbegin(), catalog.sessions.cend(), [](const auto& session) { return session.open; });
            if (next != catalog.sessions.cend()) catalog.activeSession = next->id;
        }
        catalog.removedContexts.append(contextId);
        std::sort(catalog.removedContexts.begin(), catalog.removedContexts.end());
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::restoreContexts(const QStringList& contextIds) const {
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        catalog.removedContexts.erase(std::remove_if(catalog.removedContexts.begin(), catalog.removedContexts.end(),
            [&](const auto& id) { return contextIds.contains(id); }), catalog.removedContexts.end());
        return catalog;
    });
}

Result<SessionCatalog> SessionStore::rename(QUuid id, QString name) const {
    if (id.isNull()) return Failure{StoreError::InvalidInput, "A valid session identifier is required."};
    name = name.trimmed();
    if (!validName(name)) return Failure{StoreError::InvalidInput, "Session name cannot contain control characters."};
    return mutate([&](SessionCatalog catalog) -> Result<SessionCatalog> {
        const auto session = findSession(catalog, id);
        if (session == catalog.sessions.end()) return missing();
        if (session->displayName() == name || (session->name == name && session->ordinal == 1)) return catalog;
        Session renamed = *session;
        renamed.name = name;
        renamed.ordinal = 1;
        renamed.sequenceId = QUuid::createUuid();
        SessionCatalog others = catalog;
        others.sessions.erase(findSession(others, id));
        const auto available = availableTitle(others, renamed);
        if (std::holds_alternative<Failure>(available)) return std::get<Failure>(available);
        if (!name.isEmpty() && std::get<Session>(available).ordinal != 1)
            return Failure{StoreError::Conflict, "Session title is already occupied."};
        *session = std::get<Session>(available);
        return catalog;
    });
}

QJsonObject catalogJson(const SessionCatalog& catalog, bool presentation) {
    QJsonArray sessions;
    const auto asOf = QDateTime::currentDateTimeUtc();
    for (const auto& session : catalog.sessions) {
        QJsonArray namespaces;
        for (const auto& name : session.config.namespaces) namespaces.append(name);
        QJsonArray history;
        for (const auto& time : session.usageAt) history.append(time.toUTC().toString(Qt::ISODateWithMs));
        QJsonObject row{{"id", session.id.toString(QUuid::WithoutBraces)},
            {"sequenceId", session.sequenceId.toString(QUuid::WithoutBraces)}, {"name", session.name},
            {"ordinal", QString::number(session.ordinal)}, {"contextId", session.config.contextId},
            {"namespaces", namespaces}, {"open", session.open}, {"usageAt", history},
            {"createdAt", session.createdAt.toUTC().toString(Qt::ISODateWithMs)}};
        if (presentation) {
            const auto summary = usage(session, asOf);
            row.insert("displayName", session.displayName());
            row.insert("useCount", QString::number(summary.count));
            row.insert("lastOpenedAt", summary.last.isValid() ? summary.last.toUTC().toString(Qt::ISODateWithMs) : QString{});
        }
        sessions.append(row);
    }
    QJsonObject result{{"version", catalog.removedContexts.isEmpty() ? 2 : 3}, {"sessions", sessions}, {"activeSessionId",
        catalog.activeSession ? catalog.activeSession->toString(QUuid::WithoutBraces) : QString{}}};
    if (!catalog.removedContexts.isEmpty()) result.insert("removedContexts", QJsonArray::fromStringList(catalog.removedContexts));
    return result;
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
    }
    return "InvalidData";
}
} // namespace podlord
