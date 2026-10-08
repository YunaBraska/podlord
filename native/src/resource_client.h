#pragma once
#include "kubeconfig_store.h"
#include "read_settings.h"
#include "pod_logs.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <functional>
#include <QVariantList>

namespace podlord {
class PortForwardTransport;
class ContainerTerminal;
/** True only for the core v1 Secret/ConfigMap contract, not equal kind names in other APIs. */
bool isCoreValueResource(const QJsonObject& document);
/** One rate-limited queue with four read slots and exclusive writes. Sent work keeps its session. */
class ResourceClient final : public QObject {
    Q_OBJECT
public:
    /** The external clock must return valid UTC times; default is the actual system clock. */
    explicit ResourceClient(QObject* parent = nullptr, std::function<QDateTime()> now = QDateTime::currentDateTimeUtc);
    ~ResourceClient() override;
    bool open(const QString& id, ClusterConnection connection, QStringList namespaces);
    bool close(const QString& id);
    bool refresh(const QString& id, bool confirmedAuthentication = false);
    bool inspect(const QString& id, const QString& path);
    bool dismissInspector(const QString& id);
    QJsonArray rows(const QString& id) const;
    /** Bounded in-memory request metadata. Reading this snapshot never schedules transport. */
    QVariantList requestAudit(const QString& id) const;
    /** Current collection/detail JSON and retained log text across sessions, including closed caches.
     * This byte estimate excludes allocation overhead and shared projections; it never schedules transport. */
    QVariantMap cacheDiagnostics() const;
    QJsonObject detail(const QString& id, const QString& path) const;
    /** Full accepted GET document with the detail lease and current UID; never persisted. */
    QJsonObject document(const QString& id, const QString& path) const;
    QString detailStatus(const QString& id, const QString& path) const;
    QString status(const QString& id) const;
    QString failureStatus(const QString& id) const;
    QString syncSummary(const QString& id) const;
    bool loading(const QString& id) const;
    bool syncLoading(const QString& id) const;
    double loadingProgress(const QString& id) const;
    bool initialSyncComplete(const QString& id) const;
    bool authenticationRequired(const QString& id) const;
    std::optional<ClusterConnection> connection(const QString& id) const;
    bool authenticate(const ClusterConnection& connection, const QString& session);
    bool configure(ReadSettings settings);
    bool enableRequests(bool enabled);
    /** Bind a session to one view. Empty IDs hide it; another view's binding is not stolen. */
    bool showSession(const QString& id, const QString& view = {});
    /** Move an existing visible session without closing its cache, forwards or terminal. */
    bool moveSession(const QString& fromView, const QString& toView);
    bool setFocused(bool focused, const QString& view = {});
    bool userActivity(const QString& view = {});
    bool synchronize();
    QJsonObject resource(const QString& id, const QString& path) const;
    /** Membership in the current session collection snapshot, excluding retained detail-only entries. */
    bool containsResource(const QString& id, const QString& path) const;
    Result<QString> startPortForward(const QString& id, const QString& path, int localPort, int remotePort);
    bool stopPortForward(const QString& id, const QString& token);
    QVariantList portForwards(const QString& id) const;
    QString portForwardError(const QString& id) const;
    Result<QString> startTerminal(const QString& id, const QString& path, const QString& container, const QString& shell);
    ContainerTerminal* terminal(const QString& id) const;
    bool stopTerminal(const QString& id);
    /** Confirmed, target-bound JSON Patch. One send only; no automatic retry. */
    bool applyYaml(const QString& id, const QString& path, const QString& token, const QJsonObject& baseline,
                   const QJsonObject& desired, const QJsonArray& patch);
    /** Cancels only a queued write. Already dispatched writes and their read-back finish. */
    bool cancelYamlApply(const QString& token);
    bool readBackYaml(const QString& id, const QString& path, const QString& token,
                      const QJsonObject& baseline, const QJsonObject& desired, const QString& outcome);
    /** Delete only a listed, discovery-supported UID after explicit target confirmation. Never retries. */
    bool deleteResource(const QString& id, const QString& path, const QString& token, const QJsonObject& baseline);
    /** Cancel an unsent deletion only. A sent deletion and its observational read-back finish. */
    bool cancelDeletion(const QString& token);
    /** Explicit observational GET for an unresolved original target; never sends a deletion. */
    bool readBackDeletion(const QString& id, const QString& path, const QString& token, const QJsonObject& baseline);
    /** Unresolved dispatched deletion: a valid current-target read is required before another write. */
    bool deletionNeedsObservation(const QString& id, const QString& path) const;
    bool showLogs(const QString& id, const QString& path, const QString& view = {});
    bool hideLogs(const QString& view = {});
    bool selectLogContainer(const QString& container, const QString& view = {});
    bool pauseLogs(bool paused, const QString& view = {});
    bool refreshLogs(bool foreground = true, const QString& view = {});
    QStringList logContainers(const QString& id, const QString& path) const;
    QString logSelection(const QString& id, const QString& path) const;
    QString logStatus(const QString& id, const QString& path) const;
    PodLogHistory::Removal logRemoval(const QString& id, const QString& path) const;
    bool logsPaused(const QString& id, const QString& path) const;
    QList<LogEntry> logEntries(const QString& id, const QString& path) const;
signals:
    void portForwardsChanged(const QString& id);
    void terminalChanged(const QString& id);
    /** Local scheduler admission on this client's monotonic clock, not remote arrival.
     * Path excludes query, authorization and payload; no diagnostic persistence is implied. */
    void requestStarted(const QString& id, const QString& path, qint64 monotonicMs);
    void changed(const QString& id);
    void rowsChanged(const QString& id);
    /** A dispatched detail read completed; cached timestamps are not fresh-read authority. */
    void detailFinished(const QString& id, const QString& path, bool accepted);
    void authenticationRejected(const QString& credential);
    void logsChanged(const QString& id, const QString& path);
    void yamlApplyFinished(const QString& id, const QString& path, const QString& token,
                           const QString& outcome, const QString& message, const QJsonObject& current);
    void resourceDeleteFinished(const QString& id, const QString& path, const QString& token,
                               const QString& outcome, const QString& message, bool observed);
private:
    struct Task;
    struct Terminal final {
        QString session, path, uid, container, shell;
        QUrl server;
        ContainerTerminal* screen = nullptr;
    };
    QMap<QString, Terminal> terminals_;
    void consumeTerminal(const Task& task, const QJsonObject& document);
    void terminalHandshakeFinished(const QString& token, bool accepted, bool authenticationRequired);
    struct Forward final {
        QString session, path, uid, kind, name, nameSpace, podPath;
        QUrl server;
        int localPort = 0, requestedPort = 0, remotePort = 0;
        QJsonObject service;
        QString phase = "Resolving target", selector;
        PortForwardTransport* transport = nullptr;
    };
    QMap<QString, Forward> forwards_;
    bool stopSessionForwards(const QString& id);
    bool failForward(const QString& token, const QString& message);
    void consumeForward(const Task& task, const QJsonObject& document);
    void forwardHandshakeFinished(const QString& token, const QString& stream, bool accepted, bool authenticationRequired);
    struct Collection final { QJsonArray rows; QDateTime at; QString kind; bool namespaced = false; qint64 received = 0; bool deletable = false; };
    struct Detail final { QJsonObject value; QDateTime at; QJsonObject document; };
    struct CachedResource final { QJsonObject value; qint64 received = 0; };
    struct State final {
        ClusterConnection connection;
        QStringList namespaces;
        QMap<QString, Collection> collections;
        QMap<QString, Detail> details;
        QMap<QString, PodLogHistory> logs;
        QMap<QString, CachedResource> resources;
        QJsonArray snapshot;
        QMap<QString, QString> issueByPath;
        QSet<QString> blocked;
        QSet<QString> unresolvedDeletes;
        QMap<QString, QString> failures;
        QString forwardError;
        bool open = true;
        bool suspended = false;
        int pending = 0;
        int resourcePending = 0;
        int syncPending = 0, syncTotal = 0, syncCompleted = 0;
        double progress = 0;
        bool initialized = false;
        quint64 revision = 0;
        QDateTime lastSync;
        QList<QDateTime> requestStarts;
        QDateTime healthExpiry;
    };
    enum class Read { Core, Groups, Discovery, List, Detail, Log, Apply, Verify, Delete, VerifyDelete, ForwardTarget, ForwardPods, ForwardSocket, TerminalTarget, TerminalSocket };
    struct Change final { QString token; QJsonObject baseline, desired; ClusterConnection connection; QString outcome; };
    struct Task final {
        QString id, path, collection, kind, continuation;
        Read read = Read::Core;
        bool namespaced = false;
        bool deletable = false;
        bool foreground = false;
        QJsonArray page;
        QSet<QString> tokens;
        quint64 revision = 0;
        QString logKey, container;
        QString forwardToken, streamToken, selector;
        QString terminalToken;
        qint64 received = 0;
        qint64 started = -1;
        std::optional<Change> change;
        QJsonArray patch;
    };
    QNetworkAccessManager network_;
    QMap<QString, State> states_;
    QList<Task> queue_;
    QVariantList completedRequests_;
    std::optional<Task> running_;
    QMap<QNetworkReply*, Task> reads_;
    QTimer gate_;
    QTimer expiry_;
    QTimer sync_;
    QTimer logSync_;
    struct View final {
        QString session, logKey;
        bool focused = true;
        qint64 activityAt = 0;
    };
    QMap<QString, View> views_{{QString{}, View{}}};
    const std::function<QDateTime()> now_;
    ReadSettings settings_;
    bool enabled_ = true;
    qint64 lastStart_ = -1;
    bool resetConnections_ = false;
    QElapsedTimer clock_;
    qint64 backoffUntil_ = 0;
    bool enqueue(Task task);
    static bool collectionRead(Read read);
    static bool parallelRead(const Task& task);
    void updateProgress(const QString& id);
    void suspend(const QString& credential, const QString& message);
    void expire();
    void scheduleSync();
    qint64 syncInterval(const QString& id, const State& state) const;
    void publishRows(const QString& id);
    void reconcileLogs(const QString& id);
    bool refreshSession(const QString& id, bool foreground);
    bool wanted(const QString& id) const;
    bool isVisible(const QString& id) const;
    bool logsShown(const QString& id, const QString& key) const;
    void discardHiddenReads();
    void dispatch();
    void recordRequestStarted(const Task& task);
    void finish(const Task& task, int status, const QByteArray& bytes, bool transportError, const QByteArray& retryAfter);
    void consume(const Task& task, const QJsonObject& document);
    void fail(const Task& task, QString message);
    QString logKey(const QString& id, const QString& path) const;
    const PodLogHistory* logHistory(const QString& id, const QString& path) const;
    void scheduleLogs();
    void dropQueued(const std::function<bool(const Task&)>& obsolete);
    void finishApply(const Task& task, int http, const QByteArray& bytes, bool transportError);
    void finishVerify(const Task& task);
    void finishDeletion(const Task& task, int http, const QByteArray& bytes, bool transportError);
    void finishDeletionRead(const Task& task, int http, const QByteArray& bytes, bool transportError);
    void forgetDeletedResource(const Task& task);
    void collections(const QString& id, const QString& path, const QJsonObject& document, bool foreground);
};
} // namespace podlord
