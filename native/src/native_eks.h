#pragma once
#include "kubeconfig_store.h"

namespace podlord {
/** Parses one EKS exec configuration without invoking the AWS CLI. */
Result<std::optional<EksLogin>> nativeEksConfig(const ExecCommand& command,
    const std::optional<QMap<QString, QString>>& importedProfile = {});
/** Resolves explicit --profile, then exec AWS_PROFILE, otherwise default. Never scans the home directory. */
Result<QString> awsProfileName(const ExecCommand& command);
/** Reads selected shared-config/credentials sections. Returns only fields needed for native sign-in. */
Result<QMap<QString, QString>> readAwsProfile(const QByteArray& config, const QByteArray& credentials, const QString& name);
/** Signs a short-lived EKS credential after explicit authentication confirmation. */
Result<ClusterConnection> nativeEksToken(ClusterConnection connection, QDateTime now = QDateTime::currentDateTimeUtc());
} // namespace podlord
