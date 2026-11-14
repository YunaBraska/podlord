#pragma once
#include "kubeconfig_store.h"

namespace podlord {
/** Parses one EKS exec configuration without invoking the AWS CLI. */
Result<std::optional<EksLogin>> nativeEksConfig(const ExecCommand& command,
    const std::optional<AwsProfile>& importedProfile = {});
/** Resolves --profile, then exec AWS_DEFAULT_PROFILE/AWS_PROFILE, otherwise default. Never scans the home directory. */
Result<QString> awsProfileName(const ExecCommand& command);
/** Reads selected shared-config/credentials sections. Returns only fields needed for native sign-in. */
Result<AwsProfile> readAwsProfile(const QByteArray& config, const QByteArray& credentials, const QString& name);
struct AwsRoleRequest final {
    QUrl url;
    QByteArray body;
    QMap<QByteArray, QByteArray> headers;
};
struct AwsTemporaryCredentials final {
    QByteArray accessKey, secretKey, sessionToken;
    QDateTime expiresAt;
};
/** Signs one configured role request. MFA belongs in its TLS-protected body, never in a URL. */
Result<AwsRoleRequest> nativeAwsRoleRequest(const EksLogin& source, const AwsRole& role, const QString& code,
    QDateTime now = QDateTime::currentDateTimeUtc());
/** Reads the bounded STS response without XML entity expansion or exposing server diagnostics. */
Result<AwsTemporaryCredentials> nativeAwsRoleCredentials(const QByteArray& xml,
    QDateTime now = QDateTime::currentDateTimeUtc());
/** Signs a short-lived EKS credential after explicit authentication confirmation. */
Result<ClusterConnection> nativeEksToken(ClusterConnection connection, QDateTime now = QDateTime::currentDateTimeUtc());
} // namespace podlord
