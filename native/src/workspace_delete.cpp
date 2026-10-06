#include "workspace.h"

namespace podlord {
bool Workspace::deletionPending() const { return deletion_ && deletion_->phase == "preview"; }
bool Workspace::deletionRunning() const { return deletion_ && (deletion_->phase == "sending" || deletion_->phase == "reading"); }
QString Workspace::deletionTarget() const { return deletion_ ? deletion_->target : QString{}; }
QString Workspace::deletionStatus() const {
    if (deletion_ && deletion_->session == active_ && deletion_->path == inspectorPath()) return deletion_->status;
    return client_.deletionNeedsObservation(active_, inspectorPath())
        ? "Previous deletion outcome remains uncertain. Refresh the current target before another confirmation; no deletion was retried." : QString{};
}
bool Workspace::canDeleteResource() const {
    if (busy_ || pendingLeave_ || yamlEditing() || yamlApplyLocked() || authenticationRequired() || authenticationRunning()
        || !windowVisible_ || deletionPending() || deletionRunning()) return false;
    if (client_.deletionNeedsObservation(active_, inspectorPath())) return false;
    const auto row = client_.resource(active_, inspectorPath());
    return row["deletable"].toBool() && !row["uid"].toString().isEmpty();
}
bool Workspace::canReadBackDeletion() const {
    return deletion_ && deletion_->session == active_ && deletion_->path == inspectorPath()
        && !deletionPending() && !deletionRunning() && (deletion_->phase == "uncertain" || deletion_->phase == "acknowledged")
        && !authenticationRequired() && !authenticationRunning() && !pendingLeave_;
}
bool Workspace::previewDeletion() {
    if (!canDeleteResource()) return false;
    const auto connection = client_.connection(active_);
    if (!connection) return false;
    const auto row = client_.resource(active_, inspectorPath());
    Deletion intent;
    intent.token = QUuid::createUuid().toString(QUuid::WithoutBraces); intent.session = active_; intent.path = inspectorPath();
    intent.baseline = {{"apiVersion", row["apiVersion"]}, {"kind", row["kind"]}, {"name", row["name"]}, {"namespace", row["namespace"]}, {"uid", row["uid"]}};
    intent.server = connection->server;
    intent.target = "Cluster: " + connection->server.toDisplayString() + "\nSession: " + title()
        + "\nNamespace: " + (row["namespace"].toString().isEmpty() ? "(cluster scoped)" : row["namespace"].toString())
        + "\nResource: " + row["kind"].toString() + '/' + row["name"].toString() + "\nUID: " + row["uid"].toString();
    intent.phase = "preview"; intent.status = "Confirmation only. Nothing sent.";
    deletion_ = std::move(intent); emit changed(); return true;
}
bool Workspace::confirmDeletion(bool confirmed) {
    if (!deletionPending()) return false;
    const auto connection = client_.connection(active_);
    if (!confirmed || busy_ || pendingLeave_ || yamlEditing() || active_ != deletion_->session || inspectorPath() != deletion_->path
        || !connection || connection->server != deletion_->server) {
        deletion_->phase = "canceled"; deletion_->status = "Confirmation canceled. Nothing deleted."; emit changed(); return !confirmed;
    }
    deletion_->phase = "sending"; deletion_->status = "Confirmed deletion queued or running. No automatic retry or force-delete.";
    const auto intent = *deletion_; emit changed();
    if (client_.deleteResource(intent.session, intent.path, intent.token, intent.baseline)) return true;
    deletion_->phase = "rejected"; deletion_->status = "Nothing sent: target, credentials or request policy changed. Request a new confirmation.";
    emit changed(); return false;
}
bool Workspace::readBackDeletion() {
    if (!canReadBackDeletion()) return false;
    const auto connection = client_.connection(active_);
    if (!connection || connection->server != deletion_->server) return false;
    deletion_->phase = "reading"; deletion_->status = "Reading the original target only. No deletion will be retried.";
    const auto intent = *deletion_; emit changed();
    if (client_.readBackDeletion(intent.session, intent.path, intent.token, intent.baseline)) return true;
    deletion_->phase = "uncertain"; deletion_->status = "Deletion outcome uncertain; read-back unavailable. No deletion was retried.";
    emit changed(); return false;
}
bool Workspace::cancelPendingDeletion() {
    if (!deletion_) return false;
    if (deletionPending()) {
        deletion_->phase = "canceled"; deletion_->status = "Confirmation canceled by navigation. Nothing deleted."; emit changed(); return true;
    }
    return deletion_->phase == "sending" && client_.cancelDeletion(deletion_->token);
}
bool Workspace::bindDeletion() {
    connect(this, &Workspace::changed, this, &Workspace::deletionAvailabilityChanged);
    connect(this, &Workspace::yamlEditChanged, this, &Workspace::deletionAvailabilityChanged);
    connect(this, &Workspace::yamlApplyChanged, this, &Workspace::deletionAvailabilityChanged);
    connect(&client_, &ResourceClient::detailFinished, this, [this](const QString& session, const QString& path, bool accepted) {
        if (!accepted || !deletion_ || deletion_->session != session || deletion_->path != path || deletionRunning()
            || (deletion_->phase != "uncertain" && deletion_->phase != "acknowledged")) return;
        deletion_->status = (deletion_->phase == "acknowledged" ? QString("Deletion acknowledged. ") : QString("Original deletion outcome remains uncertain. "))
            + "Current target was read successfully. Another deletion requires new confirmation; no deletion was retried.";
        emit changed();
    });
    connect(&client_, &ResourceClient::resourceDeleteFinished, this,
        [this](const QString& session, const QString& path, const QString& token, const QString& phase, const QString& message) {
            if (!deletion_ || deletion_->session != session || deletion_->path != path || deletion_->token != token) return;
            deletion_->phase = phase; deletion_->status = message;
            emit changed();
        });
    return true;
}
} // namespace podlord
