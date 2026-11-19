#include "resource_client.h"
#include "container_files.h"

namespace podlord {
bool ResourceClient::startFileRead(const QString& id, const QString& path, const QString& container, ContainerFiles* owner) {
    expire();
    const auto state = states_.constFind(id);
    const auto row = resource(id, path);
    if (!owner || !owner->busy() || state == states_.cend() || !state->open || state->suspended || !enabled_
        || !isVisible(id) || !containsResource(id, path) || row["apiVersion"] != "v1" || row["kind"] != "Pod"
        || row["status"] != "Running" || row["uid"].toString().isEmpty() || row["uid"] != owner->podUid()
        || !row["containers"].toArray().contains(container) || owner->podPath() != path || owner->container() != container)
        return false;
    Task task; task.id = id; task.path = path; task.read = Read::FileTarget; task.foreground = true;
    task.fileToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.fileUid = row["uid"].toString(); task.fileServer = state->connection.server; task.fileView = owner;
    return enqueue(task);
}
void ResourceClient::consumeFileTarget(const Task& task, const QJsonObject& document) {
    auto* owner = task.fileView.data();
    if (!owner || !owner->busy()) return;
    const auto metadata = document["metadata"].toObject();
    const auto row = resource(task.id, task.path);
    bool declared = false;
    for (const auto& value : document["spec"].toObject()["containers"].toArray())
        if (value.toObject()["name"] == owner->container()) declared = true;
    if (!containsResource(task.id, task.path) || document["apiVersion"] != "v1" || document["kind"] != "Pod"
        || metadata["uid"] != task.fileUid || metadata["uid"] != row["uid"]
        || metadata["name"] != row["name"] || metadata["namespace"] != row["namespace"]
        || metadata.contains("deletionTimestamp") || document["status"].toObject()["phase"] != "Running"
        || !declared || !sessionOpen(task.id) || states_.value(task.id).connection.server != task.fileServer) {
        owner->reject("files.targetChanged"); return;
    }
    Task socket = task; socket.path += "/exec"; socket.read = Read::FileSocket;
    if (!enqueue(socket)) owner->reject("files.targetChanged");
}
void ResourceClient::fileHandshakeFinished(const QString& token, bool accepted, bool authenticationRequired) {
    if (!running_ || running_->read != Read::FileSocket || running_->fileToken != token) return;
    const auto task = *running_;
    auto& state = states_[task.id]; --state.pending; --state.resourcePending;
    running_.reset();
    if (authenticationRequired) suspend(state.connection.credentialId, "Container file authentication failed. Confirm login explicitly.");
    completedRequests_.append(QVariantMap{{"session", task.id}, {"time", now_().toString(Qt::ISODateWithMs)}, {"method", "GET"},
        {"path", task.path}, {"priority", "Foreground"}, {"status", accepted ? "Success" : "Failed"},
        {"duration", QString::number(std::max<qint64>(0, clock_.elapsed() - task.started)) + " ms"},
        {"outcome", accepted ? "File exec negotiated" : "File exec refused"}});
    if (completedRequests_.size() > 200) completedRequests_.removeFirst();
    emit changed(task.id); dispatch(); scheduleSync(); scheduleLogs();
}
} // namespace podlord
