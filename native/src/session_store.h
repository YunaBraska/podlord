#pragma once

#include <QJsonObject>
#include <QDateTime>
#include <QList>
#include <QStringList>
#include <QUuid>
#include <functional>
#include <optional>
#include <variant>

namespace podlord {

enum class StoreError { InvalidInput, NotFound, Busy, ReadFailed, InvalidData,
                        UnsupportedVersion, WriteFailed, Conflict, CleanupFailed };
struct Failure final {
    StoreError code;
    QString message;
    /** Invalid external source input, not an owned-profile/read/write failure. */
    bool sourceInput = false;
};
template<class T> using Result = std::variant<T, Failure>;

struct SessionConfig final {
    QString contextId;
    QStringList namespaces;
    bool operator==(const SessionConfig&) const = default;
};
struct Session final {
    QUuid id;
    SessionConfig config;
    bool open = false;
    bool operator==(const Session&) const = default;
};
struct SessionCatalog final {
    QList<Session> sessions;
    std::optional<QUuid> activeSession;
    bool operator==(const SessionCatalog&) const = default;
};

/** Local tab persistence. No network, authentication, or forwarding is performed.
 * Each mutation acquires the local profile lock and atomically publishes one catalog.
 * Unsupported or malformed persisted data is rejected, never reset or overwritten.
 */
class SessionStore final {
public:
    /** Selects an absolute, non-symlink profile; validation occurs at each entrypoint. */
    explicit SessionStore(QString profile);
    /** Reads the catalog without creating a missing profile or store. */
    Result<SessionCatalog> list() const;
    /** Opens a new tab for a context, without creating a saved-session preset. */
    Result<SessionCatalog> openTab(const QString& context) const;
    /** Selects an existing tab. Repeated active selection is a no-op. */
    Result<SessionCatalog> activate(QUuid id) const;
    /** Removes the tab from the current workspace. */
    Result<SessionCatalog> close(QUuid id) const;
    /** Closes only the specified sessions in one commit. Missing, null or repeated IDs
     * fail without changing tab placement. */
    Result<SessionCatalog> closeSessions(const QList<QUuid>& ids) const;
    /** Atomically clears all tab placement and activation.
     * Used at startup when workspace restoration is disabled; never stops live transports.
     */
    Result<SessionCatalog> closeWorkspace() const;
private:
    friend class KubeconfigStore;
    Result<SessionCatalog> removeContext(const QString& contextId, const QStringList& expectedSessions,
        const std::function<Result<bool>()>& remove) const;
    const QString profile_;
    Result<SessionCatalog> mutate(
        const std::function<Result<SessionCatalog>(SessionCatalog)>& operation) const;
};

/** Portable filename for a validated, canonical imported-context identifier. */
inline QString contextViewName(const QString& context) { return "context-" + context.mid(4) + ".json"; }

/** Serializes only actual tab identities and context references. */
QJsonObject catalogJson(const SessionCatalog& catalog);
/** Returns a stable machine-readable error code, without payload contents. */
QString errorName(StoreError error);
/** Checks the shared local-profile boundary without creating or changing it. */
std::optional<Failure> profileFailure(const QString& profile);
/** Reports directory read/traverse access using the current platform's filesystem checks. */
bool readableDirectory(const QString& path);

} // namespace podlord
