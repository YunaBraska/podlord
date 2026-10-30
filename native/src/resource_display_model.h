#pragma once
#include "resource_metrics.h"
#include <QSortFilterProxyModel>

namespace podlord {
/** Display-only cap over the complete, already filtered and sorted cache. */
class ResourceDisplayModel final : public QSortFilterProxyModel {
public:
    explicit ResourceDisplayModel(QAbstractItemModel* source) {
        setSourceModel(source);
        // Eligibility depends on row position after sorting and removals.
        connect(source, &QAbstractItemModel::layoutChanged, this, [this, source] {
            if (source->rowCount() > limit_) refreshRows();
        });
        // Only shifts inside the capped prefix can change existing eligibility.
        connect(source, &QAbstractItemModel::rowsInserted, this, [this, source](const QModelIndex&, int first, int) {
            if (first < limit_ && source->rowCount() > limit_) refreshRows();
        });
        connect(source, &QAbstractItemModel::rowsRemoved, this, [this, source](const QModelIndex&, int first, int last) {
            if (first < limit_ && qint64(source->rowCount()) + last - first + 1 > limit_) refreshRows();
        });
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        const auto value = QSortFilterProxyModel::data(index, role);
        if (role != Qt::DisplayRole) return value;
        const auto field = headerData(index.column(), Qt::Horizontal, Qt::UserRole).toString();
        if (field != "cpu" && field != "memory" && field != "storage") return value;
        const auto quantity = QSortFilterProxyModel::data(index, Qt::UserRole + 6);
        if (!quantity.isValid()) return value;
        const auto full = formatMetricQuantity(quantity.toDouble(), field == "cpu");
        const auto text = value.toString();
        if (!text.startsWith(full)) return value;
        // Only compact the visible quantity. Keep qualifiers and the source's full-value copy/filter/tooltip contract.
        return formatMetricQuantity(quantity.toDouble(), field == "cpu", true) + text.mid(full.size());
    }
    bool setLimit(int value) {
        if (value < 1 || value > 5000) return false;
        if (limit_ == value) return true;
        limit_ = value; refreshRows(); return true;
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override { return !parent.isValid() && row < limit_; }
private:
    void refreshRows() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange(); endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
    int limit_ = 256;
};
}
