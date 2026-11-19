#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

namespace podlord {
/** Relationships resolved from one accepted resource snapshot. Paths and names stay local. */
class ResourceGraph final {
public:
    struct Reached final {
        QString path, relation, chain;
        int depth = 0;
    };
    explicit ResourceGraph(const QJsonArray& rows);
    QList<Reached> reach(const QString& path, int maxDepth = 6, int maxCount = 512) const;
    QJsonArray evidence(const QString& path, int maxDepth = 6, int maxCount = 128) const;
    QJsonArray links(const QString& path) const;
    QVariantMap depths(const QString& path) const;
private:
    struct Edge final {
        QString target, relation, fromPath, toPath;
        bool traverse = true, inspector = true;
    };
    QHash<QString, QJsonObject> rows_;
    QHash<QString, QList<Edge>> edges_;
};
} // namespace podlord
