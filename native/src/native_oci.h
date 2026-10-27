#pragma once
#include "kubeconfig_store.h"

namespace podlord {
/** Recognizes an OKE API-key exec configuration without running OCI CLI. */
Result<std::optional<OciLogin>> nativeOciConfig(const ExecCommand& command);
/** Produces an OKE cluster-bound token using the confirmed private key. */
Result<ClusterConnection> nativeOciToken(ClusterConnection connection, QDateTime now = QDateTime::currentDateTimeUtc());
} // namespace podlord
