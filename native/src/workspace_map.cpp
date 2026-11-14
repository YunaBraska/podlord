#include "workspace.h"
#include "workspace_runtime.h"
#include <algorithm>
#include <QClipboard>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QHash>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include <limits>

namespace podlord {
void Workspace::refreshMapContexts(bool reorder) {
    QVariantList next;
    for (const auto& value : contexts()) if (value.toMap().value("current").toBool()) next.append(value);
    QHash<QString, qsizetype> positions;
    for (qsizetype index = 0; index < mapContexts_.size(); ++index)
        positions.insert(mapContexts_[index].toMap().value("id").toString(), index);
    std::stable_sort(next.begin(), next.end(), [&](const auto& left, const auto& right) {
        const auto a = left.toMap().value("id").toString(), b = right.toMap().value("id").toString();
        if (!reorder) return positions.value(a, std::numeric_limits<qsizetype>::max())
            < positions.value(b, std::numeric_limits<qsizetype>::max());
        const auto first = runtime_->contextUsage.value(a), second = runtime_->contextUsage.value(b);
        if (first.count != second.count) return first.count > second.count;
        return first.lastUsed > second.lastUsed;
    });
    if (next == mapContexts_) return;
    mapContexts_ = std::move(next);
    emit mapContextsChanged();
}
void WorkspaceRuntime::recordContextUse(const QString& context) {
    auto& use = contextUsage[context];
    if (use.count < std::numeric_limits<qint64>::max()) ++use.count;
    use.lastUsed = std::max(use.lastUsed, now().toUTC());
    auto& pending = pendingUsage[context];
    if (pending.count < std::numeric_limits<qint64>::max()) ++pending.count;
    pending.lastUsed = use.lastUsed;
    dispatchUsageSave();
}
bool WorkspaceRuntime::dispatchUsageSave() {
    if (usageSaving || pendingUsage.isEmpty()) return false;
    usageSaving = true;
    savingUsage = std::move(pendingUsage); pendingUsage.clear();
    auto* watcher = new QFutureWatcher<Result<ContextUsageMap>>(&client);
    QObject::connect(watcher, &QFutureWatcher<Result<ContextUsageMap>>::finished, &client, [this, watcher] {
        watcher->deleteLater();
        if (finishUsageSave()) dispatchUsageSave();
    });
    usageSaveFuture = QtConcurrent::run([profile = profile, increments = savingUsage] { return recordContextUsage(profile, increments); });
    watcher->setFuture(usageSaveFuture);
    return true;
}
bool WorkspaceRuntime::finishUsageSave() {
    const auto result = usageSaveFuture.result();
    usageSaving = false;
    if (const auto* failure = std::get_if<Failure>(&result)) {
        for (auto use = savingUsage.begin(); use != savingUsage.end(); ++use) {
            auto& pending = pendingUsage[use.key()];
            pending.count += std::min(use->count, std::numeric_limits<qint64>::max() - pending.count);
            pending.lastUsed = std::max(pending.lastUsed, use->lastUsed);
        }
        savingUsage.clear();
        for (auto* window : windows) { window->error_ = failure->message; emit window->changed(); }
        return false;
    }
    // Counts were published on entry. A late completion must not restore removed ranks.
    savingUsage.clear();
    return true;
}
void WorkspaceRuntime::flushUsageSaves() {
    while (usageSaving || !pendingUsage.isEmpty()) {
        if (!usageSaving) dispatchUsageSave();
        usageSaveFuture.waitForFinished();
        if (!finishUsageSave()) { qWarning("Context usage could not be saved during shutdown."); break; }
    }
}
bool Workspace::mapOverview() const { return active_.isEmpty() || mapOverview_; }
QString Workspace::mapCatalogWarning() const {
    return sourceCatalogFailed_ || !sources_.errors.isEmpty() ? QStringLiteral("Source errors: cards may be older.") : QString{};
}
QString Workspace::mapContextCaption() const {
    const auto identity = mapContextIdentity();
    if (identity.isEmpty()) return title();
    return identity.value("name") == identity.value("cluster") ? identity.value("name").toString()
        : identity.value("name").toString() + " / " + identity.value("cluster").toString();
}
QVariantMap Workspace::mapContextIdentity() const {
    const auto session=std::find_if(catalog_.sessions.cbegin(),catalog_.sessions.cend(),[&](const auto& entry) { return entry.id==QUuid(active_); });
    if (session!=catalog_.sessions.cend()) for (const auto& source:sources_.sources) for (const auto& context:source.contexts)
        if (context.id==session->config.contextId) return {{"id", context.id}, {"name", context.displayName}, {"cluster", context.cluster}};
    return {};
}
bool Workspace::copyMapIdentity(const QString& field) {
    if (mapOverview() || (field != "name" && field != "cluster")) return false;
    const auto text = mapContextIdentity().value(field).toString();
    if (text.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(text);
    return true;
}
bool Workspace::showMapOverview() {
    if (busy_ || pendingLeave_) return false;
    const bool opening = workspacePage() != "map" || !mapOverview();
    navigation_[active_].workspace="map";
    client_.showSession({},viewId_); client_.hideLogs(viewId_);
    mapOverview_ = true;
    if (opening) refreshMapContexts(true);
    emit changed(); return true;
}
void Workspace::rememberMapSession(bool visit) {
    if (restoringMap_ || !visit) return;
    mapOverview_ = active_.isEmpty();
    if (!active_.isEmpty()) {
        pendingMapPreview_ = active_;
        if (mapPreviewNeeded()) emit mapPreviewRequested(active_);
    }
}
QString Workspace::mapPreviewUrl(const QString& context) const { return runtime_->previews.url(context); }
void Workspace::reconcileMapPreviews() {
    QSet<QString> contexts;
    for (const auto& source:sources_.sources) for (const auto& context:source.contexts) contexts.insert(context.id);
    runtime_->previews.reconcile(std::move(contexts),sources_.errors.isEmpty());
}
QString Workspace::mapPreviewError() const { return runtime_->previews.error(); }
quint64 Workspace::mapPreviewRevision() const { return runtime_->previews.revision(); }
bool Workspace::mapPreviewNeeded() const {
    return !busy_ && !active_.isEmpty() && pendingMapPreview_==active_ && client_.initialSyncComplete(active_)
        && !client_.syncLoading(active_) && client_.collectionSnapshotAvailable(active_);
}
bool Workspace::captureMapPreview(const QString& session,const QImage& image) {
    if (session!=active_ || !mapPreviewNeeded()) return false;
    const auto connection=client_.connection(session);
    if (!connection || !runtime_->previews.save(connection->contextId,image)) return false;
    pendingMapPreview_.clear(); return true;
}
}
