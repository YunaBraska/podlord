#pragma once
#include "resource_metrics.h"
#include <QSortFilterProxyModel>
#include <algorithm>

namespace podlord {
/** One page of the complete, already filtered and sorted cache. */
class ResourceDisplayModel final : public QSortFilterProxyModel {
public:
    static constexpr int PageSize = 512;
    explicit ResourceDisplayModel(QAbstractItemModel* source) {
        setSourceModel(source);
        connect(source, &QAbstractItemModel::layoutChanged, this, [this] { synchronizePage(); });
        connect(source, &QAbstractItemModel::modelReset, this, [this] { synchronizePage(); });
        connect(source, &QAbstractItemModel::rowsInserted, this, [this] { synchronizePage(); });
        connect(source, &QAbstractItemModel::rowsRemoved, this, [this] { synchronizePage(); });
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
    int page() const { return page_; }
    int pageCount() const { return std::max(1, (sourceModel()->rowCount() + PageSize - 1) / PageSize); }
    bool setPage(int value) {
        if (value < 0 || value >= pageCount()) return false;
        if (page_ == value) return true;
        page_ = value; refreshRows(); return true;
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        return !parent.isValid() && row >= page_ * PageSize && row < (page_ + 1) * PageSize;
    }
private:
    void synchronizePage() {
        page_ = std::min(page_, pageCount() - 1);
        refreshRows();
    }
    void refreshRows() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange(); endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
    int page_ = 0;
};
}
