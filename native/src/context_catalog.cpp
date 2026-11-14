#include "context_catalog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QLockFile>
#include <QSet>
#include <limits>

namespace podlord {
Result<ContextCatalog> readContextCatalog(const QString& profile) {
    if (const auto failure = profileFailure(profile)) return *failure;
    const auto directory = QDir(profile).filePath("kubeconfigs");
    if (const auto failure = profileFailure(directory)) return *failure;
    const auto path = QDir(directory).filePath("catalog.json");
    if (QFileInfo(path).isSymLink()) return Failure{StoreError::InvalidData, "Context catalog must not be a symbolic link."};
    if (!QFileInfo::exists(path)) return ContextCatalog{};
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly))
        return Failure{StoreError::ReadFailed, "Cannot read the context catalog."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (file.error() != QFile::NoError) return Failure{StoreError::ReadFailed, "Cannot read the context catalog."};
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || (root.size() != 2 && !(root.size() == 3 && root.contains("usage")))
        || !root["version"].isDouble() || !root["sources"].isObject())
        return Failure{StoreError::InvalidData, "The context catalog could not be read. Existing files were retained."};
    if (root["version"] != 1) return Failure{StoreError::UnsupportedVersion, "Unsupported context catalog format."};
    static const QRegularExpression sourceId("\\A[0-9a-f]{64}-[0-9a-f]{64}\\z");
    static const QRegularExpression contextId("\\Actx:[0-9a-f]{64}\\z");
    ContextCatalog catalog;
    const auto sources = root["sources"].toObject();
    for (auto source = sources.begin(); source != sources.end(); ++source) {
        if (!sourceId.match(source.key()).hasMatch() || !source.value().isArray())
            return Failure{StoreError::InvalidData, "Invalid entry in the context catalog."};
        QStringList contexts;
        for (const auto value : source.value().toArray()) {
            const auto id = value.toString();
            if (!value.isString() || !contextId.match(id).hasMatch() || (!contexts.isEmpty() && contexts.last() >= id))
                return Failure{StoreError::InvalidData, "Invalid context in the context catalog."};
            contexts.append(id);
        }
        catalog.sources.insert(source.key(), contexts);
    }
    if (root.contains("usage")) {
        if (!root["usage"].isObject()) return Failure{StoreError::InvalidData, "Invalid context usage record."};
        QSet<QString> members;
        for (const auto& contexts : catalog.sources) for (const auto& id : contexts) members.insert(id);
        const auto usage = root["usage"].toObject();
        for (auto entry = usage.begin(); entry != usage.end(); ++entry) {
            const auto value = entry.value().toObject();
            const auto count = value["count"].toInteger(-1);
            const auto time = QDateTime::fromString(value["lastUsed"].toString(), Qt::ISODateWithMs);
            if (!members.contains(entry.key()) || !entry.value().isObject() || value.size() != 2
                || count <= 0 || !time.isValid() || time.toUTC().toString(Qt::ISODateWithMs) != value["lastUsed"].toString())
                return Failure{StoreError::InvalidData, "Invalid context usage record. Existing data was retained."};
            catalog.usage.insert(entry.key(), {count, time});
        }
    }
    return catalog;
}
Result<bool> writeContextCatalog(const QString& profile, const ContextCatalog& catalog) {
    if (const auto failure = profileFailure(profile)) return *failure;
    if (const auto failure = profileFailure(QDir(profile).filePath("kubeconfigs"))) return *failure;
    const auto path = QDir(profile).filePath("kubeconfigs/catalog.json");
    if (QFileInfo(path).isSymLink() || QFileInfo(path).isDir())
        return Failure{StoreError::WriteFailed, "Cannot safely publish the context catalog."};
    QJsonObject sources;
    QSet<QString> members;
    for (auto source = catalog.sources.begin(); source != catalog.sources.end(); ++source) {
        sources.insert(source.key(), QJsonArray::fromStringList(source.value()));
        for (const auto& id : source.value()) members.insert(id);
    }
    QJsonObject usage;
    for (auto entry = catalog.usage.begin(); entry != catalog.usage.end(); ++entry)
        if (members.contains(entry.key())) usage.insert(entry.key(), QJsonObject{{"count", entry->count},
            {"lastUsed", entry->lastUsed.toUTC().toString(Qt::ISODateWithMs)}});
    QJsonObject root{{"version", 1}, {"sources", sources}};
    if (!usage.isEmpty()) root.insert("usage", usage);
    const auto bytes = QJsonDocument(root).toJson();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
        return Failure{StoreError::WriteFailed, "Cannot publish the context catalog. Existing data was retained."};
    return true;
}
Result<ContextUsageMap> recordContextUsage(const QString& profile, const ContextUsageMap& increments) {
    if (const auto failure = profileFailure(profile)) return *failure;
    const auto directory = QDir(profile).filePath("kubeconfigs");
    if (const auto failure = profileFailure(directory)) return *failure;
    const auto lockPath = QDir(directory).filePath("sources.lock");
    if (QFileInfo(lockPath).isSymLink() || QFileInfo(lockPath).isDir())
        return Failure{StoreError::WriteFailed, "Cannot safely save context usage."};
    QLockFile lock(lockPath); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Failure{lock.error() == QLockFile::LockFailedError ? StoreError::Busy : StoreError::WriteFailed,
        "Cannot save context usage while the context catalog is locked."};
    auto loaded = readContextCatalog(profile);
    if (const auto* failure = std::get_if<Failure>(&loaded)) return *failure;
    auto catalog = std::get<ContextCatalog>(loaded);
    QSet<QString> members;
    for (const auto& contexts : catalog.sources) for (const auto& id : contexts) members.insert(id);
    bool changed = false;
    for (auto increment = increments.begin(); increment != increments.end(); ++increment) {
        if (increment->count <= 0 || !increment->lastUsed.isValid())
            return Failure{StoreError::InvalidInput, "Context usage needs a positive count and a valid time."};
        if (!members.contains(increment.key())) continue;
        auto& use = catalog.usage[increment.key()];
        use.count += std::min(increment->count, std::numeric_limits<qint64>::max() - use.count);
        use.lastUsed = std::max(use.lastUsed, increment->lastUsed.toUTC());
        changed = true;
    }
    if (changed) {
        const auto saved = writeContextCatalog(profile, catalog);
        if (const auto* failure = std::get_if<Failure>(&saved)) return *failure;
    }
    return catalog.usage;
}
}
