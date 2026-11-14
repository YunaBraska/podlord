#pragma once
#include "session_store.h"
#include <QMap>

namespace podlord {
/** The only membership authority. Immutable source files are not a discovery index. */
struct ContextUsage final {
    qint64 count = 0;
    QDateTime lastUsed;
};
using ContextUsageMap = QMap<QString, ContextUsage>;
struct ContextCatalog final {
    QMap<QString, QStringList> sources;
    ContextUsageMap usage;
};
Result<ContextCatalog> readContextCatalog(const QString& profile);
/** Caller owns kubeconfigs/sources.lock. Publication is atomic. */
Result<bool> writeContextCatalog(const QString& profile, const ContextCatalog& catalog);
/** Atomically adds explicit uses to existing members. Removed contexts are never recreated. */
Result<ContextUsageMap> recordContextUsage(const QString& profile, const ContextUsageMap& increments);
}
