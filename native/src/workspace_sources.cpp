#include "workspace.h"
#include "workspace_runtime.h"
#include <QClipboard>
#include <QLocale>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
namespace {
QVariantMap sourceInspectionMetadata(const SourceSnapshot& source, const SourceContext& context) {
    QVariantList fields;
    for (const auto& entry : QList<QPair<QString,QString>>{
        {"Name",context.displayName},{"Context",context.name},{"Cluster",context.cluster},{"Server",context.server},
        {"User",context.user},{"Authentication",context.authType},{"Source",source.sourcePath},{"Owned copy",source.ownedPath},
        {"Imported",QLocale::system().toString(source.importedAt.toLocalTime(),QLocale::ShortFormat)},{"Hash",source.contentHash},{"Context ID",context.id},
        {"Detail",context.brokenReferences.join(", ")}})
        fields.append(QVariantMap{{"label",entry.first},{"value",entry.second.isEmpty() ? QStringLiteral("-") : entry.second}});
    return {{"contextId",context.id},{"snapshotId",source.id},
        {"name",context.displayName},{"context",context.name},{"fields",fields}};
}
}
void Workspace::clearSourceInspection() {
    sourceRead_=nullptr;
    if (!sourceInspection_) return;
    sourceInspection_.reset();
    emit sourceInspectionChanged(); emit sourceDraftChanged(); emit yamlEditChanged();
}
bool Workspace::inspectSourceContext(const QString& contextId) {
    if (busy_ || yamlApplyLocked() || deletionPending() || deletionRunning()) return false;
    for (const auto& source : sources_.sources) for (const auto& context : source.contexts) {
        if (context.id!=contextId) continue;
        const auto metadata=sourceInspectionMetadata(source,context);
        const auto snapshot=source.id;
        if (!allowLeave(Leave::Source,contextId) || !closeInspector()) return false;
        sourceInspection_=SourceInspection{metadata,{},{},{},true,false};
        emit sourceInspectionChanged(); emit sourceDraftChanged(); emit changed();
        auto* watcher=new QFutureWatcher<Result<QString>>(this);
        sourceRead_=watcher;
        connect(watcher,&QFutureWatcher<Result<QString>>::finished,this,[this,watcher] {
            const auto result=watcher->result(); watcher->deleteLater();
            if (sourceRead_!=watcher) return;
            sourceRead_=nullptr; sourceInspection_->loading=false;
            if (const auto* failure=std::get_if<Failure>(&result)) {
                sourceInspection_->error=failure->message;
            } else {
                sourceInspection_->yaml=std::get<QString>(result);
            }
            emit sourceInspectionChanged();
        });
        const auto profile=profile_;
        watcher->setFuture(QtConcurrent::run([profile,snapshot,contextId] { return KubeconfigStore(profile).ownedYaml(snapshot,contextId); }));
        return true;
    }
    return false;
}
bool Workspace::canEditSourceYaml() const {
    if (!sourceInspection_ || sourceInspection_->loading || sourceInspection_->yaml.isEmpty() || busy_ || pendingLeave_) return false;
    const auto context=sourceInspection_->metadata.value("contextId");
    for (const auto* owner : runtime_->windows)
        if (owner->busy_ && owner->sourceRemoval_.value("contextId")==context) return false;
    return true;
}
bool Workspace::beginSourceEdit() {
    if (sourceEditing() || !canEditSourceYaml()) return false;
    sourceInspection_->draft=sourceInspection_->yaml; sourceInspection_->editing=true;
    emit sourceDraftChanged(); emit yamlEditChanged(); return true;
}
bool Workspace::setSourceDraft(const QString& text) {
    if (!sourceEditing() || busy_ || pendingLeave_) return false;
    if (sourceInspection_->draft==text) return true;
    const bool dirty=yamlDirty(); sourceInspection_->draft=text;
    emit sourceDraftChanged(); if (dirty!=yamlDirty()) emit yamlEditChanged();
    return true;
}
bool Workspace::applySourceDraft() {
    if (!sourceEditing() || busy_ || pendingLeave_) return false;
    const auto snapshot=sourceInspection_->metadata.value("snapshotId").toString();
    const auto contextId=sourceInspection_->metadata.value("contextId").toString();
    const auto context=sourceInspection_->metadata.value("context").toString();
    const auto draft=sourceInspection_->draft;
    sourceInspection_->error.clear(); sourceImportError_.clear(); sourceImportNotice_.clear();
    busy_=true; emit sourceInspectionChanged(); emit changed();
    auto* watcher=new QFutureWatcher<Result<SourceSnapshot>>(this);
    connect(watcher,&QFutureWatcher<Result<SourceSnapshot>>::finished,this,[this,watcher,context,draft] {
        const auto result=watcher->result(); watcher->deleteLater(); busy_=false;
        if (const auto* failure=std::get_if<Failure>(&result)) {
            sourceInspection_->error=failure->message;
            sourceImportError_=failure->message;
            emit sourceInspectionChanged(); emit changed(); emit sourceImportFinished(false); return;
        }
        const auto& source=std::get<SourceSnapshot>(result);
        const auto selected=std::find_if(source.contexts.cbegin(),source.contexts.cend(),[&](const auto& value) { return value.name==context; });
        const auto& target=selected==source.contexts.cend() ? source.contexts.first() : *selected;
        sourceInspection_=SourceInspection{sourceInspectionMetadata(source,target),draft,{},{},false,false};
        sourceImportNotice_.clear();
        emit sourceInspectionChanged(); emit sourceDraftChanged(); emit yamlEditChanged();
        reloadCatalogs(false); emit sourceImportFinished(true);
    });
    const auto profile=profile_;
    sourceEditFuture_=QtConcurrent::run([profile,snapshot,contextId,draft] { return KubeconfigStore(profile).editOwnedYaml(snapshot,contextId,draft); });
    watcher->setFuture(sourceEditFuture_);
    return true;
}
bool Workspace::refreshSourceTable() {
    QJsonArray rows;
    for (const auto& value : contexts()) {
        auto row = QJsonObject::fromVariantMap(value.toMap());
        row["status"] = row["usable"].toBool() ? "Supported" : row["detail"].toString();
        row["rename"] = "Rename"; row["remove"] = "Remove";
        rows.append(row);
    }
    sourceRows_.setAppearance(appearance_);
    sourceRows_.publish(rows);
    return true;
}
QVariantList Workspace::sourceColumns() const { return tableColumns("source", sourceRows_); }
bool Workspace::sortSourceColumn(int column) {
    if (column < 0 || column >= 8) return false;
    if (sourceTable_.sortColumn() != column) { sourceTable_.sort(column, Qt::AscendingOrder); }
    else if (sourceTable_.sortOrder() == Qt::AscendingOrder) { sourceTable_.sort(column, Qt::DescendingOrder); }
    else { sourceTable_.sort(-1); }
    emit sourcesPresentationChanged();
    return true;
}
bool Workspace::copySourceCell(const QString& identity, int column) {
    if (identity.isEmpty() || column < 0 || column >= sourceRows_.columnCount() || !QGuiApplication::clipboard()) return false;
    for (int row = 0; row < sourceRows_.rowCount(); ++row) {
        if (sourceRows_.index(row,0).data(Qt::UserRole) != identity) continue;
        QGuiApplication::clipboard()->setText(sourceRows_.index(row,column).data().toString());
        return true;
    }
    return false;
}
}
