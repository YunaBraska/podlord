#include "resource_client.h"
#include "container_terminal.h"
#include <QRegularExpression>

namespace podlord {
ContainerTerminal* ResourceClient::terminal(const QString& id) const {
    for (const auto& terminal : terminals_) if (terminal.session == id) return terminal.screen;
    return nullptr;
}
Result<QString> ResourceClient::startTerminal(const QString& id, const QString& path, const QString& container, const QString& shell) {
    expire();
    const auto state = states_.constFind(id); const auto row = resource(id, path);
    static const QRegularExpression shellPath("^/[A-Za-z0-9_./-]{1,255}$");
    if (state == states_.cend() || !state->open || state->suspended || !enabled_ || id != visible_
        || !containsResource(id, path) || row["apiVersion"] != "v1" || row["kind"] != "Pod" || row["status"] != "Running"
        || row["uid"].toString().isEmpty() || !row["containers"].toArray().contains(container)
        || !shellPath.match(shell).hasMatch() || shell.contains("/../") || shell.endsWith("/.."))
        return Failure{StoreError::InvalidInput, "Select a Running core Pod, one declared container and an absolute POSIX shell path."};
    if (auto* existing = terminal(id); existing && existing->active()) return Failure{StoreError::Busy, "Disconnect this session's current terminal before starting another shell."};
    stopTerminal(id);
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto* screen = new ContainerTerminal(row["namespace"].toString() + '/' + row["name"].toString() + " / " + container, this);
    terminals_.insert(token, {id, path, row["uid"].toString(), container, shell, state->connection.server, screen});
    connect(screen, &ContainerTerminal::changed, this, [this, id] { emit terminalChanged(id); });
    connect(screen, &ContainerTerminal::handshakeFinished, this, [this, token](bool accepted, bool auth) { terminalHandshakeFinished(token, accepted, auth); });
    Task task; task.id = id; task.path = path; task.read = Read::TerminalTarget; task.foreground = true; task.terminalToken = token;
    if (!enqueue(task)) { screen->reject("The session cannot admit an exec request."); return Failure{StoreError::Busy, screen->status()}; }
    emit terminalChanged(id); return token;
}
bool ResourceClient::stopTerminal(const QString& id) {
    for (const auto& token : terminals_.keys()) {
        const auto current = terminals_.value(token); if (current.session != id) continue;
        // Finish a dispatched handshake before deleting its owner; queued work has not been sent.
        current.screen->stop(); terminals_.remove(token);
        dropQueued([&](const auto& task) { return task.terminalToken == token; });
        current.screen->deleteLater(); emit terminalChanged(id); return true;
    }
    return false;
}
void ResourceClient::terminalHandshakeFinished(const QString& token, bool accepted, bool auth) {
    if (!running_ || running_->read != Read::TerminalSocket || running_->terminalToken != token) return;
    const auto task = *running_; auto& state = states_[task.id]; --state.pending; --state.resourcePending;
    running_.reset();
    if (auth) suspend(state.connection.credentialId, "Terminal authentication failed. Confirm login explicitly.");
    completedRequests_.append(QVariantMap{{"session", task.id}, {"time", now_().toString(Qt::ISODateWithMs)}, {"method", "GET"},
        {"path", task.path}, {"priority", "Foreground"}, {"status", accepted ? "Success" : "Failed"},
        {"duration", QString::number(std::max<qint64>(0, clock_.elapsed() - task.started)) + " ms"}, {"outcome", accepted ? "Exec negotiated" : "Exec refused"}});
    if (completedRequests_.size() > 200) completedRequests_.removeFirst();
    emit changed(task.id); dispatch(); scheduleSync(); scheduleLogs();
}
void ResourceClient::consumeTerminal(const Task& task, const QJsonObject& document) {
    const auto current = terminals_.constFind(task.terminalToken); if (current == terminals_.cend()) return;
    const auto metadata = document["metadata"].toObject();
    bool declared = false;
    for (const auto& value : document["spec"].toObject()["containers"].toArray()) if (value.toObject()["name"] == current->container) declared = true;
    const auto cached = resource(task.id, task.path);
    if (document["apiVersion"] != "v1" || document["kind"] != "Pod" || metadata["uid"] != current->uid
        || metadata["name"] != cached["name"] || metadata["namespace"] != cached["namespace"] || metadata.contains("deletionTimestamp")
        || document["status"].toObject()["phase"] != "Running" || !declared) {
        current->screen->reject("The Pod or container changed before exec. Refresh and select the target again."); return;
    }
    Task socket; socket.id = task.id; socket.path = task.path + "/exec"; socket.read = Read::TerminalSocket;
    socket.foreground = true; socket.terminalToken = task.terminalToken;
    if (!enqueue(socket)) current->screen->reject("The session cannot admit this exec handshake.");
}
}
