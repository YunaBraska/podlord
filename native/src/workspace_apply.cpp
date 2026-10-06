#include "workspace.h"
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
bool Workspace::yamlApplyLocked() const { return yamlApply_ && (yamlApply_->phase == "preparing" || yamlApply_->phase == "preview" || yamlApply_->phase == "sending" || yamlApply_->phase == "reading" || yamlApply_->phase == "reconcile"); }
bool Workspace::yamlApplyPreview() const { return yamlApply_ && yamlApply_->phase == "preview"; }
bool Workspace::yamlReconcilePending() const { return yamlApply_ && yamlApply_->phase == "reconcile"; }
QString Workspace::yamlApplyTarget() const { return yamlApply_ ? yamlApply_->target : QString{}; }
QString Workspace::yamlApplyDiff() const { return yamlApply_ ? yamlApply_->prepared.preview : QString{}; }
QString Workspace::yamlComparison() const { return yamlApply_ ? yamlApply_->comparison : QString{}; }
QString Workspace::yamlApplyStatus() const { return yamlApply_ ? yamlApply_->status : QString{}; }
bool Workspace::canReconcileYaml() const {
    return yamlApply_ && yamlDraft_ && !yamlApplyLocked() && !yamlApply_->current.isEmpty()
        && yamlDraft_->text == yamlApply_->text
        && yamlApply_->current["metadata"].toObject()["uid"] == yamlApply_->baseline["metadata"].toObject()["uid"];
}
bool Workspace::canReadBackYaml() const {
    return yamlApply_ && yamlDraft_ && !yamlApplyLocked() && yamlApply_->phase != "canceled" && yamlApply_->phase != "invalid";
}
QVariantList Workspace::yamlConflicts() const {
    QVariantList rows;
    if (!yamlReconcilePending()) return rows;
    for (const auto& path : yamlApply_->conflicts) rows.append(QVariantMap{{"path", path},
        {"choice", yamlApply_->choices.contains(path) ? yamlApply_->choices[path] ? "mine" : "server" : "unresolved"}});
    return rows;
}
bool Workspace::yamlReconcileReady() const { return yamlReconcilePending() && yamlApply_->choices.size() == yamlApply_->conflicts.size(); }
QString Workspace::yamlConflictChoice(int index) const {
    if (!yamlReconcilePending() || index < 0 || index >= yamlApply_->conflicts.size()) return {};
    const auto path = yamlApply_->conflicts[index];
    return yamlApply_->choices.contains(path) ? yamlApply_->choices[path] ? "mine" : "server" : "unresolved";
}
bool Workspace::previewYamlApply() {
    if (!yamlDraft_ || pendingLeave_ || busy_ || yamlChecking_ || authenticationRequired() || authenticationRunning() || yamlApplyLocked()) return false;
    const auto connection = client_.connection(yamlDraft_->session);
    if (!connection || yamlDraft_->session != active_ || yamlDraft_->path != inspectorPath()) return false;
    const auto metadata = yamlDraft_->document["metadata"].toObject();
    YamlApply intent;
    intent.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    intent.session = active_; intent.path = yamlDraft_->path; intent.text = yamlDraft_->text;
    intent.target = "Cluster: " + connection->server.toDisplayString() + "\nSession: " + title()
        + "\nNamespace: " + (metadata["namespace"].toString().isEmpty() ? "(cluster scoped)" : metadata["namespace"].toString())
        + "\nResource: " + yamlDraft_->document["kind"].toString() + '/' + metadata["name"].toString()
        + "\nUID: " + metadata["uid"].toString() + "\nOriginal version: " + metadata["resourceVersion"].toString();
    intent.baseline = yamlDraft_->document;
    intent.phase = "preparing"; intent.status = "Checking the frozen local draft and preparing a bounded patch. Nothing sent.";
    const auto token = intent.token;
    const auto text = yamlDraft_->text; const auto baseline = yamlDraft_->document; const auto hidden = yamlDraft_->hiddenDraft;
    const qint64 limit = static_cast<qint64>(settings_.yamlLimitMiB) * 1048576;
    yamlApply_ = std::move(intent); emit yamlApplyChanged();
    auto* watcher = new QFutureWatcher<Result<PreparedYaml>>(this);
    connect(watcher, &QFutureWatcher<Result<PreparedYaml>>::finished, this, [this, watcher, token] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (!yamlApply_ || yamlApply_->token != token || !yamlDraft_) return;
        if (pendingLeave_) { yamlApply_->phase = "canceled"; yamlApply_->status = "Preview canceled during pending navigation. Draft retained; nothing sent."; }
        else if (const auto* failure = std::get_if<Failure>(&result)) {
            yamlApply_->phase = "invalid"; yamlApply_->status = failure->message; yamlCheckStatus_ = failure->message; emit yamlCheckChanged();
        } else {
            yamlApply_->prepared = std::get<PreparedYaml>(result); yamlApply_->phase = "preview";
            yamlApply_->status = "Preview only. Nothing sent. Confirm Apply to write to this target.";
        }
        emit yamlApplyChanged();
    });
    watcher->setFuture(QtConcurrent::run([text, baseline, hidden, limit] { return prepareYaml(text, baseline, limit, hidden); }));
    return true;
}
bool Workspace::confirmYamlApply(bool confirmed) {
    if (!yamlApplyPreview()) return false;
    if (!confirmed) { yamlApply_->phase = "canceled"; yamlApply_->status = "Preview canceled. Draft retained; nothing sent."; emit yamlApplyChanged(); return true; }
    if (!yamlDraft_ || pendingLeave_ || yamlApply_->session != active_ || yamlApply_->path != inspectorPath() || yamlApply_->text != yamlDraft_->text) return false;
    yamlApply_->phase = "sending"; yamlApply_->status = "Confirmed write queued or running. No automatic retry.";
    yamlFresh_ = false; emit yamlApplyChanged(); emit yamlEditChanged();
    const auto intent = *yamlApply_;
    if (client_.applyYaml(intent.session, intent.path, intent.token, intent.baseline, intent.prepared.document, intent.prepared.patch)) return true;
    yamlApply_->phase = "rejected"; yamlApply_->status = "Nothing sent: target, credentials or request policy changed. Draft retained. Read back before retrying.";
    emit yamlApplyChanged(); return false;
}
bool Workspace::readBackYaml() {
    if (!canReadBackYaml() || pendingLeave_) return false;
    const auto intent = *yamlApply_;
    yamlApply_->phase = "reading"; yamlApply_->status = "Reading the original resource only. No write will be retried."; emit yamlApplyChanged();
    if (client_.readBackYaml(intent.session, intent.path, intent.token, intent.baseline, intent.prepared.document, intent.phase)) return true;
    yamlApply_->phase = "uncertain"; yamlApply_->status = "Read-back unavailable. Confirm authentication if needed; no write was retried.";
    emit yamlApplyChanged(); return false;
}
bool Workspace::reconcileYaml() {
    if (!canReconcileYaml() || pendingLeave_ || yamlDraft_->text != yamlApply_->text) return false;
    const auto result = mergeYaml(yamlApply_->baseline, yamlApply_->prepared.document, yamlApply_->current);
    yamlApply_->phase = "reconcile"; yamlApply_->conflicts = result.conflicts; yamlApply_->choices.clear();
    yamlApply_->comparison = "ORIGINAL RESOURCE\n" + resourceYaml(yamlApply_->baseline)
        + "\nCURRENT SERVER RESOURCE\n" + resourceYaml(yamlApply_->current)
        + "\nYOUR REQUESTED FIELD CHANGES\n" + yamlApply_->prepared.preview;
    yamlApply_->status = "Compare original and current YAML. Choose every overlap, then prepare a new preview. Nothing sent.";
    emit yamlConflictsChanged(); emit yamlApplyChanged(); return true;
}
bool Workspace::chooseYamlConflict(int index, bool mine) {
    if (!yamlReconcilePending() || index < 0 || index >= yamlApply_->conflicts.size()) return false;
    yamlApply_->choices[yamlApply_->conflicts[index]] = mine; emit yamlApplyChanged(); return true;
}
bool Workspace::finishYamlReconcile(bool confirmed) {
    if (!yamlReconcilePending()) return false;
    if (!confirmed) { yamlApply_->phase = "conflict"; emit yamlConflictsChanged(); emit yamlApplyChanged(); return true; }
    if (!yamlReconcileReady() || !yamlDraft_) return false;
    const auto result = mergeYaml(yamlApply_->baseline, yamlApply_->prepared.document, yamlApply_->current, yamlApply_->choices);
    if (!result.conflicts.isEmpty()) return false;
    yamlDraft_->document = yamlApply_->current; yamlDraft_->baseline = resourceYaml(yamlApply_->current);
    yamlDraft_->hiddenDraft = result.document;
    yamlDraft_->text = resourceYaml(result.document);
    yamlApply_.reset(); emit yamlConflictsChanged(); emit yamlApplyChanged(); emit yamlTextChanged(); emit yamlEditChanged();
    return previewYamlApply();
}
bool Workspace::saveYamlLimit(const QString& value) {
    static const QRegularExpression integer("^[1-9][0-9]*$");
    bool valid = false; const int limit = value.toInt(&valid);
    if (!valid || !integer.match(value).hasMatch()) { settingsError_ = "YAML limit must be a positive whole number of MiB."; emit changed(); return false; }
    auto desired = settings_; desired.yamlLimitMiB = limit; return savePolicy(desired);
}
bool Workspace::bindYamlApply() {
    connect(this, &Workspace::yamlTextChanged, this, &Workspace::yamlApplyChanged);
    connect(&client_, &ResourceClient::yamlApplyFinished, this, [this](const QString& session, const QString& path, const QString& token,
                                                                   const QString& outcome, const QString& message, const QJsonObject& current) {
        if (!yamlApply_ || yamlApply_->token != token || !yamlDraft_ || yamlDraft_->session != session || yamlDraft_->path != path) return;
        yamlApply_->phase = outcome; yamlApply_->status = message; yamlApply_->current = current;
        if (outcome == "applied") {
            yamlApply_->prepared = {}; yamlApply_->baseline = {}; yamlApply_->current = {};
            yamlApply_->text.clear(); yamlApply_->comparison.clear(); yamlApply_->conflicts.clear(); yamlApply_->choices.clear();
            emit yamlConflictsChanged();
            yamlDraft_.reset(); emit yamlTextChanged(); emit yamlEditChanged(); publishInspector();
        }
        emit yamlApplyChanged();
    });
    return true;
}
} // namespace podlord
