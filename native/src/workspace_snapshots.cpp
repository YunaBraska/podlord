#include "workspace.h"
#include "workspace_runtime.h"
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace podlord {
void ResourceTable::prepare(QPromise<PreparedSnapshot>& promise, QJsonArray source, QString cluster, int sortColumn) {
    if (promise.isCanceled()) return;
    ResourceTable table;
    table.publish(source, cluster);
    PreparedSnapshot result;
    result.source = std::move(source); result.cluster = std::move(cluster);
    result.fields = table.fields_; result.identity = table.identityField_;
    int healthy = 0, warning = 0, critical = 0;
    for (int row = 0; row < table.rowCount(); ++row) {
        if (promise.isCanceled()) return;
        auto& entry = table.rows_[row];
        const auto& document = std::as_const(entry.document);
        entry.identity = document[table.identityField_].toString();
        const auto metric = resourceMetricContribution(document);
        if (metric.group >= 0) entry.metrics = std::make_shared<const ResourceMetricContribution>(metric);
        for (int column = 0; column < table.columnCount(); ++column)
            if (table.fields_[column] != "createdAt") table.data(table.index(row, column), Qt::DisplayRole);
        for (int column = 0; column < 4; ++column)
            if (document[table.fields_[column]].isString()) entry.metadataStrings |= 1 << column;
        entry.prepared = true;
        if (document["kubernetesEvent"].toBool()) result.events.append(document);
        const int severity = document["problemSeverity"].toInt();
        if (severity == 2) ++critical;
        else if (severity == 1) ++warning;
        else ++healthy;
    }
    if (promise.isCanceled()) return;
    table.prepareSort(sortColumn);
    result.rows = std::move(table.rows_); result.sortValues = std::move(table.sortValues_);
    result.sortColumn = table.sortValueColumn_;
    result.health = {{"total", result.source.size()}, {"healthy", healthy}, {"warning", warning}, {"critical", critical}};
    promise.addResult(std::move(result));
}

bool ResourceTable::publishPrepared(const PreparedSnapshot& snapshot, bool replace) {
    // Prepared projections have the real resource schema, not inspector/value-table schemas.
    if (fields_ != snapshot.fields || identityField_ != snapshot.identity || preserveInputOrder_) return false;
    const bool unchanged = cluster_ == snapshot.cluster && rows_ == snapshot.rows;
    if (!replace && !unchanged) return false;
    const int previous = rowCount(), next = int(snapshot.rows.size());
    const int retained = std::min(previous, next);
    if (replace && next != previous) {
        // Structural observers must see the incoming retained values, while Qt's
        // before/after checks require them not to change inside begin/end.
        for (int row = 0; row < retained; ++row) rows_[row] = snapshot.rows[row];
        cluster_ = snapshot.cluster;
        sortValues_ = snapshot.sortValues; sortValueColumn_ = snapshot.sortColumn;
    }
    if (replace && next > previous) {
        beginInsertRows({}, previous, next - 1);
        for (int row = previous; row < next; ++row) rows_.append(snapshot.rows[row]);
        endInsertRows();
    } else if (replace && next < previous) {
        beginRemoveRows({}, next, previous - 1);
        rows_.resize(next);
        endRemoveRows();
    }
    // Every retained display field is complete and immutable. Age is always evaluated live.
    rows_ = snapshot.rows; cluster_ = snapshot.cluster;
    sortValues_ = snapshot.sortValues; sortValueColumn_ = snapshot.sortColumn;
    if (replace && !unchanged && retained > 0) emit dataChanged(index(0, 0), index(retained - 1, columnCount() - 1));
    return true;
}

ResourceMetricSummary& ResourceTable::accumulateMetrics(int row, ResourceMetricSummary& summary) const {
    if (row < 0 || row >= rows_.size()) return summary;
    const auto& entry = rows_[row];
    if (entry.metrics) return summary.add(*entry.metrics);
    return entry.prepared ? summary : summary.add(resourceMetricContribution(entry.document));
}

bool ResourceTable::retainPreparedSort(PreparedSnapshot& snapshot) const {
    if (fields_ != snapshot.fields || identityField_ != snapshot.identity || cluster_ != snapshot.cluster || rows_ != snapshot.rows) return false;
    snapshot.sortValues = sortValues_; snapshot.sortColumn = sortValueColumn_;
    return true;
}

void Workspace::prepareSnapshot() {
    if (windowClosed_ || !windowVisible_ || active_.isEmpty() || runtime_->owners.value(active_) != this) {
        if (snapshotPreparation_) snapshotPreparation_->watcher->cancel();
        return;
    }
    const auto source = client_.rows(active_);
    const auto cluster = activeCluster();
    const auto prepared = preparedSnapshots_.constFind(active_);
    const bool ready = prepared != preparedSnapshots_.cend() && prepared->source == source && prepared->cluster == cluster;
    if (snapshotPreparation_) {
        if (snapshotPreparation_->session == active_ && (snapshotPreparation_->source != source || snapshotPreparation_->cluster != cluster))
            snapshotPreparation_->watcher->cancel();
        return;
    }
    if (ready || source.isEmpty()) return;
    const auto session = active_;
    auto* watcher = new QFutureWatcher<ResourceTable::PreparedSnapshot>(this);
    snapshotPreparation_ = SnapshotPreparation{session, cluster, source, watcher};
    connect(watcher, &QFutureWatcher<ResourceTable::PreparedSnapshot>::finished, this, [this, watcher, session] {
        snapshotPreparation_.reset();
        if (!watcher->isCanceled() && watcher->future().resultCount() == 1 && !windowClosed_ && windowVisible_
            && runtime_->owners.value(session) == this) {
            auto result = watcher->result();
            if (result.source == client_.rows(session) && result.cluster == activeCluster(session)) {
                if (active_ == session) rows_.retainPreparedSort(result);
                preparedSnapshots_.insert(session, result);
                if (active_ == session) rows_.publishPrepared(result);
            }
        }
        watcher->deleteLater();
        prepareSnapshot();
    });
    watcher->setFuture(QtConcurrent::run(&ResourceTable::prepare, source, cluster, currentNavigation().column));
}
}
