#include "workspace.h"
#include "workspace_runtime.h"
#include <algorithm>

namespace podlord {
bool Workspace::mapOverview() const { return active_.isEmpty() || mapHistory_.value(mapHistoryIndex_).isEmpty(); }
QString Workspace::mapCatalogWarning() const {
    return sourceCatalogFailed_ || !sources_.errors.isEmpty() ? QStringLiteral("Source errors: cards may be older.") : QString{};
}
QString Workspace::mapContextCaption() const {
    const auto session=std::find_if(catalog_.sessions.cbegin(),catalog_.sessions.cend(),[&](const auto& entry) { return entry.id==QUuid(active_); });
    if (session!=catalog_.sessions.cend()) for (const auto& source:sources_.sources) for (const auto& context:source.contexts)
        if (context.id==session->config.contextId) return context.displayName==context.cluster ? context.displayName : context.displayName+" / "+context.cluster;
    return title();
}
bool Workspace::canMapBack() const { return mapHistoryIndex_>0; }
bool Workspace::canMapForward() const { return mapHistoryIndex_+1<mapHistory_.size(); }
bool Workspace::showMapOverview() {
    if (busy_ || pendingLeave_) return false;
    navigation_[active_].workspace="map";
    client_.showSession({},viewId_); client_.hideLogs(viewId_);
    if (!mapOverview()) { mapHistory_=mapHistory_.mid(0,mapHistoryIndex_+1); mapHistory_.append(QString{}); ++mapHistoryIndex_; }
    emit changed(); return true;
}
bool Workspace::navigateMap(int step) {
    if (busy_ || pendingLeave_ || (step!=-1 && step!=1)) return false;
    int target=mapHistoryIndex_+step;
    while (target>=0 && target<mapHistory_.size()) {
        const auto session=mapHistory_[target];
        if (session.isEmpty() || std::any_of(catalog_.sessions.cbegin(),catalog_.sessions.cend(),[&](const auto& entry) { return entry.id==QUuid(session); })) {
            if (!session.isEmpty() && session!=active_ && !allowLeave(Leave::MapHistory,session,target)) return false;
            return visitMapHistory(session,target);
        }
        target+=step;
    }
    return false;
}
bool Workspace::visitMapHistory(const QString& session,int index) {
    if (index<0 || index>=mapHistory_.size() || mapHistory_[index]!=session) return false;
    if (!session.isEmpty() && std::none_of(catalog_.sessions.cbegin(),catalog_.sessions.cend(),[&](const auto& entry) { return entry.id==QUuid(session); })) return false;
    mapNavigating_=true;
    const bool accepted=session.isEmpty() || activate(session);
    mapNavigating_=false;
    if (!accepted || (!session.isEmpty() && active_!=session)) return false;
    mapHistoryIndex_=index; navigation_[active_].workspace="map";
    if (!session.isEmpty()) rememberMapSession();
    client_.showSession(session,viewId_);
    if (session.isEmpty()) client_.hideLogs(viewId_);
    emit changed(); return true;
}
void Workspace::rememberMapSession(bool visit) {
    if (mapNavigating_) return;
    if (visit && !active_.isEmpty()) {
        pendingMapPreview_=active_;
        if (mapPreviewNeeded()) emit mapPreviewRequested(active_);
    }
    QSet<QUuid> sessions;
    for (const auto& session:catalog_.sessions) sessions.insert(session.id);
    QStringList retained; int current=0;
    for (int index=0;index<mapHistory_.size();++index) {
        const auto& session=mapHistory_[index];
        if (session.isEmpty() || sessions.contains(QUuid(session))) {
            if (retained.isEmpty() || retained.last()!=session) retained.append(session);
        }
        if (index<=mapHistoryIndex_) current=std::max(0,int(retained.size())-1);
    }
    mapHistory_=retained.isEmpty() ? QStringList{QString{}} : retained; mapHistoryIndex_=current;
    if (!visit) return;
    if (active_.isEmpty()) {
        if (!mapHistory_.value(mapHistoryIndex_).isEmpty()) { mapHistory_.append(QString{}); mapHistoryIndex_=int(mapHistory_.size())-1; }
        return;
    }
    if (workspacePage()!="map" || mapHistory_.value(mapHistoryIndex_)==active_) return;
    mapHistory_=mapHistory_.mid(0,mapHistoryIndex_+1); mapHistory_.append(active_); ++mapHistoryIndex_;
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
