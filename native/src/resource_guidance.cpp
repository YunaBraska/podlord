#include "resource_guidance.h"
#include <QJsonArray>
#include <QSet>

namespace podlord {
QVariantList resourceGuidance(const QJsonObject& document) {
    QVariantList result;
    QSet<QString> seen;
    const auto status = document["status"].toObject();
    const auto kind = document["kind"].toString();
    const auto version = document["apiVersion"].toString();
    const QString lifecycle = "https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/";
    const QString debug = "https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/";
    const QString pressure = "https://kubernetes.io/docs/concepts/scheduling-eviction/node-pressure-eviction/";
    const auto add = [&](const QString& code, const QString& scope, const QString& title,
                         const QString& detail, const QString& documentation, const QString& tone = "warning") {
        const auto id = code + '\n' + scope;
        if (result.size() >= 8 || seen.contains(id)) return;
        seen.insert(id);
        result.append(QVariantMap{{"code", code}, {"scope", scope}, {"title", title},
            {"detail", detail}, {"documentation", documentation}, {"tone", tone}});
    };
    if (version == "v1" && kind == "Pod") {
        if (status["reason"] == "Evicted")
            add("Evicted", "Pod", "Pod was evicted", "Inspect Pod Events and the node's pressure conditions. Do not assume a memory limit was the cause.", pressure, "danger");
        const auto conditions = status["conditions"].toArray();
        for (const auto& value : conditions) {
            const auto condition = value.toObject();
            if (condition["type"] == "PodScheduled" && condition["status"] == "False" && condition["reason"] == "Unschedulable")
                add("Unschedulable", "Pod", "Pod cannot be scheduled", "Read scheduling Events, then compare resource requests, node selectors, affinity and taints with available nodes.", debug);
        }
        for (const auto& field : {"initContainerStatuses", "containerStatuses", "ephemeralContainerStatuses"}) {
            const auto containers = status[field].toArray();
            for (const auto& value : containers) {
                if (result.size() >= 8) return result;
                const auto container = value.toObject();
                const auto name = container["name"].toString();
                if (name.isEmpty()) continue;
                const auto state = container["state"].toObject();
                const auto reason = state["waiting"].toObject()["reason"].toString();
                const auto scope = QString::fromLatin1(field) + ": " + name;
                if (reason == "ImagePullBackOff" || reason == "ErrImagePull" || reason == "InvalidImageName")
                    add(reason, scope, "Container image is unavailable", "Check the image reference, registry reachability and referenced image-pull credentials. Events identify the actual pull failure.", "https://kubernetes.io/docs/concepts/containers/images/", "danger");
                else if (reason == "CrashLoopBackOff")
                    add(reason, scope, "Container is in restart backoff", "Backoff is a symptom, not a root cause. Inspect container logs, termination details, configuration and startup/liveness probes.", lifecycle, "danger");
                else if (reason == "CreateContainerConfigError" || reason == "CreateContainerError")
                    add(reason, scope, "Container could not be created", "Inspect Events and referenced configuration, Secrets and volumes. Verify the failing reference without exposing Secret values.", debug, "danger");
                if (state["terminated"].toObject()["reason"] == "OOMKilled")
                    add("OOMKilled", scope, "Container termination recorded as OOMKilled", "Compare measured memory, container limits and node pressure before changing resources. The status alone does not identify the allocation responsible.", "https://kubernetes.io/docs/tasks/configure-pod-container/assign-memory-resource/", "danger");
            }
        }
        for (const auto& field : {"initContainerStatuses", "containerStatuses", "ephemeralContainerStatuses"}) {
            const auto containers = status[field].toArray();
            for (const auto& value : containers) {
                if (result.size() >= 8) return result;
                const auto container = value.toObject();
                const auto name = container["name"].toString();
                if (!name.isEmpty() && container["lastState"].toObject()["terminated"].toObject()["reason"] == "OOMKilled")
                    add("PreviousOOMKilled", QString::fromLatin1(field) + ": " + name, "Previous container termination: OOMKilled", "This is historical evidence, not proof of a current failure. Review memory measurements, limits and the subsequent container state.", "https://kubernetes.io/docs/tasks/configure-pod-container/assign-memory-resource/");
            }
        }
    } else if (version == "v1" && kind == "Node") {
        const auto conditions = status["conditions"].toArray();
        for (const auto& value : conditions) {
            if (result.size() >= 8) return result;
            const auto condition = value.toObject();
            const auto type = condition["type"].toString();
            const auto state = condition["status"].toString();
            if (type == "Ready" && (state == "False" || state == "Unknown"))
                add("NodeReady" + state, "Node", state == "Unknown" ? "Node readiness is unknown" : "Node is not ready", "Inspect node conditions, Events and kubelet/connectivity health before rescheduling or changing workloads.", "https://kubernetes.io/docs/concepts/architecture/nodes/", state == "False" ? "danger" : "warning");
            else if (state == "True" && (type == "MemoryPressure" || type == "DiskPressure" || type == "PIDPressure"))
                add(type, "Node", "Node reports " + type, "Inspect node resource usage and eviction Events. Pressure can affect scheduling and cause evictions; do not delete or raise limits automatically.", pressure);
        }
    } else if (version == "v1" && kind == "PersistentVolumeClaim") {
        const auto phase = status["phase"].toString();
        if (phase == "Pending" || phase == "Lost")
            add("Claim" + phase, "PersistentVolumeClaim", "Volume claim is " + phase, "Inspect claim Events, StorageClass/provisioner, matching volumes and binding mode. Pending may be expected until a consumer is scheduled.", "https://kubernetes.io/docs/concepts/storage/persistent-volumes/", phase == "Lost" ? "danger" : "warning");
    } else if (version == "apps/v1" && kind == "Deployment") {
        const auto conditions = status["conditions"].toArray();
        for (const auto& value : conditions) {
            const auto condition = value.toObject();
            if (condition["type"] == "Progressing" && condition["status"] == "False" && condition["reason"] == "ProgressDeadlineExceeded")
                add("ProgressDeadlineExceeded", "Deployment", "Deployment progress deadline exceeded", "Inspect the ReplicaSet, Pods and rollout Events to identify blocked progress. Do not increase the deadline or roll back without reviewing the cause.", "https://kubernetes.io/docs/concepts/workloads/controllers/deployment/", "danger");
        }
    }
    return result;
}
}
