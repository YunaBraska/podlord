#pragma once
#include "kubeconfig_store.h"

namespace podlord {
/** Parses one EKS exec configuration without invoking the AWS CLI. */
Result<std::optional<EksLogin>> nativeEksConfig(const ExecCommand& command);
/** Signs a short-lived EKS credential after explicit authentication confirmation. */
Result<ClusterConnection> nativeEksToken(ClusterConnection connection, QDateTime now = QDateTime::currentDateTimeUtc());
} // namespace podlord
