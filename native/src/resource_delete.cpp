#include "resource_client.h"
#include <QJsonDocument>

namespace podlord {
bool ResourceClient::deleteResource(const QString& id, const QString& path, const QString& token, const QJsonObject& baseline) {
    const auto connection = this->connection(id);
    const auto row = resource(id, path);
    if (!connection || token.isEmpty() || !isVisible(id) || !wanted(id) || states_[id].suspended
        || !row["deletable"].toBool() || baseline["uid"].toString().isEmpty()
        || deletionNeedsObservation(id, path)
        || row["uid"] != baseline["uid"] || row["kind"] != baseline["kind"] || row["apiVersion"] != baseline["apiVersion"]
        || row["name"] != baseline["name"] || row["namespace"].toString() != baseline["namespace"].toString()) return false;
    if (running_ && running_->change && running_->id == id && running_->path == path) return false;
    for (const auto& waiting : queue_) if (waiting.change && waiting.id == id && waiting.path == path) return false;
    Task task; task.id = id; task.path = path; task.read = Read::Delete; task.foreground = true;
    task.kind = row["kind"].toString(); task.namespaced = !row["namespace"].toString().isEmpty();
    task.change = Change{token, baseline, {}, *connection, {}};
    return enqueue(std::move(task));
}
bool ResourceClient::cancelDeletion(const QString& token) {
    const auto before = queue_.size();
    dropQueued([&](const auto& task) { return task.read == Read::Delete && task.change && task.change->token == token; });
    return before != queue_.size();
}
bool ResourceClient::deletionNeedsObservation(const QString& id, const QString& path) const {
    const auto state = states_.constFind(id);
    return state != states_.cend() && state->unresolvedDeletes.contains(path);
}
bool ResourceClient::readBackDeletion(const QString& id, const QString& path, const QString& token, const QJsonObject& baseline) {
    const auto connection = this->connection(id);
    if (!connection || token.isEmpty() || !isVisible(id) || !states_[id].open || baseline["uid"].toString().isEmpty()) return false;
    Task task; task.id = id; task.path = path; task.read = Read::VerifyDelete; task.foreground = true;
    task.kind = baseline["kind"].toString(); task.namespaced = !baseline["namespace"].toString().isEmpty();
    task.change = Change{token, baseline, {}, *connection, "uncertain"};
    return enqueue(std::move(task));
}
void ResourceClient::finishDeletion(const Task& task, int http, const QByteArray& bytes, bool transportError) {
    if (http >= 300 && http < 500) {
        emit resourceDeleteFinished(task.id, task.path, task.change->token, "rejected",
            QString("Deletion rejected (HTTP %1). Nothing will be retried or force-deleted. ").arg(http)
                + states_[task.id].failures.value(task.path), false);
        return;
    }
    QJsonParseError error;
    const auto response = QJsonDocument::fromJson(bytes, &error).object();
    const auto& baseline = task.change->baseline;
    const auto metadata = response["metadata"].toObject();
    const bool receipt = (response["kind"] == "Status" && response["apiVersion"] == "v1" && response["status"] == "Success")
        || (response["apiVersion"] == baseline["apiVersion"] && response["kind"] == baseline["kind"]
            && metadata["uid"] == baseline["uid"] && metadata["name"] == baseline["name"]
            && metadata["namespace"].toString() == baseline["namespace"].toString());
    const bool acknowledged = http >= 200 && http < 300 && !transportError && error.error == QJsonParseError::NoError && receipt;
    states_[task.id].unresolvedDeletes.insert(task.path);
    Task read = task; read.read = Read::VerifyDelete; read.change->outcome = acknowledged ? "acknowledged" : "uncertain";
    emit resourceDeleteFinished(task.id, task.path, task.change->token, "reading",
        acknowledged ? "Deletion acknowledged; checking the original target. No automatic retry."
                     : "Deletion outcome uncertain; checking the original target. No automatic retry.", false);
    if (!enqueue(read)) emit resourceDeleteFinished(task.id, task.path, task.change->token, read.change->outcome,
        acknowledged ? "Deletion acknowledged, but read-back unavailable. No deletion was retried."
                     : "Deletion outcome uncertain; read-back unavailable. No deletion was retried.", false);
}
void ResourceClient::forgetDeletedResource(const Task& task) {
    auto& state = states_[task.id];
    for (auto& collection : state.collections) {
        if (collection.received > task.received) continue;
        for (int i = collection.rows.size() - 1; i >= 0; --i) {
            const auto row = collection.rows[i].toObject();
            if (row["path"] == task.path) collection.rows.removeAt(i);
        }
    }
    if (state.resources.value(task.path).received <= task.received) {
        state.resources.remove(task.path);
        state.details.remove(task.path);
        for (auto it = state.logs.begin(); it != state.logs.end();) {
            if (it->path == task.path) it = state.logs.erase(it); else ++it;
        }
    }
    state.failures.remove(task.path); state.blocked.remove(task.path);
    state.unresolvedDeletes.remove(task.path);
    publishRows(task.id);
}
void ResourceClient::finishDeletionRead(const Task& task, int http, const QByteArray& bytes, bool transportError) {
    QJsonParseError error;
    const auto response = QJsonDocument::fromJson(bytes, &error).object();
    const bool parsed = error.error == QJsonParseError::NoError;
    const auto& change = *task.change;
    const QString prefix = change.outcome == "acknowledged" ? "Deletion acknowledged. " : "Deletion outcome remains uncertain. ";
    if (http == 404 && !transportError && parsed && response["apiVersion"] == "v1" && response["kind"] == "Status"
        && response["reason"] == "NotFound" && response["code"].toInt() == 404) {
        forgetDeletedResource(task);
        emit detailFinished(task.id, task.path, false);
        emit resourceDeleteFinished(task.id, task.path, change.token, change.outcome,
            prefix + "Target observed absent. Observation is not proof of exactly-once execution. No deletion was retried.", true);
        return;
    }
    const auto metadata = response["metadata"].toObject();
    const auto& baseline = change.baseline;
    const bool valid = http >= 200 && http < 300 && !transportError && parsed
        && response["apiVersion"] == baseline["apiVersion"] && response["kind"] == baseline["kind"]
        && metadata["name"] == baseline["name"] && metadata["namespace"].toString() == baseline["namespace"].toString()
        && !metadata["uid"].toString().isEmpty() && !metadata["resourceVersion"].toString().isEmpty();
    if (!valid) {
        emit resourceDeleteFinished(task.id, task.path, change.token, change.outcome,
            prefix + "Read-back unavailable. Confirm authentication if needed, then read back explicitly; no deletion was retried.", false);
        return;
    }
    Task detail = task; detail.read = Read::Detail; consume(detail, response);
    if (states_[task.id].failures.contains(task.path)) {
        emit resourceDeleteFinished(task.id, task.path, change.token, change.outcome,
            prefix + "Read-back unavailable: invalid resource. No deletion was retried.", false);
        return;
    }
    const QString observed = metadata["uid"] != baseline["uid"] ? "A different UID is present under this name; it was not deleted. "
        : !metadata["deletionTimestamp"].toString().isEmpty() ? "Original target is terminating; Kubernetes grace and finalizers are retained. "
                                                          : "Original target is still present. ";
    emit detailFinished(task.id, task.path, true);
    emit resourceDeleteFinished(task.id, task.path, change.token, change.outcome,
        prefix + observed + "No deletion was retried or force-deleted.", true);
}
} // namespace podlord
