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
                        UnsupportedVersion, WriteFailed, Conflict };
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
    QUuid sequenceId;
    QString name;
    quint64 ordinal = 1;
    SessionConfig config;
    bool open = false;
    QDateTime createdAt;
    QList<QDateTime> usageAt;
    /** Returns the base name with its replacement suffix, using the automatic fallback if unnamed. */
    QString displayName() const;
    bool operator==(const Session&) const = default;
};
struct SessionCatalog final {
    QList<Session> sessions;
    std::optional<QUuid> activeSession;
    /** Context exclusions share the session commit, so a cascade has no second write. */
    QStringList removedContexts;
    bool operator==(const SessionCatalog&) const = default;
};

/** Local session persistence. No network, authentication, or forwarding is performed.
 * Each mutation acquires the local profile lock and atomically publishes one catalog.
 * Unsupported or malformed persisted data is rejected, never reset or overwritten.
 */
class SessionStore final {
public:
    /** Selects an absolute, non-symlink profile; validation occurs at each entrypoint. */
    explicit SessionStore(QString profile);
    /** Reads the catalog without creating a missing profile or store. */
    Result<SessionCatalog> list() const;
    /** Ranks the selection list using the preceding 30 days, without changing tab order.
     * Invalid reference times return InvalidInput. Recorded history is retained.
     */
    Result<SessionCatalog> selection(QDateTime asOf = QDateTime::currentDateTimeUtc()) const;
    /** Creates a closed session after normalizing its external configuration and name. */
    Result<SessionCatalog> create(SessionConfig config, QString name = {}) const;
    /** Copies changed configuration into a new session, preserving the old session.
     * Equivalent canonical configuration in the same sequence is a no-op. Both explicit and automatic
     * base names receive the next remaining sequence suffix, skipping occupied titles.
     */
    Result<SessionCatalog> snapshot(QUuid id, SessionConfig config) const;
    /** Opens/selects a session and records one activation; repeated active selection is a no-op. */
    Result<SessionCatalog> activate(QUuid id) const;
    /** Closes the session without deleting it or its usage history. */
    Result<SessionCatalog> close(QUuid id) const;
    /** Closes only the specified sessions in one commit. Missing, null or repeated IDs
     * fail without changing any session, its configuration or usage history. */
    Result<SessionCatalog> closeSessions(const QList<QUuid>& ids) const;
    /** Atomically clears open placement and activation, retaining sessions, order and usage.
     * Used at startup when workspace restoration is disabled; never stops live transports.
     */
    Result<SessionCatalog> closeWorkspace() const;
    /** Removes the session and clears its active selection, if any. */
    Result<SessionCatalog> remove(QUuid id) const;
    /** Renames a session and starts an independent replacement sequence.
     * An empty name selects an available unnamed label; occupied explicit titles conflict.
     */
    Result<SessionCatalog> rename(QUuid id, QString name) const;
private:
    friend class KubeconfigStore;
    Result<SessionCatalog> removeContext(const QString& contextId, const QStringList& expectedSessions) const;
    Result<SessionCatalog> restoreContexts(const QStringList& contextIds) const;
    const QString profile_;
    Result<SessionCatalog> mutate(
        const std::function<Result<SessionCatalog>(SessionCatalog)>& operation) const;
};

/** Serializes the catalog; presentation labels are included only when requested. */
QJsonObject catalogJson(const SessionCatalog& catalog, bool presentation = false);
/** Returns a stable machine-readable error code, without payload contents. */
QString errorName(StoreError error);
/** Checks the shared local-profile boundary without creating or changing it. */
std::optional<Failure> profileFailure(const QString& profile);
/** Reports directory read/traverse access using the current platform's filesystem checks. */
bool readableDirectory(const QString& path);

} // namespace podlord
