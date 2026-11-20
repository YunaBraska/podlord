#include "resource_graph.h"
#include <QCoreApplication>
#include <cstdio>

using namespace podlord;
namespace {
QJsonObject row(const QString& kind, const QString& name, const QString& uid, const QString& nameSpace = {}) {
    const auto path = "/" + kind + "/" + name;
    return {{"path", path}, {"kind", kind}, {"name", name}, {"uid", uid}, {"namespace", nameSpace},
        {"apiVersion", kind == "ReplicaSet" || kind == "Deployment" ? "apps/v1" : "v1"}, {"status", "Ready"}};
}
bool run() {
    auto pod = row("Pod", "app-1", "pod", "team");
    pod["node"] = "worker";
    pod["volumeClaims"] = QJsonArray{"claim"};
    pod["secretRefs"] = QJsonArray{"secret"};
    pod["configMapRefs"] = QJsonArray{"config"};
    pod["owners"] = QJsonArray{QJsonObject{{"uid", "rs"}, {"kind", "ReplicaSet"}, {"apiVersion", "apps/v1"}}};
    auto replica = row("ReplicaSet", "app-set", "rs", "team");
    replica["owners"] = QJsonArray{QJsonObject{{"uid", "deployment"}, {"kind", "Deployment"}, {"apiVersion", "apps/v1"}}};
    auto claim = row("PersistentVolumeClaim", "claim", "claim", "team");
    claim["boundVolume"] = "disk";
    auto ciliumPod = row("Pod", "cilium-worker", "cilium", "kube-system");
    ciliumPod["node"] = "worker";
    auto event = row("Event", "warning", "event", "team");
    event["kubernetesEvent"] = true; event["eventTargetUid"] = "pod"; event["eventReason"] = "FailedMount";
    QJsonArray rows{pod, replica, claim, ciliumPod, event,
        row("Deployment", "app", "deployment", "team"), row("Node", "worker", "node"),
        row("PersistentVolume", "disk", "volume"), row("Secret", "secret", "secret", "team"),
        row("ConfigMap", "config", "config", "team"), row("CiliumNode", "worker", "cilium-node"),
        row("CiliumEndpoint", "app-1", "endpoint", "team"), row("Namespace", "team", "namespace")};
    const ResourceGraph graph(rows);
    const auto depths = graph.depths("/Pod/app-1");
    const auto check = [&](const char* path, int depth) { return depths.value(QString::fromLatin1(path), -1).toInt() == depth; };
    if (!check("/Pod/app-1", 0) || !check("/Node/worker", 1) || !check("/PersistentVolumeClaim/claim", 1)
        || !check("/Secret/secret", 1) || !check("/ConfigMap/config", 1) || !check("/ReplicaSet/app-set", 1)
        || !check("/CiliumEndpoint/app-1", 1) || !check("/Event/warning", 1) || !check("/Namespace/team", 1)
        || !check("/Deployment/app", 2) || !check("/PersistentVolume/disk", 2)
        || !check("/CiliumNode/worker", 2) || !check("/Pod/cilium-worker", 2)) return false;
    const auto links = graph.links("/Pod/app-1");
    for (const auto& value : links) if (value.toObject()["kind"] == "Event") return false;
    const auto namespaceReach = graph.depths("/Namespace/team");
    if (namespaceReach.value("/Pod/app-1", -1).toInt() != 1 || namespaceReach.contains("/Node/worker")) return false;
    const auto evidence = graph.evidence("/Pod/app-1");
    for (const auto& value : evidence) if (value.toObject()["path"] == "/PersistentVolume/disk")
        return value.toObject()["graphDepth"] == 2
            && value.toObject()["graphParentPath"] == "/PersistentVolumeClaim/claim"
            && value.toObject()["graphChain"].toString().contains("Pod > PersistentVolumeClaim > PersistentVolume");
    return false;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (run()) return 0;
    std::fprintf(stderr, "Resource graph depth or link failed\n");
    return 1;
}
