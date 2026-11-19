#include "resource_graph.h"
#include <QQueue>
#include <QSet>
#include <algorithm>
#include <tuple>

namespace podlord {
ResourceGraph::ResourceGraph(const QJsonArray& rows) {
    QHash<QString, QString> byUid, byKindName;
    const auto key = [](const QString& kind, const QString& nameSpace, const QString& name) {
        return kind + '\n' + nameSpace + '\n' + name;
    };
    for (const auto& value : rows) {
        const auto row = value.toObject();
        const auto path = row["path"].toString(), name = row["name"].toString();
        if (path.isEmpty() || name.isEmpty()) continue;
        rows_.insert(path, row);
        if (!row["uid"].toString().isEmpty()) byUid.insert(row["uid"].toString(), path);
        byKindName.insert(key(row["kind"].toString(), row["namespace"].toString(), name), path);
    }
    QSet<QString> added;
    const auto connect = [&](const QString& parent, const QString& child, const QString& outward,
                             const QString& inward, bool traverse = true, bool inspector = true) {
        if (parent.isEmpty() || child.isEmpty() || parent == child || !rows_.contains(parent) || !rows_.contains(child)) return;
        const auto identity = parent + '\n' + child + '\n' + outward;
        if (added.contains(identity)) return;
        added.insert(identity);
        edges_[parent].append({child, outward, parent, child, traverse, inspector});
        edges_[child].append({parent, inward, parent, child, traverse, inspector});
    };
    for (auto item = rows_.cbegin(); item != rows_.cend(); ++item) {
        const auto& row = item.value();
        const auto path = item.key(), kind = row["kind"].toString();
        const auto name = row["name"].toString(), nameSpace = row["namespace"].toString();
        for (const auto& value : row["owners"].toArray()) {
            const auto owner = value.toObject();
            const auto parent = byUid.value(owner["uid"].toString());
            const auto resolved = rows_.value(parent);
            if (resolved["kind"] == owner["kind"] && resolved["apiVersion"] == owner["apiVersion"]
                && (nameSpace.isEmpty() || resolved["namespace"] == nameSpace))
                connect(parent, path, "Dependent", "Owner");
        }
        if (!nameSpace.isEmpty())
            connect(byKindName.value(key("Namespace", {}, nameSpace)), path, "Resource", "Namespace", false);
        if (kind == "Pod") {
            connect(byKindName.value(key("Node", {}, row["node"].toString())), path,
                nameSpace == "kube-system" && name.startsWith("cilium-") ? "Cilium agent on node" : "Pod on node", "Node");
            for (const auto& value : row["volumeClaims"].toArray())
                connect(byKindName.value(key("PersistentVolumeClaim", nameSpace, value.toString())), path,
                    "Mounted by Pod", "Volume claim");
            for (const auto& value : row["imagePullSecrets"].toArray())
                connect(byKindName.value(key("Secret", nameSpace, value.toString())), path,
                    "Used by Pod", "Image pull Secret");
            for (const auto& value : row["secretRefs"].toArray())
                connect(byKindName.value(key("Secret", nameSpace, value.toString())), path,
                    "Used by Pod", "Secret");
            for (const auto& value : row["configMapRefs"].toArray())
                connect(byKindName.value(key("ConfigMap", nameSpace, value.toString())), path,
                    "Used by Pod", "ConfigMap");
            connect(path, byKindName.value(key("CiliumEndpoint", nameSpace, name)), "Cilium endpoint", "Pod");
        } else if (kind == "PersistentVolumeClaim") {
            connect(byKindName.value(key("PersistentVolume", {}, row["boundVolume"].toString())), path,
                "Claim", "Volume");
        } else if (kind == "Node") {
            connect(path, byKindName.value(key("CiliumNode", {}, name)), "Cilium node", "Node");
        }
        if (row["kubernetesEvent"].toBool())
            connect(byUid.value(row["eventTargetUid"].toString()), path, "Event", "Resource", true, false);
    }
    for (auto& edges : edges_)
        std::sort(edges.begin(), edges.end(), [](const Edge& left, const Edge& right) {
            return std::tie(left.relation, left.target) < std::tie(right.relation, right.target);
        });
}
QList<ResourceGraph::Reached> ResourceGraph::reach(const QString& path, int maxDepth, int maxCount) const {
    QList<Reached> result;
    if (!rows_.contains(path) || maxDepth < 0 || maxCount < 1) return result;
    QSet<QString> seen{path};
    QQueue<Reached> pending;
    pending.enqueue({path, "selected", rows_.value(path)["kind"].toString(), 0});
    while (!pending.isEmpty() && result.size() < maxCount) {
        const auto current = pending.dequeue();
        result.append(current);
        if (current.depth >= maxDepth) continue;
        for (const auto& edge : edges_.value(current.path)) {
            if (seen.contains(edge.target)) continue;
            seen.insert(edge.target);
            const auto next = Reached{edge.target, edge.relation,
                current.chain + " > " + rows_.value(edge.target)["kind"].toString(), current.depth + 1};
            if (edge.traverse) pending.enqueue(next);
            else if (current.depth == 0 && result.size() < maxCount) result.append(next);
        }
    }
    return result;
}
QJsonArray ResourceGraph::evidence(const QString& path, int maxDepth, int maxCount) const {
    QJsonArray result;
    for (const auto& reached : reach(path, maxDepth, maxCount)) {
        auto row = rows_.value(reached.path);
        row["graphDepth"] = reached.depth;
        row["graphRelation"] = reached.relation;
        row["graphChain"] = reached.chain;
        result.append(row);
    }
    return result;
}
QJsonArray ResourceGraph::links(const QString& path) const {
    QJsonArray result;
    const auto selected = rows_.value(path);
    if (selected.isEmpty()) return result;
    for (const auto& edge : edges_.value(path)) {
        if (!edge.inspector) continue;
        const auto target = rows_.value(edge.target), from = rows_.value(edge.fromPath), to = rows_.value(edge.toPath);
        result.append(QJsonObject{{"path", edge.target}, {"relation", edge.relation}, {"name", target["name"]},
            {"kind", target["kind"]}, {"namespace", target["namespace"]}, {"from", from["name"]},
            {"to", to["name"]}, {"fromPath", edge.fromPath}, {"toPath", edge.toPath}, {"status", to["status"]}});
    }
    return result;
}
QVariantMap ResourceGraph::depths(const QString& path) const {
    QVariantMap result;
    for (const auto& item : reach(path)) result.insert(item.path, item.depth);
    return result;
}
} // namespace podlord
