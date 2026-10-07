#include "resource_client.h"
#include "yaml_apply.h"
#include <QJsonDocument>

namespace podlord {
bool ResourceClient::applyYaml(const QString& id, const QString& path, const QString& token, const QJsonObject& baseline,
                               const QJsonObject& desired, const QJsonArray& patch) {
    const auto connection = this->connection(id);
    const auto identity = baseline["metadata"].toObject();
    const auto row = resource(id, path);
    const QJsonObject uidGuard{{"op", "test"}, {"path", "/metadata/uid"}, {"value", identity["uid"]}};
    const QJsonObject versionGuard{{"op", "test"}, {"path", "/metadata/resourceVersion"}, {"value", identity["resourceVersion"]}};
    if (!connection || token.isEmpty() || patch.size() < 3 || !isVisible(id) || !wanted(id) || states_[id].suspended
        || row["uid"] != identity["uid"] || row["name"] != identity["name"]
        || row["apiVersion"] != baseline["apiVersion"] || row["kind"] != baseline["kind"] || row["namespace"].toString() != identity["namespace"].toString()
        || patch[0] != uidGuard || patch[1] != versionGuard
        || QJsonDocument(patch).toJson(QJsonDocument::Compact).size() > static_cast<qint64>(settings_.yamlLimitMiB) * 1048576) return false;
    if (running_ && running_->change && running_->id == id && running_->path == path) return false;
    for (const auto& waiting : queue_) if (waiting.change && waiting.id == id && waiting.path == path) return false;
    Task task; task.id = id; task.path = path; task.read = Read::Apply; task.foreground = true;
    task.kind = baseline["kind"].toString(); task.namespaced = !identity["namespace"].toString().isEmpty();
    task.change = Change{token, baseline, desired, *connection, {}}; task.patch = patch;
    return enqueue(std::move(task));
}
bool ResourceClient::cancelYamlApply(const QString& token) {
    const auto before = queue_.size();
    dropQueued([&](const auto& task) { return task.read == Read::Apply && task.change && task.change->token == token; });
    return before != queue_.size();
}
bool ResourceClient::readBackYaml(const QString& id, const QString& path, const QString& token,
                                  const QJsonObject& baseline, const QJsonObject& desired, const QString& outcome) {
    const auto connection = this->connection(id);
    if (!connection || token.isEmpty() || !states_[id].open || !isVisible(id)) return false;
    Task task; task.id = id; task.path = path; task.read = Read::Verify; task.foreground = true;
    task.kind = baseline["kind"].toString(); task.namespaced = !baseline["metadata"].toObject()["namespace"].toString().isEmpty();
    task.change = Change{token, baseline, desired, *connection, outcome == "rejected" ? "rejected" : "uncertain"};
    return enqueue(std::move(task));
}
void ResourceClient::finishApply(const Task& task, int http, const QByteArray& bytes, bool transportError) {
    Task read = task; read.read = Read::Verify; read.patch = {};
    auto& state = states_[task.id];
    const bool conflict = http == 409 || http == 412 || http == 422;
    if (http >= 300 && http < 500 && !conflict) {
        emit yamlApplyFinished(task.id, task.path, task.change->token, "rejected",
            state.failures.value(task.path, "Write rejected. Draft retained; no automatic retry."), {});
        return;
    }
    QJsonParseError error;
    const auto response = QJsonDocument::fromJson(bytes, &error).object();
    const bool acknowledged = http >= 200 && http < 300 && !transportError && error.error == QJsonParseError::NoError
        && response["apiVersion"] == task.change->baseline["apiVersion"] && response["kind"] == task.change->baseline["kind"]
        && response["metadata"].toObject()["uid"] == task.change->baseline["metadata"].toObject()["uid"]
        && !response["metadata"].toObject()["resourceVersion"].toString().isEmpty()
        && yamlChangesObserved(task.change->baseline, task.change->desired, response);
    read.change->outcome = acknowledged ? "applied" : conflict ? "conflict" : "uncertain";
    if (!enqueue(read)) emit yamlApplyFinished(task.id, task.path, task.change->token, "uncertain",
        acknowledged ? "Write acknowledged; fresh read-back unavailable. Draft retained. No write was retried."
                     : "Write outcome uncertain; read-back unavailable. Draft retained. No write was retried.", {});
}
void ResourceClient::finishVerify(const Task& task) {
    const auto& change = *task.change;
    const auto& state = states_[task.id];
    const auto current = state.failures.contains(task.path) ? QJsonObject{} : document(task.id, task.path);
    if (current.isEmpty()) {
        emit yamlApplyFinished(task.id, task.path, change.token, "uncertain", "Read-back unavailable. Write outcome remains unresolved; draft retained. No automatic write retry.", {});
        return;
    }
    const auto original = change.baseline["metadata"].toObject(), metadata = current["metadata"].toObject();
    if (metadata["uid"] != original["uid"]) {
        emit yamlApplyFinished(task.id, task.path, change.token, "conflict",
            "Resource identity changed. Original draft retained; do not apply it to the replacement. No write was retried.", current);
        return;
    }
    if (change.outcome == "applied") {
        emit yamlApplyFinished(task.id, task.path, change.token, "applied", "Write acknowledged; fresh resource loaded.", current); return;
    }
    if (yamlChangesObserved(change.baseline, change.desired, current)) {
        emit yamlApplyFinished(task.id, task.path, change.token, "observed", "Desired changes observed. This does not prove exactly-once execution. No write was retried; draft retained.", current); return;
    }
    if (metadata["resourceVersion"] != original["resourceVersion"]) {
        emit yamlApplyFinished(task.id, task.path, change.token, "conflict", "Resource changed. Compare and reconcile explicitly; draft retained. No automatic overwrite.", current); return;
    }
    const bool rejected = change.outcome == "conflict" || change.outcome == "rejected";
    emit yamlApplyFinished(task.id, task.path, change.token, rejected ? "rejected" : "uncertain",
        rejected ? "API rejected the patch; original resource unchanged. Draft retained."
                                     : "Read-back did not observe the desired changes. Outcome remains uncertain; draft retained. No write was retried.", current);
}
} // namespace podlord
