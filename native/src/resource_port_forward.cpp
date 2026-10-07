#include "resource_client.h"
#include "port_forward_transport.h"
#include <QRegularExpression>

namespace podlord {
namespace {
int portNumber(const QJsonValue& value) {
    const auto number = value.toInteger(-1);
    return value.isDouble() && value.toDouble() == number && number >= 1 && number <= 65535 ? static_cast<int>(number) : 0;
}
bool runningPod(const QJsonObject& pod, const QString& nameSpace) {
    const auto metadata = pod["metadata"].toObject();
    static const QRegularExpression name("^[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$");
    return (!pod.contains("apiVersion") || pod["apiVersion"] == "v1") && (!pod.contains("kind") || pod["kind"] == "Pod") && metadata["namespace"] == nameSpace
        && name.match(metadata["name"].toString()).hasMatch() && !metadata["uid"].toString().isEmpty()
        && !metadata.contains("deletionTimestamp") && pod["status"].toObject()["phase"] == "Running";
}
}
Result<QString> ResourceClient::startPortForward(const QString& id, const QString& path, int localPort, int remotePort) {
    expire();
    const auto state = states_.constFind(id);
    const auto row = resource(id, path);
    if (state == states_.cend() || !state->open || state->suspended || !enabled_ || !isVisible(id)
        || !containsResource(id, path) || row["apiVersion"] != "v1" || row["uid"].toString().isEmpty()
        || row["namespace"].toString().isEmpty() || (row["kind"] != "Service" && (row["kind"] != "Pod" || row["status"] != "Running")))
        return Failure{StoreError::InvalidInput, "Select a Running core Pod or namespaced core Service in an authenticated session."};
    if (localPort < 1 || localPort > 65535 || remotePort < 1 || remotePort > 65535)
        return Failure{StoreError::InvalidInput, "Both TCP ports must be whole numbers from 1 through 65535."};
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto* transport = new PortForwardTransport(this);
    const auto listener = transport->listen(localPort);
    if (const auto* failure = std::get_if<Failure>(&listener)) { delete transport; return *failure; }
    Forward forward;
    forward.session = id; forward.path = path; forward.uid = row["uid"].toString(); forward.kind = row["kind"].toString();
    forward.name = row["name"].toString(); forward.nameSpace = row["namespace"].toString(); forward.server = state->connection.server;
    forward.localPort = localPort; forward.requestedPort = remotePort; forward.transport = transport;
    forwards_.insert(token, forward); states_[id].forwardError.clear();
    connect(transport, &PortForwardTransport::connectionClosed, this, [this, token](const QString& stream) {
        dropQueued([&](const auto& task) { return task.forwardToken == token && task.streamToken == stream; });
    });
    connect(transport, &PortForwardTransport::connectionRequested, this, [this, token](const QString& stream) {
        const auto current = forwards_.constFind(token); if (current == forwards_.cend()) return;
        Task task; task.id = current->session; task.path = current->podPath + "/portforward";
        task.read = Read::ForwardSocket; task.foreground = true; task.forwardToken = token; task.streamToken = stream;
        if (!enqueue(task)) current->transport->rejectStream(stream);
    });
    connect(transport, &PortForwardTransport::handshakeFinished, this, [this, token](const QString& stream, bool accepted, bool auth) {
        forwardHandshakeFinished(token, stream, accepted, auth);
    });
    connect(transport, &PortForwardTransport::streamFailed, this, [this, token](const QString& message) {
        const auto current = forwards_.constFind(token); if (current == forwards_.cend()) return;
        const auto id = current->session; states_[id].forwardError = message; emit portForwardsChanged(id);
    });
    if (forward.kind == "Pod") {
        QSet<int> declared;
        const auto cached = document(id, path);
        for (const auto& container : cached["spec"].toObject()["containers"].toArray())
            for (const auto& value : container.toObject()["ports"].toArray()) {
                const auto port = value.toObject(); if (port["protocol"].toString("TCP") == "TCP") declared.insert(portNumber(port["containerPort"]));
            }
        declared.remove(0);
        if (!declared.isEmpty() && !declared.contains(remotePort)) {
            failForward(token, "The requested TCP port is not declared by this Pod.");
            return Failure{StoreError::InvalidInput, "The requested TCP port is not declared by this Pod."};
        }
        auto& ready = forwards_[token]; ready.podPath = path; ready.remotePort = remotePort; ready.phase = "Listening";
        transport->targetReady(); emit portForwardsChanged(id); return token;
    }
    Task task; task.id = id; task.path = path; task.read = Read::ForwardTarget; task.kind = forward.kind;
    task.namespaced = true; task.foreground = true; task.forwardToken = token;
    if (!enqueue(task)) { stopPortForward(id, token); return Failure{StoreError::InvalidInput, "The session cannot admit this port-forward request."}; }
    emit portForwardsChanged(id); return token;
}
QVariantList ResourceClient::portForwards(const QString& id) const {
    QVariantList rows;
    for (auto it = forwards_.cbegin(); it != forwards_.cend(); ++it) if (it->session == id)
        rows.append(QVariantMap{{"id", it.key()}, {"path", it->path}, {"uid", it->uid}, {"kind", it->kind}, {"name", it->name},
            {"namespace", it->nameSpace}, {"localPort", it->localPort}, {"remotePort", it->requestedPort}, {"resolvedPort", it->remotePort},
            {"status", it->phase}, {"endpoint", "127.0.0.1:" + QString::number(it->localPort)}});
    return rows;
}
QString ResourceClient::portForwardError(const QString& id) const {
    const auto state = states_.constFind(id);
    return state == states_.cend() ? QString{} : state->forwardError;
}
bool ResourceClient::stopPortForward(const QString& id, const QString& token) {
    const auto current = forwards_.constFind(token);
    if (current == forwards_.cend() || current->session != id) return false;
    auto* transport = current->transport; forwards_.remove(token);
    dropQueued([&](const auto& task) { return task.forwardToken == token; });
    transport->stop(); transport->deleteLater(); emit portForwardsChanged(id); return true;
}
bool ResourceClient::stopSessionForwards(const QString& id) {
    for (const auto& token : forwards_.keys()) if (forwards_.value(token).session == id) stopPortForward(id, token);
    return true;
}
bool ResourceClient::failForward(const QString& token, const QString& message) {
    const auto current = forwards_.constFind(token); if (current == forwards_.cend()) return false;
    const auto id = current->session; states_[id].forwardError = message;
    return stopPortForward(id, token);
}
void ResourceClient::forwardHandshakeFinished(const QString& token, const QString& stream, bool accepted, bool auth) {
    if (!running_ || running_->forwardToken != token || running_->streamToken != stream) return;
    const auto task = *running_;
    auto& state = states_[task.id]; --state.pending; --state.resourcePending;
    if (!accepted) state.forwardError = auth ? "Port-forward authentication failed. Confirm authentication manually."
        : "The port-forward stream could not be opened. Retry explicitly; no automatic retry was sent.";
    if (auth) suspend(state.connection.credentialId, state.forwardError);
    running_.reset(); emit changed(task.id); emit portForwardsChanged(task.id);
    dispatch(); scheduleSync(); scheduleLogs();
}
void ResourceClient::consumeForward(const Task& task, const QJsonObject& document) {
    auto current = forwards_.find(task.forwardToken); if (current == forwards_.end()) return;
    auto& forward = *current;
    if (task.read == Read::ForwardTarget) {
        const auto metadata = document["metadata"].toObject();
        if (document["apiVersion"] != "v1" || document["kind"] != forward.kind || metadata["namespace"] != forward.nameSpace
            || metadata["name"] != forward.name || metadata["uid"] != forward.uid || metadata.contains("deletionTimestamp")) {
            failForward(task.forwardToken, "The port-forward target changed identity or returned an invalid resource. Refresh and select it again."); return;
        }
        {
            const auto selector = document["spec"].toObject()["selector"].toObject();
            static const QRegularExpression key("^(?:[a-z0-9][a-z0-9.-]*/)?[A-Za-z0-9][A-Za-z0-9_.-]*$");
            static const QRegularExpression value("^(?:[A-Za-z0-9][A-Za-z0-9_.-]*)?$");
            QStringList labels;
            for (auto entry = selector.constBegin(); entry != selector.constEnd(); ++entry) {
                if (!key.match(entry.key()).hasMatch() || !entry.value().isString() || !value.match(entry.value().toString()).hasMatch()) {
                    failForward(task.forwardToken, "The Service returned an invalid Pod selector."); return;
                }
                labels.append(entry.key() + '=' + entry.value().toString());
            }
            if (labels.isEmpty()) { failForward(task.forwardToken, "This Service has no Pod selector. Select a Pod directly."); return; }
            forward.service = document; forward.selector = labels.join(',');
            Task pods; pods.id = task.id; pods.path = "/api/v1/namespaces/" + forward.nameSpace + "/pods";
            pods.read = Read::ForwardPods; pods.foreground = true; pods.forwardToken = task.forwardToken; pods.selector = forward.selector;
            if (!enqueue(pods)) failForward(task.forwardToken, "The session cannot admit Service target resolution.");
            return;
        }
    } else {
        if (document["apiVersion"] != "v1" || document["kind"] != "PodList" || !document["items"].isArray() || !document["metadata"].isObject()) {
            failForward(task.forwardToken, "Invalid Service Pod-list response."); return;
        }
        QJsonObject pod;
        const auto selector = forward.service["spec"].toObject()["selector"].toObject();
        for (const auto& value : document["items"].toArray()) {
            const auto candidate = value.toObject(); if (!runningPod(candidate, forward.nameSpace)) continue;
            const auto labels = candidate["metadata"].toObject()["labels"].toObject();
            bool matches = true; for (auto it = selector.constBegin(); it != selector.constEnd(); ++it) matches = matches && labels[it.key()] == it.value();
            if (matches) { pod = candidate; break; }
        }
        if (pod.isEmpty()) {
            const QJsonValue continuation = document["metadata"].toObject()["continue"];
            if (!continuation.isUndefined() && !continuation.isString()) { failForward(task.forwardToken, "Invalid Service Pod-list continuation."); return; }
            if (!continuation.toString().isEmpty()) {
                auto next = task;
                if (next.tokens.contains(continuation.toString())) { failForward(task.forwardToken, "Repeated Service Pod-list continuation."); return; }
                next.tokens.insert(continuation.toString()); next.continuation = continuation.toString();
                if (!enqueue(next)) failForward(task.forwardToken, "The session cannot continue Service target resolution.");
            } else failForward(task.forwardToken, "This Service has no Running backing Pod.");
            return;
        }
        QJsonObject selected;
        for (const auto& value : forward.service["spec"].toObject()["ports"].toArray()) {
            const auto port = value.toObject();
            if (portNumber(port["port"]) == forward.requestedPort && port["protocol"].toString("TCP") == "TCP") { selected = port; break; }
        }
        if (selected.isEmpty()) { failForward(task.forwardToken, "Choose a declared TCP port of this Service."); return; }
        const auto target = selected.value("targetPort");
        forward.remotePort = target.isUndefined() ? portNumber(selected["port"]) : portNumber(target);
        if (target.isString()) {
            for (const auto& container : pod["spec"].toObject()["containers"].toArray())
                for (const auto& value : container.toObject()["ports"].toArray()) {
                    const auto port = value.toObject();
                    if (port["name"] == target && port["protocol"].toString("TCP") == "TCP") forward.remotePort = portNumber(port["containerPort"]);
                }
        }
        if (!forward.remotePort) { failForward(task.forwardToken, "The Service target port could not be resolved to a valid TCP Pod port."); return; }
        forward.podPath = "/api/v1/namespaces/" + forward.nameSpace + "/pods/" + pod["metadata"].toObject()["name"].toString();
    }
    forward.phase = "Listening";
    forward.transport->targetReady(); emit portForwardsChanged(task.id);
}
}
