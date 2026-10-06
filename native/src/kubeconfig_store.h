#pragma once
#include "session_store.h"
#include <QSslConfiguration>
#include <QUrl>

namespace podlord {
struct SourceContext final {
    QString id;
    QString name;
    QString displayName;
    QString cluster;
    QString user;
    QString nameSpace;
    QString server;
    QString authType;
    QStringList brokenReferences;
};
struct SourceSnapshot final {
    QString id;
    QString sourcePath;
    QString contentHash;
    QString ownedPath;
    QDateTime importedAt;
    QList<SourceContext> contexts;
    QStringList warnings;
};
struct SourceIssue final { QString ownedPath; Failure failure; };
struct SourceCatalog final {
    QList<SourceSnapshot> sources;
    QList<SourceIssue> errors;
};
struct SourceImportIssue final { QString sourcePath; Failure failure; };
struct SourceImportReport final {
    QList<SourceSnapshot> sources;
    QList<SourceImportIssue> errors;
};
struct ExecCommand final {
    QString program, workingDirectory, apiVersion;
    QStringList arguments;
    QMap<QString, QString> environment;
    QJsonObject info;
};
/** Effective connection. Resolving an exec command does not authorize its execution. */
struct ClusterConnection final {
    QString contextId;
    QString credentialId;
    QUrl server;
    QSslConfiguration tls;
    QByteArray authorization;
    QString peerName;
    bool insecure = false;
    std::optional<ExecCommand> exec;
    bool credentialReady = true;
    QDateTime expiresAt;
};
/** Validates and installs a complete PEM client certificate/key pair. */
Result<QSslConfiguration> clientCertificate(QSslConfiguration tls, const QByteArray& certificate, const QByteArray& key);
/** Owns immutable kubeconfig content keyed by absolute source path and content hash.
 * Imports and listing never execute credential plugins or contact clusters.
 * Public metadata omits credential contents and parser exception payloads.
 */
class KubeconfigStore final {
public:
    explicit KubeconfigStore(QString profile);
    /** Imports a real UTF-8 YAML file, retaining prior snapshots and session bindings.
     * Identical path/content refreshes import recency without adding another snapshot.
     */
    Result<SourceSnapshot> importFile(QString sourcePath) const;
    Result<SourceSnapshot> importText(QString originPath, const QString& yaml) const;
    /** Renames only the display label of an existing immutable context snapshot.
     * Empty input restores the canonical name. Identity, YAML, credentials and sessions
     * are unchanged; lock contention and invalid input fail without a partial write.
     */
    Result<SourceSnapshot> renameContext(const QString& contextId, QString displayName) const;
    /** Removes the context from the available catalog and deletes its saved sessions
     * in one atomic session-catalog commit. The caller must explicitly confirm the
     * exact affected session identifiers; stale confirmation returns Conflict.
     * Original files and immutable content used by other contexts are unchanged.
     * Does not stop live transports; the workspace closes successful removals.
     */
    Result<SessionCatalog> removeContext(const QString& contextId, QStringList expectedSessions) const;
    /** Explicitly reads generated configs from a selected absolute k3d executable.
     * No shell, kubeconfig merge, cluster authentication or session activation runs.
     * Retains healthy snapshots and reports individual cluster export failures.
     */
    Result<SourceImportReport> importK3d(const QString& executable) const;
    /** Imports a file or recursively scans a directory, expanding ~ and ~/ paths.
     * Reports every failed regular file without discarding successful imports.
     * Directory symlinks and the private profile subtree are not traversed.
     * Empty directories return an explicit empty report. No plugin or network runs.
     */
    Result<SourceImportReport> importPath(QString sourcePath) const;
    /** Lists owned snapshots newest first, reporting individual file errors separately.
     * Directory failures reject the scan; malformed records never suppress healthy sources.
     * Listing never changes persisted data.
     */
    Result<SourceCatalog> list() const;
    /** Resolves one owned context, including paths relative to the original source.
     * Referenced credentials/certificates must be regular files of at most 16 MiB;
     * symlinks to regular files remain supported. Failures never include their contents.
     * Cached auth-provider access/id tokens use ordinary bearer authentication, without refresh.
     * Unsupported terminal-only/provider-refresh/proxy/impersonation configurations fail explicitly;
     * this operation never runs a plugin, opens a browser, or contacts a cluster.
     */
    Result<ClusterConnection> connection(const QString& contextId) const;
private:
    Result<SourceSnapshot> importContent(QString canonicalPath, const QByteArray& bytes) const;
    const QString profile_;
};
/** Serializes only display metadata; no tokens, private keys, or original YAML. */
QJsonObject sourceJson(const SourceSnapshot& source);
/** Serializes import outcomes without credentials or original YAML. */
QJsonObject sourceImportJson(const SourceImportReport& report);
} // namespace podlord
