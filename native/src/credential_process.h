#pragma once
#include "kubeconfig_store.h"
#include "native_oidc.h"
#include "native_eks.h"
#include "native_gke.h"
#include <QProcess>

namespace podlord {
/** Owns confirmed credential processes and process-lifetime credentials, never disk secrets. */
class CredentialProcess final : public QObject {
    Q_OBJECT
public:
    explicit CredentialProcess(QObject* parent = nullptr);
    ~CredentialProcess() override;
    /** Starts one explicitly confirmed command per credential context, without a shell or stdin. */
    bool start(ClusterConnection connection, const QString& session);
    bool cancel(const QString& credential);
    bool invalidate(const QString& credential);
    /** Cancels owned authentication and forgets its result, including late completions. */
    bool revoke(const QString& credential);
    bool running(const QString& credential) const;
    QString instruction(const QString& credential) const;
    std::optional<ClusterConnection> cached(const QString& credential) const;
    Result<ClusterConnection> result(const QString& credential) const;
signals:
    void changed();
    void completed(const QString& credential, const QString& session);
    void revoked(const QString& credential);
private:
    struct Job final { QProcess* process; QByteArray output; QString failure; };
    QMap<QString, Job> jobs_;
    QMap<QString, NativeOidcLogin*> nativeJobs_;
    QMap<QString, NativeGkeLogin*> gkeJobs_;
    QMap<QString, Result<ClusterConnection>> results_;
    void finish(const ClusterConnection& connection, const QString& session, QProcess* process);
};
} // namespace podlord
